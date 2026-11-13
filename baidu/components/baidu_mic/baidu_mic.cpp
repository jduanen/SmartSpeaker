// The ALSA setup and Speex AEC/preprocess configuration are ported from the
// C++ app's src/audio/alsa/input.cpp, which derives from Genie
// (Copyright 2021 The Board of Trustees of the Leland Stanford Junior
// University, Apache License 2.0).

#include "baidu_mic.h"

#ifdef USE_HOST

#include "esphome/core/log.h"

#include <algorithm>
#include <pthread.h>

namespace esphome::baidu_mic {

static const char *const TAG = "baidu_mic";

void BaiduMicrophone::setup() {
  this->channels_ = this->echo_cancellation_ ? 3 : 2;
  this->audio_stream_info_ = audio::AudioStreamInfo(16, 1, SAMPLE_RATE);
}

void BaiduMicrophone::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Baidu microphone:\n"
                "  Device: %s\n"
                "  Echo cancellation: %s",
                this->device_.c_str(), YESNO(this->echo_cancellation_));
}

void BaiduMicrophone::start() {
  if (this->is_failed())
    return;
  this->listeners_++;
}

void BaiduMicrophone::stop() {
  if (this->listeners_ > 0)
    this->listeners_--;
}

void BaiduMicrophone::loop() {
  // state_ and the component status are only touched here, on the main loop
  if (this->thread_done_) {
    this->thread_.join();
    this->thread_started_ = false;
    this->thread_done_ = false;
    this->capturing_ = false;
    if (this->thread_failed_) {
      this->thread_failed_ = false;
      this->status_momentary_error("alsa", 1000);  // retry in 1 s
    }
    this->state_ = microphone::STATE_STOPPED;
  }

  switch (this->state_) {
    case microphone::STATE_STOPPED:
      if (this->listeners_ > 0 && !this->thread_started_ && !this->status_has_error()) {
        this->state_ = microphone::STATE_STARTING;
        this->stop_requested_ = false;
        this->thread_started_ = true;
        this->thread_ = std::thread(&BaiduMicrophone::capture_loop_, this);
        pthread_setname_np(this->thread_.native_handle(), "baidu_mic");  // for top -H
      }
      break;
    case microphone::STATE_STARTING:
      if (this->capturing_)
        this->state_ = microphone::STATE_RUNNING;
      break;
    case microphone::STATE_RUNNING:
      if (this->listeners_ == 0) {
        this->state_ = microphone::STATE_STOPPING;
        this->stop_requested_ = true;
      }
      break;
    case microphone::STATE_STOPPING:
      break;
  }
}

bool BaiduMicrophone::open_() {
  int err = snd_pcm_open(&this->pcm_, this->device_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
  if (err < 0) {
    ESP_LOGE(TAG, "snd_pcm_open(%s): %s", this->device_.c_str(), snd_strerror(err));
    this->pcm_ = nullptr;
    return false;
  }
  snd_pcm_hw_params_t *hw = nullptr;
  snd_pcm_hw_params_alloca(&hw);
  if ((err = snd_pcm_hw_params_any(this->pcm_, hw)) < 0 ||
      (err = snd_pcm_hw_params_set_access(this->pcm_, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
      (err = snd_pcm_hw_params_set_format(this->pcm_, hw, SND_PCM_FORMAT_S16_LE)) < 0 ||
      (err = snd_pcm_hw_params_set_rate(this->pcm_, hw, SAMPLE_RATE, 0)) < 0 ||
      (err = snd_pcm_hw_params_set_channels(this->pcm_, hw, this->channels_)) < 0 ||
      (err = snd_pcm_hw_params(this->pcm_, hw)) < 0 || (err = snd_pcm_prepare(this->pcm_)) < 0) {
    ESP_LOGE(TAG, "configuring %s: %s", this->device_.c_str(), snd_strerror(err));
    snd_pcm_close(this->pcm_);
    this->pcm_ = nullptr;
    return false;
  }

  if (this->echo_cancellation_) {
    spx_int32_t rate = SAMPLE_RATE;
    this->echo_state_ = speex_echo_state_init_mc(FRAME_LENGTH, (SAMPLE_RATE * 300) / 1000, 1, 1);
    speex_echo_ctl(this->echo_state_, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    this->pp_state_ = speex_preprocess_state_init(FRAME_LENGTH, SAMPLE_RATE);
    spx_int32_t on = 1, echo_suppress = -1;
    speex_preprocess_ctl(this->pp_state_, SPEEX_PREPROCESS_SET_DENOISE, &on);
    speex_preprocess_ctl(this->pp_state_, SPEEX_PREPROCESS_SET_DEREVERB, &on);
    speex_preprocess_ctl(this->pp_state_, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &echo_suppress);
    speex_preprocess_ctl(this->pp_state_, SPEEX_PREPROCESS_SET_ECHO_STATE, this->echo_state_);
  }

  this->mono_.assign(FRAME_LENGTH, 0);
  this->reference_.assign(FRAME_LENGTH, 0);
  this->filtered_.assign(FRAME_LENGTH, 0);
  this->samples_read_ = 0;
  this->dc_x1_ = this->dc_y1_ = 0;
  return true;
}

void BaiduMicrophone::close_() {
  if (this->pp_state_ != nullptr) {
    speex_preprocess_state_destroy(this->pp_state_);
    this->pp_state_ = nullptr;
  }
  if (this->echo_state_ != nullptr) {
    speex_echo_state_destroy(this->echo_state_);
    this->echo_state_ = nullptr;
  }
  if (this->pcm_ != nullptr) {
    snd_pcm_close(this->pcm_);
    this->pcm_ = nullptr;
  }
}

void BaiduMicrophone::process_(const int16_t *in, int16_t *out) {
  for (size_t i = 0; i < FRAME_LENGTH; i++) {
    const int16_t *frame = in + i * this->channels_;
    // average MIC1/MIC2, then a one-pole DC blocker (~13 Hz at 16 kHz)
    float x = (int32_t(frame[0]) + frame[1]) / 2;
    this->dc_y1_ = x - this->dc_x1_ + 0.995f * this->dc_y1_;
    this->dc_x1_ = x;
    this->mono_[i] = (int16_t) std::max(-32768.0f, std::min(32767.0f, this->dc_y1_));
    this->reference_[i] = this->channels_ == 3 ? frame[2] : 0;
    // drop the first 200 ms: the ADC power-up click would upset the AEC
    if (this->samples_read_ < SAMPLE_RATE / 5) {
      this->mono_[i] = 0;
      this->reference_[i] = 0;
    }
    this->samples_read_++;
  }

  if (this->echo_state_ != nullptr) {
    speex_echo_cancellation(this->echo_state_, this->mono_.data(), this->reference_.data(), out);
    // the preprocessor runs after the AEC on purpose (it uses the echo state)
    speex_preprocess_run(this->pp_state_, out);
  } else {
    std::copy(this->mono_.begin(), this->mono_.end(), out);
  }
}

void BaiduMicrophone::capture_loop_() {
  if (!this->open_()) {
    this->close_();
    this->thread_failed_ = true;
    this->thread_done_ = true;
    return;
  }
  ESP_LOGD(TAG, "Capture started");
  this->capturing_ = true;

  std::vector<int16_t> pcm(FRAME_LENGTH * this->channels_);
  std::vector<uint8_t> out(FRAME_LENGTH * sizeof(int16_t));
  while (!this->stop_requested_) {
    snd_pcm_sframes_t n = snd_pcm_readi(this->pcm_, pcm.data(), FRAME_LENGTH);
    if (n < 0) {
      ESP_LOGW(TAG, "snd_pcm_readi: %s", snd_strerror((int) n));
      if (snd_pcm_recover(this->pcm_, (int) n, 1) < 0) {
        this->thread_failed_ = true;
        break;
      }
      continue;
    }
    if ((size_t) n != FRAME_LENGTH)
      continue;  // short read (only at stop); the AEC needs whole frames
    this->process_(pcm.data(), reinterpret_cast<int16_t *>(out.data()));
    this->data_callbacks_.call(out);
  }

  this->close_();
  ESP_LOGD(TAG, "Capture stopped");
  this->thread_done_ = true;
}

}  // namespace esphome::baidu_mic

#endif  // USE_HOST
