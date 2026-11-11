/*
* SmartSpeaker derived from Genie
*/

// -*- mode: cpp; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// This file is part of Genie
//
// Copyright 2021 The Board of Trustees of the Leland Stanford Junior University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "audioplayer.hpp"

#include <alsa/asoundlib.h>
#include <algorithm>
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::AudioPlayer"

smartspeaker::AudioPlayer::AudioPlayer(App *app) : app(app), stopping(false) {}

smartspeaker::AudioPlayer::~AudioPlayer() { stop(); }

gboolean smartspeaker::AudioPlayer::play_sound(enum Sound_t id,
                                        AudioDestination destination) {
  switch (id) {
    case Sound_t::WAKE:
      return play_location(app->config->sound_wake, destination);
    case Sound_t::NO_INPUT:
      return play_location(app->config->sound_no_input, destination);
    case Sound_t::TOO_MUCH_INPUT:
      return play_location(app->config->sound_too_much_input, destination);
    case Sound_t::NEWS_INTRO:
      return play_location(app->config->sound_news_intro, destination);
    case Sound_t::ALARM_CLOCK_ELAPSED:
      return play_location(app->config->sound_alarm_clock_elapsed, destination);
    case Sound_t::WORKING:
      return play_location(app->config->sound_working, destination);
    case Sound_t::STT_ERROR:
      return play_location(app->config->sound_stt_error, destination);
  }
  return false;
}

gboolean smartspeaker::AudioPlayer::play_location(const gchar *location,
                                           AudioDestination destination) {
  if (!location || strlen(location) < 1)
    return false;

  gchar *path;
  if (*location == '/')
    path = g_strdup(location);
  else
    path = g_build_filename(app->config->esphome_sound_dir, location, nullptr);

  const char *device = app->config->audio_output_device_alerts;
  if (destination == AudioDestination::MUSIC)
    device = app->config->audio_output_device_music;
  else if (destination == AudioDestination::VOICE)
    device = app->config->audio_output_device_voice;

  stop();
  worker = std::thread(&AudioPlayer::play_wav, this, std::string(path),
                       std::string(device));
  g_free(path);
  return true;
}

bool smartspeaker::AudioPlayer::play_url(const std::string &url,
                                  AudioDestination destination, gint64 ref_id) {
  g_warning("play_url not supported yet (Phase 3): %s", url.c_str());
  return false;
}

bool smartspeaker::AudioPlayer::say(const std::string &text, gint64 ref_id) {
  g_warning("say not supported yet (Phase 4): %s", text.c_str());
  return false;
}

gboolean smartspeaker::AudioPlayer::clean_queue() { return stop(); }

gboolean smartspeaker::AudioPlayer::stop() {
  if (worker.joinable()) {
    stopping = true;
    worker.join();
  }
  stopping = false;
  return true;
}

/**
 * Runs on the worker thread. Accepts canonical RIFF/WAVE files holding
 * 16-bit PCM; other chunks before "data" are skipped.
 */
void smartspeaker::AudioPlayer::play_wav(std::string path, std::string device) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) {
    g_warning("Cannot open %s: %s", path.c_str(), strerror(errno));
    return;
  }

  char riff[12];
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t rate = 0, data_size = 0;
  bool have_fmt = false, have_data = false;
  if (fread(riff, 1, 12, f) == 12 && !memcmp(riff, "RIFF", 4) &&
      !memcmp(riff + 8, "WAVE", 4)) {
    char id[4];
    uint32_t size;
    while (fread(id, 1, 4, f) == 4 && fread(&size, 4, 1, f) == 1) {
      if (!memcmp(id, "fmt ", 4) && size >= 16) {
        uint8_t fmt[16];
        if (fread(fmt, 1, 16, f) != 16)
          break;
        memcpy(&format, fmt, 2);
        memcpy(&channels, fmt + 2, 2);
        memcpy(&rate, fmt + 4, 4);
        memcpy(&bits, fmt + 14, 2);
        have_fmt = true;
        fseek(f, size - 16 + (size & 1), SEEK_CUR);
      } else if (!memcmp(id, "data", 4)) {
        data_size = size;
        have_data = true;
        break;
      } else {
        fseek(f, size + (size & 1), SEEK_CUR);
      }
    }
  }
  if (!have_fmt || !have_data || format != 1 || bits != 16 || channels < 1) {
    g_warning("%s is not a 16-bit PCM WAV file", path.c_str());
    fclose(f);
    return;
  }

  snd_pcm_t *pcm;
  int err = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    g_warning("Cannot open %s: %s", device.c_str(), snd_strerror(err));
    fclose(f);
    return;
  }
  err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                           SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1,
                           100000);
  if (err < 0) {
    g_warning("Cannot set %u Hz x%u on %s: %s", rate, channels, device.c_str(),
              snd_strerror(err));
    snd_pcm_close(pcm);
    fclose(f);
    return;
  }

  g_message("Playing %s (%u Hz x%u) on %s", path.c_str(), rate, channels,
            device.c_str());
  const size_t frames_per_chunk = rate / 50; // 20 ms
  std::vector<int16_t> buf(frames_per_chunk * channels);
  size_t frames_left = data_size / (2 * channels);
  while (frames_left > 0 && !stopping) {
    size_t n = fread(buf.data(), 2 * channels,
                     std::min(frames_per_chunk, frames_left), f);
    if (n == 0)
      break;
    frames_left -= n;
    snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf.data(), n);
    if (w < 0)
      w = snd_pcm_recover(pcm, w, 0);
    if (w < 0) {
      g_warning("ALSA write failed: %s", snd_strerror(w));
      break;
    }
  }
  if (stopping)
    snd_pcm_drop(pcm);
  else
    snd_pcm_drain(pcm);
  snd_pcm_close(pcm);
  fclose(f);
}
