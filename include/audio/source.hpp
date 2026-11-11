/*
 * SmartSpeaker: a playable audio source feeding the mixer
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "pa_ringbuffer.h"

namespace smartspeaker {

/**
 * Fetches and decodes one URL or file at a time on its own thread,
 * converting to mono at the mixer rate, into a lock-free ring buffer that
 * the mixer thread drains.
 */
class Source {
public:
  enum class State { IDLE, PLAYING, PAUSED, FINISHED };

  /** `on_change` runs on the GLib main loop after state changes. */
  Source(const char *name, unsigned rate, std::function<void()> on_change);
  ~Source();

  void play(const std::string &location);
  void stop();
  void pause();
  void resume();
  State state() const { return st; }

  /** Mixer thread: copy up to `n` samples into `out`; returns the count. */
  size_t read(int16_t *out, size_t n);

private:
  const char *name;
  const unsigned rate;
  std::function<void()> on_change;

  std::thread worker;
  std::atomic<bool> cancel;
  std::atomic<bool> eof;
  std::atomic<State> st;

  PaUtilRingBuffer ring;
  std::vector<int16_t> ring_data;

  void run(std::string location);
  void set_state(State s);
};

} // namespace smartspeaker
