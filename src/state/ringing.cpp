/*
 * SmartSpeaker: a finished Assist timer is ringing
 */

#include "state/ringing.hpp"
#include "app.hpp"
#include "audio/audioinput.hpp"
#include "audio/audioplayer.hpp"
#include "audio/audiovolume.hpp"
#include "audio/mixer.hpp"
#include "leds.hpp"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "smartspeaker::state::Ringing"

namespace smartspeaker {
namespace state {

void Ringing::enter() {
  State::enter();
  app->leds->animate(LedsState_t::Ringing);
  app->audio_volume_controller->duck();
  app->audio_input->wake_word()->set_stop_enabled(true);
  if (!app->audio_player->play_sound(Sound_t::ALARM_CLOCK_ELAPSED))
    g_warning("No [sound] alarm_clock_elapsed configured; ringing silently");
  timeout_source = g_timeout_add_seconds(RING_MINUTES * 60, on_timeout, app);
}

void Ringing::exit() {
  app->audio_input->wake_word()->set_stop_enabled(false);
  g_source_remove(timeout_source);
  app->mixer->announce.stop();
  State::exit();
}

gboolean Ringing::on_timeout(gpointer data) {
  App *app = static_cast<App *>(data);
  // stop as if the button were pressed; the source is removed in exit()
  app->dispatch(new events::TogglePlayback());
  return G_SOURCE_CONTINUE;
}

void Ringing::react(events::voice::ResponseFinished *) {
  app->audio_player->play_sound(Sound_t::ALARM_CLOCK_ELAPSED);
}

void Ringing::react(events::TogglePlayback *) {
  app->transit(new Sleeping(app));
}

void Ringing::react(events::Wake *) {
  app->transit(new Sleeping(app));
}

void Ringing::react(events::StopWord *) {
  app->transit(new Sleeping(app));
}

} // namespace state
} // namespace smartspeaker
