#pragma once

#include <string>

namespace smartspeaker {

class App;

namespace ws {

class Client {
public:
  Client(App *app);
  ~Client();

  void send_thingtalk(const char *code);
  void send_command(const std::string &text);
};

} // namespace ws
} // namespace smartspeaker
