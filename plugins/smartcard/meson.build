sources = files(
  'gsd-smartcard-manager.c',
  'gsd-smartcard-service.c',
  'gsd-smartcard-utils.c',
  'main.c'
)

sources += main_helper_sources

enum_headers = files(
  'gsd-smartcard-manager.h',
  'gsd-smartcard-utils.h'
)

enum_types = 'gsd-smartcard-enum-types'

sources += gnome.mkenums(
  enum_types,
  sources: enum_headers,
  c_template: enum_types + '.c.in',
  h_template: enum_types + '.h.in'
)

gdbus = 'org.gnome.SettingsDaemon.Smartcard'

sources += gnome.gdbus_codegen(
  gdbus,
  gdbus + '.xml',
  interface_prefix: gdbus + '.',
  namespace: 'GsdSmartcardService',
  object_manager: true
)

deps = plugins_deps + [
  gio_unix_dep,
  libnotify_dep,
  smartcard_deps
]

cflags += ['-DGCK_API_SUBJECT_TO_CHANGE=1']

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
