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

namespace smartspeaker {
namespace state {

class Disabled : public State {
public:
  static const constexpr char *NAME = "Disabled";

  Disabled(App *app) : State{app} {}

  void enter() override;
  void exit() override;
  const char *name() override { return NAME; };

  // The "actual" handlers that do things
  void react(events::ToggleDisabled *) override;
  void react(events::Panic *) override;

  // Events that state::State handles that we want to ignore
  void react(events::Wake *) override {}
  void react(events::TextMessage *) override {}
};

} // namespace state
} // namespace smartspeaker
