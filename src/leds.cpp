/*
 * SmartSpeaker: the 12-LED ring
 *
 * Derived from Genie's leds.cpp (Copyright 2021 The Board of Trustees of the
 * Leland Stanford Junior University, Apache License 2.0). The ring is an
 * is31fl3236 behind /sys/class/leds/isled01: `user_space` 1 hands it to us
 * (0 gives it back to the kernel effects), `set_all_led_color` takes twelve
 * decimal 0xRRGGBB values.
 *
 * Each frame is rendered from the app state (Satellite1 led_ring.yaml
 * control_leds priority); a 50 ms timer runs only while something animates,
 * and unchanged frames aren't written (the driver logs every write).
 */

#include "leds.hpp"

#include <algorithm>
#include <cmath>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::Leds"

namespace smartspeaker {

static const unsigned TICK_MS = 50;
static const gint64 VOLUME_SHOW_US = 1000000;
static const gint64 ERROR_SHOW_US = 2000000;

namespace {
struct Rgb {
  float r, g, b;
};
const Rgb RED = {1, 0, 0};
const Rgb BLACK = {0, 0, 0};
const Rgb INIT_BLUE = {0.094f, 0.733f, 0.949f}; // Satellite1 init twinkle

int pixel(Rgb c, float level) {
  auto ch = [level](float v) {
    return std::max(0, std::min(255, (int)lroundf(v * level * 255)));
  };
  return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

/** 0..1..0 over `period` ticks. */
float triangle(unsigned tick, unsigned period) {
  float x = (float)(tick % period) / period;
  return x < 0.5f ? 2 * x : 2 - 2 * x;
}
} // namespace

static bool write_file(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  if (!f)
    return false;
  bool ok = fputs(text, f) >= 0;
  return fclose(f) == 0 && ok;
}

Leds::Leds(App *app) : app(app) {
  for (int &v : last)
    v = -1;
}

Leds::~Leds() {
  if (timer)
    g_source_remove(timer);
  if (initialized) {
    gchar *user = g_strdup_printf("%s/user_space", app->config->leds_path);
    write_file(user, "0");
    g_free(user);
  }
  g_free(path_all);
}

int Leds::init() {
  Config *c = app->config.get();
  if (!c->leds_enabled)
    return false;
  ring = {c->leds_ring_on, (float)c->leds_ring_brightness,
          ((c->leds_ring_color >> 16) & 0xff) / 255.0f,
          ((c->leds_ring_color >> 8) & 0xff) / 255.0f,
          (c->leds_ring_color & 0xff) / 255.0f};
  if (g_strcmp0(c->leds_type, "aw") != 0) {
    g_warning("Invalid leds type in configuration file");
    return false;
  }

  gchar *user = g_strdup_printf("%s/user_space", c->leds_path);
  gchar *brightness = g_strdup_printf("%s/brightness", c->leds_path);
  // take the ring from the kernel effects; colors carry the brightness
  initialized = write_file(user, "1") && write_file(brightness, "255");
  g_free(user);
  g_free(brightness);
  if (!initialized) {
    g_warning("Cannot take over the LEDs at %s", c->leds_path);
    return false;
  }
  path_all = g_strdup_printf("%s/set_all_led_color", c->leds_path);
  update();
  return true;
}

void Leds::animate(LedsState_t s) {
  if (state == LedsState_t::Listening)
    spin = (tick / 2) % COUNT;
  state = s;
  update();
}

void Leds::set_connected(bool c) {
  connected = c;
  ever_connected = ever_connected || c;
  update();
}

void Leds::set_muted(bool m) {
  muted = m;
  update();
}

void Leds::show_volume(float v) {
  volume = v;
  volume_until = g_get_monotonic_time() + VOLUME_SHOW_US;
  update();
}

void Leds::show_error() {
  error_until = g_get_monotonic_time() + ERROR_SHOW_US;
  update();
}

void Leds::set_ring(bool on, float brightness, float r, float g, float b) {
  ring = {on, brightness, r, g, b};
  Config *c = app->config.get();
  if (!c->leds_enabled)
    return;
  c->leds_ring_on = on;
  c->leds_ring_brightness = brightness;
  c->leds_ring_color = pixel({r, g, b}, 1.0f);
  gchar *color = g_strdup_printf("%06x", c->leds_ring_color);
  gchar *level = g_strdup_printf("%.2f", brightness);
  c->set_string("leds", "ring", on ? "true" : "false");
  c->set_string("leds", "ring_color", color);
  c->set_string("leds", "ring_brightness", level);
  g_free(color);
  g_free(level);
  update();
}

/** Render now; keep ticking while the frame animates. */
void Leds::update() {
  if (!initialized)
    return;
  int frame[COUNT];
  bool animated = render(frame);
  write(frame);
  if (animated && !timer) {
    timer = g_timeout_add(TICK_MS, on_tick, this);
  } else if (!animated && timer) {
    g_source_remove(timer);
    timer = 0;
  }
}

gboolean Leds::on_tick(gpointer data) {
  Leds *self = static_cast<Leds *>(data);
  self->tick++;
  int frame[COUNT];
  bool animated = self->render(frame);
  self->write(frame);
  if (!animated)
    self->timer = 0;
  return animated ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

// Satellite1 control_leds, highest priority first. Returns true if the
// frame changes over time (or an overlay is waiting to expire).
bool Leds::render(int f[COUNT]) {
  gint64 now = g_get_monotonic_time();
  Rgb accent = {ring.r, ring.g, ring.b};
  // phases: max(ring brightness, 20%); overlays a bit brighter
  float level = std::max(ring.brightness, 0.2f);
  float bright = std::min(level + 0.1f, 1.0f);
  for (int i = 0; i < COUNT; i++)
    f[i] = 0;

  if (!connected) {
    // init / no HA: twinkle
    Rgb c = ever_connected ? RED : INIT_BLUE;
    for (int i = 0; i < COUNT; i++)
      twinkle[i] *= 0.8f;
    if (g_random_int_range(0, 2) == 0)
      twinkle[g_random_int_range(0, COUNT)] = 1.0f;
    for (int i = 0; i < COUNT; i++)
      f[i] = pixel(c, 0.66f * twinkle[i]);
    return true;
  }

  if (now < volume_until) {
    float lit = volume * COUNT;
    for (int i = 0; i < COUNT; i++)
      f[i] = i < lit ? pixel(accent, bright * std::min(lit - i, 1.0f)) : 0;
    if (volume <= 0)
      f[0] = pixel(RED, bright);
    return true;
  }

  switch (state) {
    case LedsState_t::Ringing: {
      float p = triangle(tick, 20);
      for (int i = 0; i < COUNT; i++)
        f[i] = pixel(accent, level * p);
      if (muted)
        f[1] = f[4] = pixel(RED, bright);
      return true;
    }
    case LedsState_t::Listening: {
      // two heads half a ring apart, tails behind, clockwise
      int head = (tick / 2) % COUNT;
      for (int k = 0; k < 2; k++) {
        int h = head + k * COUNT / 2;
        f[h % COUNT] = pixel(accent, level);
        f[(h + COUNT - 1) % COUNT] = pixel(accent, level * 0.75f);
        f[(h + COUNT - 2) % COUNT] = pixel(accent, level * 0.5f);
      }
      return true;
    }
    case LedsState_t::Processing: {
      // two opposite LEDs breathing where the spin stopped
      float p = 1.0f - triangle(tick, 8);
      f[spin] = f[(spin + COUNT / 2) % COUNT] = pixel(accent, level * p);
      return true;
    }
    case LedsState_t::Saying: {
      // counter-clockwise
      int head = COUNT - 1 - (int)((tick / 2) % COUNT);
      for (int k = 0; k < 2; k++) {
        int h = head + k * COUNT / 2;
        f[h % COUNT] = pixel(accent, level);
        f[(h + 1) % COUNT] = pixel(accent, level * 0.75f);
        f[(h + 2) % COUNT] = pixel(accent, level * 0.5f);
      }
      return true;
    }
    default:
      break;
  }

  if (now < error_until) {
    float p = 1.0f - triangle(tick, 8);
    for (int i = 0; i < COUNT; i++)
      f[i] = pixel(RED, bright * p);
    return true;
  }

  // idle: the light entity, with the mute marks on top
  for (int i = 0; i < COUNT; i++)
    f[i] = ring.on ? pixel(accent, ring.brightness) : 0;
  if (muted) {
    for (int i = 0; i < COUNT; i += 3) {
      f[i] = pixel(RED, bright);
      if (ring.on)
        f[(i + 1) % COUNT] = f[(i + COUNT - 1) % COUNT] = 0;
    }
  }
  return false;
}

void Leds::write(const int f[COUNT]) {
  if (memcmp(f, last, sizeof(last)) == 0)
    return;
  memcpy(last, f, sizeof(last));
  char buf[COUNT * 9 + 1];
  int n = 0;
  for (int i = 0; i < COUNT; i++)
    n += snprintf(buf + n, sizeof(buf) - n, "%d ", f[i]);
  if (!write_file(path_all, buf))
    g_warning("Cannot write %s", path_all);
}

} // namespace smartspeaker
