/*
 * SmartSpeaker: minimal mDNS responder for HA discovery (RFC 6762/6763)
 *
 * One service instance, <name>._esphomelib._tcp.local, on <name>.local.
 * Questions are matched by name and type; matching answers go to the
 * multicast group, and also straight back to the sender for legacy
 * (non-5353) queries. Names in our replies are written uncompressed.
 */

#include "esphome/mdns.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>

#include "config.h"
#include "esphome/server.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::esphome::Mdns"

namespace smartspeaker {
namespace esphome {

static const char *const GROUP = "224.0.0.251";
static const uint16_t MDNS_PORT = 5353;
static const char *const SERVICE = "_esphomelib._tcp.local";

enum : uint16_t { TYPE_A = 1, TYPE_PTR = 12, TYPE_TXT = 16, TYPE_SRV = 33,
                  TYPE_ANY = 255 };
static const uint16_t CLASS_IN = 1;
static const uint16_t CACHE_FLUSH = 0x8000; // unique records
static const uint32_t TTL_HOST = 120;       // RFC 6762 10: names of hosts
static const uint32_t TTL_OTHER = 4500;

// "version": the ESPHome version we claim, as in DeviceInfo (server.cpp)
static const char *const VERSION = "46.29.0";

/** IPv4 of wlan0, else the first non-loopback interface; 0 if none. */
static uint32_t local_ipv4() {
  struct ifaddrs *list = nullptr, *it;
  uint32_t found = 0;
  if (getifaddrs(&list) != 0)
    return 0;
  for (it = list; it; it = it->ifa_next) {
    if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET ||
        (it->ifa_flags & IFF_LOOPBACK))
      continue;
    uint32_t ip = ((struct sockaddr_in *)it->ifa_addr)->sin_addr.s_addr;
    if (strcmp(it->ifa_name, "wlan0") == 0) {
      found = ip;
      break;
    }
    if (!found)
      found = ip;
  }
  freeifaddrs(list);
  return found;
}

// ── DNS encoding ─────────────────────────────────────────────────────────────

static void put16(std::vector<uint8_t> &o, uint16_t v) {
  o.push_back(v >> 8);
  o.push_back(v & 0xff);
}

static void put32(std::vector<uint8_t> &o, uint32_t v) {
  put16(o, v >> 16);
  put16(o, v & 0xffff);
}

static void put_name(std::vector<uint8_t> &o, const std::string &name) {
  size_t start = 0;
  while (start < name.size()) {
    size_t dot = name.find('.', start);
    if (dot == std::string::npos)
      dot = name.size();
    o.push_back((uint8_t)(dot - start));
    o.insert(o.end(), name.begin() + start, name.begin() + dot);
    start = dot + 1;
  }
  o.push_back(0);
}

/** Record header; returns where to patch the data length. */
static size_t put_record(std::vector<uint8_t> &o, const std::string &name,
                         uint16_t type, bool unique, uint32_t ttl) {
  put_name(o, name);
  put16(o, type);
  put16(o, CLASS_IN | (unique ? CACHE_FLUSH : 0));
  put32(o, ttl);
  size_t at = o.size();
  put16(o, 0);
  return at;
}

static void end_record(std::vector<uint8_t> &o, size_t at) {
  uint16_t len = o.size() - at - 2;
  o[at] = len >> 8;
  o[at + 1] = len & 0xff;
}

/** Read a (possibly compressed) name; false if malformed. */
static bool read_name(const uint8_t *p, size_t len, size_t *pos,
                      std::string &out) {
  size_t at = *pos;
  bool jumped = false;
  for (int guard = 0; guard < 64; guard++) {
    if (at >= len)
      return false;
    uint8_t l = p[at];
    if (l == 0) {
      if (!jumped)
        *pos = at + 1;
      return true;
    }
    if ((l & 0xc0) == 0xc0) {
      if (at + 1 >= len)
        return false;
      if (!jumped)
        *pos = at + 2;
      jumped = true;
      at = (size_t)(l & 0x3f) << 8 | p[at + 1];
      continue;
    }
    if (at + 1 + l > len)
      return false;
    if (!out.empty())
      out += '.';
    out.append((const char *)p + at + 1, l);
    at += 1 + l;
  }
  return false;
}

// ── Mdns ─────────────────────────────────────────────────────────────────────

Mdns::Mdns(Server *server, uint16_t port) : server(server), port(port) {}

Mdns::~Mdns() {
  if (announce_timer)
    g_source_remove(announce_timer);
  if (join_timer)
    g_source_remove(join_timer);
  if (source) {
    g_source_destroy(source);
    g_source_unref(source);
  }
  if (socket)
    g_object_unref(socket);
}

bool Mdns::start() {
  GError *error = nullptr;
  socket = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
                        G_SOCKET_PROTOCOL_UDP, &error);
  if (!socket) {
    g_warning("mDNS socket: %s", error->message);
    g_error_free(error);
    return false;
  }
  GInetAddress *any = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
  GSocketAddress *bind_addr = g_inet_socket_address_new(any, MDNS_PORT);
  bool ok = g_socket_bind(socket, bind_addr, TRUE, &error);
  g_object_unref(bind_addr);
  g_object_unref(any);
  if (!ok) {
    g_warning("mDNS on port %u: %s", MDNS_PORT, error->message);
    g_error_free(error);
    return false;
  }
  g_socket_set_multicast_ttl(socket, 255);
  g_socket_set_multicast_loopback(socket, FALSE);
  if (try_join(this))
    join_timer = g_timeout_add_seconds(5, try_join, this);
  return true;
}

/** Join the group once the network is up (WiFi may still be connecting). */
gboolean Mdns::try_join(gpointer data) {
  Mdns *self = static_cast<Mdns *>(data);
  GError *error = nullptr;
  GInetAddress *group = g_inet_address_new_from_string(GROUP);
  bool ok = g_socket_join_multicast_group(self->socket, group, FALSE, nullptr,
                                          &error);
  g_object_unref(group);
  if (!ok) {
    if (!self->join_timer)
      g_message("mDNS waiting for the network: %s", error->message);
    g_error_free(error);
    return G_SOURCE_CONTINUE;
  }
  self->join_timer = 0;
  self->source = g_socket_create_source(self->socket, G_IO_IN, nullptr);
  g_source_set_callback(self->source, (GSourceFunc)on_readable, self, nullptr);
  g_source_attach(self->source, nullptr);
  g_message("mDNS: %s._esphomelib._tcp.local on port %u",
            self->server->node_name().c_str(), self->port);
  self->announce();
  return G_SOURCE_REMOVE;
}

void Mdns::announce() {
  if (!source)
    return; // not joined yet; joining announces
  // RFC 6762 8.3: at least twice, one second apart
  announcements_left = 3;
  if (!announce_timer)
    on_announce(this);
}

gboolean Mdns::on_announce(gpointer data) {
  Mdns *self = static_cast<Mdns *>(data);
  if (self->announcements_left-- <= 0) {
    self->announce_timer = 0;
    return G_SOURCE_REMOVE;
  }
  self->send(self->response(true, true, true, true, 0), nullptr);
  if (!self->announce_timer)
    self->announce_timer = g_timeout_add_seconds(1, on_announce, self);
  return G_SOURCE_CONTINUE;
}

gboolean Mdns::on_readable(GSocket *socket, GIOCondition, gpointer data) {
  Mdns *self = static_cast<Mdns *>(data);
  uint8_t buf[1500];
  GSocketAddress *from = nullptr;
  gssize n = g_socket_receive_from(socket, &from, (gchar *)buf, sizeof(buf),
                                   nullptr, nullptr);
  if (n > 0)
    self->answer(buf, n, from);
  if (from)
    g_object_unref(from);
  return G_SOURCE_CONTINUE;
}

void Mdns::answer(const uint8_t *p, size_t len, GSocketAddress *from) {
  if (len < 12 || (p[2] & 0x80)) // too short, or a response
    return;
  uint16_t id = p[0] << 8 | p[1];
  uint16_t questions = p[4] << 8 | p[5];
  std::string instance = server->node_name() + "." + SERVICE;
  std::string host = server->node_name() + ".local";

  bool ptr = false, srv = false, txt = false, a = false;
  size_t pos = 12;
  for (uint16_t q = 0; q < questions; q++) {
    std::string name;
    if (!read_name(p, len, &pos, name) || pos + 4 > len)
      return;
    uint16_t type = p[pos] << 8 | p[pos + 1];
    pos += 4;
    bool any = type == TYPE_ANY;
    if (g_ascii_strcasecmp(name.c_str(), SERVICE) == 0 &&
        (any || type == TYPE_PTR))
      ptr = true;
    else if (g_ascii_strcasecmp(name.c_str(), instance.c_str()) == 0) {
      srv = srv || any || type == TYPE_SRV;
      txt = txt || any || type == TYPE_TXT;
    } else if (g_ascii_strcasecmp(name.c_str(), host.c_str()) == 0 &&
               (any || type == TYPE_A))
      a = true;
  }
  if (!(ptr || srv || txt || a))
    return;

  uint16_t src_port =
      g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(from));
  if (src_port != MDNS_PORT) {
    // legacy unicast query (RFC 6762 6.7): reply directly, echoing the id
    send(response(ptr, srv, txt, a, id), from);
  } else {
    send(response(ptr, srv, txt, a, 0), nullptr);
  }
}

std::vector<uint8_t> Mdns::response(bool ptr, bool srv, bool txt, bool a,
                                    uint16_t id) {
  std::string name = server->node_name();
  std::string instance = name + "." + SERVICE;
  std::string host = name + ".local";
  uint32_t ip = local_ipv4();
  // a PTR answer brings the rest along as additional records
  bool extra = ptr;
  srv = srv || extra;
  txt = txt || extra;
  a = (a || srv) && ip;

  std::vector<uint8_t> o;
  put16(o, id);
  put16(o, 0x8400); // response, authoritative
  put16(o, 0);      // questions
  put16(o, (ptr ? 1 : 0) + (srv ? 1 : 0) + (txt ? 1 : 0) + (a ? 1 : 0));
  put16(o, 0);
  put16(o, 0);

  if (ptr) {
    size_t at = put_record(o, SERVICE, TYPE_PTR, false, TTL_OTHER);
    put_name(o, instance);
    end_record(o, at);
  }
  if (srv) {
    size_t at = put_record(o, instance, TYPE_SRV, true, TTL_HOST);
    put16(o, 0); // priority
    put16(o, 0); // weight
    put16(o, port);
    put_name(o, host);
    end_record(o, at);
  }
  if (txt) {
    std::string mac;
    for (char ch : server->mac_address())
      if (ch != ':')
        mac += g_ascii_tolower(ch);
    std::vector<std::string> entries = {
        "friendly_name=" + server->display_name(),
        std::string("version=") + VERSION,
        "mac=" + mac,
        "platform=linux",
        "board=xiaodu-xdh-1d-a1",
        "network=wifi",
        std::string(server->encrypted() ? "api_encryption="
                                        : "api_encryption_supported=") +
            "Noise_NNpsk0_25519_ChaChaPoly_SHA256",
        "project_name=smartspeaker.xiaodu",
        std::string("project_version=") + PACKAGE_VERSION,
    };
    size_t at = put_record(o, instance, TYPE_TXT, true, TTL_OTHER);
    for (const auto &e : entries) {
      o.push_back((uint8_t)e.size());
      o.insert(o.end(), e.begin(), e.end());
    }
    end_record(o, at);
  }
  if (a) {
    size_t at = put_record(o, host, TYPE_A, true, TTL_HOST);
    const uint8_t *b = (const uint8_t *)&ip; // network order
    o.insert(o.end(), b, b + 4);
    end_record(o, at);
  }
  return o;
}

void Mdns::send(const std::vector<uint8_t> &packet, GSocketAddress *to) {
  GSocketAddress *dest = to;
  if (!dest) {
    GInetAddress *group = g_inet_address_new_from_string(GROUP);
    dest = g_inet_socket_address_new(group, MDNS_PORT);
    g_object_unref(group);
  }
  GError *error = nullptr;
  if (g_socket_send_to(socket, dest, (const gchar *)packet.data(),
                       packet.size(), nullptr, &error) < 0) {
    g_debug("mDNS send: %s", error->message);
    g_error_free(error);
  }
  if (!to)
    g_object_unref(dest);
}

} // namespace esphome
} // namespace smartspeaker
