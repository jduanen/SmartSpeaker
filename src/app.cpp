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

    printf("LEDs enabled: %d\n", config->leds_enabled);

    leds = std::make_unique<Leds>(this);
    leds->init();
    printf("Starting\n");
    leds->animate(LedsState_t::Starting);
    usleep(1000000);  // Delay for 1 second (1,000,000 microseconds)

    printf("Listening\n");
    leds->animate(LedsState_t::Listening);
    usleep(1000000);  // Delay for 1 second (1,000,000 microseconds)

    printf("Processing\n");
    leds->animate(LedsState_t::Processing);
    usleep(1000000);  // Delay for 1 second (1,000,000 microseconds)
    return 0;
  }
}
