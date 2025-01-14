sources = files(
  'gsd-sound-manager.c',
  'main.c'
)
sources += main_helper_sources

deps = plugins_deps + [
  gio_dep,
  libpulse_mainloop_glib_dep
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
