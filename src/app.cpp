/*
* SmartSpeaker derived from Genie
*/

#include <glib-unix.h>
#include <glib.h>
#include <unistd.h>

#include "app.hpp"
#include "config.hpp"
#include "evinput.hpp"
#include "leds.hpp"
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

    ev_input = std::make_unique<EVInput>(this);
    ev_input->init();

    this->current_state = new state::Sleeping(this);
    this->current_state->enter();

    g_debug("start main loop\n");
    g_main_loop_run(main_loop);
    g_debug("main loop returned\n");

    //// TODO clean up before exiting

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
void genie::App::track_processing_event(ProcessingEventType event_type) {
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

void genie::App::replay_deferred_events() {
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
