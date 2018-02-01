desktop = 'org.gnome.SettingsDaemon.Clipboard.desktop'

configure_file(
  input: desktop + '.in',
  output: desktop,
  configuration: plugins_conf,
  install: true,
  install_dir: gsd_xdg_autostart
)

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
  install_dir: gsd_libexecdir
)
