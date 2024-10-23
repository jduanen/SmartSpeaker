/*
*
*/

#include <glib-unix.h>
#include <glib.h>
#include <unistd.h>

#include "config.hpp"
#include "app.hpp"
#include "leds.hpp"

namespace smartspeaker {
  App::App() { printf("APP Create"); }

  App::~App() { printf("APP Remove"); }

  int smartspeaker::App::exec(int argc, char *argv[]) {
    config = std::make_unique<Config>();
    config->load();

    printf("LEDs enabled: %d, path: %s\n", config->leds_enabled, config->leds_path);

    leds = std::make_unique<Leds>(this);
    leds->init();


// Usage
for (auto color : EnumIterator<Color>()) {
    // Use color
}


    int i = 0;
    for (auto s : EnumIterator<LedsState_t>()) {
      printf("> %d\n", i++);
      leds->animate(s);
      usleep(5000000);  // Delay for 5 secs
    }
    return 0;
  }
}
