/*
 * SmartSpeaker: FLAC / MP3 / WAV decoding to interleaved S16
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"

#include "audio/stream.hpp"

namespace smartspeaker {

class Decoder {
public:
  ~Decoder();

  /** Detect the format from the first bytes of `stream` and open it. */
  bool open(ByteStream *stream);

  /** Read up to `frames` interleaved frames; 0 at the end. */
  size_t read(int16_t *out, size_t frames);

  unsigned channels = 0;
  unsigned sample_rate = 0;

private:
  enum class Format { NONE, FLAC, MP3, WAV } format = Format::NONE;
  ByteStream *stream = nullptr;
  drflac *flac = nullptr;
  drmp3 mp3;
  drwav wav;
};

} // namespace smartspeaker
