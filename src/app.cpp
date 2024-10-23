/*
*
*/

#include <glib-unix.h>
#include <glib.h>
#include <unistd.h>

#include "config.hpp"
#include "app.hpp"
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

  this->current_state = new state::Sleeping(this);
  this->current_state->enter();

  g_debug("start main loop\n");
  g_main_loop_run(main_loop);
  g_debug("main loop returned\n");

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
    int i = 0;
    for (auto s : EnumIterator<LedsState_t>()) {
      printf("> %d\n", i++);
      leds->animate(s);
      usleep(10 * 1000000);  // Delay for 10 secs
    }
    return 0;
*/
