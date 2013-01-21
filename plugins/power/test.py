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
sys.path.insert(0, builddir)
import gsdtestcase
import gsdpowerconstants
import gsdpowerenums

import dbus

from gi.repository import Gio


class PowerPluginTest(gsdtestcase.GSDTestCase):
    '''Test the power plugin'''

    def setUp(self):
        # start mock upowerd
        (self.upowerd, self.obj_upower) = self.spawn_server_template(
            'upower', {'OnBattery': True}, stdout=subprocess.PIPE)
        gsdtestcase.set_nonblock(self.upowerd.stdout)

        # start mock gnome-shell screensaver
        (self.screensaver, self.obj_screensaver) = self.spawn_server_template(
            'gnome_screensaver', stdout=subprocess.PIPE)
        gsdtestcase.set_nonblock(self.screensaver.stdout)

        self.start_logind()

        # ensure that our tests don't lock the screen when the screensaver
        # gets active
        self.settings_screensaver = Gio.Settings('org.gnome.desktop.screensaver')
        self.settings_screensaver['lock-enabled'] = False

        self.settings_gsd_power = Gio.Settings('org.gnome.settings-daemon.plugins.power')

        # start power plugin
        self.settings_gsd_power['active'] = False
        Gio.Settings.sync()
        self.plugin_log_write = open(os.path.join(self.workdir, 'plugin_power.log'), 'wb')
        # avoid painfully long delays of actions for tests
        env = os.environ.copy()
        env['GSD_ACTION_DELAY'] = '1'
        self.daemon = subprocess.Popen(
            [os.path.join(builddir, 'gsd-test-power')],
            # comment out this line if you want to see the logs in real time
            stdout=self.plugin_log_write,
            stderr=subprocess.STDOUT,
            env=env)

        # you can use this for reading the current daemon log in tests
        self.plugin_log = open(self.plugin_log_write.name)

        # wait until plugin is ready
        timeout = 100
        while timeout > 0:
            time.sleep(0.1)
            timeout -= 1
            log = self.plugin_log.read()
            if 'System inhibitor fd is' in log:
                break

        # always start with zero idle time
        self.reset_idle_timer()

        # flush notification log
        try:
            self.p_notify.stdout.read()
        except IOError:
            pass

    def tearDown(self):
        # reactivate screen, in case tests triggered a display blank
        self.reset_idle_timer()

        self.daemon.terminate()
        self.daemon.wait()
        self.plugin_log.close()
        self.plugin_log_write.flush()
        self.plugin_log_write.close()

        self.upowerd.terminate()
        self.upowerd.wait()
        self.screensaver.terminate()
        self.screensaver.wait()
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

    def check_no_suspend(self, seconds):
        '''Check that no Suspend or Hibernate is requested in the given time'''

        # wait for specified time to ensure it didn't do anything
        time.sleep(seconds)
        # check that it did not suspend or hibernate
        log = self.logind.stdout.read()
        if log:
            self.assertFalse(b' Suspend' in log, 'unexpected Suspend request')
            self.assertFalse(b' Hibernate' in log, 'unexpected Hibernate request')

    def check_blank(self, timeout):
        '''Check that blank is requested.

        Fail after the given timeout.
        '''
        # check that it request blank
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested blank
            log = self.plugin_log.read()

            if 'TESTSUITE: Blanked screen' in log:
                break
        else:
            self.fail('timed out waiting for blank')

    def check_unblank(self, timeout):
        '''Check that unblank is requested.

        Fail after the given timeout.
        '''
        # check that it request blank
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested unblank
            log = self.plugin_log.read()

            if 'TESTSUITE: Unblanked screen' in log:
                break
        else:
            self.fail('timed out waiting for unblank')

    def check_no_blank(self, seconds):
        '''Check that no blank is requested in the given time'''

        # wait for specified time to ensure it didn't blank
        time.sleep(seconds)
        # check that it did not blank
        log = self.plugin_log.read()
        self.assertFalse('TESTSUITE: Blanked screen' in log, 'unexpected blank request')

    def test_sleep_inactive_blank(self):
        '''screensaver/blank interaction'''

        self.obj_screensaver.SetActive(True)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # blank is supposed to happen straight away
        self.check_blank(2)

        # wiggle the mouse now and check for unblank; this is expected to pop up
        # the locked screen saver
        self.reset_idle_timer()
        self.check_unblank(2)

        # Check for no blank before the normal blank timeout
        self.check_no_blank(gsdpowerconstants.SCREENSAVER_TIMEOUT_BLANK - 4)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # and check for blank after the blank timeout
        self.check_blank(10)

    def test_sleep_inactive_battery(self):
        '''sleep-inactive-battery-timeout'''

        self.settings_session['idle-delay'] = 2
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 5
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'

        # wait for idle delay; should not yet suspend
        self.check_no_suspend(2)

        # suspend should happen after inactive sleep timeout + 1 s notification
        # delay + 1 s error margin
        self.check_for_suspend(7)

    def test_sleep_inhibition(self):
        '''Does not sleep under idle inhibition'''

        self.settings_session['idle-delay'] = 2
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 5
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_IDLE | gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND))
        try:
            self.check_no_suspend(7)
        finally:
            self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id))

    def test_action_critical_battery(self):
        '''action on critical battery'''

        # add a fake battery with 30%/2 hours charge to upower
        bat_path = self.obj_upower.AddDischargingBattery('mock_BAT', 'Mock Bat', 30.0, 1200)
        obj_bat = self.system_bus_con.get_object('org.freedesktop.UPower', bat_path)
        self.obj_upower.EmitSignal('', 'DeviceAdded', 's', [bat_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        time.sleep(1)

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
        self.check_no_suspend(5)

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
