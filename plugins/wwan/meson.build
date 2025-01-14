sources = files(
  'cc-wwan-device.c',
  'gsd-wwan-manager.c',
  'main.c'
)
sources += main_helper_sources

deps = plugins_deps + [gio_dep, gcr_dep, mm_glib_dep, polkit_gobject_dep]

cflags += ['-DGNOMECC_DATA_DIR="@0@"'.format(gsd_pkgdatadir)]

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

