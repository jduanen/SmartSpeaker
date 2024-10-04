/*
*
*/

#include "app.hpp"

int main(int argc, char *argv[]) {
  using namespace smartspeaker;
  printf("SmartSpeaker App\n");
  App app;
  return app.exec(argc, argv);
};
