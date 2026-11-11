/*
*
*/

#include "app.hpp"

int main(int argc, char *argv[]) {
  using namespace smartspeaker;
  // the launcher redirects output to a log file; keep it line by line
  setvbuf(stdout, nullptr, _IOLBF, 0);
  printf("SmartSpeaker App\n");
  App app;
  printf("...\n");
  g_debug("Starting App\n");
  return app.exec(argc, argv);
  g_debug("App Done\n");
}
