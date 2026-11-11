/*
 * SmartSpeaker: entities exposed to Home Assistant over the ESPHome API
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "pb.h"

namespace smartspeaker {

class App;

namespace esphome {

/**
 * Sends one message: (message type, nanopb descriptor, message struct).
 */
using Send =
    std::function<void(uint32_t type, const pb_msgdesc_t *fields, const void *msg)>;

/**
 * The entity registry. Holds each entity's state, describes the entities
 * for ListEntities, and applies HA commands. State changes are pushed to
 * subscribed clients through `broadcast`.
 */
class Entities {
public:
  Entities(App *app);

  void list(const Send &send);
  void states(const Send &send);

  /** Apply a command message; false if `type` isn't an entity command. */
  bool handle(uint32_t type, const uint8_t *payload, size_t len);

  // Called by the app when device-side state changes
  void set_mic_muted(bool muted);
  void volume_changed();

  bool wake_sound() const { return wake_sound_; }

  Send broadcast;

private:
  App *const app;

  bool mic_muted = false;
  bool wake_sound_ = true;
  std::string wake_word;
  std::string sensitivity;
  bool media_muted = false;

  bool led_on = true;
  float led_brightness = 0.66f;
  float led_red = 0.09f, led_green = 0.73f, led_blue = 1.0f;

  void push_media_player();
  void push_switch(uint32_t key, bool state);
  void push_select(uint32_t key, const std::string &state);
  void push_light();
};

} // namespace esphome
} // namespace smartspeaker
