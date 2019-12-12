all_plugins = [
  ['a11y-settings', 'A11ySettings'],
  ['color', 'Color'],
  ['datetime', 'Datetime'],
  ['power', 'Power'],
  ['housekeeping', 'Housekeeping'],
  ['keyboard', 'Keyboard'],
  ['media-keys', 'MediaKeys'],
  ['screensaver-proxy', 'ScreensaverProxy'],
  ['sharing', 'Sharing'],
  ['sound', 'Sound'],
  ['usb-protection', 'UsbProtection'],
  ['xsettings', 'XSettings'],
  ['smartcard', 'Smartcard'],
  ['wacom', 'Wacom'],
  ['print-notifications', 'PrintNotifications'],
  ['rfkill', 'Rfkill'],
  ['wwan', 'Wwan'],
]

disabled_plugins = []

if not enable_smartcard
  disabled_plugins += ['smartcard']
endif

if not enable_wacom
  disabled_plugins += ['wacom']
endif

if not enable_cups
  disabled_plugins += ['cups']
endif

if not enable_rfkill
  disabled_plugins += ['rfkill']
endif

if not enable_wwan
  disabled_plugins += ['wwan']
endif

plugin_install_wants = {
  'xsettings': ['gnome-session-x11-services.target.wants/'],
#  'dummy': ['required-started.target.wants/'],
#  'wacom': ['wacom.target.wants/'],
#  'smartcard': ['smartcard.target.wants/'],
}

plugins_conf = configuration_data()
plugins_conf.set('libexecdir', gsd_libexecdir)

plugins_deps = [libgsd_dep]

plugins_cflags = ['-DGNOME_SETTINGS_LOCALEDIR="@0@"'.format(gsd_localedir)]

all_plugins_file = []

cflags = [
  '-DG_LOG_DOMAIN="common"'
] + plugins_cflags
plugin_name = 'common'
subdir('common')

foreach plugin: all_plugins
  plugin_name = plugin[0]
  plugin_name_case = plugin[1]
  plugin_dbus_name='org.gnome.SettingsDaemon.@0@'.format(plugin_name_case)

  desktop = 'org.gnome.SettingsDaemon.@0@.desktop'.format(plugin[1])

  if disabled_plugins.contains(plugin_name)
    desktop_in_file = files(join_paths('dummy', 'org.gnome.SettingsDaemon.Dummy.desktop.in'))
  else
    desktop_in_file = files(join_paths(plugin_name, desktop + '.in'))
  endif

  cflags = [
    '-DG_LOG_DOMAIN="@0@-plugin"'.format(plugin_name),
    '-DPLUGIN_NAME="@0@"'.format(plugin_name),
    '-DPLUGIN_DBUS_NAME="@0@"'.format(plugin_dbus_name),
  ] + plugins_cflags

  desktop = 'org.gnome.SettingsDaemon.@0@.desktop'.format(plugin[1])
  desktop_conf = configuration_data()
  desktop_conf.set('libexecdir', gsd_libexecdir)
  desktop_conf.set('systemd_hidden', enable_systemd ? 'true' : 'false')
  desktop_conf.set('pluginname', plugin_name)
  configure_file(
    input: desktop_in_file,
    output: desktop,
    configuration: desktop_conf,
    install: true,
    install_dir: gsd_xdg_autostart
  )

  if not disabled_plugins.contains(plugin_name)
    user_target = 'gsd-@0@.target'.format(plugin_name)
    user_service = 'gsd-@0@.service'.format(plugin_name)

    unit_conf = configuration_data()
    unit_conf.set('libexecdir', gsd_libexecdir)
    unit_conf.set('plugin_dbus_name', plugin_dbus_name)

    if enable_systemd
      configure_file(
        input: join_paths(plugin_name, user_service + '.in'),
        output: user_service,
        configuration: unit_conf,
        install: true,
        install_dir: systemd_userunitdir
     )
      configure_file(
        input: join_paths(plugin_name, user_target + '.in'),
        output: user_target,
        configuration: unit_conf,
        install: true,
        install_dir: systemd_userunitdir
     )

     if plugin_name in plugin_install_wants
       foreach target: plugin_install_wants[plugin_name]
         meson.add_install_script('meson-add-wants.sh', systemd_userunitdir, target, user_target)
       endforeach
     endif
   endif

    subdir(plugin_name)
  endif
endforeach
