/*
*
*/

#pragma once

#include "config.h"
#include "config.hpp"

#include <stdio.h>
#include <memory>

namespace smartspeaker {

class Leds;

class App {
public:
  App();
  ~App();

  std::unique_ptr<Config> config;

	int exec(int argc, char *argv[]);

private:
  std::unique_ptr<Leds> leds;
};

} // namespace smartspeaker
