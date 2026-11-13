#pragma once

#include "esphome/core/defines.h"
#if defined(USE_HOST) && defined(USE_MDNS)

#include <netinet/in.h>

#include <cstdint>
#include <string>
#include <vector>

#include "esphome/components/mdns/mdns_component.h"
#include "esphome/core/component.h"

namespace esphome::baidu_mdns {

/// Minimal mDNS responder (RFC 6762/6763, IPv4) for the records that
/// MDNSComponent compiled: PTR, SRV and TXT per service, and A for
/// <name>.local. Ported from the C++ app's src/esphome/mdns.cpp.
class BaiduMdns : public Component {
 public:
  void set_mdns(mdns::MDNSComponent *mdns) { this->mdns_ = mdns; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  // after MDNSComponent (AFTER_CONNECTION) has compiled its records
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION - 1.0f; }

 protected:
  bool try_join_();
  void announce_();
  void answer_(const uint8_t *p, size_t len, const struct sockaddr_in &from);
  /// Records for the questions matched; service bits are indexes into get_services().
  std::vector<uint8_t> response_(uint32_t ptr, uint32_t srv, uint32_t txt, bool a, uint16_t id);
  void send_(const std::vector<uint8_t> &packet, const struct sockaddr_in *to);
  bool has_psk_() const;

  mdns::MDNSComponent *mdns_{nullptr};
  int fd_{-1};
  bool joined_{false};
  uint32_t last_join_try_{0};
  int announcements_left_{0};
  uint32_t last_announce_{0};
  bool announced_psk_{false};
};

}  // namespace esphome::baidu_mdns

#endif
