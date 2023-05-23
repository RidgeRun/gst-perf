# Define source code
gst_plugin_sources = [
  'gstperf.c',
]

gst_plugin_headers = [
  'gstperf.h',
]

# Build plugin library
gst_plugin = library('gstperf',
  gst_plugin_sources,
  c_args : [c_args],
  include_directories : [configinc],
  install : true,
  install_dir : plugins_install_dir,
  dependencies : [plugin_deps]
)

pkgconfig.generate(gst_plugin, install_dir : plugins_pkgconfig_install_dir)
