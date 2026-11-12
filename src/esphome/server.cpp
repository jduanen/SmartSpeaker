/*
 * SmartSpeaker: ESPHome native API server (plaintext)
 *
 * Handles the connection-level messages (hello, device info, entity
 * listing, state subscription, ping, disconnect) and hands entity commands
 * to Entities. Unknown messages are logged and ignored, so newer clients
 * keep working.
 */

#include "esphome/server.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>

#include "app.hpp"
#include "leds.hpp"
#include "config.h"
#include "config.hpp"
#include "esphome/api.pb.h"
#include "esphome/api_ids.h"
#include "esphome/frame.hpp"
#include "pb_decode.h"
#include "voice/session.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::esphome::Server"

namespace smartspeaker {
namespace esphome {

// The same API version linux-voice-assistant reports; 1.15+ clients would
// also expect DeviceCapabilities for voice features.
static const uint32_t API_VERSION_MAJOR = 1;
static const uint32_t API_VERSION_MINOR = 10;
// HA records this as the firmware version; like linux-voice-assistant we
// report the aioesphomeapi release our proto/api.proto came from.
static const char *ESPHOME_VERSION = "46.29.0";

static char *s(const char *str) { return const_cast<char *>(str); }

/** MAC of wlan0 (the Xiaodu's only network), else the first non-loopback. */
static std::string read_mac() {
  gchar *mac = nullptr;
  if (!g_file_get_contents("/sys/class/net/wlan0/address", &mac, nullptr,
                           nullptr)) {
    GDir *dir = g_dir_open("/sys/class/net", 0, nullptr);
    const gchar *ifname;
    while (dir && !mac && (ifname = g_dir_read_name(dir))) {
      if (strcmp(ifname, "lo") == 0)
        continue;
      gchar *path = g_build_filename("/sys/class/net", ifname, "address", nullptr);
      g_file_get_contents(path, &mac, nullptr, nullptr);
      g_free(path);
    }
    if (dir)
      g_dir_close(dir);
  }
  std::string result = mac ? g_strstrip(mac) : "00:00:00:00:00:00";
  g_free(mac);
  for (auto &ch : result)
    ch = g_ascii_toupper(ch);
  return result;
}

Server::Server(App *app) : entities(app), app(app) {
  mac = read_mac();
  name = app->config->esphome_name;
  if (name.empty()) {
    // xiaodu-<last 3 MAC bytes>, like ESPHome's name_add_mac_suffix
    std::string suffix;
    for (char ch : mac.substr(9))
      if (ch != ':')
        suffix += g_ascii_tolower(ch);
    name = "xiaodu-" + suffix;
  }

  entities.broadcast = [this](uint32_t type, const pb_msgdesc_t *fields,
                              const void *msg) {
    for (Connection *c : connections)
      if (c->subscribed)
        send(c, type, fields, msg);
  };
}

Server::~Server() {
  while (!connections.empty())
    close(connections.front());
  if (service) {
    g_socket_service_stop(service);
    g_object_unref(service);
  }
}

bool Server::start() {
  GError *error = nullptr;
  service = g_socket_service_new();
  if (!g_socket_listener_add_inet_port(G_SOCKET_LISTENER(service),
                                       app->config->esphome_port, nullptr,
                                       &error)) {
    g_critical("Cannot listen on port %zu: %s", app->config->esphome_port,
               error->message);
    g_error_free(error);
    return false;
  }
  g_signal_connect(service, "incoming", G_CALLBACK(on_incoming), this);
  g_socket_service_start(service);
  g_message("ESPHome API '%s' (%s) listening on port %zu", name.c_str(),
            mac.c_str(), app->config->esphome_port);
  return true;
}

gboolean Server::on_incoming(GSocketService *service, GSocketConnection *conn,
                             GObject *source, gpointer user_data) {
  Server *self = static_cast<Server *>(user_data);
  Connection *c = new Connection();
  c->server = self;
  c->conn = G_SOCKET_CONNECTION(g_object_ref(conn));

  GSocket *sock = g_socket_connection_get_socket(conn);
  g_socket_set_option(sock, IPPROTO_TCP, TCP_NODELAY, 1, nullptr);
  // Writes are synchronous; don't let a stalled client hang the main loop.
  g_socket_set_timeout(sock, 5);

  GSocketAddress *addr = g_socket_connection_get_remote_address(conn, nullptr);
  if (addr) {
    gchar *ip = g_inet_address_to_string(
        g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(addr)));
    c->peer = ip;
    g_free(ip);
    g_object_unref(addr);
  }
  g_message("Client %s connected", c->peer.c_str());

  self->connections.push_back(c);
  self->read_next(c);
  return TRUE;
}

void Server::read_next(Connection *c) {
  g_input_stream_read_async(
      g_io_stream_get_input_stream(G_IO_STREAM(c->conn)), c->buf,
      sizeof(c->buf), G_PRIORITY_DEFAULT, nullptr, on_read, c);
}

void Server::on_read(GObject *source, GAsyncResult *res, gpointer user_data) {
  Connection *c = static_cast<Connection *>(user_data);
  Server *self = c->server;
  GError *error = nullptr;
  gssize n = g_input_stream_read_finish(G_INPUT_STREAM(source), res, &error);
  if (n < 0 && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
    // The socket timeout is there for writes; an idle client (HA pings
    // every 20 s) is fine, so keep reading.
    g_error_free(error);
    self->read_next(c);
    return;
  }
  if (n <= 0) {
    if (error) {
      g_message("Client %s read error: %s", c->peer.c_str(), error->message);
      g_error_free(error);
    }
    self->close(c);
    return;
  }

  c->rx.insert(c->rx.end(), c->buf, c->buf + n);
  size_t pos = 0;
  while (!c->closing) {
    uint32_t type;
    size_t off, len, consumed;
    FrameResult r = parse_frame(c->rx.data() + pos, c->rx.size() - pos, &type,
                                &off, &len, &consumed);
    if (r == FrameResult::INCOMPLETE)
      break;
    if (r == FrameResult::ERROR) {
      g_warning("Client %s sent a bad frame (preamble 0x%02x); encryption "
                "isn't supported yet",
                c->peer.c_str(), c->rx[pos]);
      c->closing = true;
      break;
    }
    self->handle(c, type, c->rx.data() + pos + off, len);
    pos += consumed;
  }

  if (c->closing) {
    self->close(c);
    return;
  }
  c->rx.erase(c->rx.begin(), c->rx.begin() + pos);
  self->read_next(c);
}

void Server::send(Connection *c, uint32_t type, const pb_msgdesc_t *fields,
                  const void *msg) {
  if (c->closing)
    return;
  std::vector<uint8_t> frame;
  if (!encode_frame(type, fields, msg, frame)) {
    g_warning("Failed to encode message type %u", type);
    return;
  }
  GError *error = nullptr;
  if (!g_output_stream_write_all(
          g_io_stream_get_output_stream(G_IO_STREAM(c->conn)), frame.data(),
          frame.size(), nullptr, nullptr, &error)) {
    g_message("Client %s write error: %s", c->peer.c_str(), error->message);
    g_error_free(error);
    // close after the current read callback finishes with this connection
    c->closing = true;
  }
}

void Server::close(Connection *c) {
  g_message("Client %s disconnected", c->peer.c_str());
  if (c == voice_client) {
    voice_client = nullptr;
    app->voice->set_client(nullptr);
    app->leds->set_connected(false);
  }
  connections.remove(c);
  g_io_stream_close(G_IO_STREAM(c->conn), nullptr, nullptr);
  g_object_unref(c->conn);
  delete c;
}

void Server::handle(Connection *c, uint32_t type, const uint8_t *payload,
                    size_t len) {
  Send reply = [this, c](uint32_t t, const pb_msgdesc_t *f, const void *m) {
    send(c, t, f, m);
  };

  switch (type) {
    case MSG_HELLO_REQUEST: {
      HelloRequest req = HelloRequest_init_zero;
      pb_istream_t stream = pb_istream_from_buffer(payload, len);
      if (pb_decode(&stream, HelloRequest_fields, &req))
        g_message("Hello from %s (%s, API %u.%u)", c->peer.c_str(),
                  req.client_info ? req.client_info : "", req.api_version_major,
                  req.api_version_minor);
      pb_release(HelloRequest_fields, &req);

      HelloResponse resp = HelloResponse_init_zero;
      resp.api_version_major = API_VERSION_MAJOR;
      resp.api_version_minor = API_VERSION_MINOR;
      resp.server_info = s("smartspeaker " PACKAGE_VERSION);
      resp.name = s(name.c_str());
      send(c, MSG_HELLO_RESPONSE, HelloResponse_fields, &resp);
      break;
    }

    case MSG_AUTHENTICATION_REQUEST: {
      // Deprecated password login; older clients wait for this reply.
      AuthenticationResponse resp = AuthenticationResponse_init_zero;
      send(c, MSG_AUTHENTICATION_RESPONSE, AuthenticationResponse_fields, &resp);
      break;
    }

    case MSG_DEVICE_INFO_REQUEST: {
      DeviceInfoResponse resp = DeviceInfoResponse_init_zero;
      resp.name = s(name.c_str());
      resp.friendly_name = s("Xiaodu Speaker");
      resp.mac_address = s(mac.c_str());
      resp.esphome_version = s(ESPHOME_VERSION);
      resp.compilation_time = s(__DATE__ ", " __TIME__);
      resp.manufacturer = s("Baidu");
      resp.model = s("Xiaodu XDH-1D-A1");
      resp.project_name = s("smartspeaker.xiaodu");
      resp.project_version = s(PACKAGE_VERSION);
      resp.voice_assistant_feature_flags = VoiceSession::FEATURES;
      send(c, MSG_DEVICE_INFO_RESPONSE, DeviceInfoResponse_fields, &resp);
      break;
    }

    case MSG_DEVICE_CAPABILITIES_REQUEST: {
      DeviceCapabilitiesResponse resp = DeviceCapabilitiesResponse_init_zero;
      send(c, MSG_DEVICE_CAPABILITIES_RESPONSE,
           DeviceCapabilitiesResponse_fields, &resp);
      break;
    }

    case MSG_LIST_ENTITIES_REQUEST: {
      entities.list(reply);
      ListEntitiesDoneResponse done = ListEntitiesDoneResponse_init_zero;
      send(c, MSG_LIST_ENTITIES_DONE_RESPONSE, ListEntitiesDoneResponse_fields,
           &done);
      break;
    }

    case MSG_SUBSCRIBE_STATES_REQUEST:
      c->subscribed = true;
      entities.states(reply);
      break;

    case MSG_PING_REQUEST: {
      PingResponse resp = PingResponse_init_zero;
      send(c, MSG_PING_RESPONSE, PingResponse_fields, &resp);
      break;
    }

    case MSG_PING_RESPONSE:
      break;

    case MSG_DISCONNECT_REQUEST: {
      DisconnectResponse resp = DisconnectResponse_init_zero;
      send(c, MSG_DISCONNECT_RESPONSE, DisconnectResponse_fields, &resp);
      c->closing = true;
      break;
    }

    case MSG_SUBSCRIBE_VOICE_ASSISTANT_REQUEST: {
      SubscribeVoiceAssistantRequest req = SubscribeVoiceAssistantRequest_init_zero;
      pb_istream_t stream = pb_istream_from_buffer(payload, len);
      if (!pb_decode(&stream, SubscribeVoiceAssistantRequest_fields, &req))
        break;
      if (req.subscribe) {
        g_message("Client %s subscribed to the voice assistant", c->peer.c_str());
        voice_client = c;
        app->voice->set_client(reply);
        app->leds->set_connected(true);
      } else if (c == voice_client) {
        g_message("Client %s unsubscribed from the voice assistant",
                  c->peer.c_str());
        voice_client = nullptr;
        app->voice->set_client(nullptr);
        app->leds->set_connected(false);
      }
      break;
    }

    // Subscriptions we accept but have nothing to send for
    case MSG_SUBSCRIBE_LOGS_REQUEST:
    case MSG_SUBSCRIBE_HOMEASSISTANT_SERVICES_REQUEST:
    case MSG_SUBSCRIBE_HOME_ASSISTANT_STATES_REQUEST:
      break;

    default:
      if (c == voice_client && app->voice->handle(type, payload, len))
        break;
      if (!entities.handle(type, payload, len))
        g_message("Ignoring message type %u from %s", type, c->peer.c_str());
      break;
  }
}

} // namespace esphome
} // namespace smartspeaker
