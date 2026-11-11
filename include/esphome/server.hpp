/*
 * SmartSpeaker: ESPHome native API server (plaintext)
 */

#pragma once

#include <gio/gio.h>
#include <list>
#include <string>
#include <vector>

#include "esphome/entities.hpp"

namespace smartspeaker {

class App;

namespace esphome {

class Server {
public:
  Server(App *app);
  ~Server();

  bool start();

  Entities entities;

private:
  struct Connection {
    Server *server;
    GSocketConnection *conn;
    std::string peer;
    std::vector<uint8_t> rx;
    uint8_t buf[2048];
    bool subscribed = false;
    bool closing = false;
  };

  App *const app;
  GSocketService *service = nullptr;
  std::list<Connection *> connections;
  Connection *voice_client = nullptr; // subscribed to the voice assistant
  std::string name;
  std::string mac;

  static gboolean on_incoming(GSocketService *service,
                              GSocketConnection *conn, GObject *source,
                              gpointer user_data);
  static void on_read(GObject *source, GAsyncResult *res, gpointer user_data);

  void read_next(Connection *c);
  void handle(Connection *c, uint32_t type, const uint8_t *payload, size_t len);
  void send(Connection *c, uint32_t type, const pb_msgdesc_t *fields,
            const void *msg);
  void close(Connection *c);
};

} // namespace esphome
} // namespace smartspeaker
