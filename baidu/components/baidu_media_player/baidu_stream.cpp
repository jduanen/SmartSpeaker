#include "baidu_stream.h"

#ifdef USE_HOST

#include "esphome/core/log.h"

#include <curl/curl.h>

#include <algorithm>
#include <cstring>

namespace esphome::baidu_media_player {

static const char *const TAG = "baidu_media_player.stream";

ByteStream::ByteStream(const std::atomic<bool> &cancel) : cancel_(cancel) {}

ByteStream::~ByteStream() {
  if (this->file_ != nullptr)
    fclose(this->file_);
  this->stop_http_();
}

void ByteStream::start_http_() {
  CURL *c = curl_easy_init();
  this->curl_ = c;
  curl_easy_setopt(c, CURLOPT_URL, this->url_.c_str());
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, std::min(10L, this->stall_seconds_));
  // give up on a stalled stream: under 1 byte/s for stall_seconds
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, this->stall_seconds_);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data_);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, this);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "esphome-baidu");
  // The Xiaodu rootfs has no CA bundle; HA serves media over the LAN.
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
  this->multi_ = curl_multi_init();
  curl_multi_add_handle((CURLM *) this->multi_, c);
}

void ByteStream::stop_http_() {
  if (this->multi_ != nullptr) {
    curl_multi_remove_handle((CURLM *) this->multi_, (CURL *) this->curl_);
    curl_multi_cleanup((CURLM *) this->multi_);
    this->multi_ = nullptr;
  }
  if (this->curl_ != nullptr) {
    curl_easy_cleanup((CURL *) this->curl_);
    this->curl_ = nullptr;
  }
}

bool ByteStream::open(const std::string &location, long stall_seconds, unsigned retries) {
  if (location.compare(0, 7, "http://") == 0 || location.compare(0, 8, "https://") == 0) {
    this->url_ = location;
    this->stall_seconds_ = stall_seconds;
    this->retries_ = retries;
    this->start_http_();
    return true;
  }

  std::string path = location;
  if (path.compare(0, 7, "file://") == 0)
    path = path.substr(7);
  this->file_ = fopen(path.c_str(), "rb");
  if (this->file_ == nullptr)
    ESP_LOGW(TAG, "Cannot open %s", path.c_str());
  return this->file_ != nullptr;
}

size_t ByteStream::on_data_(char *ptr, size_t size, size_t nmemb, void *user) {
  ByteStream *self = static_cast<ByteStream *>(user);
  size_t n = size * nmemb;
  // after a restart, drop what the previous attempt already delivered
  size_t skip = std::min<uint64_t>(self->discard_, n);
  self->discard_ -= skip;
  self->pending_.insert(self->pending_.end(), ptr + skip, ptr + n);
  self->received_ += n - skip;
  return n;
}

/// Make at least `want` bytes available in pending_, unless the stream ends.
bool ByteStream::fill_(size_t want) {
  while (this->pending_.size() - this->pending_off_ < want && !this->cancel_) {
    if (this->file_ != nullptr) {
      uint8_t buf[8192];
      size_t n = fread(buf, 1, sizeof(buf), this->file_);
      if (n == 0)
        return false;
      this->pending_.insert(this->pending_.end(), buf, buf + n);
      continue;
    }
    if (this->multi_ == nullptr || this->curl_done_)
      return false;

    this->drop_consumed_();  // before curl appends more
    int running = 0;
    curl_multi_perform((CURLM *) this->multi_, &running);
    if (!running) {
      int left;
      CURLMsg *msg = curl_multi_info_read((CURLM *) this->multi_, &left);
      CURLcode result = msg && msg->msg == CURLMSG_DONE ? msg->data.result : CURLE_OK;
      if (result == CURLE_OPERATION_TIMEDOUT && this->retries_ > 0) {
        this->retries_--;
        ESP_LOGW(TAG, "Download stalled after %llu bytes; retrying", (unsigned long long) this->received_);
        this->stop_http_();
        this->discard_ = this->received_;
        this->start_http_();
        continue;
      }
      if (result != CURLE_OK)
        ESP_LOGW(TAG, "Download failed: %s", curl_easy_strerror(result));
      this->curl_done_ = true;
    } else if (this->pending_.size() - this->pending_off_ < want) {
      int fds;
      curl_multi_wait((CURLM *) this->multi_, nullptr, 0, 100, &fds);
    }
  }
  return this->pending_.size() - this->pending_off_ >= want;
}

size_t ByteStream::peek(void *buf, size_t n) {
  this->fill_(n);
  size_t avail = std::min(n, this->pending_.size() - this->pending_off_);
  memcpy(buf, this->pending_.data() + this->pending_off_, avail);
  return avail;
}

size_t ByteStream::read(void *buf, size_t n) {
  size_t done = 0;
  uint8_t *out = static_cast<uint8_t *>(buf);
  while (done < n && !this->cancel_) {
    if (this->pending_.size() == this->pending_off_ && !this->fill_(1))
      break;
    size_t chunk = std::min(n - done, this->pending_.size() - this->pending_off_);
    memcpy(out + done, this->pending_.data() + this->pending_off_, chunk);
    this->pending_off_ += chunk;
    done += chunk;
    this->drop_consumed_();
  }
  this->pos_ += done;
  return done;
}

/// Drop the bytes already read, except the stream's first KEEP_HEAD bytes
/// while the read position is still within them.
void ByteStream::drop_consumed_() {
  if (this->pending_off_ == 0 || this->base_ + this->pending_off_ <= KEEP_HEAD)
    return;
  this->pending_.erase(this->pending_.begin(), this->pending_.begin() + this->pending_off_);
  this->base_ += this->pending_off_;
  this->pending_off_ = 0;
}

bool ByteStream::seek_to(uint64_t position) {
  if (position >= this->pos_)
    return this->skip(position - this->pos_);
  if (position < this->base_)
    return false;
  this->pending_off_ = position - this->base_;
  this->pos_ = position;
  return true;
}

bool ByteStream::skip(size_t n) {
  uint8_t buf[4096];
  while (n > 0) {
    size_t got = this->read(buf, std::min(n, sizeof(buf)));
    if (got == 0)
      return false;
    n -= got;
  }
  return true;
}

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
