desktop = 'org.gnome.SettingsDaemon.Keyboard.desktop'

configure_file(
  input: desktop + '.in',
  output: desktop,
  configuration: plugins_conf,
  install: true,
  install_dir: gsd_xdg_autostart
)

sources = files(
  'gsd-keyboard-manager.c',
  'main.c'
)

deps = plugins_deps + [
  gtk_dep,
  libcommon_dep,
  x11_dep
]

executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: [top_inc, data_inc],
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_dir: gsd_libexecdir
)
