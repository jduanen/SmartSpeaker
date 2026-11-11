/*
 * SmartSpeaker: a finished Assist timer is ringing
 */

#pragma once

#include <glib.h>

#include "events.hpp"
#include "state.hpp"

namespace smartspeaker {
namespace state {

/**
 * Repeats the timer sound until PLAYPAUSE is pressed, or for at most
 * RING_MINUTES (HA Voice PE rings for 15 minutes).
 */
class Ringing : public State {
public:
  static const constexpr char *NAME = "Ringing";
  static const unsigned RING_MINUTES = 15;

  Ringing(App *app) : State{app} {}

  void enter() override;
  void exit() override;
  const char *name() override { return NAME; };

  void react(events::TogglePlayback *) override;
  void react(events::voice::ResponseFinished *) override;
  void react(events::voice::TimerFinished *) override {}

private:
  guint timeout_source = 0;
  static gboolean on_timeout(gpointer data);
};

} // namespace state
} // namespace smartspeaker
