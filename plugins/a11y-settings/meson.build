desktop = 'org.gnome.SettingsDaemon.A11ySettings.desktop'

configure_file(
  input: desktop + '.in',
  output: desktop,
  configuration: plugins_conf,
  install: true,
  install_dir: gsd_xdg_autostart
)

sources = files(
  'gsd-a11y-settings-manager.c',
  'main.c'
)

deps = plugins_deps + [
  gio_dep,
  gsettings_desktop_dep
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
