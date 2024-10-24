/*
* SmartSpeaker derived from Genie
*/

#pragma once

#include "config.hpp"
#include "evinput.hpp"

#include <stdio.h>
#include <memory>
#include <thread>

namespace smartspeaker {

class Config;
class EVInput;
class Leds;

enum class ProcessingEventType {
  START_STT,
  END_STT,
  START_GENIE,
  END_GENIE,
  START_TTS,
  END_TTS,
  DONE,
};

class App {
public:
  App();
  ~App();

  static gboolean sigint_handler(gpointer data);
  static gboolean sigterm_handler(gpointer data);

  std::unique_ptr<Config> config;

	int exec(int argc, char *argv[]);

  /*
   * @brief Dispatch a state `event`. This method is _thread-safe_
   *
   * When called it queues handling of the `event` on the main thread via
   * [g_idle_add](https://docs.gtk.org/glib/func.idle_add.html)
   *
   * After the `event` is handled it is deleted
   */
  template <typename E> guint dispatch(E *event) {
    g_debug("DISPATCH EVENT %s", typeid(E).name());
    DispatchUserData *dispatch_user_data = new DispatchUserData(this, event);
    return g_idle_add(handle<E>, dispatch_user_data);
  }

  /*
   * Defer the handling of this event until the next state.
   */
  template <typename E> void defer(E *event) {
    if (event != current_event) {
      g_critical("only the currently handled event can be deferred");
      return;
    }
    current_event = nullptr;
    deferred_events.emplace(event);
  }

private:
  /*
   * @brief Structure passed as `user_data` through the GLib main loop when
   * dispatching an event.
   *
   * Contains a pointer to the app instance and a pointer to the event being
   * dispatched.
   *
   * After the event is handled the structure is deleted, which in turn
   * deletes the referenced event.
   */
  struct DispatchUserData {
    App *app;
    state::events::Event *event;

    DispatchUserData(App *app, state::events::Event *event)
        : app(app), event(event) {}
    ~DispatchUserData() { delete event; }
  };

  std::thread::id main_thread;
  GMainLoop *main_loop;

  std::unique_ptr<EVInput> ev_input;
  std::unique_ptr<Leds> leds;
};

} // namespace smartspeaker
