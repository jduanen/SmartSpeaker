/*
 * SmartSpeaker: blocking byte stream over an HTTP(S) URL or a local file
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace smartspeaker {

/**
 * Pull-style reader for the decoders. HTTP(S) is driven through the libcurl
 * multi interface from inside read(), so download and decode share one
 * thread. read() returns 0 at end of stream, on error, or once `cancel` is
 * set.
 */
class ByteStream {
public:
  ByteStream(const std::atomic<bool> &cancel);
  ~ByteStream();

  /**
   * `location` is an http(s):// or file:// URL, or an absolute path. An HTTP
   * transfer that receives nothing for `stall_seconds` is restarted up to
   * `retries` times, skipping the bytes already delivered.
   */
  bool open(const std::string &location, long stall_seconds = 30,
            unsigned retries = 0);

  size_t read(void *buf, size_t n);
  /** Copy the next `n` bytes without consuming them. */
  size_t peek(void *buf, size_t n);
  /** Forward-only seek relative to the current position. */
  bool skip(size_t n);
  uint64_t position() const { return pos; }

private:
  const std::atomic<bool> &cancel;
  FILE *file = nullptr;
  void *curl = nullptr;  // CURL *
  void *multi = nullptr; // CURLM *
  bool curl_done = false;
  std::string url;
  long stall_seconds = 30;
  unsigned retries = 0;
  uint64_t received = 0; // HTTP body bytes delivered to `pending`
  uint64_t discard = 0;  // bytes to drop after a restart
  std::vector<uint8_t> pending; // downloaded or peeked, not yet read
  size_t pending_off = 0;
  uint64_t pos = 0;

  void start_http();
  void stop_http();
  bool fill(size_t want);
  static size_t on_data(char *ptr, size_t size, size_t nmemb, void *user);
};

} // namespace smartspeaker
