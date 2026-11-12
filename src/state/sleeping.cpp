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

#include "state/sleeping.hpp"
#include "app.hpp"
#include "audio/audioplayer.hpp"
#include "audio/audiovolume.hpp"
#include "audio/mixer.hpp"
#include "leds.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::Sleeping"

namespace smartspeaker {
namespace state {

void Sleeping::enter() {
  State::enter();
  app->audio_volume_controller->unduck();
  app->leds->animate(LedsState_t::Sleeping);
}

void Sleeping::react(events::TogglePlayback *) {
  // Satellite1 single click, after the states that stop something: pause
  // the music, else start Assist unless the mic is muted
  if (app->mixer->media.state() == Source::State::PLAYING)
    app->mixer->media.pause();
  else if (!app->mic_muted)
    app->transit(new Listening(app));
}

void Sleeping::react(events::Wake *wake) {
  app->transit(new Listening(app, wake->phrase));
}

} // namespace state
} // namespace smartspeaker
