/*
*
*/

#pragma once

#include "config.hpp"

#include <stdio.h>
#include <memory>

namespace smartspeaker {

class Config;
class Leds;

class App {
public:
  App();
  ~App();

  static gboolean sigint_handler(gpointer data);
  static gboolean sigterm_handler(gpointer data);

  std::unique_ptr<Config> config;

	int exec(int argc, char *argv[]);

private:
  std::thread::id main_thread;
  GMainLoop *main_loop;

  std::unique_ptr<Leds> leds;
};

} // namespace smartspeaker
