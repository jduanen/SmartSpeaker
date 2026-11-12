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
#include "audio/audio.hpp"

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

  // Audio Defaults
  // -------------------------------------------------------------------------
  static const constexpr char *DEFAULT_AUDIO_OUTPUT_DEVICE = "hw:audiocodec";
  static const constexpr char *DEFAULT_AUDIO_INPUT_DEVICE = "hw:audiocodec";
  static const constexpr char *DEFAULT_AUDIO_VOLUME_CONTROL = "LINEOUT volume";
  static const constexpr char *DEFAULT_AUDIO_OUTPUT_FIFO = "/tmp/playback.fifo";

  // ESPHome Defaults
  // -------------------------------------------------------------------------
  static const constexpr char *DEFAULT_ESPHOME_NAME = ""; // xiaodu-<MAC suffix>
  static const size_t DEFAULT_ESPHOME_PORT = 6053;
  static const constexpr char *DEFAULT_ESPHOME_MODEL_DIR = "/opt/ss/share/models";
  static const constexpr char *DEFAULT_ESPHOME_SOUND_DIR = "/opt/ss/share/sounds";
  static const constexpr char *DEFAULT_WAKEWORD_ACTIVE = "okay_nabu";
  static const constexpr char *DEFAULT_WAKEWORD_SENSITIVITY = "Slightly sensitive";

  // Buttons Defaults
  // -------------------------------------------------------------------------
  static const constexpr char *DEFAULT_EVINPUT_DEV = "/dev/input/event0";

  // Leds Defaults
  // -------------------------------------------------------------------------

  // the "LED ring" light; Satellite1's default color (HA blue)
  static const constexpr char *DEFAULT_LEDS_RING_COLOR = "17bbf2";
  static constexpr double DEFAULT_LEDS_RING_BRIGHTNESS = 0.66;

  Config();
  ~Config();
  void load();
  void save();
  /** Set one key and save the file (settings changed from HA). */
  void set_string(const char *section, const char *key, const char *value);

  // Configuration File Values
  // =========================================================================
  //
  // Loaded from `/opt/ss/config.ini`
  //

  // General
  // -------------------------------------------------------------------------

  size_t retry_interval;
  size_t connect_timeout;

  // Audio
  // -------------------------------------------------------------------------

  gchar *audio_output_device;
  gchar *audio_output_device_music;
  gchar *audio_output_device_alerts;
  gchar *audio_output_device_voice;
  gchar *audio_output_fifo;
  gchar *audio_volume_control;
  gchar *audio_input_device;
  bool audio_input_stereo;
  bool audio_input_stereo2mono;
  bool audio_ec_enabled;
  bool audio_ec_loopback;

  // ESPHome native API
  // -------------------------------------------------------------------------

  gchar *esphome_name;
  size_t esphome_port;
  gchar *esphome_encryption_key; // base64 PSK; empty means plaintext
  gchar *esphome_model_dir;
  gchar *esphome_sound_dir;

  // Wake word (set from HA, saved back here)
  // -------------------------------------------------------------------------

  gchar *wakeword_active;      // model id in esphome_model_dir
  gchar *wakeword_sensitivity; // WakeWord::SENSITIVITIES

  // Sound files (16-bit PCM WAV; relative paths are under esphome_sound_dir)
  // -------------------------------------------------------------------------

  gchar *sound_wake;
  gchar *sound_no_input;
  gchar *sound_news_intro;
  gchar *sound_alarm_clock_elapsed;
  gchar *sound_working;
  gchar *sound_stt_error;
  gchar *sound_too_much_input;
  gchar *sound_mute_on;
  gchar *sound_mute_off;
  gchar *sound_button_double;
  gchar *sound_button_triple;
  gchar *sound_button_long;

  // Buttons
  // -------------------------------------------------------------------------
  bool buttons_enabled;
  gchar *evinput_device;

  // Leds
  // -------------------------------------------------------------------------

  bool leds_enabled;
  gchar *leds_type;
  gchar *leds_path;

  // the "LED ring" light entity, saved when HA changes it
  bool leds_ring_on;
  int leds_ring_color; // 0xRRGGBB
  double leds_ring_brightness;

protected:
private:
  GKeyFile *key_file = nullptr;

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
