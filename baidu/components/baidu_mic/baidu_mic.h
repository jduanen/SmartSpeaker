#pragma once

#ifdef USE_HOST

#include "esphome/components/microphone/microphone.h"
#include "esphome/core/component.h"

#include <alsa/asoundlib.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace esphome::baidu_mic {

/// ALSA capture from the Xiaodu codec with Speex AEC (see microphone.py).
/// Like the ESP32 I2S microphone, start()/stop() count listeners, loop()
/// starts and stops a capture thread, and the data callbacks run on that
/// thread.
class BaiduMicrophone : public microphone::Microphone, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void start() override;
  void stop() override;

  void set_device(const std::string &device) { this->device_ = device; }
  void set_echo_cancellation(bool enabled) { this->echo_cancellation_ = enabled; }

 protected:
  static const uint32_t SAMPLE_RATE = 16000;
  // 32 ms: the frame size the C++ app's AEC was tuned with
  static const size_t FRAME_LENGTH = 512;

  bool open_();
  void close_();
  void capture_loop_();
  void process_(const int16_t *in, int16_t *out);

  std::string device_;
  bool echo_cancellation_{true};
  unsigned channels_{3};

  std::atomic<int> listeners_{0};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> capturing_{false};  // set by the thread once the PCM is open
  std::atomic<bool> thread_done_{false};
  std::atomic<bool> thread_failed_{false};
  bool thread_started_{false};
  std::thread thread_;

  // capture thread only
  snd_pcm_t *pcm_{nullptr};
  SpeexEchoState *echo_state_{nullptr};
  SpeexPreprocessState *pp_state_{nullptr};
  std::vector<int16_t> mono_, reference_, filtered_;
  size_t samples_read_{0};
  float dc_x1_{0}, dc_y1_{0};
};

}  // namespace esphome::baidu_mic

#endif  // USE_HOST
