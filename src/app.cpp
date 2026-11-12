/*
* SmartSpeaker derived from Genie
*/

#include <glib-unix.h>
#include <glib.h>
#include <curl/curl.h>
#include <unistd.h>

#include "app.hpp"
#include "audio/audioinput.hpp"
#include "audio/audioplayer.hpp"
#include "audio/audiovolume.hpp"
#include "audio/mixer.hpp"
#include "config.hpp"
#include "utils/net.hpp"
#include "evinput.hpp"
#include "leds.hpp"
#include "esphome/server.hpp"
#include "voice/session.hpp"

#include "EnumIterator.hpp"

smartspeaker::App::App() {
  main_thread = std::this_thread::get_id();
}

smartspeaker::App::~App() { g_main_loop_unref(main_loop); }

int smartspeaker::App::exec(int argc, char *argv[]) {
  main_loop = g_main_loop_new(NULL, FALSE);

  g_unix_signal_add(SIGINT, sigint_handler, main_loop);
  g_unix_signal_add(SIGTERM, sigterm_handler, main_loop);

  config = std::make_unique<Config>();
  config->load();

  g_debug("LEDs enabled: %d, path: %s\n", config->leds_enabled, config->leds_path);  // TMP TMP TMP

  leds = std::make_unique<Leds>(this);
  leds->init();
  leds->animate(LedsState_t::Starting);

  curl_global_init(CURL_GLOBAL_ALL);
  mixer = std::make_unique<Mixer>(this);
  mixer->start();

  audio_volume_controller = std::make_unique<AudioVolumeController>(this);

  audio_player = std::make_unique<AudioPlayer>(this);

  audio_input = std::make_unique<AudioInput>(this);

  ev_input = std::make_unique<EVInput>(this);
  ev_input->init();

  voice = std::make_unique<VoiceSession>(this);
  esphome = std::make_unique<esphome::Server>(this);
  esphome->start();

  this->current_state = new state::Sleeping(this);
  this->current_state->enter();

  //// TODO clean up before exiting
  g_debug("start main loop\n");
  g_main_loop_run(main_loop);
  g_debug("main loop returned\n");

  audio_input->close();

  return EXIT_SUCCESS;
}

void smartspeaker::App::set_mic_muted(bool muted) {
  mic_muted = muted;
  g_message("Microphone %s", muted ? "muted" : "unmuted");
  audio_input->set_muted(muted);
  leds->set_muted(muted);
  esphome->entities.set_mic_muted(muted);
  play_feedback(muted ? Sound_t::MUTE_ON : Sound_t::MUTE_OFF);
}

void smartspeaker::App::play_feedback(Sound_t sound) {
  if (mixer->announce.state() != Source::State::PLAYING)
    audio_player->play_sound(sound);
}

gboolean smartspeaker::App::sigint_handler(gpointer data) {
  GMainLoop *loop = static_cast<GMainLoop *>(data);
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

gboolean smartspeaker::App::sigterm_handler(gpointer data) {
  // exit the process directly, to avoid any crash on termination
  // that could cause us to restart
  exit(0);
}

void smartspeaker::App::replay_deferred_events() {
  // steal all the deferred events
  // this is necessary because handling the event
  // might queue more deferred events
  std::queue<DeferredEvent> copy;
  std::swap(copy, deferred_events);

  // replay the events
  while (!copy.empty()) {
    auto &defer = copy.front();
    current_event = defer.event;
    defer.dispatch(current_state);
    delete current_event;
    current_event = nullptr;
    copy.pop();
  }
}


/*
    int i = 0;
    for (auto s : EnumIterator<LedsState_t>()) {
      printf("> %d\n", i++);
      leds->animate(s);
      usleep(10 * 1000000);  // Delay for 10 secs
    }
    return 0;
*/
