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

#include <glib-unix.h>
#include <glib.h>
#include <memory>
#include <string>
#include <vector>

namespace smartspeaker {
namespace state {
namespace events {

struct Event {
  virtual ~Event() = default;
};

template <typename Value> struct Request {
  virtual ~Request() = default;

  virtual void resolve(const Value &) = 0;
  virtual void reject(const char *error_code, const char *error_message) = 0;
};

template <> struct Request<void> {
  virtual ~Request() = default;

  virtual void resolve() = 0;
  virtual void reject(const char *error_code, const char *error_message) = 0;
};

template <typename Value> struct DummyRequest : public Request<Value> {
  void resolve(const Value &) {}
  void reject(const char *error_code, const char *error_message) {}
};

template <typename Value> struct RequestEvent : public Event {
  RequestEvent(Request<Value> *req) : request(req) {}
  RequestEvent(std::unique_ptr<Request<Value>> &&req)
      : request(std::move(req)) {}

  void resolve(const Value &v) { request->resolve(v); }
  void reject(const char *error_code, const char *error_message) {
    request->reject(error_code, error_message);
  }

private:
  std::unique_ptr<Request<Value>> request;
};

template <> struct RequestEvent<void> : public Event {
  RequestEvent(Request<void> *req) : request(req) {}
  RequestEvent(std::unique_ptr<Request<void>> &&req)
      : request(std::move(req)) {}

  void resolve() { request->resolve(); }
  void reject(const char *error_code, const char *error_message) {
    request->reject(error_code, error_message);
  }

private:
  std::unique_ptr<Request<void>> request;
};

// Audio Input Events
// ===========================================================================

struct Wake : Event {};

struct InputFrame : Event {
  AudioFrame frame;

  InputFrame(AudioFrame frame) : frame(std::move(frame)) {}
};

struct InputDone : Event {
  bool vad_detected;

  InputDone(bool vad_detected) : vad_detected(vad_detected) {}
};

struct InputNotDetected : Event {};

struct InputTimeout : Event {};

// Conversation Events
// ===========================================================================

struct TextMessage : Event {
  gint64 id;
  std::string text;

  TextMessage(gint64 id, const gchar *text) : id(id), text(text) {}
};

struct AskSpecialMessage : Event {
  std::string ask;
  gint64 text_id;

  // NOTE   Per the spec, the `ask` field may be `null` in the JSON, which
  //        results in the `const gchar *` being `nullptr`. Since it's undefined
  //        behavior to create a `std::string` from a null `char *` we need
  //        to check for that condition. In this case, we simply use the empty
  //        string in place of `null`.
  AskSpecialMessage(const gchar *ask, gint64 text_id)
      : ask(ask == nullptr ? "" : ask), text_id(text_id) {}
};

// Button Events
// ===========================================================================

struct AdjustVolume : Event {
  int delta; // 1 or -1

  AdjustVolume(int delta) : delta(delta) {}
};

struct TogglePlayback : Event {};

struct Panic : Event {};

struct ToggleDisabled : Event {};

struct ToggleConfigMode : Event {};

// Speech-To-Text (STT) Events
// ===========================================================================

namespace stt {

struct TextResponse : Event {
  std::string text;

  TextResponse(const char *text) : text(text) {}
};

struct ErrorResponse : Event {
  int code;
  std::string message;

  ErrorResponse(int code, const char *message) : code(code), message(message) {}
};

} // namespace stt

} // namespace events
} // namespace state
} // namespace smartspeaker
