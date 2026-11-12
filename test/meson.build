# Wake word engine over WAV files (Phase 5 verification); not built by
# default: ninja -C builddir test/wakeword_wavs
_test_sources = [
  'wakeword_wavs.cpp',
  '../src/audio/wakeword.cpp',
  '../src/config.cpp',
  '../src/audio/dr_libs.c',
]
if host_machine.cpu_family() == 'arm'
  _test_sources += '../src/glibc_compat.c'
endif

executable('wakeword_wavs',
  _test_sources,
  include_directories : [
    include_directories('..'),
    include_directories('../include'),
    include_directories('../include/audio'),
    include_directories('../third_party/dr_libs'),
  ],
  dependencies : [dependency('glib-2.0'), tflm_dep, micro_speech_features_dep],
  build_by_default : false,
)
