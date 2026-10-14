cdata = configuration_data()
cdata.set_quoted('VERSION', meson.project_version())
cdata.set_quoted('PACKAGE', meson.project_name())
cdata.set_quoted('PACKAGE_NAME', meson.project_name())

host_system = host_machine.system()
if host_system == 'linux'
  cdata.set('IS_LINUX', 1)
elif host_system == 'windows' or host_system == 'cygwin'
  cdata.set('IS_WINDOWS', 1)
elif host_system == 'darwin'
  cdata.set('IS_MACOSX', 1)
endif

configure_file(output : 'config.h', configuration : cdata)

shared_module('gstperf',
  'gstperf.c',
  c_args : ['-DHAVE_CONFIG_H'],
  dependencies : [gst_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
