/*
 * SmartSpeaker: FLAC / MP3 / WAV decoding to interleaved S16
 */

#include "audio/decoder.hpp"

#include <glib.h>
#include <string.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::Decoder"

namespace smartspeaker {

// dr_libs callbacks. The stream only moves forward, which is all the
// decoders need when they aren't asked to seek within the audio.
static size_t on_read(void *user, void *buf, size_t n) {
  return static_cast<ByteStream *>(user)->read(buf, n);
}

static bool on_seek(void *user, int offset, int origin_cur) {
  ByteStream *s = static_cast<ByteStream *>(user);
  if (origin_cur)
    return offset >= 0 && s->skip(offset);
  return offset >= 0 && (uint64_t)offset >= s->position() &&
         s->skip(offset - s->position());
}

static drflac_bool32 flac_seek(void *u, int off, drflac_seek_origin o) {
  return o != DRFLAC_SEEK_END && on_seek(u, off, o == DRFLAC_SEEK_CUR);
}
static drflac_bool32 flac_tell(void *u, drflac_int64 *pos) {
  *pos = static_cast<ByteStream *>(u)->position();
  return DRFLAC_TRUE;
}
static drmp3_bool32 mp3_seek(void *u, int off, drmp3_seek_origin o) {
  return o != DRMP3_SEEK_END && on_seek(u, off, o == DRMP3_SEEK_CUR);
}
static drmp3_bool32 mp3_tell(void *u, drmp3_int64 *pos) {
  *pos = static_cast<ByteStream *>(u)->position();
  return DRMP3_TRUE;
}
static drwav_bool32 wav_seek(void *u, int off, drwav_seek_origin o) {
  return o != DRWAV_SEEK_END && on_seek(u, off, o == DRWAV_SEEK_CUR);
}
static drwav_bool32 wav_tell(void *u, drwav_int64 *pos) {
  *pos = static_cast<ByteStream *>(u)->position();
  return DRWAV_TRUE;
}

Decoder::~Decoder() {
  if (format == Format::FLAC)
    drflac_close(flac);
  else if (format == Format::MP3)
    drmp3_uninit(&mp3);
  else if (format == Format::WAV)
    drwav_uninit(&wav);
}

bool Decoder::open(ByteStream *s) {
  stream = s;
  uint8_t magic[4] = {0};
  if (s->peek(magic, 4) < 4)
    return false; // empty, failed or cancelled; the caller reports it

  if (memcmp(magic, "fLaC", 4) == 0) {
    flac = drflac_open(on_read, flac_seek, flac_tell, s, nullptr);
    if (!flac)
      return false;
    format = Format::FLAC;
    channels = flac->channels;
    sample_rate = flac->sampleRate;
  } else if (memcmp(magic, "RIFF", 4) == 0) {
    if (!drwav_init(&wav, on_read, wav_seek, wav_tell, s, nullptr))
      return false;
    format = Format::WAV;
    channels = wav.channels;
    sample_rate = wav.sampleRate;
  } else {
    // ID3 tag or a raw MPEG frame; dr_mp3 skips anything else it can't sync to
    if (!drmp3_init(&mp3, on_read, mp3_seek, mp3_tell, nullptr, s, nullptr))
      return false;
    format = Format::MP3;
    channels = mp3.channels;
    sample_rate = mp3.sampleRate;
  }
  g_message("Decoding %s, %u Hz x%u",
            format == Format::FLAC ? "FLAC"
            : format == Format::WAV ? "WAV"
                                    : "MP3",
            sample_rate, channels);
  return channels > 0 && sample_rate > 0;
}

size_t Decoder::read(int16_t *out, size_t frames) {
  switch (format) {
    case Format::FLAC:
      return drflac_read_pcm_frames_s16(flac, frames, out);
    case Format::MP3:
      return drmp3_read_pcm_frames_s16(&mp3, frames, out);
    case Format::WAV:
      return drwav_read_pcm_frames_s16(&wav, frames, out);
    default:
      return 0;
  }
}

} // namespace smartspeaker
