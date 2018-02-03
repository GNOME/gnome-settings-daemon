enabled_plugins = [
  ['a11y-settings', 'A11ySettings'],
  ['clipboard', 'Clipboard'],
  ['color', 'Color'],
  ['datetime', 'Datetime'],
  ['dummy', ''],
  ['power', 'Power'],
  ['housekeeping', 'Housekeeping'],
  ['keyboard', 'Keyboard'],
  ['media-keys', 'MediaKeys'],
  ['mouse', 'Mouse'],
  ['screensaver-proxy', 'ScreensaverProxy'],
  ['sharing', 'Sharing'],
  ['sound', 'Sound'],
  ['xsettings', 'XSettings']
]

if enable_smartcard
  enabled_plugins += [['smartcard', 'Smartcard']]
endif

if enable_wacom
  enabled_plugins += [['wacom', 'Wacom']]
endif

if enable_cups
  enabled_plugins += [['print-notifications', 'PrintNotifications']]
endif

if enable_rfkill
  enabled_plugins += [['rfkill', 'Rfkill']]
endif

plugins_conf = configuration_data()
plugins_conf.set('libexecdir', gsd_libexecdir)

plugins_deps = [libgsd_dep]

plugins_cflags = ['-DGNOME_SETTINGS_LOCALEDIR="@0@"'.format(gsd_localedir)]

foreach plugin: [['common', '']] + enabled_plugins
  plugin_name = plugin[0]

  cflags = [
    '-DG_LOG_DOMAIN="@0@-plugin"'.format(plugin_name),
    '-DPLUGIN_NAME="@0@"'.format(plugin_name),
  ] + plugins_cflags

  if not ['common', 'dummy'].contains(plugin_name)
    desktop = 'org.gnome.SettingsDaemon.@0@.desktop'.format(plugin[1])

    configure_file(
      input: join_paths(plugin_name, desktop + '.in'),
      output: desktop,
      configuration: plugins_conf,
      install: true,
      install_dir: gsd_xdg_autostart
    )
  endif

  subdir(plugin_name)
endforeach
