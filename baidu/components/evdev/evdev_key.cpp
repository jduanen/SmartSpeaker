#include "evdev_key.h"
#ifdef USE_HOST

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>

#include "esphome/core/log.h"

namespace esphome::evdev {

static const char *const TAG = "evdev";

void EvdevKey::setup() {
  this->fd_ = open(this->device_.c_str(), O_RDONLY | O_NONBLOCK);
  if (this->fd_ < 0) {
    ESP_LOGE(TAG, "Cannot open %s: %s", this->device_.c_str(), strerror(errno));
    this->mark_failed();
    return;
  }
  // start from the key's current state
  uint8_t keys[KEY_MAX / 8 + 1] = {};
  bool held = ioctl(this->fd_, EVIOCGKEY(sizeof(keys)), keys) >= 0 && (keys[this->key_ / 8] & (1 << (this->key_ % 8)));
  this->publish_initial_state(held);
}

void EvdevKey::loop() {
  if (this->fd_ < 0)
    return;
  struct input_event ev;
  while (read(this->fd_, &ev, sizeof(ev)) == sizeof(ev)) {
    // value 1 = down, 0 = up, 2 = autorepeat (ignored)
    if (ev.type == EV_KEY && ev.code == this->key_ && ev.value != 2) {
      ESP_LOGD(TAG, "'%s' %s", this->get_name().c_str(), ev.value == 1 ? "down" : "up");
      this->publish_state(ev.value == 1);
    }
  }
}

void EvdevKey::dump_config() {
  LOG_BINARY_SENSOR("", "evdev key", this);
  ESP_LOGCONFIG(TAG, "  Device: %s\n  Key code: %u", this->device_.c_str(), this->key_);
}

}  // namespace esphome::evdev

#endif
