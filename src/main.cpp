/*
*
*/

#include "app.hpp"

int main(int argc, char *argv[]) {
  using namespace smartspeaker;
  printf("SmartSpeaker App\n");
  App app;
  printf("...\n");
  return app.exec(argc, argv);
  printf("DONE");
}
