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

#include "state/listening.hpp"
#include "app.hpp"
#include "audio/audioplayer.hpp"
#include "audio/audioinput.hpp"
#include "audio/audiovolume.hpp"
#include "leds.hpp"
#include "esphome/server.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::Listening"

namespace smartspeaker {
namespace state {

void Listening::enter() {
  State::enter();

  app->leds->animate(LedsState_t::Listening);
  app->audio_input->wake();
  app->audio_volume_controller->duck();
  if (!is_follow_up && app->esphome->entities.wake_sound()) {
    g_message("Playing WAKE sound...\n");
    app->audio_player->play_sound(Sound_t::WAKE);
  }
}

void Listening::exit() {
  app->audio_input->sleep();
  State::exit();
}

void Listening::react(events::Wake *) {
  g_debug("FIXME Received Wake event while in Listening state.\n");
}

void Listening::react(events::InputFrame *input_frame) {
  // TODO Phase 4: stream to HA as VoiceAssistantAudio
}

void Listening::react(events::TogglePlayback *) {
  app->transit(new Sleeping(app));
}

} // namespace state
} // namespace smartspeaker
