#!/usr/bin/env python
'''GNOME settings daemon tests for power plugin.'''

__author__ = 'Martin Pitt <martin.pitt@ubuntu.com>'
__copyright__ = '(C) 2013 Canonical Ltd.'
__license__ = 'GPL v2 or later'

import unittest
import subprocess
import sys
import time
import os
import os.path

project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
builddir = os.environ.get('BUILDDIR', os.path.dirname(__file__))

sys.path.insert(0, os.path.join(project_root, 'tests'))
import gsdtestcase

import dbus

from gi.repository import Gio


class PowerPluginTest(gsdtestcase.GSDTestCase):
    '''Test the power plugin'''

    def setUp(self):
        # start mock upowerd
        (self.upowerd, self.obj_upower) = self.spawn_server_template(
            'upower', {'OnBattery': True}, stdout=subprocess.PIPE)
        gsdtestcase.set_nonblock(self.upowerd.stdout)

        self.start_logind()

        self.settings_gsd_power = Gio.Settings('org.gnome.settings-daemon.plugins.power')

        # start power plugin
        self.settings_gsd_power['active'] = False
        Gio.Settings.sync()
        self.plugin_log = open(os.path.join(self.workdir, 'plugin_power.log'), 'wb')
        # avoid painfully long delays of actions for tests
        env = os.environ.copy()
        env['GSD_ACTION_DELAY'] = '1'
        self.daemon = subprocess.Popen(
            [os.path.join(builddir, 'gsd-test-power')],
            # comment out this line if you want to see the logs in real time
            stdout=self.plugin_log,
            stderr=subprocess.STDOUT,
            env=env)
        # give it some time to settle down
        time.sleep(1)

        # always start with zero idle time
        self.reset_idle_timer()

    def tearDown(self):
        # reactivate screen, in case tests triggered a display blank
        self.reset_idle_timer()

        self.daemon.terminate()
        self.daemon.wait()
        self.plugin_log.flush()
        self.plugin_log.close()

        self.upowerd.terminate()
        self.upowerd.wait()
        self.stop_logind()

        # reset all changed gsettings, so that tests are independent from each
        # other
        for k in self.settings_gsd_power.list_keys():
            self.settings_gsd_power.reset(k)
        Gio.Settings.sync()

    def check_for_suspend(self, timeout):
        '''Check that Suspend() or Hibernate() is requested.

        Fail after the tiven timeout.
        '''
        # check that it request suspend
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested suspend
            try:
                log = self.logind.stdout.read()
            except IOError:
                break

            if log and (b' Suspend ' in log or b' Hibernate ' in log):
                break
        else:
            self.fail('timed out waiting for logind Suspend() call')

    def test_sleep_inactive_battery_no_blank(self):
        '''sleep-inactive-battery-timeout without screen blanking'''

        self.settings_session['idle-delay'] = 2
        # disable screen blanking
        self.settings_gsd_power['sleep-display-battery'] = 0
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 5
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'

        # wait for idle delay
        time.sleep(2)

        # check that it did not suspend or hibernate yet
        log = self.logind.stdout.read()
        self.assertFalse(b' Suspend' in log, 'too early Suspend request')

        # suspend should happen after inactive sleep timeout + 1 s notification
        # delay + 1 s error margin
        self.check_for_suspend(7)

    def test_sleep_inactive_battery_with_blank(self):
        '''sleep-inactive-battery-timeout with screen blanking'''

        self.settings_session['idle-delay'] = 2
        # set blank timeout > sleep timeout, which should adjust sleep timeout
        self.settings_gsd_power['sleep-display-battery'] = 2
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 1
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'

        # wait for idle delay + display sleep time
        time.sleep(4)

        # check that it did not suspend or hibernate yet
        log = self.logind.stdout.read()
        self.assertFalse(b' Suspend' in log, 'too early Suspend request')

        # suspend should happen after timeout_blank + 12 s screen saver fade +
        # 1 s notification delay + 1 s error margin
        self.check_for_suspend(16)

    def test_action_critical_battery(self):
        '''action on critical battery'''

        # add a fake battery with 30%/2 hours charge to upower
        bat_path = self.obj_upower.AddDischargingBattery('mock_BAT', 'Mock Bat', 30.0, 1200)
        obj_bat = self.system_bus_con.get_object('org.freedesktop.UPower', bat_path)
        self.obj_upower.EmitSignal('', 'DeviceAdded', 's', [bat_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        time.sleep(1)

        # flush notification log
        try:
            self.p_notify.stdout.read()
        except IOError:
            pass

        # now change battery to critical charge
        obj_bat.Set('org.freedesktop.UPower.Device', 'TimeToEmpty',
                    dbus.Int64(30, variant_level=1),
                    dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')
        self.obj_upower.EmitSignal('', 'DeviceChanged', 's', [obj_bat.object_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        time.sleep(0.5)
        # we should have gotten a notification now
        notify_log = self.p_notify.stdout.read()

        self.check_for_suspend(5)

        # verify notification
        self.assertRegex(notify_log, b'[0-9.]+ Notify "Power" 0 "battery-.*" ".*battery critical.*"')

    def test_action_multiple_batteries(self):
        '''critical actions for multiple batteries'''

        # add two fake batteries to upower
        bat1_path = self.obj_upower.AddDischargingBattery('mock_BAT1', 'Bat0', 30.0, 1200)
        obj_bat1 = self.system_bus_con.get_object('org.freedesktop.UPower', bat1_path)
        self.obj_upower.EmitSignal('', 'DeviceAdded', 's', [bat1_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        bat2_path = self.obj_upower.AddDischargingBattery('mock_BAT2', 'Bat2', 40.0, 1600)
        obj_bat2 = self.system_bus_con.get_object('org.freedesktop.UPower', bat2_path)
        self.obj_upower.EmitSignal('', 'DeviceAdded', 's', [bat2_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # now change one battery to critical charge
        obj_bat1.Set('org.freedesktop.UPower.Device', 'TimeToEmpty',
                     dbus.Int64(30, variant_level=1),
                     dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat1.Set('org.freedesktop.UPower.Device', 'Energy',
                     dbus.Double(0.5, variant_level=1),
                     dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat1.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')
        self.obj_upower.EmitSignal('', 'DeviceChanged', 's', [bat1_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        # wait long enough to ensure it didn't do anything (as we still have
        # the second battery)
        time.sleep(5)
        # check that it did not suspend or hibernate
        log = self.logind.stdout.read()
        self.assertFalse(b' Suspend' in log, 'unexpected Suspend request')
        self.assertFalse(b' Hibernate' in log, 'unexpected Hibernate request')

        # now change the other battery to critical charge as well
        obj_bat2.Set('org.freedesktop.UPower.Device', 'TimeToEmpty',
                     dbus.Int64(25, variant_level=1),
                     dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat2.Set('org.freedesktop.UPower.Device', 'Energy',
                     dbus.Double(0.4, variant_level=1),
                     dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat2.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')
        self.obj_upower.EmitSignal('', 'DeviceChanged', 's', [bat2_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        self.check_for_suspend(5)


# avoid writing to stderr
unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
