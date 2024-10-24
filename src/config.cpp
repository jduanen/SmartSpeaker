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
#include <string.h>

#include "leds.hpp"
#include <memory>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::Config"

smartspeaker::Config::Config() {}

smartspeaker::Config::~Config() {
  g_free(leds_path);
  g_free(leds_type);
}

static inline bool is_key_not_found_error(GError *error) {
  return error->domain == G_KEY_FILE_ERROR &&
         (error->code == G_KEY_FILE_ERROR_GROUP_NOT_FOUND ||
          error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND);
}

int smartspeaker::Config::get_leds_effect_string(const char *section, const char *key,
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

  int i;
  if (strcmp(value, "none") == 0) {
    i = (int)LedsAnimation_t::None;
  } else if (strcmp(value, "solid") == 0) {
    i = (int)LedsAnimation_t::Solid;
  } else if (strcmp(value, "circular") == 0) {
    i = (int)LedsAnimation_t::Circular;
  } else if (strcmp(value, "pulse") == 0) {
    i = (int)LedsAnimation_t::Pulse;
  } else {
    g_warning("Failed to parse [%s] %s from config file, using 'none'", section,
              key);
    i = (int)LedsAnimation_t::None;
  }
  free(value);
  return i;
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

    leds_starting_effect = get_leds_effect_string("leds", "starting_effect",
                                                  DEFAULT_LEDS_STARTING_EFFECT);
    leds_starting_color = get_dec_color_from_hex_string(
        "leds", "starting_color", DEFAULT_LEDS_STARTING_COLOR);
    leds_sleeping_effect = get_leds_effect_string("leds", "sleeping_effect",
                                                  DEFAULT_LEDS_SLEEPING_EFFECT);
    leds_sleeping_color = get_dec_color_from_hex_string(
        "leds", "sleeping_color", DEFAULT_LEDS_SLEEPING_COLOR);
    leds_listening_effect = get_leds_effect_string(
        "leds", "listening_effect", DEFAULT_LEDS_LISTENING_EFFECT);
    leds_listening_color = get_dec_color_from_hex_string(
        "leds", "listening_color", DEFAULT_LEDS_LISTENING_COLOR);
    leds_processing_effect = get_leds_effect_string(
        "leds", "processing_effect", DEFAULT_LEDS_PROCESSING_EFFECT);
    leds_processing_color = get_dec_color_from_hex_string(
        "leds", "processing_color", DEFAULT_LEDS_PROCESSING_COLOR);
    leds_saying_effect = get_leds_effect_string("leds", "saying_effect",
                                                DEFAULT_LEDS_SAYING_EFFECT);
    leds_saying_color = get_dec_color_from_hex_string(
        "leds", "saying_color", DEFAULT_LEDS_SAYING_COLOR);
    leds_config_effect = get_leds_effect_string("leds", "config_effect",
                                                DEFAULT_LEDS_CONFIG_EFFECT);
    leds_config_color = get_dec_color_from_hex_string(
        "leds", "config_color", DEFAULT_LEDS_CONFIG_COLOR);
    leds_error_effect = get_leds_effect_string("leds", "error_effect",
                                               DEFAULT_LEDS_ERROR_EFFECT);
    leds_error_color = get_dec_color_from_hex_string("leds", "error_color",
                                                     DEFAULT_LEDS_ERROR_COLOR);
    leds_net_error_effect = get_leds_effect_string(
        "leds", "net_error_effect", DEFAULT_LEDS_NET_ERROR_EFFECT);
    leds_net_error_color = get_dec_color_from_hex_string(
        "leds", "net_error_color", DEFAULT_LEDS_NET_ERROR_COLOR);
    leds_disabled_effect = get_leds_effect_string("leds", "disabled_effect",
                                                  DEFAULT_LEDS_DISABLED_EFFECT);
    leds_disabled_color = get_dec_color_from_hex_string(
        "leds", "disabled_color", DEFAULT_LEDS_DISABLED_COLOR);
  } else {
    leds_type = nullptr;
    leds_path = nullptr;
  }
}
