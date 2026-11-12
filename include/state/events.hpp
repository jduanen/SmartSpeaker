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

#include "../audio/audio.hpp"
#include <glib.h>
#include <string>

namespace smartspeaker {
namespace state {
namespace events {

struct Event {
  virtual ~Event() = default;
};

// Audio Input Events
// ===========================================================================

struct Wake : Event {
  std::string phrase; // sent to HA as wake_word_phrase

  Wake(std::string phrase) : phrase(std::move(phrase)) {}
};

/** The "stop" word, heard during a reply, announcement or timer ring. */
struct StopWord : Event {};

struct InputFrame : Event {
  AudioFrame frame;

  InputFrame(AudioFrame frame) : frame(std::move(frame)) {}
};

// Button Events
// ===========================================================================

struct AdjustVolume : Event {
  int delta; // 1 or -1

  AdjustVolume(int delta) : delta(delta) {}
};

struct TogglePlayback : Event {};

struct Panic : Event {};

/** MUTE key or the HA "Mute microphone" switch. */
struct ToggleMute : Event {};

/** PLAYPAUSE double_press, triple_press or long_press (an HA event). */
struct ButtonPress : Event {
  std::string type;

  ButtonPress(std::string type) : type(std::move(type)) {}
};

struct ToggleConfigMode : Event {};

// Voice Pipeline Events (from Home Assistant, via VoiceSession)
// ===========================================================================

namespace voice {

/** HA's VAD heard the end of speech; stop streaming the mic. */
struct VadEnd : Event {};

/** The response audio is ready to play (TTS_END, or early TTS streaming). */
struct TtsUrl : Event {
  std::string url;

  TtsUrl(std::string url) : url(std::move(url)) {}
};

/** The pipeline run is over; nothing more is coming. */
struct RunEnd : Event {};

struct Error : Event {
  std::string code;
  std::string message;

  Error(std::string code, std::string message)
      : code(std::move(code)), message(std::move(message)) {}
};

/** HA asked the device to play an announcement. */
struct Announce : Event {
  std::string media;
  std::string preannounce;
  bool start_conversation;
  // false for media_player announcements: HA isn't waiting for
  // VoiceAssistantAnnounceFinished
  bool notify_ha;

  Announce(std::string media, std::string preannounce, bool start_conversation,
           bool notify_ha = true)
      : media(std::move(media)), preannounce(std::move(preannounce)),
        start_conversation(start_conversation), notify_ha(notify_ha) {}
};

/** A timer set through Assist ran out. */
struct TimerFinished : Event {};

/** The announce source finished playing (TTS, announcement or a sound). */
struct ResponseFinished : Event {};

} // namespace voice

} // namespace events
} // namespace state
} // namespace smartspeaker
