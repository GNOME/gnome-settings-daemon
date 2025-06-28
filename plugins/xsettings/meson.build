gsd_xsettings_gtk = files('gsd-xsettings-gtk.c')

fc_monitor = files('fc-monitor.c')

wm_button_layout_translation = files('wm-button-layout-translation.c')

sources = gsd_xsettings_gtk + fc_monitor + wm_button_layout_translation + files(
  'gsd-xsettings-manager.c',
  'xsettings-common.c',
  'xsettings-manager.c',
  'main.c'
)

sources += main_helper_sources

deps = plugins_deps + [
  gsd_enums_dep,
  gio_dep,
  x11_dep,
  xfixes_dep,
  libcommon_dep,
  gsettings_desktop_dep,
  dependency('fontconfig')
]

cflags += ['-DGTK_MODULES_DIRECTORY="@0@"'.format(gsd_gtk_modules_directory)]

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

programs = [
  ['test-gtk-modules', gsd_xsettings_gtk + ['test-gtk-modules.c'], cflags],
  ['test-fontconfig-monitor', fc_monitor, cflags + ['-DFONTCONFIG_MONITOR_TEST']],
  ['test-wm-button-layout-translations', wm_button_layout_translation + ['test-wm-button-layout-translations.c'], []]
]

foreach program: programs
  executable(
    program[0],
    program[1],
    include_directories: top_inc,
    dependencies: deps,
    c_args: program[2]
  )
endforeach

install_data(
  files('00-xrdb'),
  install_dir: join_paths(gsd_sysconfdir, 'xdg/Xwayland-session.d')
)

test_py = find_program('test.py')

envs = [
  'BUILDDIR=' + meson.current_build_dir(),
  'TOP_BUILDDIR=' + meson.project_build_root()
]

test(
  'test-xsettings',
  test_py,
  env: envs,
  timeout: 300
)
