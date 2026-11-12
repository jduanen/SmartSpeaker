/*
 * SmartSpeaker: run the wake word engine over WAV files
 *
 *   wakeword_wavs <model_dir> <wake_word_id> [--stop] [--sensitivity N] file.wav...
 *
 * Files must be 16 kHz mono 16-bit. Prints each detection with its time in
 * the file; exits 0. Writes config.ini in the current directory (WakeWord
 * saves its settings), so run it from a scratch directory.
 */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio/wakeword.hpp"
#include "config.hpp"
#include "dr_wav.h"

using smartspeaker::AudioFrame;
using smartspeaker::Config;
using smartspeaker::WakeWord;

int main(int argc, char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <model_dir> <wake_word_id> [--stop] "
                    "[--sensitivity 0|1|2] file.wav...\n", argv[0]);
    return 2;
  }

  Config config;
  config.load();
  g_free(config.esphome_model_dir);
  config.esphome_model_dir = g_strdup(argv[1]);
  g_free(config.wakeword_active);
  config.wakeword_active = g_strdup(argv[2]);

  WakeWord ww(&config);
  if (ww.active() != argv[2]) {
    fprintf(stderr, "cannot load wake word %s\n", argv[2]);
    return 1;
  }

  int first = 3;
  bool stop = false;
  for (; first < argc && argv[first][0] == '-'; first++) {
    if (strcmp(argv[first], "--stop") == 0) {
      stop = true;
    } else if (strcmp(argv[first], "--sensitivity") == 0 && first + 1 < argc) {
      ww.set_sensitivity(WakeWord::SENSITIVITIES[atoi(argv[++first]) % 3]);
    }
  }
  ww.set_stop_enabled(stop);
  printf("wake word %s, %s%s\n", ww.active_phrase().c_str(),
         ww.sensitivity().c_str(), stop ? ", stop enabled" : "");

  for (int i = first; i < argc; i++) {
    unsigned channels, rate;
    drwav_uint64 frames;
    gchar *data = nullptr;
    gsize size = 0;
    int16_t *pcm = nullptr;
    // the app builds dr_wav without stdio
    if (g_file_get_contents(argv[i], &data, &size, nullptr))
      pcm = drwav_open_memory_and_read_pcm_frames_s16(
          data, size, &channels, &rate, &frames, nullptr);
    g_free(data);
    if (!pcm || channels != 1 || rate != 16000) {
      printf("%s: not a 16 kHz mono WAV\n", argv[i]);
      drwav_free(pcm, nullptr);
      continue;
    }
    int detections = 0;
    for (drwav_uint64 at = 0; at + ww.pv_frame_length <= frames;
         at += ww.pv_frame_length) {
      AudioFrame frame(ww.pv_frame_length);
      memcpy(frame.samples, pcm + at, ww.pv_frame_length * sizeof(int16_t));
      WakeWord::Detection d = ww.process(&frame);
      if (d != WakeWord::Detection::NONE) {
        printf("%s: %s at %.2f s\n", argv[i],
               d == WakeWord::Detection::WAKE ? "WAKE" : "STOP",
               (double)at / rate);
        detections++;
      }
    }
    printf("%s: %d detection(s) in %.1f s\n", argv[i], detections,
           (double)frames / rate);
    drwav_free(pcm, nullptr);
  }
  return 0;
}
