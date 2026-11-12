#pragma once

#ifdef USE_HOST

#include "baidu_source.h"

#include "esphome/components/baidu_speaker/baidu_speaker.h"
#include "esphome/components/media_player/media_player.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include <deque>
#include <string>

namespace esphome::baidu_media_player {

/// Media player with a media and an announcement pipeline (see
/// media_player.py). Commands follow ESPHome's speaker media player: a new URL
/// replaces the pipeline's playlist unless it is enqueued, STOP with
/// `announcement` stops the announcement pipeline, and the state is
/// ANNOUNCING while an announcement plays.
class BaiduMediaPlayer : public media_player::MediaPlayer, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  media_player::MediaPlayerTraits get_traits() override;
  bool is_muted() const override { return this->muted_; }

  /// Registers both sources as mixer inputs; from generated code, before the
  /// speaker's setup().
  void set_speaker(baidu_speaker::BaiduSpeaker *speaker);
  void set_volume_increment(float increment) { this->volume_increment_ = increment; }
  void set_volume_initial(float volume) { this->volume_initial_ = volume; }

 protected:
  struct Pipeline {
    Source source;
    std::deque<std::string> playlist;  // front is the item playing
  };
  struct VolumeRestoreState {
    float volume;
    bool is_muted;
  };

  void control(const media_player::MediaPlayerCall &call) override;
  void play_url_(Pipeline &pipeline, const std::string &url, bool enqueue);
  void stop_(Pipeline &pipeline);
  void advance_(Pipeline &pipeline);
  void update_state_();
  void set_volume_(float volume);
  void set_mute_(bool muted);
  void save_volume_();

  baidu_speaker::BaiduSpeaker *speaker_{nullptr};
  // the WiFi can drop whole TCP flows for tens of seconds; short TTS and
  // announcement files are cheap to fetch again
  Pipeline media_{Source("media", 30, 0), {}};
  Pipeline announcement_{Source("announcement", 5, 2), {}};
  bool muted_{false};
  float volume_increment_{0.05f};
  float volume_initial_{0.5f};
  ESPPreferenceObject pref_;
};

}  // namespace esphome::baidu_media_player

#endif  // USE_HOST
