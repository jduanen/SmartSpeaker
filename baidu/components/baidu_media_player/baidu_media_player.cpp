#include "baidu_media_player.h"

#ifdef USE_HOST

#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::baidu_media_player {

static const char *const TAG = "baidu_media_player";

void BaiduMediaPlayer::set_speaker(baidu_speaker::BaiduSpeaker *speaker) {
  this->speaker_ = speaker;
  speaker->add_input(&this->media_.source, true);
  speaker->add_input(&this->announcement_.source, false);
}

void BaiduMediaPlayer::setup() {
  this->state = media_player::MEDIA_PLAYER_STATE_IDLE;
  this->pref_ = this->make_entity_preference<VolumeRestoreState>();
  VolumeRestoreState restore;
  if (this->pref_.load(&restore)) {
    this->set_volume_(restore.volume);
    this->set_mute_(restore.is_muted);
  } else {
    this->set_volume_(this->volume_initial_);
    this->set_mute_(false);
  }
  this->publish_state();
}

void BaiduMediaPlayer::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Baidu media player:\n"
                "  Volume increment: %.0f%%\n"
                "  Supported format: FLAC 48000 Hz mono (media and announcements)",
                this->volume_increment_ * 100);
}

media_player::MediaPlayerTraits BaiduMediaPlayer::get_traits() {
  auto traits = media_player::MediaPlayerTraits();
  traits.set_supports_pause(true);
  // the mixer's own format, so HA's ffmpeg proxy and TTS deliver it without
  // resampling here (and Music Assistant reads the same list)
  for (auto purpose : {media_player::MediaPlayerFormatPurpose::PURPOSE_DEFAULT,
                       media_player::MediaPlayerFormatPurpose::PURPOSE_ANNOUNCEMENT}) {
    traits.get_supported_formats().push_back(
        media_player::MediaPlayerSupportedFormat{"flac", baidu_speaker::BaiduSpeaker::RATE, 1, purpose, 2});
  }
  return traits;
}

void BaiduMediaPlayer::play_url_(Pipeline &pipeline, const std::string &url, bool enqueue) {
  if (enqueue && !pipeline.playlist.empty()) {
    pipeline.playlist.push_back(url);
    return;
  }
  pipeline.playlist.clear();
  pipeline.playlist.push_back(url);
  pipeline.source.play(url);
}

void BaiduMediaPlayer::stop_(Pipeline &pipeline) {
  pipeline.playlist.clear();
  pipeline.source.stop();
}

void BaiduMediaPlayer::advance_(Pipeline &pipeline) {
  if (pipeline.source.state() != Source::State::FINISHED)
    return;
  if (pipeline.source.failed() && !pipeline.playlist.empty())
    ESP_LOGW(TAG, "Could not play %s", pipeline.playlist.front().c_str());
  if (!pipeline.playlist.empty())
    pipeline.playlist.pop_front();
  if (pipeline.playlist.empty())
    pipeline.source.stop();
  else
    pipeline.source.play(pipeline.playlist.front());
}

void BaiduMediaPlayer::control(const media_player::MediaPlayerCall &call) {
  const bool announce = call.get_announcement().value_or(false);
  Pipeline &pipeline = announce ? this->announcement_ : this->media_;
  const auto &cmd = call.get_command();

  if (call.get_media_url().has_value()) {
    bool enqueue = cmd.has_value() && *cmd == media_player::MEDIA_PLAYER_COMMAND_ENQUEUE;
    this->play_url_(pipeline, *call.get_media_url(), enqueue);
  } else if (call.get_volume().has_value()) {
    this->set_volume_(*call.get_volume());
    this->save_volume_();
  } else if (cmd.has_value()) {
    switch (*cmd) {
      case media_player::MEDIA_PLAYER_COMMAND_PLAY:
        this->media_.source.resume();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_PAUSE:
        this->media_.source.pause();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_TOGGLE:
        if (this->media_.source.state() == Source::State::PAUSED)
          this->media_.source.resume();
        else
          this->media_.source.pause();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_STOP:
        this->stop_(pipeline);
        break;
      case media_player::MEDIA_PLAYER_COMMAND_MUTE:
        this->set_mute_(true);
        this->save_volume_();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_UNMUTE:
        this->set_mute_(false);
        this->save_volume_();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_VOLUME_UP:
        this->set_volume_(std::min(1.0f, this->volume + this->volume_increment_));
        this->save_volume_();
        break;
      case media_player::MEDIA_PLAYER_COMMAND_VOLUME_DOWN:
        this->set_volume_(std::max(0.0f, this->volume - this->volume_increment_));
        this->save_volume_();
        break;
      default:
        ESP_LOGD(TAG, "Unsupported command %s", media_player::media_player_command_to_string(*cmd));
        break;
    }
  }
  // publish now: an item that fails at once must still show ANNOUNCING or
  // PLAYING, or voice_assistant would wait for a reply that never starts
  this->update_state_();
  this->publish_state();
}

void BaiduMediaPlayer::loop() {
  this->advance_(this->announcement_);
  this->advance_(this->media_);
  auto old_state = this->state;
  this->update_state_();
  if (this->state != old_state)
    this->publish_state();
}

void BaiduMediaPlayer::update_state_() {
  auto ann = this->announcement_.source.state();
  auto media = this->media_.source.state();
  if (ann == Source::State::PLAYING || ann == Source::State::PAUSED || !this->announcement_.playlist.empty()) {
    this->state = media_player::MEDIA_PLAYER_STATE_ANNOUNCING;
  } else if (media == Source::State::PAUSED) {
    this->state = media_player::MEDIA_PLAYER_STATE_PAUSED;
  } else if (media == Source::State::PLAYING || !this->media_.playlist.empty()) {
    this->state = media_player::MEDIA_PLAYER_STATE_PLAYING;
  } else {
    this->state = media_player::MEDIA_PLAYER_STATE_IDLE;
  }
}

void BaiduMediaPlayer::set_volume_(float volume) {
  this->volume = std::max(0.0f, std::min(1.0f, volume));
  this->speaker_->set_volume(this->volume);
}

void BaiduMediaPlayer::set_mute_(bool muted) {
  this->muted_ = muted;
  this->speaker_->set_mute_state(muted);
}

void BaiduMediaPlayer::save_volume_() {
  VolumeRestoreState state{this->volume, this->muted_};
  this->pref_.save(&state);
}

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
