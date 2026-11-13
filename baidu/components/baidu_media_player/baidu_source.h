#pragma once

#ifdef USE_HOST

#include "esphome/components/baidu_speaker/baidu_speaker.h"
#include "esphome/components/baidu_speaker/pa_ringbuffer.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace esphome::baidu_media_player {

/// Fetches and decodes one URL or file at a time on its own thread,
/// converting to mono at the mixer rate, into a lock-free ring buffer that
/// the mixer thread drains. Ported from the C++ app's src/audio/source.cpp.
class Source : public baidu_speaker::MixerInput {
 public:
  enum class State { IDLE, PLAYING, PAUSED, FINISHED };

  /// An HTTP fetch that stalls for `stall_seconds` is retried `retries` times.
  Source(const char *name, long stall_seconds, unsigned retries);
  ~Source() override;

  // main loop
  void play(const std::string &location);
  void stop();
  void pause();
  void resume();
  State state() const { return this->state_; }
  /// The last item ended without being read (bad URL or format).
  bool failed() const { return this->failed_; }

  // mixer thread
  size_t read(int16_t *out, size_t n) override;

 protected:
  void run_(std::string location);

  const char *name_;
  const long stall_seconds_;
  const unsigned retries_;

  std::thread worker_;
  std::atomic<bool> cancel_{false};
  std::atomic<bool> eof_{false};
  std::atomic<bool> failed_{false};
  std::atomic<bool> flush_{false};  // set by stop(); the reader flushes
  std::atomic<State> state_{State::IDLE};
  // underrun tracking (see read()): play() clears started_; gap_ is the
  // mixer thread's count of samples missing in the current gap
  std::atomic<bool> started_{false};
  size_t gap_{0};

  PaUtilRingBuffer ring_;
  std::vector<int16_t> ring_data_;
};

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
