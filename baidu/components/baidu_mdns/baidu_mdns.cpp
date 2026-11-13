#include "baidu_mdns.h"
#if defined(USE_HOST) && defined(USE_MDNS)

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

namespace esphome::baidu_mdns {

static const char *const TAG = "baidu_mdns";

static const char *const GROUP = "224.0.0.251";
static const uint16_t MDNS_PORT = 5353;

enum : uint16_t { TYPE_A = 1, TYPE_PTR = 12, TYPE_TXT = 16, TYPE_SRV = 33, TYPE_ANY = 255 };
static const uint16_t CLASS_IN = 1;
static const uint16_t CACHE_FLUSH = 0x8000;  // unique records
static const uint32_t TTL_HOST = 120;        // RFC 6762 10: names of hosts
static const uint32_t TTL_OTHER = 4500;
static const uint32_t JOIN_RETRY_MS = 5000;
static const uint32_t ANNOUNCE_INTERVAL_MS = 1000;

static const char *str(const mdns::MDNSString *s) { return reinterpret_cast<const char *>(s); }

/** IPv4 of wlan0, else the first non-loopback interface; 0 if none. */
static uint32_t local_ipv4() {
  struct ifaddrs *list = nullptr;
  uint32_t found = 0;
  if (getifaddrs(&list) != 0)
    return 0;
  for (struct ifaddrs *it = list; it; it = it->ifa_next) {
    if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || (it->ifa_flags & IFF_LOOPBACK))
      continue;
    uint32_t ip = reinterpret_cast<struct sockaddr_in *>(it->ifa_addr)->sin_addr.s_addr;
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
    o.push_back(static_cast<uint8_t>(dot - start));
    o.insert(o.end(), name.begin() + start, name.begin() + dot);
    start = dot + 1;
  }
  o.push_back(0);
}

/** Record header; returns where to patch the data length. */
static size_t put_record(std::vector<uint8_t> &o, const std::string &name, uint16_t type, bool unique,
                         uint32_t ttl) {
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

static void put_txt(std::vector<uint8_t> &o, const std::string &entry) {
  o.push_back(static_cast<uint8_t>(entry.size()));
  o.insert(o.end(), entry.begin(), entry.end());
}

/** Read a (possibly compressed) name; false if malformed. */
static bool read_name(const uint8_t *p, size_t len, size_t *pos, std::string &out) {
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
      at = static_cast<size_t>(l & 0x3f) << 8 | p[at + 1];
      continue;
    }
    if (at + 1 + l > len)
      return false;
    if (!out.empty())
      out += '.';
    out.append(reinterpret_cast<const char *>(p) + at + 1, l);
    at += 1 + l;
  }
  return false;
}

// ── BaiduMdns ────────────────────────────────────────────────────────────────

static std::string service_name(const mdns::MDNSService &s) {
  return std::string(str(s.service_type)) + "." + str(s.proto) + ".local";
}

bool BaiduMdns::has_psk_() const {
#if defined(USE_API) && defined(USE_API_NOISE)
  return api::global_api_server != nullptr && api::global_api_server->get_noise_ctx().has_psk();
#else
  return false;
#endif
}

void BaiduMdns::setup() {
  this->fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (this->fd_ < 0) {
    ESP_LOGE(TAG, "socket: %s", strerror(errno));
    this->mark_failed();
    return;
  }
  int one = 1;
  setsockopt(this->fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(MDNS_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(this->fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
    ESP_LOGE(TAG, "bind to port %u: %s", MDNS_PORT, strerror(errno));
    close(this->fd_);
    this->fd_ = -1;
    this->mark_failed();
    return;
  }
  fcntl(this->fd_, F_SETFL, fcntl(this->fd_, F_GETFL) | O_NONBLOCK);
  unsigned char ttl = 255, loop = 0;
  setsockopt(this->fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  setsockopt(this->fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
  this->announced_psk_ = this->has_psk_();
  this->last_join_try_ = millis();
  if (!this->try_join_())
    ESP_LOGI(TAG, "Waiting for the network to join %s", GROUP);
}

/** Join the group once the network is up (WiFi may still be connecting). */
bool BaiduMdns::try_join_() {
  struct ip_mreq mreq {};
  mreq.imr_multiaddr.s_addr = inet_addr(GROUP);
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(this->fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0)
    return false;
  this->joined_ = true;
  ESP_LOGI(TAG, "Responding for %s.local", App.get_name().c_str());
  this->announce_();
  return true;
}

void BaiduMdns::announce_() {
  // RFC 6762 8.3: at least twice, one second apart
  this->announcements_left_ = 3;
  this->last_announce_ = millis() - ANNOUNCE_INTERVAL_MS;
}

void BaiduMdns::loop() {
  if (this->fd_ < 0)
    return;
  uint32_t now = millis();
  if (!this->joined_) {
    if (now - this->last_join_try_ >= JOIN_RETRY_MS) {
      this->last_join_try_ = now;
      this->try_join_();
    }
    return;
  }

  // the TXT advertises whether HA has provisioned a key; re-announce when that changes
  bool psk = this->has_psk_();
  if (psk != this->announced_psk_) {
    this->announced_psk_ = psk;
    this->announce_();
  }

  if (this->announcements_left_ > 0 && now - this->last_announce_ >= ANNOUNCE_INTERVAL_MS) {
    this->announcements_left_--;
    this->last_announce_ = now;
    uint32_t all = (1u << this->mdns_->get_services().size()) - 1;
    this->send_(this->response_(all, all, all, true, 0), nullptr);
  }

  uint8_t buf[1500];
  for (;;) {
    struct sockaddr_in from {};
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(this->fd_, buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&from), &from_len);
    if (n <= 0)
      break;
    this->answer_(buf, n, from);
  }
}

void BaiduMdns::answer_(const uint8_t *p, size_t len, const struct sockaddr_in &from) {
  if (len < 12 || (p[2] & 0x80))  // too short, or a response
    return;
  uint16_t id = p[0] << 8 | p[1];
  uint16_t questions = p[4] << 8 | p[5];
  const auto &services = this->mdns_->get_services();
  std::string host = std::string(App.get_name().c_str()) + ".local";

  uint32_t ptr = 0, srv = 0, txt = 0;
  bool a = false;
  size_t pos = 12;
  for (uint16_t q = 0; q < questions; q++) {
    std::string name;
    if (!read_name(p, len, &pos, name) || pos + 4 > len)
      return;
    uint16_t type = p[pos] << 8 | p[pos + 1];
    pos += 4;
    bool any = type == TYPE_ANY;
    if (strcasecmp(name.c_str(), host.c_str()) == 0) {
      a = a || any || type == TYPE_A;
      continue;
    }
    for (size_t i = 0; i < services.size(); i++) {
      std::string service = service_name(services[i]);
      std::string instance = std::string(App.get_name().c_str()) + "." + service;
      if (strcasecmp(name.c_str(), service.c_str()) == 0 && (any || type == TYPE_PTR))
        ptr |= 1u << i;
      else if (strcasecmp(name.c_str(), instance.c_str()) == 0) {
        if (any || type == TYPE_SRV)
          srv |= 1u << i;
        if (any || type == TYPE_TXT)
          txt |= 1u << i;
      }
    }
  }
  if (!(ptr || srv || txt || a))
    return;

  if (ntohs(from.sin_port) != MDNS_PORT) {
    // legacy unicast query (RFC 6762 6.7): reply directly, echoing the id
    this->send_(this->response_(ptr, srv, txt, a, id), &from);
  } else {
    this->send_(this->response_(ptr, srv, txt, a, 0), nullptr);
  }
}

std::vector<uint8_t> BaiduMdns::response_(uint32_t ptr, uint32_t srv, uint32_t txt, bool a, uint16_t id) {
  const auto &services = this->mdns_->get_services();
  std::string name = App.get_name().c_str();
  std::string host = name + ".local";
  uint32_t ip = local_ipv4();
  bool psk = this->has_psk_();
  // a PTR answer brings its SRV and TXT along, and an SRV the A record
  srv |= ptr;
  txt |= ptr;
  a = (a || srv) && ip;

  std::vector<uint8_t> o;
  put16(o, id);
  put16(o, 0x8400);  // response, authoritative
  put16(o, 0);       // questions
  put16(o, __builtin_popcount(ptr) + __builtin_popcount(srv) + __builtin_popcount(txt) + (a ? 1 : 0));
  put16(o, 0);
  put16(o, 0);

  for (size_t i = 0; i < services.size(); i++) {
    const auto &s = services[i];
    std::string service = service_name(s);
    std::string instance = name + "." + service;
    if (ptr & (1u << i)) {
      size_t at = put_record(o, service, TYPE_PTR, false, TTL_OTHER);
      put_name(o, instance);
      end_record(o, at);
    }
    if (srv & (1u << i)) {
      size_t at = put_record(o, instance, TYPE_SRV, true, TTL_HOST);
      put16(o, 0);  // priority
      put16(o, 0);  // weight
      put16(o, s.port.value());
      put_name(o, host);
      end_record(o, at);
    }
    if (txt & (1u << i)) {
      size_t at = put_record(o, instance, TYPE_TXT, true, TTL_OTHER);
      bool any = false;
      for (const auto &r : s.txt_records) {
        std::string key = str(r.key);
        // MDNSComponent fixed these at boot; follow the live key state instead
        if (key == "api_encryption" || key == "api_encryption_supported")
          key = psk ? "api_encryption" : "api_encryption_supported";
        else if (key == "api_provisioning")
          continue;
        put_txt(o, key + "=" + str(r.value));
        any = true;
      }
#if defined(USE_API) && defined(USE_API_NOISE) && !defined(USE_API_NOISE_PSK_FROM_YAML)
      if (!psk && service == "_esphomelib._tcp.local") {
        put_txt(o, "api_provisioning=zero-psk");
        any = true;
      }
#endif
      if (!any)
        o.push_back(0);  // RFC 6763 6.1: an empty TXT holds one empty string
      end_record(o, at);
    }
  }
  if (a) {
    size_t at = put_record(o, host, TYPE_A, true, TTL_HOST);
    put32(o, ntohl(ip));
    end_record(o, at);
  }
  return o;
}

void BaiduMdns::send_(const std::vector<uint8_t> &packet, const struct sockaddr_in *to) {
  struct sockaddr_in group {};
  if (to == nullptr) {
    group.sin_family = AF_INET;
    group.sin_port = htons(MDNS_PORT);
    group.sin_addr.s_addr = inet_addr(GROUP);
    to = &group;
  }
  if (sendto(this->fd_, packet.data(), packet.size(), 0, reinterpret_cast<const struct sockaddr *>(to),
             sizeof(*to)) < 0)
    ESP_LOGV(TAG, "sendto: %s", strerror(errno));
}

void BaiduMdns::dump_config() {
  ESP_LOGCONFIG(TAG, "mDNS responder:\n  Hostname: %s.local\n  Services: %u", App.get_name().c_str(),
                static_cast<unsigned>(this->mdns_->get_services().size()));
}

}  // namespace esphome::baidu_mdns

#endif
