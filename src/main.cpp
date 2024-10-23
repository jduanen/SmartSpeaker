/*
*
*/

#include "app.hpp"

int main(int argc, char *argv[]) {
  using namespace smartspeaker;
  printf("SmartSpeaker App\n");
  App app;
  printf("...\n");
  g_debug("Starting App\n");
  return app.exec(argc, argv);
  g_debug("App Done\n");
}
