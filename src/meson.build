cpp = meson.get_compiler('cpp')

# Include paths: project root (for config.h) and the include/ tree (glib headers,
# pa_ringbuffer/pa_memorybarrier pulled from the device).
_incDirs = [
  include_directories('..'),
  include_directories('../include'),
  include_directories('../include/audio'),
  include_directories('../include/audio/alsa'),
  include_directories('../include/audio/pulseaudio'),
]

# ── Dependencies ───────────────────────────────────────────────────────────────

_deps = []

# glib-2.0: main loop, signals, logging, data structures
_deps += dependency('glib-2.0')

# gio-unix-2.0: g_unix_signal_add (SIGINT/SIGTERM handling)
_deps += dependency('gio-unix-2.0')

# ALSA: audio capture/playback via hw:* devices
_deps += dependency('alsa')

# PulseAudio: alternative audio backend
_deps += dependency('libpulse')
_deps += dependency('libpulse-simple')
_deps += dependency('libpulse-mainloop-glib')

# WebRTC audio processing: VAD (voice activity detection)
_deps += dependency('webrtc-audio-processing')

# libdl: dlopen/dlsym for loading libpv_porcupine.so at runtime
_deps += cpp.find_library('dl')

# POSIX threads: std::thread in AudioInput
_deps += dependency('threads')

# GStreamer: audio playback pipelines in AudioPlayer
_deps += dependency('gstreamer-1.0')

# JSON-GLib: JSON serialization for TTS POST body
_deps += dependency('json-glib-1.0')

# libsoup: HTTP client for TTS URL requests
_deps += dependency('libsoup-2.4')

# libevdev: Linux input device event handling (button input)
_deps += dependency('libevdev')

# speexdsp: acoustic echo cancellation and audio preprocessing
_deps += dependency('speexdsp')

# ── Source files ───────────────────────────────────────────────────────────────

_sources = [
  'main.cpp',
  'app.cpp',
  'config.cpp',
  'leds.cpp',
  'evinput.cpp',
  'stt.cpp',
  'spotifyd.cpp',
  'ws-protocol/client.cpp',
  'webrtc_vad_compat.c',
  'glibc_compat.c',

  # State machine
  'state/state.cpp',
  'state/sleeping.cpp',
  'state/listening.cpp',
  'state/processing.cpp',
  'state/saying.cpp',
  'state/config.cpp',
  'state/disabled.cpp',

  # Audio subsystem
  'audio/audioinput.cpp',
  'audio/audioplayer.cpp',
  'audio/audiovolume.cpp',
  'audio/wakeword.cpp',

  # ALSA backend
  'audio/alsa/input.cpp',
  'audio/alsa/audiofifo.cpp',
  'audio/alsa/volume.cpp',
  'audio/alsa/pa_ringbuffer.c',   # C file; meson handles mixed C/C++ automatically

  # PulseAudio backend
  'audio/pulseaudio/input.cpp',
  'audio/pulseaudio/volume.cpp',
]

executable(
  app_command,
  _sources,
  cpp_args : ['-DG_LOG_USE_STRUCTURED=1'],
  include_directories : _incDirs,
  dependencies : _deps,
  install : true,
)
