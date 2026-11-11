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
#include <chrono>

namespace smartspeaker {

class App;

namespace state {

class Machine;

class State {
private:
  std::chrono::steady_clock::time_point enter_time;
  std::chrono::steady_clock::time_point exit_time;

public:
  static const constexpr char *NAME = "State";

  App *app;

  State(App *app) : enter_time(), exit_time(), app(app) {}
  virtual ~State() = default;

  virtual void enter();
  virtual void exit();
  virtual const char *name() = 0;

  // note: you must not define a react for the base Event class
  // otherwise you won't get any error when you add new event classes
  // and forget to declare them here
  virtual void react(events::Wake *);
  virtual void react(events::InputFrame *input_frame);
  virtual void react(events::AdjustVolume *adjust_volume);
  virtual void react(events::TogglePlayback *);
  virtual void react(events::Panic *);
  virtual void react(events::ToggleDisabled *);
  virtual void react(events::ToggleConfigMode *);
  virtual void react(events::voice::VadEnd *);
  virtual void react(events::voice::TtsUrl *tts);
  virtual void react(events::voice::RunEnd *);
  virtual void react(events::voice::Error *error);
  virtual void react(events::voice::Announce *announce);
  virtual void react(events::voice::TimerFinished *);
  virtual void react(events::voice::ResponseFinished *);
};

} // namespace state
} // namespace smartspeaker
