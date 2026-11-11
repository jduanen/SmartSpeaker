/*
 * SmartSpeaker: blocking byte stream over an HTTP(S) URL or a local file
 */

#include "audio/stream.hpp"

#include <algorithm>
#include <curl/curl.h>
#include <glib.h>
#include <string.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::ByteStream"

namespace smartspeaker {

ByteStream::ByteStream(const std::atomic<bool> &cancel) : cancel(cancel) {}

ByteStream::~ByteStream() {
  if (file)
    fclose(file);
  if (multi) {
    curl_multi_remove_handle((CURLM *)multi, (CURL *)curl);
    curl_multi_cleanup((CURLM *)multi);
  }
  if (curl)
    curl_easy_cleanup((CURL *)curl);
}

bool ByteStream::open(const std::string &location) {
  if (location.compare(0, 7, "http://") == 0 ||
      location.compare(0, 8, "https://") == 0) {
    CURL *c = curl_easy_init();
    curl = c;
    curl_easy_setopt(c, CURLOPT_URL, location.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    // give up on a stalled stream: under 1 byte/s for 30 s
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "smartspeaker");
    // The Xiaodu rootfs has no CA bundle; HA serves media over the LAN.
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    multi = curl_multi_init();
    curl_multi_add_handle((CURLM *)multi, c);
    return true;
  }

  std::string path = location;
  if (path.compare(0, 7, "file://") == 0)
    path = path.substr(7);
  file = fopen(path.c_str(), "rb");
  if (!file)
    g_warning("Cannot open %s", path.c_str());
  return file != nullptr;
}

size_t ByteStream::on_data(char *ptr, size_t size, size_t nmemb, void *user) {
  ByteStream *self = static_cast<ByteStream *>(user);
  self->pending.insert(self->pending.end(), ptr, ptr + size * nmemb);
  return size * nmemb;
}

/** Make at least `want` bytes available in `pending`, unless the stream ends. */
bool ByteStream::fill(size_t want) {
  while (pending.size() - pending_off < want && !cancel) {
    if (file) {
      uint8_t buf[8192];
      size_t n = fread(buf, 1, sizeof(buf), file);
      if (n == 0)
        return false;
      pending.insert(pending.end(), buf, buf + n);
      continue;
    }
    if (!multi || curl_done)
      return false;

    // compact before curl appends more
    if (pending_off > 0) {
      pending.erase(pending.begin(), pending.begin() + pending_off);
      pending_off = 0;
    }
    int running = 0;
    curl_multi_perform((CURLM *)multi, &running);
    if (!running) {
      int left;
      CURLMsg *msg = curl_multi_info_read((CURLM *)multi, &left);
      if (msg && msg->msg == CURLMSG_DONE && msg->data.result != CURLE_OK)
        g_warning("Download failed: %s", curl_easy_strerror(msg->data.result));
      curl_done = true;
    } else if (pending.size() - pending_off < want) {
      int fds;
      curl_multi_wait((CURLM *)multi, nullptr, 0, 100, &fds);
    }
  }
  return pending.size() - pending_off >= want;
}

size_t ByteStream::peek(void *buf, size_t n) {
  fill(n);
  size_t avail = std::min(n, pending.size() - pending_off);
  memcpy(buf, pending.data() + pending_off, avail);
  return avail;
}

size_t ByteStream::read(void *buf, size_t n) {
  size_t done = 0;
  uint8_t *out = static_cast<uint8_t *>(buf);
  while (done < n && !cancel) {
    if (pending.size() == pending_off && !fill(1))
      break;
    size_t chunk = std::min(n - done, pending.size() - pending_off);
    memcpy(out + done, pending.data() + pending_off, chunk);
    pending_off += chunk;
    done += chunk;
    if (pending_off == pending.size()) {
      pending.clear();
      pending_off = 0;
    }
  }
  pos += done;
  return done;
}

bool ByteStream::skip(size_t n) {
  uint8_t buf[4096];
  while (n > 0) {
    size_t got = read(buf, std::min(n, sizeof(buf)));
    if (got == 0)
      return false;
    n -= got;
  }
  return true;
}

} // namespace smartspeaker
