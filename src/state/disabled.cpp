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

#include "state/disabled.hpp"
#include "app.hpp"
#include "audio/audioplayer.hpp"
#include "audio/audiovolume.hpp"
#include "audio/mixer.hpp"
#include "leds.hpp"
#include "voice/session.hpp"
#include "esphome/server.hpp"
#include "state/sleeping.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::Disabled"

namespace smartspeaker {
namespace state {

void Disabled::enter() {
  State::enter();
  app->leds->animate(LedsState_t::Disabled);
  app->esphome->entities.set_mic_muted(true);
}

void Disabled::exit() {
  app->esphome->entities.set_mic_muted(false);
  State::exit();
}

void Disabled::react(events::Panic *) {
  g_warning("PANIC!!! :D");
  app->audio_player->stop();
}

void Disabled::react(events::ToggleDisabled *) {
  g_message("ENABLING....");
  app->transit(new Sleeping(app));
}

void Disabled::react(events::voice::Announce *announce) {
  // no conversation while muted; report it done once it has started
  app->mixer->announce.play(announce->media);
  if (announce->notify_ha)
    app->voice->announce_finished(true);
}

void Disabled::react(events::voice::TimerFinished *) {
  app->audio_player->play_sound(Sound_t::ALARM_CLOCK_ELAPSED);
}

} // namespace state
} // namespace smartspeaker
