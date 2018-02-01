enabled_plugins = [
  'a11y-settings',
  'clipboard',
  'color',
  'datetime',
  'dummy',
  'power',
  'housekeeping',
  'keyboard',
  'media-keys',
  'mouse',
  'screensaver-proxy',
  'sharing',
  'sound',
  'xsettings'
]

if enable_smartcard
  enabled_plugins += ['smartcard']
endif

if enable_wacom
  enabled_plugins += ['wacom']
endif

if enable_cups
  enabled_plugins += ['print-notifications']
endif

if enable_rfkill
  enabled_plugins += ['rfkill']
endif

plugins_conf = configuration_data()
plugins_conf.set('libexecdir', gsd_libexecdir)

plugins_deps = [libgsd_dep]

plugins_cflags = ['-DGNOME_SETTINGS_LOCALEDIR="@0@"'.format(gsd_localedir)]

foreach plugin_name: ['common'] + enabled_plugins
  cflags = [
    '-DG_LOG_DOMAIN="@0@-plugin"'.format(plugin_name),
    '-DPLUGIN_NAME="@0@"'.format(plugin_name),
  ] + plugins_cflags

  subdir(plugin_name)
endforeach
