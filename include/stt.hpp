#pragma once

#include "audio/audio.hpp"

namespace smartspeaker {

class App;

class STT {
public:
  STT(App *app);
  ~STT();

  void begin_session(bool is_follow_up);
  void send_frame(AudioFrame frame);
  void send_done();
  void abort();
};

} // namespace smartspeaker
