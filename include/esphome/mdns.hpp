/*
 * SmartSpeaker: minimal mDNS responder for HA discovery
 *
 * Answers queries for _esphomelib._tcp.local, <name>._esphomelib._tcp.local
 * and <name>.local (PTR, SRV, TXT, A; IPv4 only), and announces them at
 * startup and when the TXT changes. The TXT keys follow ESPHome's
 * mdns_component.cpp.
 */

#pragma once

#include <gio/gio.h>
#include <string>
#include <vector>

namespace smartspeaker {
namespace esphome {

class Server;

class Mdns {
public:
  Mdns(Server *server, uint16_t port);
  ~Mdns();

  bool start();
  /** Send our records unsolicited (startup, or the TXT changed). */
  void announce();

private:
  Server *const server;
  const uint16_t port;
  GSocket *socket = nullptr;
  GSource *source = nullptr;
  guint announce_timer = 0;
  guint join_timer = 0;
  int announcements_left = 0;

  static gboolean on_readable(GSocket *socket, GIOCondition cond,
                              gpointer data);
  static gboolean on_announce(gpointer data);
  static gboolean try_join(gpointer data);
  void answer(const uint8_t *query, size_t len, GSocketAddress *from);
  std::vector<uint8_t> response(bool ptr, bool srv, bool txt, bool a,
                                uint16_t id);
  void send(const std::vector<uint8_t> &packet, GSocketAddress *to);
};

} // namespace esphome
} // namespace smartspeaker
