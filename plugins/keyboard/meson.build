sources = files(
  'gsd-keyboard-manager.c',
  'main.c'
)

deps = plugins_deps + [
  gsd_enums_dep,
  gtk_dep,
  libcommon_dep,
  x11_dep
]

executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)
