brltty_metainfo = meson.project_source_root() / 'subprojects' / 'brltty' / 'Autostart' / 'AppStream' / 'org.a11y.brltty.metainfo.xml'
braille_devices_header = custom_target(
  'gsd-usb-protection-braille-devices.h',
  output: 'gsd-usb-protection-braille-devices.h',
  command: [
    find_program('gen-braille-usb-devices.py'),
    brltty_metainfo,
    '@OUTPUT@'
  ],
  depend_files: brltty_metainfo
)

sources = files(
  'gsd-usb-protection-manager.c',
  'main.c'
)
sources += main_helper_sources
sources += braille_devices_header

deps = plugins_deps + [
  gsd_enums_dep,
  gsettings_desktop_dep,
  libcommon_dep,
  libnotify_dep,
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
