/*
 * SmartSpeaker: on-device wake word detection (microWakeWord)
 *
 * Mirrors esphome/components/micro_wake_word: 16 kHz audio becomes 40 mel
 * features every 10 ms (ESPMicroSpeechFeatures), which drive streaming
 * TFLite Micro models. A word is detected when the mean of its last
 * `sliding_window_size` probabilities exceeds the cutoff.
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "audio/audio.hpp"
#include "config.hpp"

struct FrontendState; // ESPMicroSpeechFeatures frontend.h

namespace smartspeaker {

class WakeWord {
public:
  enum class Detection { NONE, WAKE, STOP };

  /** A wake word HA can choose (VoiceAssistantConfigurationResponse). */
  struct Info {
    std::string id; // file stem, e.g. "okay_nabu"
    std::string phrase; // e.g. "Okay Nabu"
    std::vector<std::string> languages;
  };

  static const char *const SENSITIVITIES[3];

  WakeWord(Config *config);
  ~WakeWord();

  // Audio input thread
  // -------------------------------------------------------------------------

  /** Run the frame through the active models. */
  Detection process(const AudioFrame *frame);

  // Main loop
  // -------------------------------------------------------------------------

  const std::vector<Info> &available() const { return infos; }
  std::string active();
  std::string active_phrase();
  /** Switch to wake word `id` (one of available()); saved to config.ini. */
  bool set_active(const std::string &id);
  std::string sensitivity();
  /** One of SENSITIVITIES; saved to config.ini. */
  bool set_sensitivity(const std::string &level);
  /** The "stop" word is only listened for during replies and timer rings. */
  void set_stop_enabled(bool enabled);

  int32_t pv_frame_length;
  size_t sample_rate;

  struct Model;

private:

  Config *const config;
  std::mutex lock; // models, active_model, stop_enabled, cutoffs
  std::vector<std::unique_ptr<Model>> models;
  std::vector<Info> infos;
  Model *active_model = nullptr;
  Model *stop_model = nullptr;
  bool stop_enabled = false;
  size_t sensitivity_level = 0;

  ::FrontendState *frontend = nullptr;

  void apply_sensitivity();
};

} // namespace smartspeaker
