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

#include "audioinput.hpp"
#include "alsa/input.hpp"

// note: we need to redefine G_LOG_DOMAIN here or the definition will
// bleed into the functions declared in the header, which will break
// the logging, but also break the One Definition Rule and cause potential havoc
#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::AudioInput"

smartspeaker::AudioInput::AudioInput(App *app)
    : app(app), wakeword(nullptr), input(nullptr), state(State::WAITING) {
  wakeword = std::make_unique<WakeWord>(app);

  sample_rate = wakeword->sample_rate;
  pv_frame_length = wakeword->pv_frame_length;
  channels = 1;

  input = std::make_unique<AudioInputAlsa>(app);
  if (!input->init(app->config->audio_input_device, wakeword->sample_rate,
                   channels, pv_frame_length)) {
    g_error("failed to initialized audio input driver");
    return;
  }

  g_message("Initialized audio input\n");
  input_thread = std::thread(&AudioInput::loop, this);
}

smartspeaker::AudioInput::~AudioInput() {}

void smartspeaker::AudioInput::close() {
  state.store(State::CLOSED);
  input_thread.join();
}

/**
 * @brief Tell the audio input loop (running on its own thread) to start
 * streaming frames if it wasn't. Thread-safe.
 *
 * Essentially synthesizes hearing the wake-word, hence the name.
 */
void smartspeaker::AudioInput::wake() {
  State expect = State::WAITING;
  if (state.compare_exchange_strong(expect, State::LISTENING))
    g_message("[AudioInput] -> State::LISTENING");
}

/**
 * @brief Stop streaming frames and go back to waiting for the wake-word.
 * Thread-safe. The end of speech is decided by HA's VAD, not on the device.
 */
void smartspeaker::AudioInput::sleep() {
  State expect = State::LISTENING;
  if (state.compare_exchange_strong(expect, State::WAITING))
    g_message("[AudioInput] -> State::WAITING");
}

void smartspeaker::AudioInput::loop_waiting() {
  AudioFrame new_frame = input->read_frame(pv_frame_length);

  if (new_frame.length == 0) {
    return;
  }

  // Drop from the queue if there are too many items
  while (frame_buffer.size() > BUFFER_MAX_FRAMES) {
    frame_buffer.pop();
  }

  // Check the new frame for the wake-word
  bool detected = wakeword->process(&new_frame);

  // Add the new frame to the queue
  frame_buffer.push(std::move(new_frame));

  if (!detected) {
    // wake-word not found
    return;
  }

  g_message("Wakeword detected in waiting state");
  app->dispatch(new state::events::Wake());

  g_debug("Sending prior %zd frames\n", frame_buffer.size());

  while (!frame_buffer.empty()) {
    app->dispatch(
        new state::events::InputFrame(std::move(frame_buffer.front())));
    frame_buffer.pop();
  }

  wake();
}

void smartspeaker::AudioInput::loop_listening() {
  AudioFrame new_frame = input->read_frame(pv_frame_length);

  if (new_frame.length == 0) {
    return;
  }

  app->dispatch(new state::events::InputFrame(std::move(new_frame)));
}

void smartspeaker::AudioInput::loop() {
  for (;;) {
    switch (state) {
      case State::CLOSED:
        return;
      case State::WAITING:
        loop_waiting();
        break;
      case State::LISTENING:
        loop_listening();
        break;
    }
  }
}
