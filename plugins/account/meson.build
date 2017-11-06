sources = files(
  'gsd-account-manager.c',
  'main.c'
)

sources += gnome.gdbus_codegen(
  'org.freedesktop.Accounts',
  'org.freedesktop.Accounts.xml',
  interface_prefix: 'org.freedesktop.',
  namespace: 'Gsd',
  object_manager: false,
  annotations: ['org.freedesktop.Accounts', 'org.gtk.GDBus.C.Name', 'Accounts']
)

sources += gnome.gdbus_codegen(
  'org.freedesktop.Accounts.User',
  'org.freedesktop.Accounts.User.xml',
  interface_prefix: 'org.freedesktop.',
  namespace: 'Gsd',
  object_manager: false,
  annotations: ['org.freedesktop.Accounts.User', 'org.gtk.GDBus.C.Name', 'AccountsUser']
)

deps = plugins_deps + [
  gio_unix_dep,
  libnotify_dep,
  nss_dep
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
