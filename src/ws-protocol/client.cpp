#include "ws-protocol/client.hpp"

namespace smartspeaker {
namespace ws {

Client::Client(App *app) {}
Client::~Client() {}
void Client::send_thingtalk(const char *code) {}
void Client::send_command(const std::string &text) {}

} // namespace ws
} // namespace smartspeaker
