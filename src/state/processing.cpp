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

#include "state/processing.hpp"
#include "app.hpp"
#include "leds.hpp"
#include "voice/session.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::Processing"

namespace smartspeaker {
namespace state {

void Processing::enter() {
  State::enter();
  app->leds->animate(LedsState_t::Processing);
}

void Processing::react(events::TogglePlayback *) {
  app->voice->stop();
  app->transit(new Sleeping(app));
}

void Processing::react(events::voice::TtsUrl *tts) {
  app->transit(new Saying(app, tts->url));
}

void Processing::react(events::voice::RunEnd *) {
  // no response to speak ("never mind")
  app->transit(new Sleeping(app));
}

void Processing::react(events::voice::Error *error) {
  State::react(error);
  app->transit(new Sleeping(app));
}

} // namespace state
} // namespace smartspeaker
