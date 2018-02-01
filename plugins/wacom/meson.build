desktop = 'org.gnome.SettingsDaemon.Wacom.desktop'

configure_file(
  input: desktop + '.in',
  output: desktop,
  configuration: plugins_conf,
  install: true,
  install_dir: gsd_xdg_autostart
)

policy = 'org.gnome.settings-daemon.plugins.wacom.policy'

policy_in = configure_file(
  input: policy + '.in.in',
  output: policy + '.in',
  configuration: plugins_conf
)

custom_target(
  policy,
  input: policy_in,
  output: policy,
  command: intltool_xml_cmd,
  install: true,
  install_dir: join_paths(gsd_datadir, 'polkit-1', 'actions')
)

sources = files(
  'gsd-wacom-manager.c',
  'gsd-wacom-oled.c',
  'main.c'
)

deps = plugins_deps + [
  gtk_dep,
  libcommon_dep,
  m_dep,
  pango_dep
]

cflags += ['-DLIBEXECDIR="@0@"'.format(gsd_libexecdir)]

executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: [top_inc, data_inc],
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_dir: gsd_libexecdir
)

if enable_gudev
  deps = [
    gudev_dep,
    m_dep
  ]

  programs = [
    'gsd-wacom-led-helper',
    'gsd-wacom-oled-helper',
  ]

  foreach program: programs
    executable(
      program,
      program + '.c',
      include_directories: top_inc,
      dependencies: deps,
      install: true,
      install_dir: gsd_libexecdir
    )
  endforeach
endif
