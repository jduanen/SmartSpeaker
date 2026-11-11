/*
* SmartSpeaker derived from Genie
*/

// -*- mode: cpp; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//
// This file is part of Genie
//
// Copyright 2021 The Board of Trustees of the Leland Stanford Junior University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "evinput.hpp"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

smartspeaker::EVInput::EVInput(App *app) : app(app) {}

smartspeaker::EVInput::~EVInput() { g_source_unref(source); }

gboolean smartspeaker::EVInput::event_prepare(GSource *source, gint *timeout) {
  *timeout = -1;
  return false;
}

gboolean smartspeaker::EVInput::event_check(GSource *source) {
  InputEventSource *event_source = (InputEventSource *)source;
  gboolean rc;
  rc = (event_source->event_poll_fd.revents & G_IO_IN);
  return rc;
}

gboolean smartspeaker::EVInput::event_dispatch(GSource *g_source, GSourceFunc callback,
                                        gpointer user_data) {
  EVInput *ev_input = static_cast<EVInput *>(user_data);
  InputEventSource *source = (InputEventSource *)g_source;
  input_event ev;
  ssize_t rc;

  while ((rc = read(source->event_poll_fd.fd, &ev, sizeof(ev))) ==
         (ssize_t)sizeof(ev)) {
    g_debug("Event type %d, code %d, value %d", ev.type, ev.code, ev.value);
    // Dispatch key events
    if (ev.type == EV_KEY && ev.value == 0) {
      // For key constant names/values see:
      //
      // https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h
      //
      switch (ev.code) {
        case KEY_VOLUMEUP:
          ev_input->app->dispatch(new state::events::AdjustVolume(1));
          break;
        case KEY_VOLUMEDOWN:
          ev_input->app->dispatch(new state::events::AdjustVolume(-1));
          break;
        case KEY_MUTE:
          ev_input->app->dispatch(new state::events::ToggleDisabled());
          break;
        case KEY_PLAYPAUSE:
          ev_input->app->dispatch(new state::events::TogglePlayback());
          break;
        default:
          g_warning("Unhandled button up event, code=%d", ev.code);
          break;
      }
    }
  }
  if (rc < 0 && errno != EAGAIN)
    g_print("source read failed %s\n", strerror(errno));

  return callback(user_data);
}

gboolean smartspeaker::EVInput::callback(gpointer user_data) {
  return G_SOURCE_CONTINUE;
}

int smartspeaker::EVInput::init() {
  if (!app->config->buttons_enabled)
    return false;

  int fd, rc = 1;

  source = g_source_new(&event_funcs, sizeof(InputEventSource));
  InputEventSource *event_source = (InputEventSource *)source;

  fd = open(app->config->evinput_device, O_RDONLY | O_NONBLOCK);
  if (fd == -1) {
    g_print("Failed to open %s (%s)\n", app->config->evinput_device,
            strerror(errno));
    return false;
  }

  event_source->event_poll_fd.fd = fd;
  event_source->event_poll_fd.events = G_IO_IN;

  char name[256] = "?";
  ioctl(fd, EVIOCGNAME(sizeof(name)), name);
  g_debug("Input device name: \"%s\"\n", name);

  g_source_set_priority(source, G_PRIORITY_DEFAULT);
  g_source_add_poll(source, &event_source->event_poll_fd);
  g_source_set_can_recurse(source, true);
  g_source_set_callback(source, (GSourceFunc)callback, this, NULL);
  g_source_attach(source, NULL);

  return true;
}
