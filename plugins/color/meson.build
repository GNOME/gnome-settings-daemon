sources = files(
  'gnome-datetime-source.c',
  'gsd-color-calibrate.c',
  'gsd-color-manager.c',
  'gsd-color-state.c',
  'gsd-night-light.c',
  'gsd-night-light-common.c',
  'main.c'
)
sources += main_helper_sources

deps = plugins_deps + [
  colord_dep,
  libgeoclue_dep,
  libnotify_dep,
  libcommon_dep,
  m_dep,
]

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

sources = files(
  'gcm-self-test.c',
  'gnome-datetime-source.c',
  'gsd-night-light.c',
  'gsd-night-light-common.c'
)

test_unit = 'gcm-self-test'

exe = executable(
  test_unit,
  sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: '-DTESTDATADIR="@0@"'.format(join_paths(meson.current_source_dir(), 'test-data'))
)

envs = ['GSETTINGS_SCHEMA_DIR=@0@'.format(join_paths(meson.project_build_root(), 'data'))]
test(test_unit, exe, env: envs)
