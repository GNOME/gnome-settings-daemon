desktops = []
if not enable_smartcard
  desktops += ['org.gnome.SettingsDaemon.Smartcard']
endif

if not enable_cups
  desktops += ['org.gnome.SettingsDaemon.PrintNotifications']
endif

if not enable_rfkill
  desktops += ['org.gnome.SettingsDaemon.Rfkill']
endif

if not enable_wacom
  desktops += ['org.gnome.SettingsDaemon.Wacom']
endif

foreach desktop: desktops
  dummy_conf = configuration_data()
  dummy_conf.set('libexecdir', gsd_libexecdir)
  dummy_conf.set('pluginname', desktop)

  configure_file(
    input: 'org.gnome.SettingsDaemon.Dummy.desktop.in',
    output: desktop + '.desktop',
    configuration: dummy_conf,
    install: true,
    install_dir: gsd_xdg_autostart
  )
endforeach
