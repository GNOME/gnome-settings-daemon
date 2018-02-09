sources = files(
  'bus-watch-namespace.c',
  'gsd-media-keys-manager.c',
  'gsd-screenshot-utils.c',
  'main.c',
  'mpris-controller.c'
)

marshal = 'gsd-marshal'

sources += gnome.genmarshal(
  marshal,
  sources: marshal + '.list',
  prefix: marshal.underscorify(),
  internal: true
)

sources += gnome.gdbus_codegen(
  'shell-key-grabber',
  'org.gnome.ShellKeyGrabber.xml',
  interface_prefix: 'org.gnome.',
  namespace: 'Shell'
)

deps = plugins_deps + [
  gsettings_desktop_dep,
  libcanberra_gtk_dep,
  libcommon_dep,
  libgvc_dep,
  libpulse_mainloop_glib_dep,
  m_dep,
  upower_glib_dep
]

if enable_gudev
  deps += gudev_dep
endif

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

program = 'audio-selection-test'

executable(
  program,
  program + '.c',
  include_directories: top_inc,
  dependencies: deps
)
