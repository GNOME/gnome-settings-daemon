project(
  'gnome-settings-daemon', 'c',
  version: '3.28.1',
  license: [ 'GPL2+', 'LGPLv2+' ],
  meson_version: '>= 0.44.0'
)

gsd_version = meson.project_version()
version_array = gsd_version.split('.')
gsd_major_version = version_array[0].to_int()

gsd_api_version_minor = 0
gsd_api_version = '@0@.@1@'.format(gsd_major_version, gsd_api_version_minor)
gsd_api_name = '@0@-@1@'.format(meson.project_name(), gsd_api_version)

gsd_prefix = get_option('prefix')
gsd_bindir = join_paths(gsd_prefix, get_option('bindir'))
gsd_datadir = join_paths(gsd_prefix, get_option('datadir'))
gsd_includedir = join_paths(gsd_prefix, get_option('includedir'))
gsd_libdir = join_paths(gsd_prefix, get_option('libdir'))
gsd_libexecdir = join_paths(gsd_prefix, get_option('libexecdir'))
gsd_localedir = join_paths(gsd_prefix, get_option('localedir'))
gsd_sysconfdir = join_paths(gsd_prefix, get_option('sysconfdir'))

gsd_pkgdatadir = join_paths(gsd_datadir, meson.project_name())
gsd_pkgincludedir = join_paths(gsd_includedir, gsd_api_name)
gsd_pkglibdir = join_paths(gsd_libdir, gsd_api_name)

gsd_schemadir = join_paths(gsd_datadir, 'glib-2.0', 'schemas')

gsd_xdg_autostart = join_paths(gsd_sysconfdir, 'xdg', 'autostart')

gsd_buildtype = get_option('buildtype')

host_is_darwin = host_machine.system().contains('darwin')
host_is_linux = host_machine.system().contains('linux')
host_is_linux_not_s390 = host_is_linux and not host_machine.cpu().contains('s390')

cc = meson.get_compiler('c')

config_h = configuration_data()

# defines
set_defines = [
  ['PACKAGE_NAME', meson.project_name()],
  ['PACKAGE_VERSION', gsd_version],
  # i18n
  ['GETTEXT_PACKAGE', meson.project_name()]
]

foreach define: set_defines
  config_h.set_quoted(define[0], define[1])
endforeach

# compiler flags
common_flags = ['-DHAVE_CONFIG_H']
compiler_flags = []

if gsd_buildtype.contains('debug')
  common_flags += ['-DG_ENABLE_DEBUG']

  test_cflags = [
    '-Wcast-align',
    '-Wmissing-declarations',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wno-strict-aliasing',
    '-Wno-sign-compare',
    '-Wpointer-arith'
  ]

  compiler_flags = cc.get_supported_arguments(test_cflags)
elif gsd_buildtype.contains('release')
  common_flags += ['-DG_DISABLE_CAST_CHECKS']
endif

# Workaround for meson's bug
# https://github.com/mesonbuild/meson/pull/1896
if get_option('b_ndebug') == true
  common_flags += ['-DG_DISABLE_ASSERT']
endif

add_project_arguments(common_flags + compiler_flags, language: 'c')

colord_dep = dependency('colord', version: '>= 1.0.2')
geocode_glib_dep = dependency('geocode-glib-1.0', version: '>= 3.10.0')
gio_dep = dependency('gio-2.0', version: '>= 2.53.0')
gio_unix_dep = dependency('gio-unix-2.0')
gnome_desktop_dep = dependency('gnome-desktop-3.0', version: '>= 3.11.1')
gsettings_desktop_dep = dependency('gsettings-desktop-schemas', version: '>= 3.23.3')
gtk_dep = dependency('gtk+-3.0', version: '>= 3.15.3')
gtk_x11_dep = dependency('gtk+-x11-3.0')
gweather_dep = dependency('gweather-3.0', version: '>= 3.9.5')
lcms_dep = dependency('lcms2', version: '>= 2.2')
libcanberra_gtk_dep = dependency('libcanberra-gtk3')
libgeoclue_dep = dependency('libgeoclue-2.0', version: '>= 2.3.1')
libnotify_dep = dependency('libnotify', version: '>= 0.7.3')
libpulse_mainloop_glib_dep = dependency('libpulse-mainloop-glib', version: '>= 2.0')
pango_dep = dependency('pango', version: '>= 1.20.0')
polkit_gobject_dep = dependency('polkit-gobject-1', version: '>= 0.103')
upower_glib_dep = dependency('upower-glib', version: '>= 0.99.0')
x11_dep = dependency('x11')
xtst_dep = dependency('xtst')

m_dep = cc.find_library('m')

# ALSA integration (default enabled)
enable_alsa = get_option('alsa')
assert(enable_alsa or not host_is_linux, 'ALSA is not optional on Linux platforms')

libgvc = subproject(
  'gvc',
  default_options: [
    'static=true',
    'alsa=' + enable_alsa.to_string()
  ]
)
libgvc_dep = libgvc.get_variable('libgvc_dep')

# GUdev integration (default enabled)
enable_gudev = get_option('gudev')
if enable_gudev
  assert(enable_gudev, 'GUdev is not optional on Linux platforms')
  gudev_dep = dependency('gudev-1.0')
endif
config_h.set('HAVE_GUDEV', enable_gudev)

# Check for libwayland-client
enable_wayland = get_option('wayland')
if enable_wayland
  assert(enable_gudev, 'GUDev support is required for wayland support.')
  wayland_client_dep = dependency('wayland-client')
endif
config_h.set10('HAVE_WAYLAND', enable_wayland)

# wacom (disabled for s390/s390x and non Linux platforms)
enable_wacom = host_is_linux_not_s390
if enable_wacom
  assert(enable_gudev, 'GUDev support is required for wacom support.')
  libwacom_dep = dependency('libwacom', version: '>= 0.7')
endif
config_h.set10('HAVE_WACOM', enable_wacom)

# smartcard section
enable_smartcard = get_option('smartcard')
if enable_smartcard
  nss_dep = dependency('nss', version: '>= 3.11.2')

  system_nssdb_dir = get_option('nssdb_dir')
  if system_nssdb_dir == ''
    system_nssdb_dir = join_paths(gsd_sysconfdir, 'pki', 'nssdb')
  endif
endif

# CUPS
enable_cups = get_option('cups')
if enable_cups
  cups_dep = dependency('cups', version : '>= 1.4', required: false)
  assert(cups_dep.found(), 'CUPS 1.4 or newer not found')

  # FIXME: 1.6 cflags generate a lot of errors
  '''
  cups_cflags = []
  if cups_dep.version().version_compare('>= 1.6')
    cups_cflags += '-D_PPD_DEPRECATED=""'
  endif

  cups_dep = declare_dependency(
    dependencies: cups_dep,
    compile_args: cups_cflags
  )
  '''
endif

# Rfkill
enable_rfkill = get_option('rfkill')
assert(enable_rfkill or not host_is_linux, 'rfkill is not optional on Linux platforms')
if enable_rfkill
  assert(cc.has_header('linux/rfkill.h'), 'rfkill support requested but RFKill headers not found')

  udev_dir = get_option('udev_dir')
  if udev_dir == ''
    udev_dir = dependency('udev').get_pkgconfig_variable('udevdir')
  endif
endif

# Sharing plugin
enable_network_manager = get_option('network_manager')
assert(enable_network_manager or not host_is_linux, 'NetworkManager support is not optional on Linux platforms')
if enable_network_manager
  # network manager
  libnm_dep = dependency('libnm', version: '>= 1.0')
endif
config_h.set('HAVE_NETWORK_MANAGER', enable_network_manager)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

po_dir = join_paths(meson.source_root(), 'po')

top_inc = include_directories('.')

subdir('tests')
subdir('gnome-settings-daemon')
subdir('data')
subdir('plugins')
subdir('po')

configure_file(
  output: 'config.h',
  configuration: config_h
)

meson.add_install_script(
  'meson_post_install.py',
  gsd_datadir
)

output = '\n        ' + meson.project_name() + ' ' + meson.project_version() +'\n'
output += '        =============================\n\n'
output += '        prefix:                   ' + gsd_prefix + '\n'
output += '        exec_prefix:              ' + gsd_prefix + '\n'
output += '        libdir:                   ' + gsd_libdir + '\n'
output += '        libexecdir:               ' + gsd_libexecdir + '\n'
output += '        bindir:                   ' + gsd_bindir + '\n'
output += '        sysconfdir:               ' + gsd_sysconfdir + '\n'
output += '        datadir:                  ' + gsd_datadir + '\n\n'
output += '        source code location:     ' + meson.source_root() + '\n'
output += '        compiler:                 ' + cc.get_id() + '\n'
output += '        cflags:                   ' + ' '.join(compiler_flags) + '\n\n'
output += '        ALSA support:             ' + enable_alsa.to_string() + '\n'
output += '        NetworkManager support:   ' + enable_network_manager.to_string() + '\n'
output += '        Smartcard support:        ' + enable_smartcard.to_string() + '\n'
output += '        Cups support:             ' + enable_cups.to_string() + '\n'
output += '        Wayland support:          ' + enable_wayland.to_string() + '\n'
output += '        Wacom support:            ' + enable_wacom.to_string() + '\n'
output += '        RFKill support:           ' + enable_rfkill.to_string() + '\n'
if enable_smartcard
  output += '        System nssdb:             ' + system_nssdb_dir + '\n'
endif
if enable_rfkill
  output += '        udev dir:                 ' + udev_dir + '\n'
endif
message(output)
