/*
 * SmartSpeaker: in-process mixer, the only writer to the playback device
 */

#include "audio/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <glib.h>
#include <vector>

#include "app.hpp"
#include "esphome/server.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::Mixer"

namespace smartspeaker {

Mixer::Mixer(App *app)
    // the WiFi drops whole TCP flows for tens of seconds; short TTS and
    // announcement files are cheap to fetch again
    : media("media", RATE, 30, 0, [this] { sources_changed(); }),
      announce("announce", RATE, 5, 2, [this] { sources_changed(); }),
      app(app),
      running(false) {}

// Runs on the main loop after a source changes state
void Mixer::sources_changed() {
  if (app->esphome)
    app->esphome->entities.media_player_changed();

  Source::State now = announce.state();
  if (now == Source::State::FINISHED && last_announce != now)
    app->dispatch(new state::events::voice::ResponseFinished());
  last_announce = now;
}

Mixer::~Mixer() {
  running = false;
  if (thread.joinable())
    thread.join();
  if (pcm)
    snd_pcm_close(pcm);
}

bool Mixer::start() {
  const char *device = app->config->audio_output_device;
  int err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    g_critical("Cannot open %s: %s", device, snd_strerror(err));
    pcm = nullptr;
    return false;
  }
  // mono: both LINEOUT channels carry DACL on the Xiaodu
  err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1,
                           4 * 20000 /* us, 4 periods */);
  if (err < 0) {
    g_critical("Cannot configure %s: %s", device, snd_strerror(err));
    return false;
  }
  running = true;
  thread = std::thread(&Mixer::run, this);
  g_message("Mixer playing to %s at %u Hz", device, RATE);
  return true;
}

void Mixer::duck(float db, unsigned ramp_ms) {
  std::lock_guard<std::mutex> lock(duck_lock);
  duck_target = powf(10.0f, db / 20.0f);
  duck_step = ramp_ms ? 1.0f / (ramp_ms * (RATE / 1000)) : 0.0f;
}

void Mixer::run() {
  std::vector<int16_t> m(PERIOD), a(PERIOD), out(PERIOD);
  float gain = 1.0f;

  while (running) {
    size_t nm = media.read(m.data(), PERIOD);
    size_t na = announce.read(a.data(), PERIOD);
    std::fill(m.begin() + nm, m.end(), 0);
    std::fill(a.begin() + na, a.end(), 0);

    float target, step;
    {
      std::lock_guard<std::mutex> lock(duck_lock);
      target = duck_target;
      step = duck_step;
    }
    for (unsigned i = 0; i < PERIOD; i++) {
      if (step == 0.0f)
        gain = target;
      else if (gain < target)
        gain = std::min(target, gain + step);
      else if (gain > target)
        gain = std::max(target, gain - step);
      int32_t v = (int32_t)lrintf(m[i] * gain) + a[i];
      out[i] = (int16_t)std::max(-32768, std::min(32767, v));
    }
    if (muted)
      std::fill(out.begin(), out.end(), 0);

    snd_pcm_sframes_t w = snd_pcm_writei(pcm, out.data(), PERIOD);
    if (w < 0) {
      w = snd_pcm_recover(pcm, w, 1);
      if (w < 0) {
        g_warning("ALSA write failed: %s", snd_strerror(w));
        g_usleep(20000);
      }
    }
  }
}

} // namespace smartspeaker
