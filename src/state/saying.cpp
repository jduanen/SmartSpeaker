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
#include "audio/audiovolume.hpp"
#include "audio/mixer.hpp"
#include "leds.hpp"
#include "voice/session.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::Saying"

namespace smartspeaker {
namespace state {

void Saying::enter() {
  State::enter();
  app->leds->animate(LedsState_t::Saying);
  app->audio_volume_controller->duck();
  if (!preannounce.empty()) {
    app->mixer->announce.play(preannounce);
  } else {
    app->mixer->announce.play(url);
  }
}

void Saying::exit() {
  if (!finished)
    app->mixer->announce.stop();
  if (announcement && !finished)
    app->voice->announce_finished(false);
  State::exit();
}

void Saying::react(events::voice::ResponseFinished *) {
  if (!preannounce.empty()) {
    preannounce.clear(); // the chime is done; now the announcement itself
    app->mixer->announce.play(url);
    return;
  }

  finished = true;
  if (announcement)
    app->voice->announce_finished(true);

  bool follow_up = announcement ? start_conversation
                                : app->voice->continue_conversation();
  if (follow_up)
    app->transit(new Listening(app, true));
  else
    app->transit(new Sleeping(app));
}

void Saying::react(events::TogglePlayback *) {
  // stop talking
  app->transit(new Sleeping(app));
}

void Saying::react(events::voice::Announce *announce) {
  // a new announcement replaces the current one
  app->transit(new Saying(app, announce->media, announce->preannounce,
                          announce->start_conversation));
}

} // namespace state
} // namespace smartspeaker
