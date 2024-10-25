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

namespace genie {
namespace state {

class Processing : public State {
public:
  static const constexpr char *NAME = "Processing";

  Processing(App *app) : State{app} {}

  void enter() override;
  const char *name() override { return NAME; };

  void react(events::TextMessage *text_message) override;
  void react(events::stt::TextResponse *response) override;
  void react(events::stt::ErrorResponse *response) override;
  void react(events::AskSpecialMessage *ask_special_message) override;
  void react(events::audio::PrepareEvent *prepare) override;

private:
  bool preparing_audio = false;
};

} // namespace state
} // namespace genie
