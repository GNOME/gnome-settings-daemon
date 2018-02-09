sources = files(
  'gsd-clipboard-manager.c',
  'list.c',
  'main.c',
  'xutils.c'
)

deps = plugins_deps + [
  gtk_x11_dep,
  x11_dep
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
