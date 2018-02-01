sources = files(
  'gnome-settings-bus.c',
  'gnome-settings-profile.c'
)

dbus_ifaces = [
  ['SessionManager', 'gsd-session-manager-glue'],
  ['ScreenSaver', 'gsd-screen-saver-glue'],
  ['Shell', 'gsd-shell-glue']
]

foreach iface: dbus_ifaces
  name = 'org.gnome.' + iface[0]
  sources += gnome.gdbus_codegen(
    iface[1],
    name + '.xml',
    interface_prefix: name + '.',
    namespace: 'Gsd',
    annotations: [name, 'org.gtk.GDBus.C.Name', iface[0]]
  )
endforeach

deps = [gio_unix_dep]

if enable_wayland
  deps += wayland_client_dep
endif

libgsd = shared_library(
  'gsd',
  sources: sources,
  include_directories: top_inc,
  dependencies: deps,
  install: true,
  install_dir: gsd_pkglibdir
)

libgsd_dep = declare_dependency(
  include_directories: include_directories('.'),
  link_with: libgsd
)
