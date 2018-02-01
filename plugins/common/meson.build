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
  enums + '.h',
  sources: enums_header,
  fhead: '#ifndef GSD_COMMON_ENUMS_H\n#define GSD_COMMON_ENUMS_H\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n',
  fprod: '/* enumerations from "@filename@" */\n',
  vhead: 'GType @enum_name@_get_type (void) G_GNUC_CONST;\n#define GSD_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n',
  ftail: 'G_END_DECLS\n\n#endif /* !GSD_COMMON_ENUMS_H */'
)

sources += gnome.mkenums(
  enums + '.c',
  sources: enums_header,
  fhead: '#include "gsd-device-manager.h"\n#include "gsd-common-enums.h"\n',
  fprod: '\n/* enumerations from "@filename@" */',
  vhead: 'GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {',
  vprod: '      { @VALUENAME@, "@VALUENAME@", "@valuenick@" },',
  vtail: '      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static ("@EnumName@", values);\n  }\n  return etype;\n}\n'
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

libcommon = shared_module(
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
  install_dir: gsd_libexecdir
)
