#include "baidu_decoder.h"

#ifdef USE_HOST

#include "esphome/core/log.h"

#include <cstring>

namespace esphome::baidu_media_player {

static const char *const TAG = "baidu_media_player.decoder";

// dr_libs callbacks. The stream seeks forward, or back within its first
// bytes, which is all the decoders need when they aren't asked to seek within
// the audio.
static size_t on_read(void *user, void *buf, size_t n) { return static_cast<ByteStream *>(user)->read(buf, n); }

static bool on_seek(void *user, int offset, int origin_cur) {
  ByteStream *s = static_cast<ByteStream *>(user);
  int64_t target = origin_cur ? (int64_t) s->position() + offset : offset;
  return target >= 0 && s->seek_to(target);
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
  if (this->format_ == Format::FLAC)
    drflac_close(this->flac_);
  else if (this->format_ == Format::MP3)
    drmp3_uninit(&this->mp3_);
  else if (this->format_ == Format::WAV)
    drwav_uninit(&this->wav_);
}

bool Decoder::open(ByteStream *s) {
  uint8_t magic[4] = {0};
  if (s->peek(magic, 4) < 4)
    return false;  // empty, failed or cancelled; the caller reports it

  if (memcmp(magic, "fLaC", 4) == 0) {
    this->flac_ = drflac_open(on_read, flac_seek, flac_tell, s, nullptr);
    if (this->flac_ == nullptr)
      return false;
    this->format_ = Format::FLAC;
    this->channels = this->flac_->channels;
    this->sample_rate = this->flac_->sampleRate;
  } else if (memcmp(magic, "RIFF", 4) == 0) {
    if (!drwav_init(&this->wav_, on_read, wav_seek, wav_tell, s, nullptr))
      return false;
    this->format_ = Format::WAV;
    this->channels = this->wav_.channels;
    this->sample_rate = this->wav_.sampleRate;
  } else {
    // ID3 tag or a raw MPEG frame; dr_mp3 skips anything else it can't sync to
    if (!drmp3_init(&this->mp3_, on_read, mp3_seek, mp3_tell, nullptr, s, nullptr))
      return false;
    this->format_ = Format::MP3;
    this->channels = this->mp3_.channels;
    this->sample_rate = this->mp3_.sampleRate;
  }
  ESP_LOGD(TAG, "Decoding %s, %u Hz x%u",
           this->format_ == Format::FLAC  ? "FLAC"
           : this->format_ == Format::WAV ? "WAV"
                                          : "MP3",
           this->sample_rate, this->channels);
  return this->channels > 0 && this->sample_rate > 0;
}

size_t Decoder::read(int16_t *out, size_t frames) {
  switch (this->format_) {
    case Format::FLAC:
      return drflac_read_pcm_frames_s16(this->flac_, frames, out);
    case Format::MP3:
      return drmp3_read_pcm_frames_s16(&this->mp3_, frames, out);
    case Format::WAV:
      return drwav_read_pcm_frames_s16(&this->wav_, frames, out);
    default:
      return 0;
  }
}

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
