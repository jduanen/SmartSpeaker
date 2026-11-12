#pragma once

#ifdef USE_HOST

#include "esphome/components/speaker/speaker.h"
#include "esphome/core/component.h"

#include "pa_ringbuffer.h"

#include <alsa/asoundlib.h>
#include <speex/speex_resampler.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace esphome::baidu_speaker {

/// Something the mixer thread pulls 48 kHz mono S16 samples from.
class MixerInput {
 public:
  virtual ~MixerInput() = default;
  /// Mixer thread: copy up to `n` samples into `out`; returns the count.
  virtual size_t read(int16_t *out, size_t n) = 0;
};

/// ALSA output and mixer (see speaker.py). Inputs are summed once per 20 ms
/// period; duckable inputs (media) are scaled by the duck gain first.
class BaiduSpeaker : public speaker::Speaker, public Component {
 public:
  static const unsigned RATE = 48000;
  static const unsigned PERIOD = RATE / 50;  // 20 ms

  ~BaiduSpeaker();
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_device(const std::string &device) { this->device_ = device; }
  void set_volume_control(const std::string &name) { this->volume_control_ = name; }

  /// Register an input; only before setup() (from generated code).
  void add_input(MixerInput *input, bool duckable);
  /// Ramp the duckable inputs to -`decibel_reduction` dB over `duration_ms`.
  void apply_ducking(uint8_t decibel_reduction, uint32_t duration_ms);

  // speaker::Speaker: a stream input of 16-bit PCM in audio_stream_info_
  size_t play(const uint8_t *data, size_t length) override;
  void start() override;
  void stop() override;
  void finish() override;
  bool has_buffered_data() const override;
  /// Volume is the codec's LINEOUT control, scaled linearly over its range.
  void set_volume(float volume) override;
  /// Silences all output, as ESPHome's speakers do.
  void set_mute_state(bool mute_state) override;

 protected:
  struct Input {
    MixerInput *input;
    bool duckable;
  };
  class StreamInput : public MixerInput {
   public:
    StreamInput();
    size_t read(int16_t *out, size_t n) override;
    PaUtilRingBuffer ring;
    // set by stop(); the reader flushes, keeping the ring single-reader
    std::atomic<bool> flush{false};
    std::vector<int16_t> data;
  };

  void run_();
  void apply_volume_();

  std::string device_;
  std::string volume_control_;
  snd_pcm_t *pcm_{nullptr};
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> muted_{false};
  std::vector<Input> inputs_;

  std::mutex duck_lock_;
  float duck_target_{1.0f};
  float duck_step_{0.0f};  // per sample; 0 = jump

  // play() stream (main loop side of the ring)
  StreamInput stream_;
  SpeexResamplerState *resampler_{nullptr};
  uint32_t resampler_rate_{0};
  bool finishing_{false};
};

}  // namespace esphome::baidu_speaker

#endif  // USE_HOST
