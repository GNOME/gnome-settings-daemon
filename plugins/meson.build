all_plugins = [
    ['a11y-settings', 'A11ySettings', 'GNOME accessibility'],
    ['color', 'Color', 'GNOME color management'],
    ['datetime', 'Datetime', 'GNOME date & time'],
    ['power', 'Power', 'GNOME power management'],
    ['housekeeping', 'Housekeeping', 'GNOME maintenance of expirable data'],
    ['keyboard', 'Keyboard', 'GNOME keyboard configuration'],
    ['media-keys', 'MediaKeys', 'GNOME keyboard shortcuts'],
    ['screensaver-proxy', 'ScreensaverProxy', 'GNOME FreeDesktop screensaver'],
    ['sharing', 'Sharing', 'GNOME file sharing'],
    ['sound', 'Sound', 'GNOME sound sample caching'],
    ['usb-protection', 'UsbProtection', 'GNOME USB protection'],
    ['xsettings', 'XSettings', 'GNOME XSettings'],
    ['smartcard', 'Smartcard', 'GNOME smartcard'],
    ['print-notifications', 'PrintNotifications', 'GNOME printer notifications'],
    ['rfkill', 'Rfkill', 'GNOME RFKill support'],
    ['wwan', 'Wwan', 'GNOME WWan support'],
]

disabled_plugins = []

if not enable_xsettings
    disabled_plugins += ['xsettings']
endif

if not enable_smartcard
    disabled_plugins += ['smartcard']
endif

if not enable_usb_protection
    disabled_plugins += ['usb-protection']
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

if not enable_colord
  disabled_plugins += ['color']
endif

if not enable_cups
  disabled_plugins += ['print-notifications']
endif

# Specify futher required units, 'before' or 'after' may be specified if ordering is needed
plugin_gate_units = {
    'xsettings': [
      # Both after/before. after for stopping reliably, before for synchronisation
      ['gnome-session-x11-services.target', 'after'],
      ['gnome-session-x11-services-ready.target', 'before'],
    ],
#    'smartcard': [['smartcard.target']],
}

plugins_conf = configuration_data()
plugins_conf.set('libexecdir', gsd_libexecdir)

plugins_deps = [libgsd_dep, libcanberra_dep]

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
    plugin_description = plugin[2]
    plugin_dbus_name='org.gnome.SettingsDaemon.@0@'.format(plugin_name_case)

    cflags = [
        '-DG_LOG_DOMAIN="@0@-plugin"'.format(plugin_name),
        '-DPLUGIN_NAME="@0@"'.format(plugin_name),
        '-DPLUGIN_DBUS_NAME="@0@"'.format(plugin_dbus_name),
    ] + plugins_cflags

    if not disabled_plugins.contains(plugin_name)
        user_target = 'org.gnome.SettingsDaemon.@0@.target'.format(plugin_name_case)
        user_service = 'org.gnome.SettingsDaemon.@0@.service'.format(plugin_name_case)

        unit_conf = configuration_data()
        unit_conf.set('plugin_name', plugin_name)
        unit_conf.set('description', plugin_description)
        unit_conf.set('libexecdir', gsd_libexecdir)
        unit_conf.set('plugin_dbus_name', plugin_dbus_name)

        gates_all = []
        gates_after = []
        gates_before = []
        foreach gate: plugin_gate_units.get(plugin_name, [])
            gates_all += [gate[0]]
            if gate.length() > 1
                if gate[1] == 'before'
                    gates_before += [gate[0]]
                elif gate[1] == 'after'
                    gates_after += [gate[0]]
                else
                    error('Ordering key must be either "before" or "after"')
                endif
            endif
        endforeach
        gate_unit_section = []
        if gates_all.length() > 0
            gate_unit_section += ['Requisite=' + ' '.join(gates_all)]
            gate_unit_section += ['PartOf=' + ' '.join(gates_all)]

            if gates_after.length() > 0
                gate_unit_section += ['After=' + ' '.join(gates_after)]
            endif
            if gates_before.length() > 0
                gate_unit_section += ['Before=' + ' '.join(gates_before)]
            endif
        endif
        unit_conf.set('plugin_gate_units_section', '\n'.join(gate_unit_section))

        if enable_systemd
            configure_file(
                input: 'gsd.service.in',
                output: user_service,
                configuration: unit_conf,
                install_dir: systemd_userunitdir
            )
            configure_file(
                input: 'gsd.target.in',
                output: user_target,
                configuration: unit_conf,
                install_dir: systemd_userunitdir
            )

            # Wipe out old target names if our prefix differes from the
            # systemd one, i.e. we are probably in a virtual environment and
            # may be picking up old units from a system installation.
            # This saves a lot of pain when running a new g-s-d inside
            # jhbuild on an old host.
            # TODO: Should be deleted once we can assume developers have 3.38
            #       installed on their machines.
            if gsd_prefix != systemd_dep.get_variable(pkgconfig: 'prefix')
                meson.add_install_script('sh', '-c', 'ln -vfs /dev/null "${DESTDIR:-}$1"', 'sh', systemd_userunitdir / 'gsd-@0@.target'.format(plugin_name))
            endif

            foreach target: gates_all
                meson.add_install_script('meson-add-wants.sh', systemd_userunitdir, target + '.wants/', user_service)
            endforeach
        endif

        subdir(plugin_name)
    endif
endforeach
