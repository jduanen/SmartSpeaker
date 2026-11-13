#include "baidu_leds.h"
#ifdef USE_HOST

#include <cstdio>

#include "esphome/core/log.h"

namespace esphome::baidu_leds {

static const char *const TAG = "baidu_leds";

bool BaiduLeds::write_file_(const char *name, const std::string &text) {
  std::string path = this->path_ + "/" + name;
  FILE *f = fopen(path.c_str(), "w");
  if (f == nullptr)
    return false;
  bool ok = fputs(text.c_str(), f) >= 0;
  return fclose(f) == 0 && ok;
}

void BaiduLeds::setup() {
  // take the ring from the kernel effects; the colours carry the brightness
  if (!this->write_file_("user_space", "1") || !this->write_file_("brightness", "255")) {
    ESP_LOGE(TAG, "Cannot take over the LEDs at %s", this->path_.c_str());
    this->mark_failed();
  }
}

void BaiduLeds::on_shutdown() {
  // hand the ring back to the kernel (the next program takes it again)
  this->write_file_("user_space", "0");
}

void BaiduLeds::dump_config() {
  ESP_LOGCONFIG(TAG, "Baidu LED ring:\n  Path: %s\n  LEDs: %u", this->path_.c_str(), this->num_leds_);
}

light::LightTraits BaiduLeds::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::RGB});
  return traits;
}

void BaiduLeds::write_state(light::LightState *state) {
  if (this->is_failed())
    return;
  std::string frame;
  char buf[12];
  for (uint16_t i = 0; i < this->num_leds_; i++) {
    const uint8_t *c = &this->rgb_[i * 3];
    snprintf(buf, sizeof(buf), "%u ", (unsigned) (c[0] << 16 | c[1] << 8 | c[2]));
    frame += buf;
  }
  // the driver logs every write, so skip unchanged frames
  if (frame == this->last_frame_)
    return;
  if (!this->write_file_("set_all_led_color", frame)) {
    ESP_LOGW(TAG, "Cannot write %s/set_all_led_color", this->path_.c_str());
    return;
  }
  this->last_frame_ = frame;
}

light::ESPColorView BaiduLeds::get_view_internal(int32_t index) const {
  uint8_t *c = &this->rgb_[index * 3];
  return {c, c + 1, c + 2, nullptr, &this->effect_data_[index], &this->correction_};
}

}  // namespace esphome::baidu_leds

#endif
