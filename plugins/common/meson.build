common_inc = include_directories('.')

sources = files(
  'gsd-device-manager.c',
  'gsd-device-manager-x11.c',
  'gsd-device-mapper.c',
  'gsd-input-helper.c',
  'gsd-settings-migrate.c',
  'gsd-shell-helper.c'
)

enums_header = 'gsd-device-manager.h'

enums = 'gsd-common-enums'

sources += gnome.mkenums(
  enums,
  sources: enums_header,
  c_template: enums + '.c.in',
  h_template: enums + '.h.in'
)

resource_data = files('gtk.css')

sources += gnome.compile_resources(
  'gsd-resources',
  'gsd.gresources.xml',
  c_name: 'gsd',
  dependencies: resource_data
)

deps = plugins_deps + [
  gnome_desktop_dep,
  gtk_x11_dep,
  x11_dep,
  dependency('kbproto'),
  dependency('xi')
]

ldflags = []
if host_is_darwin
  ldflags += ['-Wl,-bundle_loader,@0@'.format(join_paths(), meson.build_root(), meson.project_name(), meson.project_name())]
endif

if enable_gudev
  sources += files('gsd-device-manager-udev.c')

  deps += gudev_dep
endif

if enable_wacom
  deps += libwacom_dep
endif

libcommon = static_library(
  plugin_name,
  sources: sources,
  include_directories: [top_inc, data_inc],
  dependencies: deps,
  c_args: cflags,
  link_args: ldflags
)

libcommon_dep = declare_dependency(
  include_directories: common_inc,
  link_with: libcommon
)

executable(
  'gsd-test-input-helper',
  'test-input-helper.c',
  include_directories: top_inc,
  dependencies: gtk_dep,
  link_with: libcommon,
  install: true,
  install_rpath: gsd_pkglibdir,
  install_dir: gsd_libexecdir
)
