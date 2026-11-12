/*
 * SmartSpeaker: Assist voice pipeline over the ESPHome API
 *
 * Mirrors esphome/components/voice_assistant/voice_assistant.cpp: the run
 * starts with VoiceAssistantRequest(start), HA replies with
 * VoiceAssistantResponse(port=0: audio over the API), mic audio streams as
 * VoiceAssistantAudio until STT_VAD_END, and the reply is a TTS URL played
 * on the announce source.
 */

#include "voice/session.hpp"

#include <algorithm>
#include <glib.h>
#include <string.h>

#include "app.hpp"
#include "audio/audioinput.hpp"
#include "esphome/api.pb.h"
#include "esphome/api_ids.h"
#include "pb_decode.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::VoiceSession"

namespace smartspeaker {

namespace events = state::events;

// Mic audio kept while waiting for HA to accept the run (~3 s at 16 kHz)
static const size_t MAX_PENDING = 3 * 16000;

static char *s(const char *str) { return const_cast<char *>(str); }

template <typename T>
static bool decode(const pb_msgdesc_t *fields, const uint8_t *p, size_t n,
                   T *msg) {
  pb_istream_t stream = pb_istream_from_buffer(p, n);
  if (!pb_decode(&stream, fields, msg)) {
    g_warning("decode failed: %s", PB_GET_ERROR(&stream));
    return false;
  }
  return true;
}

VoiceSession::VoiceSession(App *app) : app(app) {}

void VoiceSession::set_client(esphome::Send send) {
  client = std::move(send);
  if (!client && running) {
    // HA went away mid-run
    running = false;
    app->dispatch(new events::voice::Error("not-connected", "HA disconnected"));
  }
}

bool VoiceSession::start(const std::string &wake_word_phrase) {
  if (!client) {
    g_warning("Cannot start a voice run: HA is not subscribed");
    return false;
  }
  running = true;
  accepted = false;
  pending.clear();
  tts_url.clear();
  tts_sent = false;
  continue_conversation_ = false;

  VoiceAssistantRequest req = VoiceAssistantRequest_init_zero;
  req.start = true;
  req.conversation_id = s(conversation_id.c_str());
  req.flags = VoiceAssistantRequestFlag_VOICE_ASSISTANT_REQUEST_USE_VAD;
  req.has_audio_settings = true;
  req.audio_settings.volume_multiplier = 1.0f;
  req.wake_word_phrase = s(wake_word_phrase.c_str());
  client(MSG_VOICE_ASSISTANT_REQUEST, VoiceAssistantRequest_fields, &req);
  g_message("Requested a voice run (conversation '%s')",
            conversation_id.c_str());
  return true;
}

void VoiceSession::stop() {
  if (!running)
    return;
  running = false;
  accepted = false;
  pending.clear();
  if (client) {
    VoiceAssistantRequest req = VoiceAssistantRequest_init_zero;
    req.start = false;
    client(MSG_VOICE_ASSISTANT_REQUEST, VoiceAssistantRequest_fields, &req);
  }
}

void VoiceSession::send_audio(const int16_t *samples, size_t count) {
  // VoiceAssistantAudio.data: 16 kHz mono S16LE (the ARM is little-endian)
  size_t bytes = count * sizeof(int16_t);
  std::vector<uint8_t> buf(PB_BYTES_ARRAY_T_ALLOCSIZE(bytes));
  pb_bytes_array_t *data = reinterpret_cast<pb_bytes_array_t *>(buf.data());
  data->size = bytes;
  memcpy(data->bytes, samples, bytes);

  VoiceAssistantAudio msg = VoiceAssistantAudio_init_zero;
  msg.data = data;
  client(MSG_VOICE_ASSISTANT_AUDIO, VoiceAssistantAudio_fields, &msg);
}

void VoiceSession::audio(const AudioFrame &frame) {
  if (!running || !client)
    return;
  if (!accepted) {
    if (pending.size() + frame.length <= MAX_PENDING)
      pending.insert(pending.end(), frame.samples,
                     frame.samples + frame.length);
    return;
  }
  send_audio(frame.samples, frame.length);
}

void VoiceSession::announce_finished(bool success) {
  if (!client)
    return;
  VoiceAssistantAnnounceFinished msg = VoiceAssistantAnnounceFinished_init_zero;
  msg.success = success;
  client(MSG_VOICE_ASSISTANT_ANNOUNCE_FINISHED,
         VoiceAssistantAnnounceFinished_fields, &msg);
}

bool VoiceSession::handle(uint32_t type, const uint8_t *payload, size_t len) {
  switch (type) {
    case MSG_VOICE_ASSISTANT_RESPONSE: {
      VoiceAssistantResponse resp = VoiceAssistantResponse_init_zero;
      if (!decode(VoiceAssistantResponse_fields, payload, len, &resp))
        return true;
      if (!running)
        return true;
      if (resp.error) {
        running = false;
        app->dispatch(
            new events::voice::Error("start-failed", "HA refused the run"));
        return true;
      }
      // port 0: audio goes over the API connection
      accepted = true;
      for (size_t i = 0; i < pending.size(); i += 512)
        send_audio(pending.data() + i, std::min<size_t>(512, pending.size() - i));
      pending.clear();
      return true;
    }

    case MSG_VOICE_ASSISTANT_EVENT_RESPONSE: {
      VoiceAssistantEventResponse ev = VoiceAssistantEventResponse_init_zero;
      if (!decode(VoiceAssistantEventResponse_fields, payload, len, &ev))
        return true;
      std::map<std::string, std::string> data;
      for (pb_size_t i = 0; i < ev.data_count; i++)
        data[ev.data[i].name ? ev.data[i].name : ""] =
            ev.data[i].value ? ev.data[i].value : "";
      pb_release(VoiceAssistantEventResponse_fields, &ev);
      on_event(ev.event_type, data);
      return true;
    }

    case MSG_VOICE_ASSISTANT_ANNOUNCE_REQUEST: {
      VoiceAssistantAnnounceRequest req = VoiceAssistantAnnounceRequest_init_zero;
      if (!decode(VoiceAssistantAnnounceRequest_fields, payload, len, &req))
        return true;
      g_message("Announcement: \"%s\"", req.text ? req.text : "");
      app->dispatch(new events::voice::Announce(
          req.media_id ? req.media_id : "",
          req.preannounce_media_id ? req.preannounce_media_id : "",
          req.start_conversation));
      pb_release(VoiceAssistantAnnounceRequest_fields, &req);
      return true;
    }

    case MSG_VOICE_ASSISTANT_TIMER_EVENT_RESPONSE: {
      VoiceAssistantTimerEventResponse ev =
          VoiceAssistantTimerEventResponse_init_zero;
      if (!decode(VoiceAssistantTimerEventResponse_fields, payload, len, &ev))
        return true;
      std::string id = ev.timer_id ? ev.timer_id : "";
      Timer t = {ev.name ? ev.name : "", ev.total_seconds, ev.seconds_left,
                 ev.is_active};
      pb_release(VoiceAssistantTimerEventResponse_fields, &ev);
      switch (ev.event_type) {
        case VoiceAssistantTimerEvent_VOICE_ASSISTANT_TIMER_STARTED:
        case VoiceAssistantTimerEvent_VOICE_ASSISTANT_TIMER_UPDATED:
          timers[id] = t;
          g_message("Timer '%s' %s: %u of %u s left", t.name.c_str(),
                    t.active ? "running" : "paused", t.seconds_left,
                    t.total_seconds);
          break;
        case VoiceAssistantTimerEvent_VOICE_ASSISTANT_TIMER_CANCELLED:
          timers.erase(id);
          g_message("Timer '%s' cancelled", t.name.c_str());
          break;
        case VoiceAssistantTimerEvent_VOICE_ASSISTANT_TIMER_FINISHED:
          timers.erase(id);
          g_message("Timer '%s' finished", t.name.c_str());
          app->dispatch(new events::voice::TimerFinished());
          break;
      }
      return true;
    }

    case MSG_VOICE_ASSISTANT_CONFIGURATION_REQUEST: {
      // voice_assistant.cpp get_configuration(): one active wake word
      WakeWord *ww = app->audio_input->wake_word();
      const auto &infos = ww->available();
      std::vector<VoiceAssistantWakeWord> words(infos.size());
      std::vector<std::vector<char *>> languages(infos.size());
      for (size_t i = 0; i < infos.size(); i++) {
        for (const auto &lang : infos[i].languages)
          languages[i].push_back(s(lang.c_str()));
        words[i] = VoiceAssistantWakeWord_init_zero;
        words[i].id = s(infos[i].id.c_str());
        words[i].wake_word = s(infos[i].phrase.c_str());
        words[i].trained_languages_count = languages[i].size();
        words[i].trained_languages = languages[i].data();
      }
      std::string active = ww->active();
      char *active_ids[] = {s(active.c_str())};

      VoiceAssistantConfigurationResponse resp =
          VoiceAssistantConfigurationResponse_init_zero;
      resp.available_wake_words_count = words.size();
      resp.available_wake_words = words.data();
      resp.active_wake_words_count = active.empty() ? 0 : 1;
      resp.active_wake_words = active_ids;
      resp.max_active_wake_words = words.empty() ? 0 : 1;
      client(MSG_VOICE_ASSISTANT_CONFIGURATION_RESPONSE,
             VoiceAssistantConfigurationResponse_fields, &resp);
      return true;
    }

    case MSG_VOICE_ASSISTANT_SET_CONFIGURATION: {
      VoiceAssistantSetConfiguration req =
          VoiceAssistantSetConfiguration_init_zero;
      if (!decode(VoiceAssistantSetConfiguration_fields, payload, len, &req))
        return true;
      // an empty list would mean no wake word; keep the current one
      if (req.active_wake_words_count > 0 && req.active_wake_words[0])
        app->audio_input->wake_word()->set_active(req.active_wake_words[0]);
      pb_release(VoiceAssistantSetConfiguration_fields, &req);
      return true;
    }

    default:
      return false;
  }
}

void VoiceSession::on_event(uint32_t event,
                            const std::map<std::string, std::string> &data) {
  auto get = [&data](const char *key) {
    auto it = data.find(key);
    return it == data.end() ? std::string() : it->second;
  };

  switch (event) {
    case VoiceAssistantEvent_VOICE_ASSISTANT_RUN_START:
      g_message("Pipeline running");
      tts_url = get("url");
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_STT_VAD_START:
      g_message("Speech started");
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_STT_VAD_END:
      g_message("Speech ended");
      accepted = false; // stop streaming
      app->dispatch(new events::voice::VadEnd());
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_STT_END:
      g_message("Heard: \"%s\"", get("text").c_str());
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_INTENT_PROGRESS:
      if (get("tts_start_streaming") == "1" && !tts_url.empty() && !tts_sent) {
        tts_sent = true;
        app->dispatch(new events::voice::TtsUrl(tts_url));
      }
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_INTENT_END:
      conversation_id = get("conversation_id");
      continue_conversation_ = get("continue_conversation") == "1";
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_TTS_START:
      g_message("Response: \"%s\"", get("text").c_str());
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_TTS_END: {
      std::string url = get("url");
      if (!tts_sent && !url.empty()) {
        tts_sent = true;
        app->dispatch(new events::voice::TtsUrl(url));
      }
      break;
    }

    case VoiceAssistantEvent_VOICE_ASSISTANT_RUN_END:
      g_message("Pipeline ended");
      running = false;
      accepted = false;
      app->dispatch(new events::voice::RunEnd());
      break;

    case VoiceAssistantEvent_VOICE_ASSISTANT_ERROR: {
      std::string code = get("code");
      // wake word outcomes are followed by RUN_END; nothing to do here
      if (code == "wake-word-timeout" || code == "wake_word_detection_aborted" ||
          code == "no_wake_word")
        break;
      g_warning("Pipeline error: %s - %s", code.c_str(), get("message").c_str());
      stop();
      app->dispatch(new events::voice::Error(code, get("message")));
      break;
    }

    default:
      g_debug("Pipeline event %u", event);
      break;
  }
}

} // namespace smartspeaker
