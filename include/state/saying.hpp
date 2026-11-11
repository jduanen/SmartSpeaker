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

#include "events.hpp"
#include "state.hpp"

#include <string>

namespace smartspeaker {
namespace state {

/**
 * Plays a pipeline reply or an announcement on the announce source, then
 * listens again if HA wants to continue the conversation.
 */
class Saying : public State {
public:
  static const constexpr char *NAME = "Saying";

  /** A pipeline reply (TTS URL). */
  Saying(App *app, std::string url)
      : State{app}, url(std::move(url)), announcement(false) {}
  /** An announcement, optionally preceded by a chime. */
  Saying(App *app, std::string url, std::string preannounce,
         bool start_conversation)
      : State{app}, url(std::move(url)), preannounce(std::move(preannounce)),
        announcement(true), start_conversation(start_conversation) {}

  void enter() override;
  void exit() override;
  const char *name() override { return NAME; };

  void react(events::TogglePlayback *) override;
  void react(events::voice::ResponseFinished *) override;
  void react(events::voice::Announce *announce) override;

private:
  std::string url;
  std::string preannounce;
  bool announcement;
  bool start_conversation = false;
  bool finished = false;
};

} // namespace state
} // namespace smartspeaker
