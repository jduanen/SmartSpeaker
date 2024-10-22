project('your_project_name', 'cpp',
        version: '0.1',
        default_options: [ 'warning_level=3', 'cpp_std=c++14' ]
)

app_command = 'smartspeaker'
app_id = meson.project_name()

lib_dir = '/home/jdn/Code/ss/lib'
libGenie = compiler.find_library('libc.so.6', dirs: lib_dir)

src_files = [
  'src/main.cpp',
]

#  'src/app.cpp',
#  'src/config.cpp',
#  'src/leds.cpp',

inc_dirs = include_directories('include')

#deps = [
#  dependency('libc.so.6'),
#  dependency('lib/libffi.so.6'),
#  dependency('libgcc_s.so.1'),
#  dependency('lib/libgio-2.0.so.0'),
#  dependency('lib/libgobject-2.0.so.0'),
#  dependency('lib/libm.so.6'),
#  dependency('lib/libmount.so.1'),
#  dependency('lib/libpcre.so.3'),
#  dependency('lib/libpthread.so.0'),
#  dependency('lib/libstdc++.so.6'),
#]

executable(
  app_command,
  src_files,
  include_directories: inc_dirs,
  dependencies: [ libGenie ],
)

#
#  link_args: [ '-L' + lib_dir ],
#  link_with: [ 'libc.so.6' ]

