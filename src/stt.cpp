#include "stt.hpp"
#include "app.hpp"

namespace smartspeaker {

STT::STT(App *app) {}
STT::~STT() {}
void STT::begin_session(bool is_follow_up) {}
void STT::send_frame(AudioFrame frame) {}
void STT::send_done() {}
void STT::abort() {}

} // namespace smartspeaker
