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

#include <cstddef>
#include <glib.h>

namespace smartspeaker {

enum class AuthMode { NONE, BEARER, COOKIE, HOME_ASSISTANT, OAUTH2 };

enum class WifiAuthMode { OPEN, WEP, WPA };

class Config {
public:
  static const size_t DEFAULT_WS_RETRY_INTERVAL = 3000;
  static const size_t DEFAULT_CONNECT_TIMEOUT = 5000;

  static const constexpr AuthMode DEFAULT_AUTH_MODE = AuthMode::OAUTH2;

  static const constexpr char *DEFAULT_LOCALE = "en-US";
  static const constexpr char *DEFAULT_VOICE = "male";
  static const constexpr char *DEFAULT_NET_WLAN_IF = "wlan0";

  // Buttons Defaults
  // -------------------------------------------------------------------------
  static const constexpr char *DEFAULT_EVINPUT_DEV = "/dev/input/event0";

  // Leds Defaults
  // -------------------------------------------------------------------------

  static const constexpr char *DEFAULT_LEDS_STARTING_EFFECT = "pulse";
  static const constexpr char *DEFAULT_LEDS_STARTING_COLOR = "0000ff";
  static const constexpr char *DEFAULT_LEDS_SLEEPING_EFFECT = "none";
  static const constexpr char *DEFAULT_LEDS_SLEEPING_COLOR = "000000";
  static const constexpr char *DEFAULT_LEDS_LISTENING_EFFECT = "pulse";
  static const constexpr char *DEFAULT_LEDS_LISTENING_COLOR = "ffffff";
  static const constexpr char *DEFAULT_LEDS_PROCESSING_EFFECT = "circular";
  static const constexpr char *DEFAULT_LEDS_PROCESSING_COLOR = "ffffff";
  static const constexpr char *DEFAULT_LEDS_SAYING_EFFECT = "pulse";
  static const constexpr char *DEFAULT_LEDS_SAYING_COLOR = "00ff00";
  static const constexpr char *DEFAULT_LEDS_CONFIG_EFFECT = "pulse";
  static const constexpr char *DEFAULT_LEDS_CONFIG_COLOR = "0000ff";
  static const constexpr char *DEFAULT_LEDS_ERROR_EFFECT = "pulse";
  static const constexpr char *DEFAULT_LEDS_ERROR_COLOR = "ff0000";
  static const constexpr char *DEFAULT_LEDS_NET_ERROR_EFFECT = "circular";
  static const constexpr char *DEFAULT_LEDS_NET_ERROR_COLOR = "ffa500";
  static const constexpr char *DEFAULT_LEDS_DISABLED_EFFECT = "solid";
  static const constexpr char *DEFAULT_LEDS_DISABLED_COLOR = "ff0000";

  Config();
  ~Config();
  void load();
  void save();

  // Configuration File Values
  // =========================================================================
  //
  // Loaded from `/opt/ss/config.ini`
  //

  // General
  // -------------------------------------------------------------------------

  size_t retry_interval;
  size_t connect_timeout;

  // Buttons
  // -------------------------------------------------------------------------
  bool buttons_enabled;
  gchar *evinput_device;

  // Leds
  // -------------------------------------------------------------------------

  bool leds_enabled;
  gchar *leds_type;
  gchar *leds_path;

  gint leds_starting_effect;
  gint leds_starting_color;
  gint leds_sleeping_effect;
  gint leds_sleeping_color;
  gint leds_listening_effect;
  gint leds_listening_color;
  gint leds_processing_effect;
  gint leds_processing_color;
  gint leds_saying_effect;
  gint leds_saying_color;
  gint leds_config_effect;
  gint leds_config_color;
  gint leds_error_effect;
  gint leds_error_color;
  gint leds_net_error_effect;
  gint leds_net_error_color;
  gint leds_disabled_effect;
  gint leds_disabled_color;

protected:
private:
  GKeyFile *key_file = nullptr;

  int get_leds_effect_string(const char *section, const char *key,
                             const gchar *default_value);
  int get_dec_color_from_hex_string(const char *section, const char *key,
                                    const gchar *default_value);
  gchar *get_string(const char *section, const char *key,
                    const gchar *default_value);
  size_t get_size(const char *section, const char *key,
                  const size_t default_value);
  size_t get_bounded_size(const char *section, const char *key,
                          const size_t default_value, const size_t min,
                          const size_t max);
  double get_double(const char *section, const char *key,
                    const double default_value);
  double get_bounded_double(const char *section, const char *key,
                            const double default_value, const double min,
                            const double max);
  bool get_bool(const char *section, const char *key, const bool default_value);
};

} // namespace smartspeaker
