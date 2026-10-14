project('gst-perf', 'c',
  version : '0.3.1',
  license : 'LGPL-2.0-or-later',
  meson_version : '>=0.55.0',
  default_options : ['warning_level=1'],
)

plugins_install_dir = join_paths(get_option('libdir'), 'gstreamer-1.0')

gst_dep = dependency('gstreamer-1.0', version : '>=1.0.0')
gstbase_dep = dependency('gstreamer-base-1.0', version : '>=1.0.0')

subdir('plugins')
