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
import signal

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
        self.daemon_death_expected = False
        self.session_log_write = open(os.path.join(self.workdir, 'gnome-session.log'), 'wb')
        self.session = subprocess.Popen(['gnome-session', '-f',
                                         '-a', os.path.join(self.workdir, 'autostart'),
                                         '--session=dummy', '--debug'],
                                        stdout=self.session_log_write,
                                        stderr=subprocess.STDOUT)

        # wait until the daemon is on the bus
        try:
            self.wait_for_bus_object('org.gnome.SessionManager',
                                     '/org/gnome/SessionManager')
        except:
            # on failure, print log
            with open(self.session_log_write.name) as f:
                print('----- session log -----\n%s\n------' % f.read())
            raise

        self.session_log = open(self.session_log_write.name)

        self.obj_session_mgr = self.session_bus_con.get_object(
            'org.gnome.SessionManager', '/org/gnome/SessionManager')

        # start mock upowerd
        (self.upowerd, self.obj_upower) = self.spawn_server_template(
            'upower', {'OnBattery': True, 'LidIsClosed': False}, stdout=subprocess.PIPE)
        gsdtestcase.set_nonblock(self.upowerd.stdout)

        # start mock gnome-shell screensaver
        (self.screensaver, self.obj_screensaver) = self.spawn_server_template(
            'gnome_screensaver', stdout=subprocess.PIPE)
        gsdtestcase.set_nonblock(self.screensaver.stdout)

        self.start_logind()

        # Set up the gnome-session presence
        obj_session_presence = self.session_bus_con.get_object(
            'org.gnome.SessionManager', '/org/gnome/SessionManager/Presence')
        self.obj_session_presence_props = dbus.Interface(obj_session_presence, dbus.PROPERTIES_IFACE)

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
        env['GSD_DISABLE_BACKLIGHT_HELPER'] = '1'
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

        daemon_running = self.daemon.poll() == None
        if daemon_running:
            self.daemon.terminate()
            self.daemon.wait()
        self.plugin_log.close()
        self.plugin_log_write.flush()
        self.plugin_log_write.close()

        self.upowerd.terminate()
        self.upowerd.wait()
        self.screensaver.terminate()
        self.screensaver.wait()
        self.stop_session()
        self.stop_logind()

        # reset all changed gsettings, so that tests are independent from each
        # other
        for schema in [self.settings_gsd_power, self.settings_session, self.settings_screensaver]:
            for k in schema.list_keys():
                schema.reset(k)
        Gio.Settings.sync()

        try:
            os.unlink('GSD_MOCK_EXTERNAL_MONITOR')
        except OSError:
            pass

        try:
            os.unlink('GSD_MOCK_brightness')
        except OSError:
            pass

        # we check this at the end so that the other cleanup always happens
        self.assertTrue(daemon_running or self.daemon_death_expected, 'daemon died during the test')

    def stop_session(self):
        '''Stop GNOME session'''

        assert self.session
        self.session.terminate()
        self.session.wait()

        self.session_log_write.flush()
        self.session_log_write.close()
        self.session_log.close()

    def get_status(self):
        return self.obj_session_presence_props.Get('org.gnome.SessionManager.Presence', 'status')

    def get_brightness(self):
        f = open('GSD_MOCK_brightness', 'r')
        ret = f.read()
        f.close()
        return int(ret)

    def set_has_external_monitor(self, external):
        f = open('GSD_MOCK_EXTERNAL_MONITOR', 'w')
        if external:
            f.write('1')
        else:
            f.write('0')
        f.close ()

        os.kill(self.daemon.pid, signal.SIGUSR2)

    def check_for_logout(self, timeout):
        '''Check that logout is requested.

        Fail after the tiven timeout.
        '''
        # check that it request suspend
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested suspend
            try:
                log = self.session_log.read()
            except IOError:
                break

            if log and (b'GsmManager: requesting logout' in log):
                break
        else:
            self.fail('timed out waiting for gnome-session logout call')

    def check_no_logout(self, seconds):
        '''Check that no logout is requested in the given time'''

        # wait for specified time to ensure it didn't do anything
        time.sleep(seconds)
        # check that it did not logout
        log = self.session_log.read()
        if log:
            self.assertFalse(b'GsmManager: requesting logout' in log, 'unexpected logout request')

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

    def check_no_dim(self, seconds):
        '''Check that mode is not set to dim in the given time'''

        # wait for specified time to ensure it didn't do anything
        time.sleep(seconds)
        # check that we don't dim
        log = self.plugin_log.read()
        if log:
            self.assertFalse(b'Doing a state transition: dim' in log, 'unexpected dim request')

    def check_dim(self, timeout):
        '''Check that mode is set to dim in the given time'''

        # wait for specified time to ensure it didn't do anything
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested dim
            log = self.plugin_log.read()

            if 'Doing a state transition: dim' in log:
                break
        else:
            self.fail('timed out waiting for dim')

    def check_undim(self, timeout):
        '''Check that mode is set to normal in the given time'''

        # wait for specified time to ensure it didn't do anything
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested normal
            log = self.plugin_log.read()

            if 'Doing a state transition: normal' in log:
                break
        else:
            self.fail('timed out waiting for normal mode')

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

        # create suspend inhibitor which should have no effect on the idle
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND))

        self.obj_screensaver.SetActive(True)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # blank is supposed to happen straight away
        self.check_blank(2)

        # wiggle the mouse now and check for unblank; this is expected to pop up
        # the locked screen saver
        self.reset_idle_timer()
        self.check_unblank(2)
        self.assertTrue(self.get_brightness() == gsdpowerconstants.GSD_MOCK_DEFAULT_BRIGHTNESS , 'incorrect unblanked brightness')

        # Check for no blank before the normal blank timeout
        self.check_no_blank(gsdpowerconstants.SCREENSAVER_TIMEOUT_BLANK - 4)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # and check for blank after the blank timeout
        self.check_blank(10)

        # Drop inhibitor
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id))

    def test_session_idle_delay(self):
        '''verify that session idle delay works as expected when changed'''

        # Verify that idle is set after 5 seconds
        self.settings_session['idle-delay'] = 5
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)
        time.sleep(7)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_IDLE)

        # Raise the idle delay, and see that we stop being idle
        # and get idle again after the timeout
        self.settings_session['idle-delay'] = 10
        self.reset_idle_timer()
        time.sleep(5)
        os.kill(self.session.pid, signal.SIGUSR2)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)
        time.sleep(10)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_IDLE)

        # Lower the delay again, and see that we get idle as we should
        self.settings_session['idle-delay'] = 5
        self.reset_idle_timer()
        time.sleep(2)
        os.kill(self.session.pid, signal.SIGUSR2)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)
        time.sleep(5)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_IDLE)

    def test_idle_time_reset_on_resume(self):
        '''Check that the IDLETIME is reset when resuming'''

        # Go idle
        self.settings_session['idle-delay'] = 5
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)
        time.sleep(7)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_IDLE)

        # Go to sleep
        self.obj_logind.EmitSignal('', 'PrepareForSleep', 'b', [True], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # Wake up
        self.obj_logind.EmitSignal('', 'PrepareForSleep', 'b', [False], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # And check we're not idle
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

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

        idle_delay = round(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)

        self.settings_session['idle-delay'] = idle_delay
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 5
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_IDLE | gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND))
        self.check_no_suspend(idle_delay + 2)
        self.check_no_dim(0)

        # Check that we didn't go to idle either
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id))

    def test_lock_on_lid_close(self):
        '''Check that we do lock on lid closing, if the machine will not suspend'''

        self.settings_screensaver['lock-enabled'] = True

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND))

        # Close the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check that we've blanked
        time.sleep(2)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')
        self.check_blank(2)

        # Drop the inhibit and see whether we suspend
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id))
        self.check_for_suspend(5)

    def test_blank_on_lid_close(self):
        '''Check that we do blank on lid closing, if the machine will not suspend'''

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND))

        # Close the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check that we've blanked
        self.check_blank(4)

        # Drop the inhibit and see whether we suspend
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id))
        self.check_for_suspend(5)

    def test_unblank_on_lid_open(self):
        '''Check that we do unblank on lid opening, if the machine will not suspend'''

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND))

        # Close the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check that we've blanked
        self.check_blank(2)

        # Reopen the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', False)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check for unblanking
        self.check_unblank(2)

        # Drop the inhibit
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id))

    def test_dim(self):
        '''Check that we do go to dim'''

        idle_delay = round(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)

        self.settings_session['idle-delay'] = idle_delay
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = idle_delay + 1
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'
        # This is an absolute percentage, and our brightness is 0..100
        dim_level = self.settings_gsd_power['idle-brightness'];

        # Check that we're not idle
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

        # Wait and check we're not idle, but dimmed
        self.check_dim(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY)
        self.assertTrue(self.get_brightness() == dim_level, 'incorrect dim brightness')

        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

        # Bring down the screensaver
        self.obj_screensaver.SetActive(True)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # Check that we blank
        self.check_blank(2)

        # Go to sleep
        self.obj_logind.EmitSignal('', 'PrepareForSleep', 'b', [True], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # Wake up
        self.obj_logind.EmitSignal('', 'PrepareForSleep', 'b', [False], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # And check that we have the pre-dim brightness
        self.assertTrue(self.get_brightness() == gsdpowerconstants.GSD_MOCK_DEFAULT_BRIGHTNESS , 'incorrect unblanked brightness')

    def test_no_suspend_lid_close(self):
        '''Check that we don't suspend on lid close with an external monitor'''

        # Add an external monitor
        self.set_has_external_monitor(True)
        time.sleep (1)

        # Close the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check for no suspend, and for no screen blanking
        self.check_no_suspend (10)
        self.check_no_blank(0)

        # Unplug the external monitor
        self.set_has_external_monitor(False)
        self.check_for_suspend (10)

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

    def test_action_critical_battery_on_start(self):
        '''action on critical battery on startup'''

        # add a fake battery with 2%/1 minute charge to upower
        bat_path = self.obj_upower.AddDischargingBattery('mock_BAT', 'Mock Bat', 2.0, 60)
        obj_bat = self.system_bus_con.get_object('org.freedesktop.UPower', bat_path)
        self.obj_upower.EmitSignal('', 'DeviceAdded', 's', [bat_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        time.sleep(5)

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

    def test_forced_logout(self):
        '''Test forced logout'''

        self.daemon_death_expected = True
        idle_delay = round(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)

        self.settings_session['idle-delay'] = idle_delay
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = idle_delay + 1
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'logout'

        self.check_for_logout(idle_delay + 2)

        # The notification should have been received before the logout, but it's saved anyway
        notify_log = self.p_notify.stdout.read()
        self.assertTrue(b'You will soon log out because of inactivity.' in notify_log)

    def test_forced_logout_inhibition(self):
        '''Test we don't force logout when inhibited'''

        idle_delay = round(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)

        self.settings_session['idle-delay'] = idle_delay
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = idle_delay + 1
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'logout'

        # create suspend inhibitor which should stop us logging out
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_LOGOUT))

        self.check_no_logout(idle_delay + 3)

        # Drop inhibitor
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id))

    def test_unindle_on_ac_plug(self):
        idle_delay = round(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)
        self.settings_session['idle-delay'] = idle_delay

        # Wait for idle
        self.check_dim(idle_delay + 2)

        # Plug in the AC
        self.obj_upower.Set('org.freedesktop.UPower', 'OnBattery', False)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check that we undim
        self.check_undim(gsdpowerconstants.POWER_UP_TIME_ON_AC / 2)

        # And wait a little more to see us dim again
        self.check_dim(idle_delay + 2)

        # Unplug the AC
        self.obj_upower.Set('org.freedesktop.UPower', 'OnBattery', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check that we undim
        self.check_undim(gsdpowerconstants.POWER_UP_TIME_ON_AC / 2)

        # And wait a little more to see us dim again
        self.check_dim(idle_delay + 2)

# avoid writing to stderr
unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
