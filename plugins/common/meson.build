common_inc = include_directories('.')

sources = files(
  'gsd-settings-migrate.c',
  'gsd-shell-helper.c'
)

main_helper_sources = files(
  'gsd-application.c',
  'gsd-main-helper.c',
)

ldflags = []
if host_is_darwin
  ldflags += ['-Wl,-bundle_loader,@0@'.format(join_paths(), meson.project_build_root(), meson.project_name(), meson.project_name())]
endif

libcommon = static_library(
  plugin_name,
  sources: sources,
  include_directories: top_inc,
  dependencies: [libgsd_dep, gio_dep],
  c_args: cflags,
  link_args: ldflags
)

libcommon_dep = declare_dependency(
  include_directories: common_inc,
  link_with: libcommon,
)
