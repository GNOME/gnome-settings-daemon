sources = files(
  'gsd-mouse-manager.c',
  'main.c'
)

deps = plugins_deps + [
  gio_dep,
  gsettings_desktop_dep,
  libcommon_dep,
  m_dep
]

cflags += ['-DLIBEXECDIR="@0@"'.format(gsd_libexecdir)]

executable(
  'gsd-' + plugin_name,
  sources,
  include_directories: [top_inc, data_inc],
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)

sources = files(
  'gsd-locate-pointer.c',
  'gsd-timeline.c'
)

deps = [
  gtk_dep,
  m_dep,
  x11_dep
]

executable(
  'gsd-locate-pointer',
  sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)
