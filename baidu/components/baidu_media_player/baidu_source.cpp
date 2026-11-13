#include "baidu_source.h"

#ifdef USE_HOST

#include "baidu_decoder.h"
#include "baidu_stream.h"

#include "esphome/core/log.h"

#include <speex/speex_resampler.h>

#include <chrono>

namespace esphome::baidu_media_player {

static const char *const TAG = "baidu_media_player.source";

static const unsigned RATE = baidu_speaker::BaiduSpeaker::RATE;
static const size_t RING_SAMPLES = 1 << 16;  // ~1.4 s at 48 kHz
static const size_t DECODE_FRAMES = 1152;    // one MP3 frame
static const int RESAMPLE_QUALITY = 3;       // speexdsp 0-10; 3 is cheap and clean

Source::Source(const char *name, long stall_seconds, unsigned retries)
    : name_(name), stall_seconds_(stall_seconds), retries_(retries), ring_data_(RING_SAMPLES) {
  PaUtil_InitializeRingBuffer(&this->ring_, sizeof(int16_t), RING_SAMPLES, this->ring_data_.data());
}

Source::~Source() {
  this->cancel_ = true;
  if (this->worker_.joinable())
    this->worker_.join();
}

void Source::play(const std::string &location) {
  this->stop();
  this->cancel_ = false;
  this->eof_ = false;
  this->failed_ = false;
  this->started_ = false;
  ESP_LOGD(TAG, "[%s] play %s", this->name_, location.c_str());
  this->state_ = State::PLAYING;
  this->worker_ = std::thread(&Source::run_, this, location);
}

void Source::stop() {
  this->cancel_ = true;
  if (this->worker_.joinable())
    this->worker_.join();
  this->flush_ = true;
  this->state_ = State::IDLE;
}

void Source::pause() {
  State expected = State::PLAYING;
  this->state_.compare_exchange_strong(expected, State::PAUSED);
}

void Source::resume() {
  State expected = State::PAUSED;
  this->state_.compare_exchange_strong(expected, State::PLAYING);
}

size_t Source::read(int16_t *out, size_t n) {
  if (this->flush_.exchange(false))
    PaUtil_FlushRingBuffer(&this->ring_);
  if (this->state_ != State::PLAYING)
    return 0;
  size_t got = PaUtil_ReadRingBuffer(&this->ring_, out, n);
  if (got == 0 && this->eof_) {
    State expected = State::PLAYING;
    this->state_.compare_exchange_strong(expected, State::FINISHED);
  }
  // underrun: the ring ran dry mid-stream, i.e. the audio arrived too slowly
  // (not while waiting for the first audio, nor at the end); reported when
  // the audio resumes, with the length of the silence it caused
  if (!this->started_) {
    this->started_ = got > 0;
    this->gap_ = 0;
    return got;
  }
  if (got > 0 && this->gap_ > 0) {
    ESP_LOGW(TAG, "[%s] underrun: %u ms of silence (the audio arrived too slowly)", this->name_,
             (unsigned) (this->gap_ * 1000 / RATE));
    this->gap_ = 0;
  }
  if (got < n && !this->eof_)
    this->gap_ += n - got;
  return got;
}

/// Worker thread: fetch, decode, downmix, resample, buffer.
void Source::run_(std::string location) {
  // let the mixer drop the previous item first, or it would drop our start
  while (this->flush_ && !this->cancel_)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

  ByteStream stream(this->cancel_);
  Decoder decoder;
  if (!stream.open(location, this->stall_seconds_, this->retries_) || !decoder.open(&stream)) {
    if (!this->cancel_) {  // a newer play() or stop() is not an error
      ESP_LOGW(TAG, "[%s] cannot play %s", this->name_, location.c_str());
      this->failed_ = true;
    }
    this->eof_ = true;
    return;
  }

  SpeexResamplerState *resampler = nullptr;
  if (decoder.sample_rate != RATE) {
    int err;
    resampler = speex_resampler_init(1, decoder.sample_rate, RATE, RESAMPLE_QUALITY, &err);
    // skip the filter's startup latency so the audio starts on time
    speex_resampler_skip_zeros(resampler);
  }

  std::vector<int16_t> pcm(DECODE_FRAMES * decoder.channels);
  std::vector<int16_t> mono(DECODE_FRAMES);
  std::vector<int16_t> out(DECODE_FRAMES * RATE / decoder.sample_rate + 64);

  size_t frames;
  while (!this->cancel_ && (frames = decoder.read(pcm.data(), DECODE_FRAMES)) > 0) {
    for (size_t i = 0; i < frames; i++) {
      int32_t sum = 0;
      for (unsigned c = 0; c < decoder.channels; c++)
        sum += pcm[i * decoder.channels + c];
      mono[i] = (int16_t) (sum / (int32_t) decoder.channels);
    }

    const int16_t *data = mono.data();
    spx_uint32_t count = frames;
    if (resampler != nullptr) {
      spx_uint32_t in_len = frames, out_len = out.size();
      speex_resampler_process_int(resampler, 0, mono.data(), &in_len, out.data(), &out_len);
      data = out.data();
      count = out_len;
    }

    // wait for the mixer to make room; the ring holds ~1.4 s
    while (!this->cancel_ && (spx_uint32_t) PaUtil_GetRingBufferWriteAvailable(&this->ring_) < count)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    PaUtil_WriteRingBuffer(&this->ring_, data, count);
  }

  if (resampler != nullptr)
    speex_resampler_destroy(resampler);
  this->eof_ = true;
}

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
