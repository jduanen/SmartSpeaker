#pragma once

#include <string>

namespace smartspeaker {

class Spotifyd {
public:
  Spotifyd();
  ~Spotifyd();

  void pause();
  void set_credentials(const std::string &username,
                       const std::string &access_token);
};

} // namespace smartspeaker
