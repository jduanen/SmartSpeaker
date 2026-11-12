/*
 * SmartSpeaker: the 12-LED ring
 *
 * Derived from Genie's leds.hpp (Copyright 2021 The Board of Trustees of the
 * Leland Stanford Junior University, Apache License 2.0). The phases, their
 * priority and their animations follow Satellite1's led_ring.yaml, scaled
 * from 24 LEDs to 12.
 */

#pragma once

#include "app.hpp"

namespace smartspeaker {

/** The app's phase; overlays (no HA, volume, error, mute) are separate. */
enum class LedsState_t {
  Starting,
  Sleeping,
  Listening,
  Processing,
  Saying,
  Ringing,
  Config,
};

class Leds {
public:
  static const int COUNT = 12;

  Leds(App *app);
  ~Leds();
  int init();

  void animate(LedsState_t state);
  /** HA subscribed to the voice assistant; before the first time, "init". */
  void set_connected(bool connected);
  void set_muted(bool muted);
  /** Show the volume bar (0..1) for a second. */
  void show_volume(float volume);
  /** Red pulse for two seconds. */
  void show_error();

  /** The "LED ring" light: idle color and the phases' accent color. */
  void set_ring(bool on, float brightness, float r, float g, float b);
  bool ring_on() const { return ring.on; }
  float ring_brightness() const { return ring.brightness; }
  float ring_red() const { return ring.r; }
  float ring_green() const { return ring.g; }
  float ring_blue() const { return ring.b; }

private:
  struct Ring {
    bool on;
    float brightness, r, g, b;
  };

  App *const app;
  bool initialized = false;
  gchar *path_all = nullptr;

  LedsState_t state = LedsState_t::Starting;
  bool connected = false;
  bool ever_connected = false;
  bool muted = false;
  Ring ring = {false, 0.66f, 0.094f, 0.733f, 0.949f};
  float volume = 0;
  gint64 volume_until = 0; // monotonic us
  gint64 error_until = 0;

  guint timer = 0;
  unsigned tick = 0;
  int spin = 0; // where the listening spin left off
  float twinkle[COUNT] = {};
  int last[COUNT];

  void update();
  static gboolean on_tick(gpointer data);
  bool render(int frame[COUNT]);
  void write(const int frame[COUNT]);
};

} // namespace smartspeaker
