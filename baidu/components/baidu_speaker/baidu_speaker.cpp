// Mixer ported from the C++ app's src/audio/mixer.cpp; the LINEOUT volume
// from its src/audio/alsa/volume.cpp, which derives from Genie (Copyright 2021
// The Board of Trustees of the Leland Stanford Junior University, Apache
// License 2.0).

#include "baidu_speaker.h"

#ifdef USE_HOST

#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>

namespace esphome::baidu_speaker {

static const char *const TAG = "baidu_speaker";

static const size_t STREAM_RING_SAMPLES = 1 << 15;  // ~0.7 s at 48 kHz
static const int RESAMPLE_QUALITY = 3;              // speexdsp 0-10, as the media sources

BaiduSpeaker::StreamInput::StreamInput() : data(STREAM_RING_SAMPLES) {
  PaUtil_InitializeRingBuffer(&this->ring, sizeof(int16_t), STREAM_RING_SAMPLES, this->data.data());
}

size_t BaiduSpeaker::StreamInput::read(int16_t *out, size_t n) {
  if (this->flush.exchange(false))
    PaUtil_FlushRingBuffer(&this->ring);
  return PaUtil_ReadRingBuffer(&this->ring, out, n);
}

BaiduSpeaker::~BaiduSpeaker() {
  this->running_ = false;
  if (this->thread_.joinable())
    this->thread_.join();
  if (this->pcm_ != nullptr)
    snd_pcm_close(this->pcm_);
  if (this->resampler_ != nullptr)
    speex_resampler_destroy(this->resampler_);
}

void BaiduSpeaker::add_input(MixerInput *input, bool duckable) { this->inputs_.push_back({input, duckable}); }

void BaiduSpeaker::setup() {
  this->inputs_.push_back({&this->stream_, false});

  int err = snd_pcm_open(&this->pcm_, this->device_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    ESP_LOGE(TAG, "Cannot open %s: %s", this->device_.c_str(), snd_strerror(err));
    this->pcm_ = nullptr;
    this->mark_failed();
    return;
  }
  // mono: both LINEOUT channels carry DACL on the Xiaodu
  err = snd_pcm_set_params(this->pcm_, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1,
                           4 * 20000 /* us, 4 periods */);
  if (err < 0) {
    ESP_LOGE(TAG, "Cannot configure %s: %s", this->device_.c_str(), snd_strerror(err));
    this->mark_failed();
    return;
  }
  this->running_ = true;
  this->thread_ = std::thread(&BaiduSpeaker::run_, this);
}

void BaiduSpeaker::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Baidu speaker:\n"
                "  Device: %s at %u Hz mono\n"
                "  Volume control: %s",
                this->device_.c_str(), RATE, this->volume_control_.c_str());
}

void BaiduSpeaker::loop() {
  if (this->finishing_ && !this->has_buffered_data()) {
    this->finishing_ = false;
    this->state_ = speaker::STATE_STOPPED;
  }
}

void BaiduSpeaker::apply_ducking(uint8_t decibel_reduction, uint32_t duration_ms) {
  std::lock_guard<std::mutex> lock(this->duck_lock_);
  this->duck_target_ = powf(10.0f, -decibel_reduction / 20.0f);
  this->duck_step_ = duration_ms ? 1.0f / (duration_ms * (RATE / 1000)) : 0.0f;
}

void BaiduSpeaker::run_() {
  std::vector<int16_t> in(PERIOD), out(PERIOD);
  std::vector<int32_t> ducked(PERIOD), plain(PERIOD);
  float gain = 1.0f;

  while (this->running_) {
    std::fill(ducked.begin(), ducked.end(), 0);
    std::fill(plain.begin(), plain.end(), 0);
    for (auto &input : this->inputs_) {
      size_t n = input.input->read(in.data(), PERIOD);
      auto &sum = input.duckable ? ducked : plain;
      for (size_t i = 0; i < n; i++)
        sum[i] += in[i];
    }

    float target, step;
    {
      std::lock_guard<std::mutex> lock(this->duck_lock_);
      target = this->duck_target_;
      step = this->duck_step_;
    }
    for (unsigned i = 0; i < PERIOD; i++) {
      if (step == 0.0f)
        gain = target;
      else if (gain < target)
        gain = std::min(target, gain + step);
      else if (gain > target)
        gain = std::max(target, gain - step);
      int32_t v = (int32_t) lrintf(ducked[i] * gain) + plain[i];
      out[i] = (int16_t) std::max(-32768, std::min(32767, v));
    }
    if (this->muted_)
      std::fill(out.begin(), out.end(), 0);

    snd_pcm_sframes_t w = snd_pcm_writei(this->pcm_, out.data(), PERIOD);
    if (w < 0) {
      w = snd_pcm_recover(this->pcm_, (int) w, 1);
      if (w < 0) {
        ESP_LOGW(TAG, "ALSA write failed: %s", snd_strerror((int) w));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
  }
}

size_t BaiduSpeaker::play(const uint8_t *data, size_t length) {
  if (this->state_ != speaker::STATE_RUNNING)
    this->start();
  if (this->stream_.flush)
    return 0;  // the mixer hasn't dropped the old data yet
  const unsigned channels = this->audio_stream_info_.get_channels();
  const uint32_t rate = this->audio_stream_info_.get_sample_rate();
  const size_t frame_bytes = channels * sizeof(int16_t);

  // take only as many input frames as the ring has room for once resampled
  size_t room = PaUtil_GetRingBufferWriteAvailable(&this->stream_.ring);
  size_t frames = std::min(length / frame_bytes, room * rate / RATE);
  if (frames > 0 && rate != RATE)
    frames--;  // leave room for the resampler's rounding
  if (frames == 0)
    return 0;

  const int16_t *pcm = reinterpret_cast<const int16_t *>(data);
  std::vector<int16_t> mono(frames);
  for (size_t i = 0; i < frames; i++) {
    int32_t sum = 0;
    for (unsigned c = 0; c < channels; c++)
      sum += pcm[i * channels + c];
    mono[i] = (int16_t) (sum / (int32_t) channels);
  }

  if (rate == RATE) {
    PaUtil_WriteRingBuffer(&this->stream_.ring, mono.data(), frames);
    return frames * frame_bytes;
  }
  if (this->resampler_ == nullptr || this->resampler_rate_ != rate) {
    if (this->resampler_ != nullptr)
      speex_resampler_destroy(this->resampler_);
    int err;
    this->resampler_ = speex_resampler_init(1, rate, RATE, RESAMPLE_QUALITY, &err);
    speex_resampler_skip_zeros(this->resampler_);
    this->resampler_rate_ = rate;
  }
  std::vector<int16_t> out(room);
  spx_uint32_t in_len = frames, out_len = room;
  speex_resampler_process_int(this->resampler_, 0, mono.data(), &in_len, out.data(), &out_len);
  PaUtil_WriteRingBuffer(&this->stream_.ring, out.data(), out_len);
  return in_len * frame_bytes;
}

void BaiduSpeaker::start() {
  this->finishing_ = false;
  this->state_ = speaker::STATE_RUNNING;
}

void BaiduSpeaker::stop() {
  this->stream_.flush = true;
  if (this->resampler_ != nullptr)
    speex_resampler_reset_mem(this->resampler_);
  this->finishing_ = false;
  this->state_ = speaker::STATE_STOPPED;
}

void BaiduSpeaker::finish() { this->finishing_ = true; }

bool BaiduSpeaker::has_buffered_data() const {
  return !this->stream_.flush && PaUtil_GetRingBufferReadAvailable(const_cast<PaUtilRingBuffer *>(&this->stream_.ring)) > 0;
}

void BaiduSpeaker::set_mute_state(bool mute_state) {
  speaker::Speaker::set_mute_state(mute_state);
  this->muted_ = mute_state;
}

void BaiduSpeaker::set_volume(float volume) {
  speaker::Speaker::set_volume(std::max(0.0f, std::min(1.0f, volume)));
  this->apply_volume_();
}

void BaiduSpeaker::apply_volume_() {
  snd_mixer_t *mixer = nullptr;
  if (snd_mixer_open(&mixer, 0) < 0)
    return;
  snd_mixer_selem_id_t *sid;
  snd_mixer_selem_id_alloca(&sid);
  snd_mixer_selem_id_set_index(sid, 0);
  snd_mixer_selem_id_set_name(sid, this->volume_control_.c_str());
  snd_mixer_elem_t *elem = nullptr;
  long min, max;
  if (snd_mixer_attach(mixer, this->device_.c_str()) < 0 || snd_mixer_selem_register(mixer, nullptr, nullptr) < 0 ||
      snd_mixer_load(mixer) < 0 || (elem = snd_mixer_find_selem(mixer, sid)) == nullptr ||
      snd_mixer_selem_get_playback_volume_range(elem, &min, &max) < 0) {
    ESP_LOGW(TAG, "Cannot find mixer control '%s' on %s", this->volume_control_.c_str(), this->device_.c_str());
  } else {
    long value = min + lroundf(this->volume_ * (max - min));
    snd_mixer_selem_set_playback_volume_all(elem, value);
    ESP_LOGD(TAG, "Volume %.0f%% (%s %ld)", this->volume_ * 100, this->volume_control_.c_str(), value);
  }
  snd_mixer_close(mixer);
}

}  // namespace esphome::baidu_speaker

#endif  // USE_HOST
