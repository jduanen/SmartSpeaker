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

#include "config.hpp"
#include <glib-unix.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include <memory>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::Config"

smartspeaker::Config::Config() {}

smartspeaker::Config::~Config() {
  g_free(audio_output_device);
  g_free(audio_output_device_music);
  g_free(audio_output_device_alerts);
  g_free(audio_output_device_voice);
  g_free(audio_output_fifo);
  g_free(audio_volume_control);
  g_free(audio_input_device);
  g_free(esphome_name);
  g_free(esphome_encryption_key);
  g_free(esphome_model_dir);
  g_free(esphome_sound_dir);
  g_free(wakeword_active);
  g_free(wakeword_sensitivity);
  g_free(sound_wake);
  g_free(sound_no_input);
  g_free(sound_news_intro);
  g_free(sound_alarm_clock_elapsed);
  g_free(sound_working);
  g_free(sound_stt_error);
  g_free(sound_too_much_input);
  g_free(sound_mute_on);
  g_free(sound_mute_off);
  g_free(sound_button_double);
  g_free(sound_button_triple);
  g_free(sound_button_long);
  g_free(leds_path);
  g_free(leds_type);
}

static inline bool is_key_not_found_error(GError *error) {
  return error->domain == G_KEY_FILE_ERROR &&
         (error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND ||
          error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND);
}

int smartspeaker::Config::get_dec_color_from_hex_string(const char *section,
                                                 const char *key,
                                                 const char *default_value) {
  GError *error = NULL;
  gchar *value = g_key_file_get_string(key_file, section, key, &error);
  if (error != NULL) {
    if (is_key_not_found_error(error)) {
      g_message("Config key [%s] %s missing, using default '%s'", section, key,
                default_value);
    } else {
      g_warning("Failed to load [%s] %s from config file, using default '%s'",
                section, key, default_value);
    }
    g_error_free(error);
    value = strdup(default_value);
  }

  unsigned short r, g, b;
  if (sscanf(value, "%02hx%02hx%02hx", &r, &g, &b) != 3) {
    g_warning("Failed to parse [%s] %s from config file, using default '%s'",
              section, key, default_value);
    value = strdup(default_value);
    sscanf(value, "%02hx%02hx%02hx", &r, &g, &b);
  }
  int i = (r << 16) + (g << 8) + b;
  // g_debug("converted hex %s in %d\n", value, i);
  free(value);
  return i;
}

char *smartspeaker::Config::get_string(const char *section, const char *key,
                                 const char *default_value) {
  GError *error = NULL;
  gchar *value = g_key_file_get_string(key_file, section, key, &error);
  if (error != NULL) {
    if (is_key_not_found_error(error)) {
      g_message("Config key [%s] %s missing, using default '%s'", section, key,
                default_value);
    } else {
      g_warning("Failed to load [%s] %s from config file, using default '%s'",
                section, key, default_value);
    }
    g_error_free(error);
    return strdup(default_value);
  }
  return value;
}

/**
 * @brief Helper to get a _size_ (unsigned integer) from the config file,
 * returning a `default_value` if there is an error or the retrieved value is
 * negative.
 *
 * Gets an integer, checks it's zero or higher, and casts it
 */
size_t smartspeaker::Config::get_size(const char *section, const char *key,
                               const size_t default_value) {
  GError *error = NULL;
  ssize_t value = g_key_file_get_integer(key_file, section, key, &error);
  if (error != NULL) {
    if (is_key_not_found_error(error)) {
      g_message("Config key [%s] %s missing, using default '%zd'", section, key,
                default_value);
    } else {
      g_warning("Failed to load [%s] %s from config file, using default '%zd'",
                section, key, default_value);
    }
    g_error_free(error);
    return default_value;
  }
  if (value < 0) {
    g_warning("Failed to load [%s] %s from config file. Value must be 0 or "
              "greater, found %zd. Using default %zd",
              section, key, value, default_value);
    g_error_free(error);
    return default_value;
  }
  return (size_t)value;
}

/**
 * @brief Like the `get_size` helper, but also checks that the retrieved value
 * is within a `min` and `max` (inclusive).
 */
size_t smartspeaker::Config::get_bounded_size(const char *section, const char *key,
                                       const size_t default_value,
                                       const size_t min, const size_t max) {
  g_assert(min <= max);
  g_assert(default_value >= min);
  g_assert(default_value <= max);

  ssize_t value = get_size(section, key, default_value);

  if (value < (ssize_t)min) {
    g_warning("CONFIG [%s] %s must be %zd or greater, found %zd. "
              "Setting to minimum.",
              section, key, min, value);
    return min;
  }

  if (value > (ssize_t)max) {
    g_warning("CONFIG [%s] %s must be %zd or less, found %zd. "
              "Setting to maximum.",
              section, key, max, value);
    return max;
  }

  return value;
}

double smartspeaker::Config::get_double(const char *section, const char *key,
                                 const double default_value) {
  GError *error = NULL;
  double value = g_key_file_get_double(key_file, section, key, &error);
  if (error != NULL) {
    if (is_key_not_found_error(error)) {
      g_message("Config key [%s] %s missing, using default '%g'", section, key,
                default_value);
    } else {
      g_warning("Failed to load [%s] %s from config file, using default %f",
                section, key, default_value);
    }
    g_error_free(error);
    return default_value;
  }
  return value;
}

double smartspeaker::Config::get_bounded_double(const char *section, const char *key,
                                         const double default_value,
                                         const double min, const double max) {
  g_assert(min <= max);
  g_assert(default_value >= min);
  g_assert(default_value <= max);

  double value = get_double(section, key, default_value);

  if (value < min) {
    g_warning("CONFIG [%s] %s must be %f or greater, found %f. "
              "Setting to default (%f).",
              section, key, min, value, default_value);
    return default_value;
  }

  if (value > max) {
    g_warning("CONFIG [%s] %s must be %f or less, found %f. "
              "Setting to default (%f).",
              section, key, max, value, default_value);
    return default_value;
  }

  return value;
}

bool smartspeaker::Config::get_bool(const char *section, const char *key,
                             const bool default_value) {
  GError *error = NULL;
  gboolean value = g_key_file_get_boolean(key_file, section, key, &error);
  if (error != NULL) {
    if (is_key_not_found_error(error)) {
      g_message("Config key [%s] %s missing, using default '%s'", section, key,
                default_value ? "true" : "false");
    } else {
      g_warning("Failed to load [%s] %s from config file, using default %s",
                section, key, default_value ? "true" : "false");
    }
    g_error_free(error);
    return default_value;
  }
  return static_cast<bool>(value);
}

void smartspeaker::Config::save() {
  GError *error = NULL;
  g_key_file_save_to_file(key_file, "config.ini", &error);
  if (error) {
    g_critical("Failed to save configuration file to disk: %s\n",
               error->message);
    g_error_free(error);
  }
}

void smartspeaker::Config::set_string(const char *section, const char *key,
                                      const char *value) {
  g_key_file_set_string(key_file, section, key, value);
  save();
}

void smartspeaker::Config::load() {
  key_file = g_key_file_new();

  GError *error = NULL;
  if (!g_key_file_load_from_file(key_file, "config.ini",
                                 (GKeyFileFlags)(G_KEY_FILE_KEEP_COMMENTS |
                                                 G_KEY_FILE_KEEP_TRANSLATIONS),
                                 &error)) {
    if (error->domain != G_FILE_ERROR || error->code != G_FILE_ERROR_NOENT)
      g_critical("config load error: %s\n", error->message);
    g_clear_error(&error);
  }

  // Audio
  // =========================================================================
  audio_output_device =
      get_string("audio", "output", DEFAULT_AUDIO_OUTPUT_DEVICE);
  audio_output_device_music =
      get_string("audio", "music_output", audio_output_device);
  audio_output_device_alerts =
      get_string("audio", "alert_output", audio_output_device);
  audio_output_device_voice =
      get_string("audio", "voice_output", audio_output_device);
  audio_output_fifo =
      get_string("audio", "output_fifo", DEFAULT_AUDIO_OUTPUT_FIFO);
  audio_volume_control =
      get_string("audio", "volume", DEFAULT_AUDIO_VOLUME_CONTROL);
  audio_input_device = get_string("audio", "input", DEFAULT_AUDIO_INPUT_DEVICE);
  audio_input_stereo2mono = get_bool("audio", "stereo2mono", false);

  // Echo Cancellation (same keys as Genie)
  // =========================================================================
  audio_ec_enabled = get_bool("ec", "enabled", false);
  audio_ec_loopback = get_bool("ec", "loopback", false);

  // ESPHome
  // =========================================================================
  esphome_name = get_string("esphome", "name", DEFAULT_ESPHOME_NAME);
  esphome_port =
      get_bounded_size("esphome", "port", DEFAULT_ESPHOME_PORT, 1, 65535);
  esphome_encryption_key = get_string("esphome", "encryption_key", "");
  esphome_model_dir =
      get_string("esphome", "model_dir", DEFAULT_ESPHOME_MODEL_DIR);
  esphome_sound_dir =
      get_string("esphome", "sound_dir", DEFAULT_ESPHOME_SOUND_DIR);

  // Wake word
  // =========================================================================
  wakeword_active = get_string("wakeword", "active", DEFAULT_WAKEWORD_ACTIVE);
  wakeword_sensitivity =
      get_string("wakeword", "sensitivity", DEFAULT_WAKEWORD_SENSITIVITY);

  // Sounds (empty disables a sound); defaults are the HA Voice PE sounds in
  // share/sounds
  // =========================================================================
  sound_wake = get_string("sound", "wake", "wake_word_triggered.flac");
  sound_no_input = get_string("sound", "no_input", "");
  sound_news_intro = get_string("sound", "news_intro", "");
  sound_alarm_clock_elapsed =
      get_string("sound", "alarm_clock_elapsed", "timer_finished.flac");
  sound_working = get_string("sound", "working", "");
  sound_stt_error = get_string("sound", "stt_error", "");
  sound_too_much_input = get_string("sound", "too_much_input", "");
  sound_mute_on = get_string("sound", "mute_on", "mute_switch_on.flac");
  sound_mute_off = get_string("sound", "mute_off", "mute_switch_off.flac");
  sound_button_double =
      get_string("sound", "button_double", "center_button_double_press.flac");
  sound_button_triple =
      get_string("sound", "button_triple", "center_button_triple_press.flac");
  sound_button_long =
      get_string("sound", "button_long", "center_button_long_press.flac");

  // Buttons
  // =========================================================================
  buttons_enabled =
      g_key_file_get_boolean(key_file, "buttons", "enabled", &error);
  if (error) {
    buttons_enabled = true;
    g_clear_error(&error);
  }

  if (buttons_enabled) {
    evinput_device = get_string("buttons", "evinput_dev", DEFAULT_EVINPUT_DEV);
  } else {
    evinput_device = nullptr;
  }

  // Leds
  // =========================================================================

  leds_enabled = g_key_file_get_boolean(key_file, "leds", "enabled", &error);
  if (error) {
    leds_enabled = false;
    g_clear_error(&error);
  }

  if (leds_enabled) {
    leds_type = get_string("leds", "type", "aw");

    leds_path = g_key_file_get_string(key_file, "leds", "path", &error);
    if (error) {
      g_warning("Missing leds path in configuration file, disabling");
      leds_enabled = false;
      g_clear_error(&error);
    }

    leds_ring_on = get_bool("leds", "ring", false);
    leds_ring_color =
        get_dec_color_from_hex_string("leds", "ring_color", DEFAULT_LEDS_RING_COLOR);
    leds_ring_brightness = get_bounded_double(
        "leds", "ring_brightness", DEFAULT_LEDS_RING_BRIGHTNESS, 0.0, 1.0);
  } else {
    leds_type = nullptr;
    leds_path = nullptr;
  }
}
