sources = files(
  'gsd-subscription-manager.c',
  'gsd-subman-common.c',
  'main.c'
)

deps = plugins_deps + [
  libnotify_dep,
  gtk_dep,
  jsonglib_dep,
  m_dep,
]

cflags += ['-DBINDIR="@0@"'.format(gsd_bindir)]
cflags += ['-DLIBEXECDIR="@0@"'.format(gsd_libexecdir)]

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

# .Register needs to be called from root as subman can't do PolicyKit...
policy = 'org.gnome.settings-daemon.plugins.subman.policy'
policy_in = configure_file(
  input: policy + '.in.in',
  output: policy + '.in',
  configuration: plugins_conf
)

i18n.merge_file(
  policy,
  input: policy_in,
  output: policy,
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(gsd_datadir, 'polkit-1', 'actions')
)

install_data('org.gnome.settings-daemon.plugins.subman.rules',
             install_dir : join_paths(gsd_datadir, 'polkit-1', 'rules.d'))

executable(
  'gsd-subman-helper',
  'gsd-subman-helper.c',
  include_directories: top_inc,
  dependencies: [gio_dep, gio_unix_dep, jsonglib_dep],
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)
