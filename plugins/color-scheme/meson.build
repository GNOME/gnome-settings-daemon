sources = files(
    'gsd-color-scheme-manager.c',
    'gsd-color-scheme.c',
    'main.c'
)
sources += main_helper_sources

deps = plugins_deps + [
  gnome_desktop_dep,
  libgeoclue_dep,
  libcommon_dep
]

cflags += ['-DBINDIR="@0@"'.format(gsd_bindir)]
cflags += ['-DUSE_GTK=1']

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
  'gsd-color-scheme-test.c',
  'gsd-color-scheme.c'
)

test_unit = 'gcm-color-scheme-test'

exe = executable(
  test_unit,
  sources,
  include_directories: top_inc,
  dependencies: deps,
  c_args: '-DTESTDATADIR="@0@"'.format(join_paths(meson.current_source_dir(), 'test-data'))
)

envs = ['GSETTINGS_SCHEMA_DIR=@0@'.format(join_paths(meson.project_build_root(), 'data'))]
test(test_unit, exe, env: envs)