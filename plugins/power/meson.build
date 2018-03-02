sources = files(
  'gpm-common.c',
  'gsd-backlight-linux.c',
  'gsd-power-manager.c',
  'main.c'
)

deps = plugins_deps + [
  libcanberra_gtk_dep,
  libcommon_dep,
  libnotify_dep,
  gnome_desktop_dep,
  m_dep,
  upower_glib_dep,
  x11_dep,
  xtst_dep,
  dependency('xext')
]

if enable_gudev
  deps += gudev_dep
endif

cflags += ['-DLIBEXECDIR="@0@"'.format(gsd_libexecdir)]

gsd_power = executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: [top_inc, data_inc],
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)

sources = files('gsd-power-enums-update.c')

enums_headers = files(
  'gsm-inhibitor-flag.h',
  'gsm-presence-flag.h'
)

enums = 'gsd-power-enums'

sources += gnome.mkenums(
  enums,
  sources: enums_headers,
  c_template: enums + '.c.in',
  h_template: enums + '.h.in'
)

gsd_power_enums_update = executable(
  'gsd-power-enums-update',
  sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: cflags
)

if enable_gudev
  policy = 'org.gnome.settings-daemon.plugins.power.policy'

  policy_in = configure_file(
    input: policy + '.in.in',
    output: policy + '.in',
    configuration: plugins_conf
  )

  i18n.merge_file(
    policy,
    input: policy_in,
    output: policy,
    po_dir: po_dir,
    install: true,
    install_dir: join_paths(gsd_datadir, 'polkit-1', 'actions')
  )

  sources = files(
    'gsd-backlight-helper.c',
    'gsd-backlight-linux.c'
  )

  deps = [
    gudev_dep,
    m_dep
  ]

  executable(
    'gsd-backlight-helper',
    sources,
    include_directories: top_inc,
    dependencies: deps,
    install: true,
    install_rpath: gsd_pkglibdir,
    install_dir: gsd_libexecdir
  )
endif

output = 'gsdpowerconstants.py'

gsdpowerconstants_py = custom_target(
  output,
  input: 'gsd-power-constants.h',
  output: output,
  build_by_default: true,
  command: [join_paths(meson.current_source_dir(), 'gsd-power-constants-update.pl'), '@INPUT@', '@OUTPUT@']
)

output = 'gsdpowerenums.py'

gsdpowerenums_py = custom_target(
  output,
  output: output,
  capture: true,
  build_by_default: true,
  command: [gsd_power_enums_update]
)

test_py = find_program('test.py')

envs = [
#  'G_DEBUG=fatal_warnings',
  'BUILDDIR=' + meson.current_build_dir(),
  'TOP_BUILDDIR=' + meson.build_root()
]

test(
  'test-power',
  test_py,
#  args: 'PowerPluginTest.test_sleep_inactive_blank',
  env: envs,
  timeout: 300
)

