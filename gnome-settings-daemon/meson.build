sources = files(
  'gnome-settings-bus.c',
  'gnome-settings-profile.c'
)

dbus_ifaces = [
  ['SessionManager', 'gsd-session-manager-glue'],
  ['ScreenSaver', 'gsd-screen-saver-glue'],
  ['Shell', 'gsd-shell-glue']
]

dbus_headers = []

codegen = find_program('codegen.py')

foreach iface: dbus_ifaces
  name = 'org.gnome.' + iface[0]

  # FIXME: Opaque target return from gdbus_codegen
  # Please see:
  #   https://bugzilla.gnome.org/show_bug.cgi?id=791015
  #   https://github.com/mesonbuild/meson/pull/2930
  '''
  dbus_sources += gnome.gdbus_codegen(
    iface[1],
    name + '.xml',
    interface_prefix: name + '.',
    namespace: 'Gsd',
    annotations: [name, 'org.gtk.GDBus.C.Name', iface[0]]
  )
  '''

  # FIXME: Ugly workaround that simulates the generation of
  #        two different targets.
  dbus_sources = custom_target(
    iface[1],
    input: name + '.xml',
    output: [iface[1] + '.h', iface[1] + '.c'],
    command: [codegen, iface[0], iface[1], meson.current_build_dir(), '@INPUT@', '@OUTPUT@']
  )

  dbus_headers += dbus_sources[0]
  sources += dbus_sources[1]
endforeach

deps = [gio_unix_dep]

if enable_wayland
  deps += wayland_client_dep
endif

libgsd = shared_library(
  'gsd',
  sources: sources + dbus_headers,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
  install_dir: gsd_pkglibdir
)

libgsd_dep = declare_dependency(
  sources: dbus_headers,
  include_directories: include_directories('.'),
  link_with: libgsd
)
