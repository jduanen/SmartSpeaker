/*
 * SmartSpeaker: entities exposed to Home Assistant over the ESPHome API
 *
 * Names and options follow the Satellite1 / HA Voice PE configurations.
 * Behavior is wired in as the phases land: mic mute, wake sound, volume and
 * URL playback now; wake word, sensitivity, LED ring and button events
 * later.
 */

#include "esphome/entities.hpp"

#include <cmath>
#include <glib.h>

#include "app.hpp"
#include "audio/audiovolume.hpp"
#include "audio/mixer.hpp"
#include "esphome/api.pb.h"
#include "esphome/api_ids.h"
#include "pb_decode.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::esphome::Entities"

namespace smartspeaker {
namespace esphome {

// Keys only need to be unique within a connection; HA identifies entities
// by device MAC + object_id.
enum Key : uint32_t {
  KEY_MEDIA_PLAYER = 1,
  KEY_MIC_MUTE,
  KEY_WAKE_SOUND,
  KEY_WAKE_WORD,
  KEY_SENSITIVITY,
  KEY_LED_RING,
  KEY_BUTTON,
};

static const char *WAKE_WORDS[] = {"Okay Nabu", "Hey Jarvis", "Hey Mycroft"};
static const char *SENSITIVITIES[] = {"Slightly sensitive",
                                      "Moderately sensitive", "Very sensitive"};
static const char *BUTTON_EVENTS[] = {"double_press", "triple_press",
                                      "long_press"};

static const uint32_t MEDIA_FEATURES =
    (1 << 0)      // PAUSE
    | (1 << 2)    // VOLUME_SET
    | (1 << 3)    // VOLUME_MUTE
    | (1 << 9)    // PLAY_MEDIA
    | (1 << 12)   // STOP
    | (1 << 14)   // PLAY
    | (1 << 20);  // MEDIA_ANNOUNCE

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

Entities::Entities(App *app)
    : app(app), wake_word(WAKE_WORDS[0]), sensitivity(SENSITIVITIES[1]) {}

void Entities::list(const Send &send) {
  ListEntitiesMediaPlayerResponse mp = ListEntitiesMediaPlayerResponse_init_zero;
  mp.object_id = s("media_player");
  mp.key = KEY_MEDIA_PLAYER;
  mp.name = s("Media Player");
  mp.supports_pause = true;
  mp.feature_flags = MEDIA_FEATURES;
  send(MSG_LIST_ENTITIES_MEDIA_PLAYER_RESPONSE,
       ListEntitiesMediaPlayerResponse_fields, &mp);

  ListEntitiesSwitchResponse sw = ListEntitiesSwitchResponse_init_zero;
  sw.object_id = s("mute");
  sw.key = KEY_MIC_MUTE;
  sw.name = s("Mute microphone");
  sw.icon = s("mdi:microphone-off");
  send(MSG_LIST_ENTITIES_SWITCH_RESPONSE, ListEntitiesSwitchResponse_fields,
       &sw);

  sw = ListEntitiesSwitchResponse_init_zero;
  sw.object_id = s("wake_sound");
  sw.key = KEY_WAKE_SOUND;
  sw.name = s("Wake sound");
  sw.icon = s("mdi:bullhorn");
  sw.entity_category = EntityCategory_ENTITY_CATEGORY_CONFIG;
  send(MSG_LIST_ENTITIES_SWITCH_RESPONSE, ListEntitiesSwitchResponse_fields,
       &sw);

  ListEntitiesSelectResponse sel = ListEntitiesSelectResponse_init_zero;
  sel.object_id = s("wake_word");
  sel.key = KEY_WAKE_WORD;
  sel.name = s("Wake word");
  sel.icon = s("mdi:account-voice");
  sel.options_count = G_N_ELEMENTS(WAKE_WORDS);
  sel.options = const_cast<char **>(WAKE_WORDS);
  sel.entity_category = EntityCategory_ENTITY_CATEGORY_CONFIG;
  send(MSG_LIST_ENTITIES_SELECT_RESPONSE, ListEntitiesSelectResponse_fields,
       &sel);

  sel = ListEntitiesSelectResponse_init_zero;
  sel.object_id = s("wake_word_sensitivity");
  sel.key = KEY_SENSITIVITY;
  sel.name = s("Wake word sensitivity");
  sel.icon = s("mdi:tune-variant");
  sel.options_count = G_N_ELEMENTS(SENSITIVITIES);
  sel.options = const_cast<char **>(SENSITIVITIES);
  sel.entity_category = EntityCategory_ENTITY_CATEGORY_CONFIG;
  send(MSG_LIST_ENTITIES_SELECT_RESPONSE, ListEntitiesSelectResponse_fields,
       &sel);

  ColorMode modes[] = {ColorMode_COLOR_MODE_RGB};
  ListEntitiesLightResponse light = ListEntitiesLightResponse_init_zero;
  light.object_id = s("led_ring");
  light.key = KEY_LED_RING;
  light.name = s("LED ring");
  light.supported_color_modes_count = G_N_ELEMENTS(modes);
  light.supported_color_modes = modes;
  send(MSG_LIST_ENTITIES_LIGHT_RESPONSE, ListEntitiesLightResponse_fields,
       &light);

  ListEntitiesEventResponse ev = ListEntitiesEventResponse_init_zero;
  ev.object_id = s("button_press");
  ev.key = KEY_BUTTON;
  ev.name = s("Button press");
  ev.device_class = s("button");
  ev.event_types_count = G_N_ELEMENTS(BUTTON_EVENTS);
  ev.event_types = const_cast<char **>(BUTTON_EVENTS);
  send(MSG_LIST_ENTITIES_EVENT_RESPONSE, ListEntitiesEventResponse_fields, &ev);
}

void Entities::states(const Send &send) {
  Send saved = broadcast;
  broadcast = send;
  push_media_player();
  push_switch(KEY_MIC_MUTE, mic_muted);
  push_switch(KEY_WAKE_SOUND, wake_sound_);
  push_select(KEY_WAKE_WORD, wake_word);
  push_select(KEY_SENSITIVITY, sensitivity);
  push_light();
  broadcast = saved;
}

void Entities::push_media_player() {
  if (!broadcast)
    return;
  MediaPlayerStateResponse st = MediaPlayerStateResponse_init_zero;
  st.key = KEY_MEDIA_PLAYER;
  Source::State media = app->mixer->media.state();
  if (app->mixer->announce.state() == Source::State::PLAYING)
    st.state = MediaPlayerState_MEDIA_PLAYER_STATE_ANNOUNCING;
  else if (media == Source::State::PLAYING)
    st.state = MediaPlayerState_MEDIA_PLAYER_STATE_PLAYING;
  else if (media == Source::State::PAUSED)
    st.state = MediaPlayerState_MEDIA_PLAYER_STATE_PAUSED;
  else
    st.state = MediaPlayerState_MEDIA_PLAYER_STATE_IDLE;
  st.volume = app->audio_volume_controller->get_volume() / 100.0f;
  st.muted = media_muted;
  broadcast(MSG_MEDIA_PLAYER_STATE_RESPONSE, MediaPlayerStateResponse_fields,
            &st);
}

void Entities::push_switch(uint32_t key, bool state) {
  if (!broadcast)
    return;
  SwitchStateResponse st = SwitchStateResponse_init_zero;
  st.key = key;
  st.state = state;
  broadcast(MSG_SWITCH_STATE_RESPONSE, SwitchStateResponse_fields, &st);
}

void Entities::push_select(uint32_t key, const std::string &state) {
  if (!broadcast)
    return;
  SelectStateResponse st = SelectStateResponse_init_zero;
  st.key = key;
  st.state = s(state.c_str());
  broadcast(MSG_SELECT_STATE_RESPONSE, SelectStateResponse_fields, &st);
}

void Entities::push_light() {
  if (!broadcast)
    return;
  LightStateResponse st = LightStateResponse_init_zero;
  st.key = KEY_LED_RING;
  st.state = led_on;
  st.brightness = led_brightness;
  st.color_brightness = 1.0f;
  st.red = led_red;
  st.green = led_green;
  st.blue = led_blue;
  st.color_mode = ColorMode_COLOR_MODE_RGB;
  broadcast(MSG_LIGHT_STATE_RESPONSE, LightStateResponse_fields, &st);
}

void Entities::set_mic_muted(bool muted) {
  if (mic_muted == muted)
    return;
  mic_muted = muted;
  push_switch(KEY_MIC_MUTE, mic_muted);
}

void Entities::media_player_changed() { push_media_player(); }

static bool is_option(const char *value, const char *const *options,
                      size_t count) {
  for (size_t i = 0; i < count; i++)
    if (g_strcmp0(value, options[i]) == 0)
      return true;
  return false;
}

bool Entities::handle(uint32_t type, const uint8_t *payload, size_t len) {
  switch (type) {
    case MSG_SWITCH_COMMAND_REQUEST: {
      SwitchCommandRequest req = SwitchCommandRequest_init_zero;
      if (!decode(SwitchCommandRequest_fields, payload, len, &req))
        return true;
      if (req.key == KEY_MIC_MUTE) {
        // The Disabled state is mic mute; entering/leaving it calls
        // set_mic_muted, which pushes the new state.
        if (req.state != mic_muted)
          app->dispatch(new state::events::ToggleDisabled());
      } else if (req.key == KEY_WAKE_SOUND) {
        wake_sound_ = req.state;
        push_switch(KEY_WAKE_SOUND, wake_sound_);
      }
      return true;
    }

    case MSG_SELECT_COMMAND_REQUEST: {
      SelectCommandRequest req = SelectCommandRequest_init_zero;
      if (!decode(SelectCommandRequest_fields, payload, len, &req))
        return true;
      if (req.key == KEY_WAKE_WORD &&
          is_option(req.state, WAKE_WORDS, G_N_ELEMENTS(WAKE_WORDS))) {
        wake_word = req.state; // TODO Phase 5: switch microWakeWord model
        push_select(KEY_WAKE_WORD, wake_word);
      } else if (req.key == KEY_SENSITIVITY &&
                 is_option(req.state, SENSITIVITIES,
                           G_N_ELEMENTS(SENSITIVITIES))) {
        sensitivity = req.state; // TODO Phase 5: probability cutoff
        push_select(KEY_SENSITIVITY, sensitivity);
      }
      pb_release(SelectCommandRequest_fields, &req);
      return true;
    }

    case MSG_LIGHT_COMMAND_REQUEST: {
      LightCommandRequest req = LightCommandRequest_init_zero;
      if (!decode(LightCommandRequest_fields, payload, len, &req))
        return true;
      if (req.has_state)
        led_on = req.state;
      if (req.has_brightness)
        led_brightness = req.brightness;
      if (req.has_rgb) {
        led_red = req.red;
        led_green = req.green;
        led_blue = req.blue;
      }
      pb_release(LightCommandRequest_fields, &req);
      push_light(); // TODO Phase 7: idle color of the LED ring
      return true;
    }

    case MSG_MEDIA_PLAYER_COMMAND_REQUEST: {
      MediaPlayerCommandRequest req = MediaPlayerCommandRequest_init_zero;
      if (!decode(MediaPlayerCommandRequest_fields, payload, len, &req))
        return true;
      if (req.has_volume)
        app->audio_volume_controller->set_volume((int)lroundf(req.volume * 100));
      if (req.has_command) {
        switch (req.command) {
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_MUTE:
            media_muted = true; // TODO Phase 6: mixer mute
            break;
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_UNMUTE:
            media_muted = false;
            break;
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_VOLUME_UP:
            app->audio_volume_controller->increment_volume();
            break;
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_VOLUME_DOWN:
            app->audio_volume_controller->decrement_volume();
            break;
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_PLAY:
            app->mixer->media.resume();
            break;
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_PAUSE:
            app->mixer->media.pause();
            break;
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_TOGGLE:
            if (app->mixer->media.state() == Source::State::PAUSED)
              app->mixer->media.resume();
            else
              app->mixer->media.pause();
            break;
          case MediaPlayerCommand_MEDIA_PLAYER_COMMAND_STOP:
            app->mixer->media.stop();
            break;
          default:
            g_message("media_player command %d not supported yet",
                      req.command);
            break;
        }
      }
      if (req.has_media_url && req.media_url) {
        // TODO Phase 6: duck media during announcements, AnnounceFinished
        if (req.has_announcement && req.announcement)
          app->mixer->announce.play(req.media_url);
        else
          app->mixer->media.play(req.media_url);
      }
      pb_release(MediaPlayerCommandRequest_fields, &req);
      push_media_player();
      return true;
    }

    default:
      return false;
  }
}

} // namespace esphome
} // namespace smartspeaker
