/*
*
*/

#include <glib-unix.h>
#include <glib.h>

#include "app.hpp"
#include "config.hpp"
#include "leds.hpp"

namespace smartspeaker {
  App::App() { printf("APP Create"); };

  App::~App() { printf("APP Remove"); };

  int smartspeaker::App::exec(int argc, char *argv[]) {
    config = std::make_unique<Config>();
    config->load();

    leds = std::make_unique<Leds>(this);
    leds->init();
    leds->animate(LedsState_t::Starting);
  };
};