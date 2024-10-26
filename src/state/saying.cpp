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

#include "state/saying.hpp"
#include "app.hpp"
#include "audio/audioplayer.hpp"
#include "leds.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::Saying"

namespace smartspeaker {
namespace state {

void Saying::enter() {
  State::enter();

  app->track_processing_event(ProcessingEventType::START_TTS);
  app->audio_player->say(text, text_id);
  app->leds->animate(LedsState_t::Saying);
}

void Saying::react(events::AskSpecialMessage *ask_special_message) {
  if (ask_special_message->ask.empty()) {
    g_message("Received empty AskSpecialMessage, round done.");
  } else {
    g_message(
        "Received AskSpecialMessage with ask=%s for text id=%" G_GINT64_FORMAT,
        ask_special_message->ask.c_str(), ask_special_message->text_id);
    if (ask_special_message->text_id == text_id) {
      follow_up = true;
    }
  }
}

} // namespace state
} // namespace smartspeaker
