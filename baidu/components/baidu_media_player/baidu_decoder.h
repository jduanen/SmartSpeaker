#pragma once

#ifdef USE_HOST

#include "baidu_stream.h"

#include "dr_flac.h"
#include "dr_mp3.h"
#include "dr_wav.h"

#include <cstddef>
#include <cstdint>

namespace esphome::baidu_media_player {

/// FLAC / MP3 / WAV decoding to interleaved S16, the format detected from the
/// first bytes. Ported from the C++ app's src/audio/decoder.cpp.
class Decoder {
 public:
  ~Decoder();

  bool open(ByteStream *stream);
  /// Read up to `frames` interleaved frames; 0 at the end.
  size_t read(int16_t *out, size_t frames);

  unsigned channels{0};
  unsigned sample_rate{0};

 protected:
  enum class Format { NONE, FLAC, MP3, WAV } format_{Format::NONE};
  drflac *flac_{nullptr};
  drmp3 mp3_;
  drwav wav_;
};

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
