#pragma once

#include "esphome/core/defines.h"
#ifdef USE_HOST

#include <algorithm>
#include <string>
#include <vector>

#include "esphome/components/light/addressable_light.h"
#include "esphome/core/component.h"

namespace esphome::baidu_leds {

/// The LED ring (is31fl3236 behind /sys/class/leds/isled01) as an addressable light.
class BaiduLeds : public light::AddressableLight {
 public:
  explicit BaiduLeds(uint16_t num_leds) : num_leds_(num_leds), rgb_(num_leds * 3), effect_data_(num_leds) {}
  void set_path(const std::string &path) { this->path_ = path; }

  void setup() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  int32_t size() const override { return this->num_leds_; }
  light::LightTraits get_traits() override;
  void write_state(light::LightState *state) override;
  void clear_effect_data() override { std::fill(this->effect_data_.begin(), this->effect_data_.end(), 0); }

 protected:
  light::ESPColorView get_view_internal(int32_t index) const override;
  bool write_file_(const char *name, const std::string &text);

  uint16_t num_leds_;
  std::string path_;
  // mutable: get_view_internal() is const but hands out writable pointers
  mutable std::vector<uint8_t> rgb_;
  mutable std::vector<uint8_t> effect_data_;
  std::string last_frame_;
};

}  // namespace esphome::baidu_leds

#endif
