#pragma once

#ifdef USE_HOST

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace esphome::baidu_media_player {

/// Pull-style reader for the decoders over an http(s):// URL, a file:// URL
/// or an absolute path. HTTP(S) is driven through the libcurl multi interface
/// from inside read(), so download and decode share one thread. read()
/// returns 0 at end of stream, on error, or once `cancel` is set. Ported from
/// the C++ app's src/audio/stream.cpp.
class ByteStream {
 public:
  explicit ByteStream(const std::atomic<bool> &cancel);
  ~ByteStream();

  /// An HTTP transfer that receives nothing for `stall_seconds` is restarted
  /// up to `retries` times, skipping the bytes already delivered.
  bool open(const std::string &location, long stall_seconds = 30, unsigned retries = 0);

  size_t read(void *buf, size_t n);
  /// Copy the next `n` bytes without consuming them.
  size_t peek(void *buf, size_t n);
  /// Seek forward relative to the current position.
  bool skip(size_t n);
  /// Seek to an absolute position: forward, or back within the first
  /// KEEP_HEAD bytes (dr_mp3 rereads the start after probing for an ID3 tag).
  bool seek_to(uint64_t position);
  uint64_t position() const { return this->pos_; }

 protected:
  static const size_t KEEP_HEAD = 64 * 1024;

  void start_http_();
  void drop_consumed_();
  void stop_http_();
  bool fill_(size_t want);
  static size_t on_data_(char *ptr, size_t size, size_t nmemb, void *user);

  const std::atomic<bool> &cancel_;
  FILE *file_{nullptr};
  void *curl_{nullptr};   // CURL *
  void *multi_{nullptr};  // CURLM *
  bool curl_done_{false};
  std::string url_;
  long stall_seconds_{30};
  unsigned retries_{0};
  uint64_t received_{0};  // HTTP body bytes delivered to pending_
  uint64_t discard_{0};   // bytes to drop after a restart
  std::vector<uint8_t> pending_;  // downloaded, not yet dropped
  size_t pending_off_{0};         // read position in pending_
  uint64_t base_{0};              // stream position of pending_[0]
  uint64_t pos_{0};
};

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
