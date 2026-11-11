/*
 * SmartSpeaker: Assist voice pipeline over the ESPHome API
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "audio/audio.hpp"
#include "esphome/entities.hpp"

namespace smartspeaker {

class App;

/**
 * Device side of an Assist pipeline run, following ESPHome's
 * voice_assistant component: request a run, stream mic audio once HA
 * accepts it, and turn HA's pipeline events into state machine events.
 * Also tracks Assist timers and answers the configuration request.
 */
class VoiceSession {
public:
  // DeviceInfo.voice_assistant_feature_flags
  static const uint32_t FEATURES = (1 << 0)    // VOICE_ASSISTANT
                                   | (1 << 2)  // API_AUDIO
                                   | (1 << 3)  // TIMERS
                                   | (1 << 4)  // ANNOUNCE
                                   | (1 << 5); // START_CONVERSATION

  VoiceSession(App *app);

  /** The connection that subscribed to voice; empty when there is none. */
  void set_client(esphome::Send send);

  /** Handle a voice message from HA; false if `type` isn't one. */
  bool handle(uint32_t type, const uint8_t *payload, size_t len);

  /** Ask HA to start a run; false if HA isn't subscribed. */
  bool start(const std::string &wake_word_phrase = "");
  /** Abort the run (button press, error). */
  void stop();
  /** Mic audio from Listening; buffered until HA accepts the run. */
  void audio(const AudioFrame &frame);
  void announce_finished(bool success);

  bool continue_conversation() const { return continue_conversation_; }

private:
  App *const app;
  esphome::Send client;

  bool running = false;  // a run was requested and hasn't ended
  bool accepted = false; // HA replied to the start request
  std::vector<int16_t> pending; // mic audio before `accepted`
  std::string conversation_id;
  bool continue_conversation_ = false;
  std::string tts_url;   // from RUN_START, for early streaming
  bool tts_sent = false; // TtsUrl already dispatched for this run

  struct Timer {
    std::string name;
    uint32_t total_seconds;
    uint32_t seconds_left;
    bool active;
  };
  std::map<std::string, Timer> timers;

  void send_audio(const int16_t *samples, size_t count);
  void on_event(uint32_t event, const std::map<std::string, std::string> &data);
};

} // namespace smartspeaker
