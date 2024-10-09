cpp = meson.get_compiler('cpp')

# base deps
_baseDeps = [ 'glib-2.0', 'gobject-2.0' ]

# shared only deps
_onlySharedDeps = [ ]

# always use as shared deps
_alwaysSharedDeps = [ 'gio-2.0' ]

_linkArgs = []
_incDirs = []
_deps = []

_deps += cpp.find_library('dl')
foreach d : _baseDeps
  _deps += dependency(d)
endforeach

foreach d : _alwaysSharedDeps
  _deps += dependency(d)
endforeach

if get_option('static') == true
  # static only deps
  _onlyStaticDeps = [  ]
  #'libmount', 'blkid', 'uuid', 'z', 'semanage', 'selinux', 'gmodule-2.0', 'pthread', 'pcre' ]
  _onlyStaticUseShared = [  ]
  #'resolv', 'ogg', 'vorbis', 'libpulse', 'mpg123', 'ffi' ]
  _gstStaticDeps = [  ]
  # 'gstreamer-full-1.0', 'gstbase-1.0', 'gstriff-1.0', 'gstaudio-1.0', 'gsttag-1.0' ]
  _gstStaticPlugins = [  ]
  #  'gstcoreelements', 'gstwavparse', 'gstpbutils-1.0', 'gstvideo-1.0', 'gstalsa', 'gstautodetect', 'gstplayback', 'gsttypefindfunctions', 'gstmpg123', 'gstsoup', 'gstpulseaudio', 'gstogg', 'gstvolume' ]

  foreach d : _onlyStaticDeps
    _deps += dependency(d, static : true)
  endforeach
  foreach d : _onlyStaticUseShared
    _deps += dependency(d, static : false)
  endforeach
  foreach d : _gstStaticDeps
    _deps += dependency(d, static : true)
  endforeach
  foreach d : _gstStaticPlugins
    _deps += dependency(d, static : true)
  endforeach

  #_linkArgs = [ '-static' ]
else
  foreach d : _onlySharedDeps
    _deps += dependency(d)
  endforeach
endif

executable(
  app_command,
  'main.cpp',
  'app.cpp',
  'config.cpp',
  'leds.cpp',
  link_args : _linkArgs,
  cpp_args : ['-DG_LOG_USE_STRUCTURED=1'],
  install : true,
  dependencies : _deps,
  include_directories : _incDirs,
)
