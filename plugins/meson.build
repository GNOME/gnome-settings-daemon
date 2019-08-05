enabled_plugins = [
  ['a11y-settings', 'A11ySettings', ''],
  ['color', 'Color', ''],
  ['datetime', 'Datetime'],
  ['dummy', ''],
  ['power', 'Power'],
  ['housekeeping', 'Housekeeping'],
  ['keyboard', 'Keyboard'],
  ['media-keys', 'MediaKeys'],
  ['screensaver-proxy', 'ScreensaverProxy'],
  ['sharing', 'Sharing'],
  ['sound', 'Sound'],
  ['xsettings', 'XSettings']
]

if enable_smartcard
  enabled_plugins += [['smartcard', 'Smartcard']]
endif

if enable_wacom
  enabled_plugins += [['wacom', 'Wacom', 'wacom.target.wants/']]
endif

if enable_cups
  enabled_plugins += [['print-notifications', 'PrintNotifications']]
endif

if enable_rfkill
  enabled_plugins += [['rfkill', 'Rfkill']]
endif

if enable_wwan
  enabled_plugins += [['wwan', 'Wwan']]
endif

plugin_install_extra_wants = {
#  'dummy': ['required-started.target.wants/'],
#  'xsettings': ['gnome-session-x11.target.wants/'],
#  'wacom': ['wacom.target.wants/'],
#  'smartcard': ['smartcard.target.wants/'],
}

plugins_conf = configuration_data()
plugins_conf.set('libexecdir', gsd_libexecdir)
plugins_conf.set('systemd_hidden', enable_systemd ? 'true' : 'false')

plugins_deps = [libgsd_dep]

plugins_cflags = ['-DGNOME_SETTINGS_LOCALEDIR="@0@"'.format(gsd_localedir)]

enabled_plugins_file = []

foreach plugin: [['common', '']] + enabled_plugins
  plugin_name = plugin[0]
  desktop = 'org.gnome.SettingsDaemon.@0@.desktop'.format(plugin[1])

  if plugin[1] == '' # common or dummy
    desktop_in_file = ''
  else
    desktop_in_file = files(join_paths(plugin_name, desktop + '.in'))
  endif

  enabled_plugins_file += [[plugin_name, plugin[1], desktop_in_file]]
endforeach

foreach plugin: enabled_plugins_file
  plugin_name = plugin[0]
  plugin_name_case = plugin[1]
  plugin_dbus_name='org.gnome.SettingsDaemon.@0@'.format(plugin_name_case)

  cflags = [
    '-DG_LOG_DOMAIN="@0@-plugin"'.format(plugin_name),
    '-DPLUGIN_NAME="@0@"'.format(plugin_name),
    '-DPLUGIN_DBUS_NAME="@0@"'.format(plugin_dbus_name),
  ] + plugins_cflags

  if not ['common', 'dummy'].contains(plugin_name)
    desktop = 'org.gnome.SettingsDaemon.@0@.desktop'.format(plugin[1])
    desktop_in_file = plugin[2]
    user_target = 'gsd-@0@.target'.format(plugin_name)
    user_service = 'gsd-@0@.service'.format(plugin_name)

    unit_conf = configuration_data()
    unit_conf.set('libexecdir', gsd_libexecdir)
    unit_conf.set('plugin_dbus_name', plugin_dbus_name)

    configure_file(
      input: desktop_in_file,
      output: desktop,
      configuration: plugins_conf,
      install: true,
      install_dir: gsd_xdg_autostart
    )
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

     wants = ['gnome-session-initialized.target.wants/']
     if plugin_name in plugin_install_extra_wants
       wants += [plugin_install_extra_wants[plugin_name]]
     endif

     foreach target: wants
       meson.add_install_script('meson-add-wants.sh', systemd_userunitdir, target, user_target)
     endforeach
   endif
  endif

  subdir(plugin_name)
endforeach
