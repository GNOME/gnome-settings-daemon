sources = files(
  'gpm-common.c',
  'gsd-power-manager.c',
  'main.c'
)
sources += main_helper_sources

deps = plugins_deps + [
  libcommon_dep,
  libnotify_dep,
  gio_unix_dep,
  gnome_desktop_dep,
  gsd_enums_dep,
  m_dep,
  upower_glib_dep
]

# required for gsd-power-enums-update, which we have
# to compile natively
native_deps = [
  dependency('glib-2.0', native: true),
  dependency('gio-2.0', native:true)
]

if host_is_linux
  deps += gudev_dep
endif

cflags += ['-DLIBEXECDIR="@0@"'.format(gsd_libexecdir)]

gsd_power = executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: top_inc,
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
  dependencies: native_deps,
  c_args: cflags,
  native: true
)

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
fs.copyfile('dbusmock-templates/mutter.py')

envs = environment()
#envs.prepend('G_DEBUG', 'fatal-warnings')
envs.set('BUILDDIR', meson.current_build_dir())
envs.set('TOP_BUILDDIR', meson.project_build_root())
envs.set('NO_AT_BRIDGE', '1')

tests = [
  'Screensaver',
  'ScreensaverNoblank',
  'Battery',
  'Lid',
  'Dim',
  'BatteryLevels',
  'Brightness',
  'BrightnessStep',
]

foreach test : tests
  test(
    'test-power @0@'.format(test),
    test_py,
    args: [ 'PowerPluginTest@0@'.format(test) ],
    env: envs,
    # The first set of tests takes very long because of SCREENSAVER_TIMEOUT_BLANK
    timeout: test == 'Screensaver' ? 180 : 120
  )
endforeach

