gsd_xsettings_gtk = files('gsd-xsettings-gtk.c')

fc_monitor = files('fc-monitor.c')

wm_button_layout_translation = files('wm-button-layout-translation.c')

sources = gsd_xsettings_gtk + fc_monitor + wm_button_layout_translation + files(
  'gsd-remote-display-manager.c',
  'gsd-xsettings-manager.c',
  'xsettings-common.c',
  'xsettings-manager.c',
  'main.c'
)

deps = plugins_deps + [
  gtk_dep,
  x11_dep,
  dependency('fontconfig')
]

cflags += ['-DGTK_MODULES_DIRECTORY="@0@"'.format(join_paths(gsd_pkglibdir, 'gtk-modules'))]

executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: [top_inc, common_inc, data_inc],
  dependencies: deps,
  c_args: cflags,
  install: true,
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
