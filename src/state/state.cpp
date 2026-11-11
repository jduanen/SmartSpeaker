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

#include "state/state.hpp"
#include "app.hpp"
#include "audio/audioplayer.hpp"
#include "audio/audioinput.hpp"
#include "audio/audiovolume.hpp"
#include "esphome/server.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::State"

namespace smartspeaker {
namespace state {

void State::enter() {
  g_message("ENTER state %s\n", name());
  enter_time = std::chrono::steady_clock::now();
}

void State::exit() {
  exit_time = std::chrono::steady_clock::now();

  auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(
      exit_time - enter_time);
  g_message("Spent %ld milliseconds in state %s", (long)delta.count(), name());
}

// Event Handling Methods
// ===========================================================================

void State::react(events::Wake *) {
  g_debug("FIXME received Wake when not in Listening state, ignoring.");
}

void State::react(events::InputFrame *input_frame) {
  // only Listening streams the mic
}

void State::react(events::AdjustVolume *adjust_volume) {
  app->audio_volume_controller->adjust_volume(
      adjust_volume->delta * AudioVolumeController::VOLUME_DELTA);
  app->esphome->entities.media_player_changed();
}

void State::react(events::TogglePlayback *) {
  g_warning("TODO Playback toggled in state %s", name());
}

void State::react(events::Panic *) {
  g_warning("PANIC!!! :D");
  app->audio_player->stop();
  app->transit(new Sleeping(app));
}

void State::react(events::ToggleDisabled *) {
  g_message("DISABLING...");
  app->transit(new Disabled(app));
}

void State::react(events::ToggleConfigMode *) {
  g_message("CONFIG MODE...");
  app->transit(new Config(app));
}

// Voice Pipeline
// ---------------------------------------------------------------------------
//
// Pipeline events only matter while a run is in progress (Listening,
// Processing, Saying); elsewhere they are stale and ignored.
//

void State::react(events::voice::VadEnd *) {}

void State::react(events::voice::TtsUrl *tts) {
  g_message("Ignoring TTS URL in state %s", name());
}

void State::react(events::voice::RunEnd *) {}

void State::react(events::voice::Error *error) {
  g_warning("Voice pipeline error in state %s: %s (%s)", name(),
            error->message.c_str(), error->code.c_str());
}

void State::react(events::voice::Announce *announce) {
  app->transit(new Saying(app, announce->media, announce->preannounce,
                          announce->start_conversation));
}

void State::react(events::voice::TimerFinished *) {
  app->transit(new Ringing(app));
}

void State::react(events::voice::ResponseFinished *) {}

} // namespace state
} // namespace smartspeaker
