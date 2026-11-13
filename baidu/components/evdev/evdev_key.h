#pragma once

#include "esphome/core/defines.h"
#ifdef USE_HOST

#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"

namespace esphome::evdev {

/// One key of a Linux input device: on while held (autorepeat events are ignored).
class EvdevKey : public binary_sensor::BinarySensor, public Component {
 public:
  void set_device(const std::string &device) { this->device_ = device; }
  void set_key(uint16_t key) { this->key_ = key; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  std::string device_;
  uint16_t key_{0};
  int fd_{-1};
};

}  // namespace esphome::evdev

#endif
