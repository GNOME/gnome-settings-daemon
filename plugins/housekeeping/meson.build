common_files = files(
  'gsd-disk-space.c',
  'gsd-disk-space-helper.c'
)

sources = common_files + files(
  'gsd-donation-reminder.c',
  'gsd-housekeeping-manager.c',
  'gsd-systemd-notify.c',
  'main.c'
)
sources += main_helper_sources

deps = plugins_deps + [
  gio_unix_dep,
  libnotify_dep
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

programs = [
  'gsd-disk-space-test',
  'gsd-empty-trash-test',
  'gsd-purge-temp-test'
]

foreach program: programs
  executable(
    program,
    common_files + [program + '.c'],
    include_directories: top_inc,
    dependencies: deps
  )
endforeach
