/*
 * SmartSpeaker: a playable audio source feeding the mixer
 */

#include "audio/source.hpp"

#include <chrono>
#include <glib.h>
#include <speex/speex_resampler.h>

#include "audio/decoder.hpp"
#include "audio/stream.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::Source"

namespace smartspeaker {

static const size_t RING_SAMPLES = 1 << 16; // ~1.4 s at 48 kHz
static const size_t DECODE_FRAMES = 1152;   // one MP3 frame
static const int RESAMPLE_QUALITY = 3;      // speexdsp 0-10; 3 is cheap and clean

Source::Source(const char *name, unsigned rate,
               std::function<void()> on_change)
    : name(name), rate(rate), on_change(std::move(on_change)), cancel(false),
      eof(false), st(State::IDLE), ring_data(RING_SAMPLES) {
  PaUtil_InitializeRingBuffer(&ring, sizeof(int16_t), RING_SAMPLES,
                              ring_data.data());
}

Source::~Source() {
  cancel = true;
  if (worker.joinable())
    worker.join();
}

void Source::set_state(State s) {
  if (st.exchange(s) == s)
    return;
  // hop to the main loop; the callback touches app state
  auto *cb = new std::function<void()>(on_change);
  g_idle_add(
      [](gpointer p) -> gboolean {
        auto *f = static_cast<std::function<void()> *>(p);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
      },
      cb);
}

void Source::play(const std::string &location) {
  stop();
  cancel = false;
  eof = false;
  g_message("[%s] play %s", name, location.c_str());
  set_state(State::PLAYING);
  worker = std::thread(&Source::run, this, location);
}

void Source::stop() {
  cancel = true;
  if (worker.joinable())
    worker.join();
  PaUtil_FlushRingBuffer(&ring);
  set_state(State::IDLE);
}

void Source::pause() {
  if (st == State::PLAYING)
    set_state(State::PAUSED);
}

void Source::resume() {
  if (st == State::PAUSED)
    set_state(State::PLAYING);
}

size_t Source::read(int16_t *out, size_t n) {
  if (st != State::PLAYING)
    return 0;
  size_t got = PaUtil_ReadRingBuffer(&ring, out, n);
  if (got == 0 && eof)
    set_state(State::FINISHED);
  return got;
}

/** Worker thread: fetch, decode, downmix, resample, buffer. */
void Source::run(std::string location) {
  ByteStream stream(cancel);
  Decoder decoder;
  if (!stream.open(location) || !decoder.open(&stream)) {
    if (!cancel) // a newer play() or stop() is not an error
      g_warning("[%s] cannot play %s", name, location.c_str());
    eof = true;
    return;
  }

  SpeexResamplerState *resampler = nullptr;
  if (decoder.sample_rate != rate) {
    int err;
    resampler = speex_resampler_init(1, decoder.sample_rate, rate,
                                     RESAMPLE_QUALITY, &err);
    // skip the filter's startup latency so the audio starts on time
    speex_resampler_skip_zeros(resampler);
  }

  std::vector<int16_t> pcm(DECODE_FRAMES * decoder.channels);
  std::vector<int16_t> mono(DECODE_FRAMES);
  std::vector<int16_t> out(DECODE_FRAMES * rate / decoder.sample_rate + 64);

  size_t frames;
  while (!cancel && (frames = decoder.read(pcm.data(), DECODE_FRAMES)) > 0) {
    for (size_t i = 0; i < frames; i++) {
      int32_t sum = 0;
      for (unsigned c = 0; c < decoder.channels; c++)
        sum += pcm[i * decoder.channels + c];
      mono[i] = (int16_t)(sum / (int32_t)decoder.channels);
    }

    const int16_t *data = mono.data();
    spx_uint32_t count = frames;
    if (resampler) {
      spx_uint32_t in_len = frames, out_len = out.size();
      speex_resampler_process_int(resampler, 0, mono.data(), &in_len,
                                  out.data(), &out_len);
      data = out.data();
      count = out_len;
    }

    // wait for the mixer to make room; the ring holds ~1.4 s
    while (!cancel &&
           (spx_uint32_t)PaUtil_GetRingBufferWriteAvailable(&ring) < count)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    PaUtil_WriteRingBuffer(&ring, data, count);
  }

  if (resampler)
    speex_resampler_destroy(resampler);
  eof = true;
}

} // namespace smartspeaker
