sources = files(
  'gsd-usbprotection-manager.c',
  'main.c'
)

#sources += gnome.gdbus_codegen(
#  'timedated',
#  'timedated1-interface.xml',
#  interface_prefix: 'org.freedesktop.'
#)

deps = plugins_deps + [
  gtk_dep,
  libcommon_dep,
  x11_dep

#  gtk_x11_dep,
#  x11_dep
]

executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: [top_inc, common_inc],
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)
