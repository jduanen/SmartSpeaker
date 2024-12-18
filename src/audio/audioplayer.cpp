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

#include "audioplayer.hpp"

#include <glib.h>
#include <gst/gst.h>
#include <string.h>

#ifdef STATIC
#include "gst/gstinitstaticplugins.h"
#endif

static const gchar *get_audio_output(const smartspeaker::Config &config,
                                     smartspeaker::AudioDestination destination) {
  switch (destination) {
    case smartspeaker::AudioDestination::MUSIC:
      return config.audio_output_device_music;
    case smartspeaker::AudioDestination::ALERT:
      return config.audio_output_device_alerts;
    case smartspeaker::AudioDestination::VOICE:
      return config.audio_output_device_voice;
    default:
      g_warn_if_reached();
      return config.audio_output_device_music;
  }
}

void smartspeaker::URLAudioTask::start() {
  g_object_set(G_OBJECT(pipeline.get()), "uri", url.c_str(), nullptr);

  // PROF_PRINT("gst pipeline started\n");
  gettimeofday(&t_start, NULL);
  gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
}

void smartspeaker::SayAudioTask::start() {
  if (soup_has_post_data)
    say_post();
  else
    say_get();

  // PROF_PRINT("gst pipeline started\n");
  gettimeofday(&t_start, NULL);
  gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
}

void smartspeaker::SayAudioTask::say_post() {
  auto_gobject_ptr<JsonBuilder> builder(json_builder_new(), adopt_mode::owned);
  json_builder_begin_object(builder.get());

  json_builder_set_member_name(builder.get(), "text");
  json_builder_add_string_value(builder.get(), text.c_str());

  json_builder_set_member_name(builder.get(), "gender");
  json_builder_add_string_value(builder.get(), voice);

  json_builder_end_object(builder.get());

  auto_gobject_ptr<JsonGenerator> gen(json_generator_new(), adopt_mode::owned);
  JsonNode *root = json_builder_get_root(builder.get());
  json_generator_set_root(gen.get(), root);
  gchar *jsonText = json_generator_to_data(gen.get(), NULL);

  g_debug("TTS body: %s", jsonText);
  g_object_set(G_OBJECT(soupsrc.get()), "post-data", jsonText, NULL);

  g_free(jsonText);
}

void smartspeaker::SayAudioTask::say_get() {
  std::unique_ptr<SoupURI, fn_deleter<SoupURI, soup_uri_free>> uri(
      soup_uri_new(base_tts_url.c_str()));
  soup_uri_set_query_from_fields(uri.get(), "text", text.c_str(), "gender",
                                 voice, nullptr);

  gchar *uristr = soup_uri_to_string(uri.get(), false);
  g_object_set(G_OBJECT(soupsrc.get()), "location", uristr, nullptr);
  g_free(uristr);
}

smartspeaker::AudioPlayer::AudioPlayer(App *appInstance)
    : app(appInstance), playing(false) {
  gst_init(NULL, NULL);
#ifdef STATIC
  gst_init_static_plugins();
#endif

  gchar *location = g_strdup_printf("%s/%s/voice/tts", app->config->nl_url,
                                    app->config->locale);
  base_tts_url = location;
  g_free(location);

  init_say_pipeline();
  init_url_pipeline();
}

static bool has_property(smartspeaker::auto_gobject_ptr<GObject> obj,
                         const char *property) {
  GObjectClass *class_ = G_OBJECT_CLASS(g_type_class_ref(obj.type()));
  bool has_prop = g_object_class_find_property(class_, property);
  g_type_class_unref(class_);

  return has_prop;
}

void smartspeaker::AudioPlayer::init_say_pipeline() {
  auto pipeline = auto_gobject_ptr<GstElement>(
      gst_pipeline_new("audio-player-say"), adopt_mode::ref_sink);
  soupsrc = auto_gobject_ptr<GstElement>(
      gst_element_factory_make("souphttpsrc", "http-source"),
      adopt_mode::ref_sink);
  soup_has_post_data =
      has_property(soupsrc.cast<GObject>(G_TYPE_OBJECT), "content-type") &&
      has_property(soupsrc.cast<GObject>(G_TYPE_OBJECT), "post-data");

  auto decoder = gst_element_factory_make("wavparse", "wav-parser");
  auto sink =
      gst_element_factory_make(app->config->audio_sink, "audio-output-say");

  if (!pipeline || !soupsrc || !decoder || !sink) {
    g_error("Gst element could not be created\n");
  }

  if (app->config->ssl_ca_file) {
    g_object_set(G_OBJECT(soupsrc.get()), "ssl-ca-file",
                 app->config->ssl_ca_file, NULL);
  }

  if (soup_has_post_data) {
    g_object_set(G_OBJECT(soupsrc.get()), "location", base_tts_url.c_str(),
                 "method", "POST", "content-type", "application/json", NULL);
  }

  const char *output_device =
      get_audio_output(*app->config, AudioDestination::VOICE);
  if (output_device) {
    g_object_set(G_OBJECT(sink), "device", output_device, NULL);
  }

  gst_bin_add_many(GST_BIN(pipeline.get()), soupsrc.get(), decoder, sink, NULL);
  gst_element_link_many(soupsrc.get(), decoder, sink, NULL);

  say_pipeline.init(this, pipeline);
}

void smartspeaker::AudioPlayer::init_url_pipeline() {
  auto sink = auto_gobject_ptr<GstElement>(
      gst_element_factory_make(app->config->audio_sink, "audio-output-url"),
      adopt_mode::ref_sink);
  const char *output_device =
      get_audio_output(*app->config, AudioDestination::MUSIC /* FIXME */);
  if (output_device)
    g_object_set(G_OBJECT(sink.get()), "device", output_device, NULL);

  auto pipeline = auto_gobject_ptr<GstElement>(
      gst_element_factory_make("playbin", "audio-player-url"),
      adopt_mode::ref_sink);
  g_object_set(G_OBJECT(pipeline.get()), "audio-sink", sink.get(), nullptr);

  url_pipeline.init(this, pipeline);
}

void smartspeaker::AudioPlayer::PipelineState::init(
    AudioPlayer *self, const auto_gobject_ptr<GstElement> &pipeline) {
  this->pipeline = pipeline;
  auto *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline.get()));
  bus_watch_id =
      gst_bus_add_watch(bus, smartspeaker::AudioPlayer::bus_call_queue, self);
  gst_object_unref(bus);
}

gboolean smartspeaker::AudioPlayer::bus_call_queue(GstBus *bus, GstMessage *msg,
                                            gpointer data) {
  AudioPlayer *obj = static_cast<AudioPlayer *>(data);
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_STREAM_STATUS:
      // PROF_PRINT("Stream status changed\n");

      // Dig into the status message... we want to know when the audio
      // stream starts, 'cause that's what we consider the when we deliver
      // to the user -- it ends both the TTS window and entire run of
      // processing tracking.
      GstStreamStatusType type;
      GstElement *owner;
      gst_message_parse_stream_status(msg, &type, &owner);

      // By printing all status events and timing when the audio starts
      // it seems like we want the "enter" type event.
      //
      // There are actually _two_ of them fired, and we want the second --
      // which is convinient for us, because we can track the event _both_
      // times, with the second time over-writing the first, which results
      // in the desired state.
      if (type == GST_STREAM_STATUS_TYPE_ENTER && obj->playing_task) {
        obj->app->dispatch(new state::events::PlayerStreamEnter(
            obj->playing_task->type, obj->playing_task->ref_id));
      }
      break;
    case GST_MESSAGE_EOS:
      g_message("End of stream");
      if (obj->playing_task) {
          obj->app->dispatch(new state::events::PlayerStreamEnd(
              obj->playing_task->type, obj->playing_task->ref_id));
          obj->playing_task->stop();
      }
      obj->playing_task = nullptr;
      obj->playing = false;
      obj->dispatch_queue();
      break;
    case GST_MESSAGE_ERROR: {
      gchar *debug;
      GError *error = NULL;
      gst_message_parse_error(msg, &error, &debug);
      g_free(debug);

      g_printerr("Error: %s\n", error->message);
      g_error_free(error);

      if (obj->playing_task) {
        obj->playing_task->stop();
        obj->playing_task = nullptr;
        obj->playing = false;
      }
      obj->dispatch_queue();
      break;
    }
    default:
      break;
  }

  return true;
}

gboolean smartspeaker::AudioPlayer::play_sound(enum Sound_t id,
                                        AudioDestination destination) {
  switch (id) {
    case Sound_t::WAKE:
      return play_location(app->config->sound_wake, destination);
    case Sound_t::NO_INPUT:
      return play_location(app->config->sound_no_input, destination);
    case Sound_t::TOO_MUCH_INPUT:
      return play_location(app->config->sound_too_much_input, destination);
    case Sound_t::NEWS_INTRO:
      return play_location(app->config->sound_news_intro, destination);
    case Sound_t::ALARM_CLOCK_ELAPSED:
      return play_location(app->config->sound_alarm_clock_elapsed, destination);
    case Sound_t::WORKING:
      return play_location(app->config->sound_working, destination);
    case Sound_t::STT_ERROR:
      return play_location(app->config->sound_stt_error, destination);
  }
  return false;
}

gboolean smartspeaker::AudioPlayer::play_location(const gchar *location,
                                           AudioDestination destination) {
  if (!location || strlen(location) < 1)
    return false;

  gchar *path;
  if (*location == '/')
    path = g_strdup(location);
  else
    path = g_build_filename(app->config->asset_dir, location, nullptr);
  gchar *uri = g_strdup_printf("file://%s", path);

  gboolean ok = play_url(uri, destination);

  g_free(uri);
  g_free(path);

  return ok;
}

bool smartspeaker::AudioPlayer::play_url(const std::string &uri,
                                  AudioDestination destination, gint64 ref_id) {
  if (uri.empty())
    return false;

  g_message("Queueing %s for playback", uri.c_str());

  player_queue.push(
      std::make_unique<URLAudioTask>(url_pipeline.pipeline, uri, ref_id));
  dispatch_queue();
  return true;
}

bool smartspeaker::AudioPlayer::say(const std::string &text, gint64 ref_id) {
  if (text.empty())
    return false;

  player_queue.push(std::make_unique<SayAudioTask>(
      say_pipeline.pipeline, soupsrc, text, base_tts_url,
      app->config->audio_voice, soup_has_post_data, ref_id));
  dispatch_queue();

  return true;
}

void smartspeaker::AudioPlayer::dispatch_queue() {
  if (!playing && !player_queue.empty()) {
    std::unique_ptr<AudioTask> &task = player_queue.front();
    playing_task = std::move(task);
    player_queue.pop();

    playing_task->start();
    playing = true;
  }
}

gboolean smartspeaker::AudioPlayer::clean_queue() {
  if (playing_task)
    playing_task->stop();
  playing_task.reset();
  while (!player_queue.empty()) {
    player_queue.pop();
  }
  playing = false;
  return true;
}

gboolean smartspeaker::AudioPlayer::stop() {
  if (!playing)
    return true;
  clean_queue();
  return true;
}
