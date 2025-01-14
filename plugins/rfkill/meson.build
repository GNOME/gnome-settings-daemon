install_data(
  '61-gnome-settings-daemon-rfkill.rules',
  install_dir: join_paths(udev_dir, 'rules.d')
)

sources = files(
  'gsd-rfkill-manager.c',
  'rfkill-glib.c',
  'main.c'
)
sources += main_helper_sources

deps = plugins_deps
deps += [
  gio_unix_dep,
  gudev_dep,
  m_dep
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
