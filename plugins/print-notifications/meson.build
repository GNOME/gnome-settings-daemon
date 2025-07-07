sources = files(
  'gsd-print-notifications-manager.c',
  'main.c'
)
sources += main_helper_sources

deps = plugins_deps + [
  cups_dep,
  libnotify_dep
]

cflags += ['-DLIBEXECDIR="@0@"'.format(gsd_libexecdir)]
cflags += ['-DBINDIR="@0@"'.format(gsd_bindir)]

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

program = 'gsd-printer'

executable(
  program,
  program + '.c',
  include_directories: top_inc,
  dependencies: deps,
  c_args: '-DGNOME_SETTINGS_LOCALEDIR="@0@"'.format(gsd_localedir),
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)
