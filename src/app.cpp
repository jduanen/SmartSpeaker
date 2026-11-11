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

#include "EnumIterator.hpp"

double time_diff(struct timeval x, struct timeval y) {
  return (((double)y.tv_sec * 1000000 + (double)y.tv_usec) -
          ((double)x.tv_sec * 1000000 + (double)x.tv_usec));
}

double time_diff_ms(struct timeval x, struct timeval y) {
  return time_diff(x, y) / 1000;
}

smartspeaker::App::App() {
  main_thread = std::this_thread::get_id();
  is_processing = FALSE;
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

/*
 * @brief Track an event that is part of a turn's remote processing.
 *
 * We want to keep a close eye the performance of our
 *
 * @param event_type
 */
void smartspeaker::App::track_processing_event(ProcessingEventType event_type) {
  // Unless we are starting a turn or already in a turn just bail out. This
  // avoids tracking the "Hi..." and any other messages at connect.
  if (!(event_type == ProcessingEventType::START_STT || is_processing)) {
    return;
  }

  switch (event_type) {
    case ProcessingEventType::START_STT:
      gettimeofday(&start_stt, NULL);
      is_processing = true;
      break;
    case ProcessingEventType::END_STT:
      gettimeofday(&end_stt, NULL);
      break;
    case ProcessingEventType::START_GENIE:
      gettimeofday(&start_genie, NULL);
      break;
    case ProcessingEventType::END_GENIE:
      gettimeofday(&end_genie, NULL);
      break;
    case ProcessingEventType::START_TTS:
      gettimeofday(&start_tts, NULL);
      break;
    case ProcessingEventType::END_TTS:
      gettimeofday(&end_tts, NULL);
      break;
    case ProcessingEventType::DONE:
      int total_ms = time_diff_ms(start_stt, end_tts);

      g_print("############# Processing Performance #################\n");
      print_processing_entry("STT", time_diff_ms(start_stt, end_stt), total_ms);
      print_processing_entry("STT->Genie", time_diff_ms(end_stt, start_genie),
                             total_ms);
      print_processing_entry("Genie", time_diff_ms(start_genie, end_genie),
                             total_ms);
      print_processing_entry("Genie->TTS", time_diff_ms(end_genie, start_tts),
                             total_ms);
      print_processing_entry("TTS", time_diff_ms(start_tts, end_tts), total_ms);
      g_print("------------------------------------------------------\n");
      print_processing_entry("Total", total_ms, total_ms);
      g_print("######################################################\n");

      is_processing = false;
      break;
  }
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

void smartspeaker::App::print_processing_entry(const char *name,
                                               double duration_ms,
                                               double total_ms) {
  g_message("  %-20s %8.2f ms  (%5.1f%%)", name, duration_ms,
            total_ms > 0 ? 100.0 * duration_ms / total_ms : 0.0);
}
