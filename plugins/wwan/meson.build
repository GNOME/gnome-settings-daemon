sources = files(
  'gsd-wwan-device.c',
  'gsd-wwan-manager.c',
  'gsd-wwan-pinentry.c',
  'main.c'
)

deps = plugins_deps + [gio_dep, gcr_base_dep, mm_glib_dep]

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

