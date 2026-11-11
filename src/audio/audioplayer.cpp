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

#include <glib.h>
#include <string.h>

#include "audio/mixer.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::AudioPlayer"

smartspeaker::AudioPlayer::AudioPlayer(App *app) : app(app) {}

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
  if (*location == '/' || strstr(location, "://"))
    path = g_strdup(location);
  else
    path = g_build_filename(app->config->esphome_sound_dir, location, nullptr);
  bool ok = play_url(path, destination);
  g_free(path);
  return ok;
}

bool smartspeaker::AudioPlayer::play_url(const std::string &url,
                                  AudioDestination destination, gint64 ref_id) {
  if (destination == AudioDestination::MUSIC)
    app->mixer->media.play(url);
  else
    app->mixer->announce.play(url);
  return true;
}

bool smartspeaker::AudioPlayer::say(const std::string &text, gint64 ref_id) {
  g_warning("say not supported yet (Phase 4): %s", text.c_str());
  return false;
}

gboolean smartspeaker::AudioPlayer::clean_queue() {
  app->mixer->media.stop();
  return true;
}

gboolean smartspeaker::AudioPlayer::stop() {
  app->mixer->announce.stop();
  app->mixer->media.stop();
  return true;
}
