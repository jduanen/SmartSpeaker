cpp = meson.get_compiler('cpp')

# Include paths: project root (for config.h) and the include/ tree
# (pa_ringbuffer/pa_memorybarrier pulled from the device).
_incDirs = [
  include_directories('..'),
  include_directories('../include'),
  include_directories('../include/audio'),
  include_directories('../include/audio/alsa'),
]

# ── Dependencies ───────────────────────────────────────────────────────────────
#
# Only libraries present on the stock Xiaodu rootfs (see README "What Stock
# Means"); everything else is vendored. libstdc++ is linked statically.

_deps = []

# glib-2.0: main loop, signals (glib-unix), logging, key files
_deps += dependency('glib-2.0')

# ALSA: audio capture/playback via hw:* devices
_deps += dependency('alsa')

# speexdsp: acoustic echo cancellation and audio preprocessing
_deps += dependency('speexdsp')

# POSIX threads: std::thread in AudioInput and AudioPlayer
_deps += dependency('threads')

# ── Source files ───────────────────────────────────────────────────────────────

_sources = [
  'main.cpp',
  'app.cpp',
  'config.cpp',
  'leds.cpp',
  'evinput.cpp',

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
]

# glibc 2.23 shims use ARM syscall numbers; only for the armhf cross build
if host_machine.cpu_family() == 'arm'
  _sources += 'glibc_compat.c'
endif

executable(
  app_command,
  _sources,
  cpp_args : ['-DG_LOG_USE_STRUCTURED=1'],
  include_directories : _incDirs,
  dependencies : _deps,
  install : true,
)
