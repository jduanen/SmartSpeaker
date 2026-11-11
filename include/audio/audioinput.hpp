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
#include "audiodriver.hpp"
#include "audioplayer.hpp"
#include "wakeword.hpp"
#include <atomic>
#include <glib.h>
#include <queue>
#include <thread>

namespace smartspeaker {

class AudioInput {
public:
  static const int32_t BUFFER_MAX_FRAMES = 32;

  enum class State {
    CLOSED,
    WAITING,
    LISTENING,
  };

  AudioInput(App *app);
  ~AudioInput();
  void close();
  void wake();
  void sleep();

private:
  // initialized once and never overwritten
  App *const app;
  std::unique_ptr<WakeWord> wakeword;
  std::unique_ptr<AudioInputDriver> input;

  // thread safe, accessed from both threads
  std::thread input_thread;
  std::atomic<State> state;

  // only accessed from the input thread
  int32_t pv_frame_length;
  size_t sample_rate;
  int16_t channels;
  std::queue<AudioFrame> frame_buffer;

  void loop();
  void loop_waiting();
  void loop_listening();
  void transition(State to_state);
};

} // namespace smartspeaker

