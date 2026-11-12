/*
 * SmartSpeaker: ESPHome native API server (plaintext)
 */

#pragma once

#include <gio/gio.h>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "esphome/entities.hpp"
#include "esphome/mdns.hpp"
#include "esphome/noise.hpp"

namespace smartspeaker {

class App;

namespace esphome {

class Server {
public:
  Server(App *app);
  ~Server();

  bool start();

  /** An API encryption key is active (Noise only, no plaintext). */
  bool encrypted() const { return !psk.empty(); }
  const std::string &node_name() const { return name; }
  const std::string &mac_address() const { return mac; }

  Entities entities;

private:
  // The first byte picks the transport: 0x00 plaintext, 0x01 Noise
  enum class Mode { UNKNOWN, PLAIN, NOISE_HELLO, NOISE_HANDSHAKE, NOISE };

  struct Connection {
    Server *server;
    GSocketConnection *conn;
    std::string peer;
    std::vector<uint8_t> rx;
    uint8_t buf[2048];
    bool subscribed = false;
    bool closing = false;
    Mode mode = Mode::UNKNOWN;
    std::unique_ptr<Noise> noise;
  };

  App *const app;
  GSocketService *service = nullptr;
  std::list<Connection *> connections;
  Connection *voice_client = nullptr; // subscribed to the voice assistant
  std::string name;
  std::string mac;
  // the API encryption key; empty means none: plaintext, or Noise with the
  // all-zeros key so HA can provision one (api_encryption_provisionable)
  std::vector<uint8_t> psk;
  std::unique_ptr<Mdns> mdns;

  static gboolean on_incoming(GSocketService *service,
                              GSocketConnection *conn, GObject *source,
                              gpointer user_data);
  static void on_read(GObject *source, GAsyncResult *res, gpointer user_data);

  void read_next(Connection *c);
  void handle(Connection *c, uint32_t type, const uint8_t *payload, size_t len);
  void handle_noise(Connection *c, const uint8_t *payload, size_t len);
  void send(Connection *c, uint32_t type, const pb_msgdesc_t *fields,
            const void *msg);
  void write(Connection *c, const std::vector<uint8_t> &bytes);
  void close(Connection *c);
  bool set_key(const std::string &base64);
};

} // namespace esphome
} // namespace smartspeaker
