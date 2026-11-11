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

#pragma once

#include "app.hpp"

#include <string>

namespace smartspeaker {

enum class AudioDestination { VOICE, MUSIC, ALERT };

/**
 * Front end for the state machine: sounds and alerts play on the mixer's
 * announce source, music on its media source.
 */
class AudioPlayer {
public:
  AudioPlayer(App *appInstance);
  gboolean play_sound(enum Sound_t id,
                      AudioDestination destination = AudioDestination::ALERT);
  bool play_url(const std::string &url,
                AudioDestination destination = AudioDestination::MUSIC,
                gint64 ref_id = -1);
  gboolean
  play_location(const gchar *location,
                AudioDestination destination = AudioDestination::MUSIC);
  gboolean stop();

private:
  App *const app;
};

} // namespace smartspeaker
