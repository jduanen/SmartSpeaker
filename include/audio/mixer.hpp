/*
 * SmartSpeaker: in-process mixer, the only writer to the playback device
 */

#pragma once

#include <alsa/asoundlib.h>
#include <atomic>
#include <mutex>
#include <thread>

#include "audio/source.hpp"

namespace smartspeaker {

class App;

/**
 * Sums the media and announce sources into one ALSA PCM (48 kHz mono S16,
 * 20 ms periods), the same model as ESPHome's mixer speaker. Media is
 * scaled by the duck gain. Volume stays on the codec's LINEOUT control.
 */
class Mixer {
public:
  static const unsigned RATE = 48000;
  static const unsigned PERIOD = RATE / 50; // 20 ms

  Mixer(App *app);
  ~Mixer();

  bool start();

  /** Ramp the media gain to `db` (0 = full) over `ramp_ms`. Thread-safe. */
  void duck(float db, unsigned ramp_ms);

  Source media;
  Source announce;

private:
  App *const app;
  snd_pcm_t *pcm = nullptr;
  std::thread thread;
  std::atomic<bool> running;

  std::mutex duck_lock;
  float duck_target = 1.0f;
  float duck_step = 0.0f; // per sample; 0 = jump

  void run();
  void sources_changed();
};

} // namespace smartspeaker
