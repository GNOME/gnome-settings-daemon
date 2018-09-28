#!/usr/bin/python3
'''GNOME settings daemon tests for power plugin.'''

__author__ = 'Martin Pitt <martin.pitt@ubuntu.com>'
__copyright__ = '(C) 2013 Canonical Ltd.'
__license__ = 'GPL v2 or later'

import unittest
import subprocess
import sys
import time
import math
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
from dbus.mainloop.glib import DBusGMainLoop

DBusGMainLoop(set_as_default=True)

import gi
gi.require_version('UPowerGlib', '1.0')
gi.require_version('UMockdev', '1.0')

from gi.repository import Gio
from gi.repository import GLib
from gi.repository import UPowerGlib
from gi.repository import UMockdev

class PowerPluginBase(gsdtestcase.GSDTestCase):
    '''Test the power plugin'''

    COMMON_SUSPEND_METHODS=['Suspend', 'Hibernate', 'SuspendThenHibernate']

    def setUp(self):
        self.mock_external_monitor_file = os.path.join(self.workdir, 'GSD_MOCK_EXTERNAL_MONITOR')
        os.environ['GSD_MOCK_EXTERNAL_MONITOR_FILE'] = self.mock_external_monitor_file

        self.check_logind_gnome_session()
        self.start_logind()
        self.daemon_death_expected = False


        # Setup umockdev testbed
        self.testbed = UMockdev.Testbed.new()
        os.environ['UMOCKDEV_DIR'] = self.testbed.get_root_dir()

        # Create a mock backlight device
        # Note that this function creates a different or even no backlight
        # device based on the name of the test.
        self.add_backlight()

        # start mock upowerd
        (self.upowerd, self.obj_upower) = self.spawn_server_template(
            'upower', {'DaemonVersion': '0.99', 'OnBattery': True, 'LidIsClosed': False}, stdout=subprocess.PIPE)
        gsdtestcase.set_nonblock(self.upowerd.stdout)

        # start mock gnome-shell screensaver
        (self.screensaver, self.obj_screensaver) = self.spawn_server_template(
            'gnome_screensaver', stdout=subprocess.PIPE)
        gsdtestcase.set_nonblock(self.screensaver.stdout)

        self.session_log_write = open(os.path.join(self.workdir, 'gnome-session.log'), 'wb', buffering=0)
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

        self.session_log = open(self.session_log_write.name, 'rb', buffering=0)

        self.obj_session_mgr = self.session_bus_con.get_object(
            'org.gnome.SessionManager', '/org/gnome/SessionManager')

        self.start_mutter()

        # Set up the gnome-session presence
        obj_session_presence = self.session_bus_con.get_object(
            'org.gnome.SessionManager', '/org/gnome/SessionManager/Presence')
        self.obj_session_presence_props = dbus.Interface(obj_session_presence, dbus.PROPERTIES_IFACE)

        # ensure that our tests don't lock the screen when the screensaver
        # gets active
        self.settings_screensaver = Gio.Settings(schema_id='org.gnome.desktop.screensaver')
        self.settings_screensaver['lock-enabled'] = False

        # Ensure we set up the external monitor state
        self.set_has_external_monitor(False)

        self.settings_gsd_power = Gio.Settings(schema_id='org.gnome.settings-daemon.plugins.power')

        Gio.Settings.sync()
        self.plugin_log_write = open(os.path.join(self.workdir, 'plugin_power.log'), 'wb', buffering=0)
        # avoid painfully long delays of actions for tests
        env = os.environ.copy()
        # Disable PulseAudio output from libcanberra
        env['CANBERRA_DRIVER'] = 'null'

        # Use dummy script as testing backlight helper
        env['GSD_BACKLIGHT_HELPER'] = os.path.join (project_root, 'plugins', 'power', 'test-backlight-helper')

        self.daemon = subprocess.Popen(
            [os.path.join(builddir, 'gsd-power'), '--verbose'],
            # comment out this line if you want to see the logs in real time
            stdout=self.plugin_log_write,
            stderr=subprocess.STDOUT,
            env=env)

        # you can use this for reading the current daemon log in tests
        self.plugin_log = open(self.plugin_log_write.name, 'rb', buffering=0)

        # wait until plugin is ready
        timeout = 100
        while timeout > 0:
            time.sleep(0.1)
            timeout -= 1
            log = self.plugin_log.read()
            if b'System inhibitor fd is' in log:
                break

        # always start with zero idle time
        self.reset_idle_timer()

        # flush notification log
        self.p_notify.stdout.read()

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
        self.stop_mutter()
        self.stop_logind()

        # reset all changed gsettings, so that tests are independent from each
        # other
        for schema in [self.settings_gsd_power, self.settings_session, self.settings_screensaver]:
            for k in schema.list_keys():
                schema.reset(k)
        Gio.Settings.sync()

        try:
            os.unlink(self.mock_external_monitor_file)
        except OSError:
            pass

        del self.testbed

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

    def check_logind_gnome_session(self):
        '''Check that gnome-session is built with logind support'''

        path = GLib.find_program_in_path ('gnome-session')
        assert(path)
        (success, data) = GLib.file_get_contents (path)
        lines = data.split(b'\n')
        new_path = None
        for line in lines:
            items = line.split()
            if items and items[0] == b'exec':
                new_path = items[1]
        if not new_path:
            self.fail("could not get gnome-session's real path from %s" % path)
        path = new_path
        ldd = subprocess.Popen(['ldd', path], stdout=subprocess.PIPE)
        out = ldd.communicate()[0]
        if not b'libsystemd.so.0' in out:
            self.fail('gnome-session is not built with logind support')

    def get_status(self):
        return self.obj_session_presence_props.Get('org.gnome.SessionManager.Presence', 'status')

    def backlight_defaults(self):
        # Hack to modify the brightness defaults before starting gsd-power.
        # The alternative would be to create two separate test files.
        if 'no_backlight' in self.id():
            return None, None
        elif 'legacy_brightness' in self.id():
            return 15, 15
        else:
            return 100, 50

    def add_backlight(self, _type="raw"):
        max_brightness, brightness = self.backlight_defaults()

        if max_brightness is None:
            self.backlight = None
            return

        # Undo mangling done in GSD
        if max_brightness >= 99:
            max_brightness += 1
            brightness += 1

        # This needs to be done before starting gsd-power!
        self.backlight = self.testbed.add_device('backlight', 'mock_backlight', None,
                                                 ['type', _type,
                                                  'max_brightness', str(max_brightness),
                                                  'brightness', str(brightness)],
                                                 [])

    def get_brightness(self):
        max_brightness = int(open(os.path.join(self.testbed.get_root_dir() + self.backlight, 'max_brightness')).read())

        # self.backlight contains the leading slash, so os.path.join doesn't quite work
        res = int(open(os.path.join(self.testbed.get_root_dir() + self.backlight, 'brightness')).read())
        # Undo mangling done in GSD
        if max_brightness >= 99:
            res -= 1
        return res

    def set_has_external_monitor(self, external):
        if external:
            val = b'1'
        else:
            val = b'0'
        GLib.file_set_contents (self.mock_external_monitor_file, val)

    def set_composite_battery_discharging(self, icon='battery-good-symbolic'):
        self.obj_upower.SetupDisplayDevice(
            UPowerGlib.DeviceKind.BATTERY,
            UPowerGlib.DeviceState.DISCHARGING,
            50., 50., 100., # 50%, charge 50 of 100
            0.01, 600, 0, # Discharge rate 0.01 with 600 seconds remaining, 0 time to full
            True, # present
            icon, UPowerGlib.DeviceLevel.NONE
        )

    def set_composite_battery_critical(self, icon='battery-caution-symbolic'):
        self.obj_upower.SetupDisplayDevice(
            UPowerGlib.DeviceKind.BATTERY,
            UPowerGlib.DeviceState.DISCHARGING,
            2., 2., 100., # 2%, charge 2 of 100
            0.01, 60, 0, # Discharge rate 0.01 with 60 seconds remaining, 0 time to full
            True, # present
            icon, UPowerGlib.DeviceLevel.CRITICAL
        )

    def check_for_logout(self, timeout):
        '''Check that logout is requested.

        Fail after the given timeout.
        '''
        # check that it request logout
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested logout
            log = self.session_log.read()
            if log is None:
                continue

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

    def check_for_suspend(self, timeout, methods=COMMON_SUSPEND_METHODS):
        '''Check that one of the given suspend methods are requested. Default
        methods are Suspend() or Hibernate() but also HibernateThenSuspend()
        is valid.

        Fail after the given timeout.
        '''

        # Create a list of byte string needles to search for
        needles = [' {} '.format(m).encode('ascii') for m in methods]

        suspended = False

        # check that it request suspend
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
            # check that it requested suspend
            log = self.logind.stdout.read()
            if log is None:
                continue

            for n in needles:
                if n in log:
                    suspended = True
                    break

            if suspended:
                break

        if not suspended:
            self.fail('timed out waiting for logind suspend call, methods: %s' % ', '.join(methods))

    def check_for_lid_inhibited(self, timeout=0):
        '''Check that the lid inhibitor has been added.

        Fail after the given timeout.
        '''
        self.check_plugin_log('Adding lid switch system inhibitor', timeout,
                              'Timed out waiting for lid inhibitor')

    def check_for_lid_uninhibited(self, timeout=0):
        '''Check that the lid inhibitor has been dropped.

        Fail after the given timeout.
        '''
        self.check_plugin_log('uninhibiting lid close', timeout,
                              'Timed out waiting for lid uninhibition')

    def check_no_lid_uninhibited(self, timeout=0):
        '''Check that the lid inhibitor has been dropped.

        Fail after the given timeout.
        '''
        time.sleep(timeout)
        # check that it requested uninhibition
        log = self.plugin_log.read()

        if b'uninhibiting lid close' in log:
            self.fail('lid uninhibit should not have happened')

    def check_no_suspend(self, seconds, methods=COMMON_SUSPEND_METHODS):
        '''Check that no Suspend or Hibernate is requested in the given time'''

        # wait for specified time to ensure it didn't do anything
        time.sleep(seconds)
        # check that it did not suspend or hibernate
        log = self.logind.stdout.read()
        if log is None:
            return

        for m in methods:
            needle = ' {} '.format(m).encode('ascii')

            self.assertFalse(needle in log, 'unexpected %s request' % m)

    def check_suspend_no_hibernate(self, seconds):
        '''Check that Suspend was requested and not Hibernate, in the given time'''

        # wait for specified time to ensure it didn't do anything
        time.sleep(seconds)
        # check that it did suspend and didn't hibernate
        log = self.logind.stdout.read()
        if log:
            self.assertTrue(b' Suspend' in log, 'missing Suspend request')
            self.assertFalse(b' Hibernate' in log, 'unexpected Hibernate request')

    def check_plugin_log(self, needle, timeout=0, failmsg=None):
        '''Check that needle is found in the log within the given timeout.
        Returns immediately when found.

        Fail after the given timeout.
        '''
        if type(needle) == str:
            needle = needle.encode('ascii')
        # Fast path if the message was already logged
        log = self.plugin_log.read()
        if needle in log:
            return

        while timeout > 0:
            time.sleep(0.5)
            timeout -= 0.5

            # read new data (lines) from the log
            log = self.plugin_log.read()
            if needle in log:
                break
        else:
            if failmsg is not None:
                self.fail(failmsg)
            else:
                self.fail('timed out waiting for needle "%s"' % needle)

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

        self.check_plugin_log('Doing a state transition: dim', timeout,
                              'timed out waiting for dim')

    def check_undim(self, timeout):
        '''Check that mode is set to normal in the given time'''

        self.check_plugin_log('Doing a state transition: normal', timeout,
                              'timed out waiting for normal mode')

    def check_blank(self, timeout):
        '''Check that blank is requested.

        Fail after the given timeout.
        '''

        self.check_plugin_log('TESTSUITE: Blanked screen', timeout,
                              'timed out waiting for blank')

    def check_unblank(self, timeout):
        '''Check that unblank is requested.

        Fail after the given timeout.
        '''

        self.check_plugin_log('TESTSUITE: Unblanked screen', timeout,
                              'timed out waiting for unblank')

    def check_no_blank(self, seconds):
        '''Check that no blank is requested in the given time'''

        # wait for specified time to ensure it didn't blank
        time.sleep(seconds)
        # check that it did not blank
        log = self.plugin_log.read()
        self.assertFalse(b'TESTSUITE: Blanked screen' in log, 'unexpected blank request')

    def check_no_unblank(self, seconds):
        '''Check that no unblank is requested in the given time'''

        # wait for specified time to ensure it didn't unblank
        time.sleep(seconds)
        # check that it did not unblank
        log = self.plugin_log.read()
        self.assertFalse(b'TESTSUITE: Unblanked screen' in log, 'unexpected unblank request')

class PowerPluginTest1(PowerPluginBase):
    def test_screensaver(self):
        # Note that the screensaver mock object
        # doesn't know how to get out of being active,
        # be it if the lock is disabled, or not.

        self.obj_screensaver.Lock()
        # 0.3 second animation
        time.sleep(1)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # blank is supposed to happen straight away
        self.check_blank(2)

        # Wait a bit for the active watch to be registered through dbus, then
        # fake user activity and check that the screen is unblanked.
        time.sleep(0.5)
        self.reset_idle_timer()
        self.check_unblank(2)

        # Check for no blank before the normal blank timeout
        self.check_no_blank(gsdpowerconstants.SCREENSAVER_TIMEOUT_BLANK - 4)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # and check for blank after the blank timeout
        self.check_blank(10)

        # Wait a bit for the active watch to be registered through dbus, then
        # fake user activity and check that the screen is unblanked.
        time.sleep(0.5)
        self.reset_idle_timer()
        self.check_unblank(2)

        # check no blank and then blank
        self.check_no_blank(gsdpowerconstants.SCREENSAVER_TIMEOUT_BLANK - 4)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')
        self.check_blank(10)

    def test_sleep_inactive_blank(self):
        '''screensaver/blank interaction'''

        # create suspend inhibitor which should have no effect on the idle
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND),
            dbus_interface='org.gnome.SessionManager')

        self.obj_screensaver.SetActive(True)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # blank is supposed to happen straight away
        self.check_blank(2)

        # Wait a bit for the active watch to be registered through dbus, then
        # fake user activity and check that the screen is unblanked.
        time.sleep(0.5)
        self.reset_idle_timer()
        self.check_unblank(2)
        self.assertTrue(self.get_brightness() == gsdpowerconstants.GSD_MOCK_DEFAULT_BRIGHTNESS , 'incorrect unblanked brightness (%d != %d)' % (self.get_brightness(), gsdpowerconstants.GSD_MOCK_DEFAULT_BRIGHTNESS))

        # Check for no blank before the normal blank timeout
        self.check_no_blank(gsdpowerconstants.SCREENSAVER_TIMEOUT_BLANK - 4)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # and check for blank after the blank timeout
        self.check_blank(10)

        # Drop inhibitor
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')

class PowerPluginTest2(PowerPluginBase):
    def test_screensaver_no_unblank(self):
        '''Ensure the screensaver is not unblanked for new inhibitors.'''

        # Lower idle delay a lot
        self.settings_session['idle-delay'] = 1

        # Bring down the screensaver
        self.obj_screensaver.SetActive(True)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # Check that we blank
        self.check_blank(2)

        # Create the different possible inhibitors
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_IDLE | gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND | gsdpowerenums.GSM_INHIBITOR_FLAG_LOGOUT),
            dbus_interface='org.gnome.SessionManager')

        self.check_no_unblank(2)

        # Drop inhibitor
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')

        self.check_no_unblank(2)

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
        # Resolve possible race condition, see also https://gitlab.gnome.org/GNOME/mutter/issues/113
        time.sleep(0.2)
        self.reset_idle_timer()
        time.sleep(5)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)
        time.sleep(10)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_IDLE)

        # Lower the delay again, and see that we get idle as we should
        self.settings_session['idle-delay'] = 5
        # Resolve possible race condition, see also https://gitlab.gnome.org/GNOME/mutter/issues/113
        time.sleep(0.2)
        self.reset_idle_timer()
        time.sleep(2)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)
        time.sleep(5)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_IDLE)

    def test_idle_time_reset_on_resume(self):
        '''Check that the IDLETIME is reset when resuming'''

        self.settings_screensaver['lock-enabled'] = False

        # Go idle
        self.settings_session['idle-delay'] = 5
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)
        time.sleep(7)
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_IDLE)

        # Go to sleep
        self.logind_obj.EmitSignal('', 'PrepareForSleep', 'b', [True], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # Wake up
        self.logind_obj.EmitSignal('', 'PrepareForSleep', 'b', [False], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # And check we're not idle
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

class PowerPluginTest3(PowerPluginBase):
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

    def _test_suspend_no_hibernate(self):
        '''suspend-no-hibernate'''

        self.settings_session['idle-delay'] = 2
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 5
        # Hibernate isn't possible, so it should end up suspending
        # FIXME
        self.settings_gsd_power['critical-battery-action'] = 'hibernate'

        # wait for idle delay; should not yet hibernate
        self.check_no_suspend(2)

        # suspend should happen after inactive sleep timeout + 1 s notification
        # delay + 1 s error margin
        self.check_suspend_no_hibernate(7)

    def test_sleep_inhibition(self):
        '''Does not sleep under idle inhibition'''

        idle_delay = round(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)

        self.settings_session['idle-delay'] = idle_delay
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 5
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_IDLE | gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND),
            dbus_interface='org.gnome.SessionManager')
        self.check_no_suspend(idle_delay + 2)
        self.check_no_dim(0)

        # Check that we didn't go to idle either
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')

class PowerPluginTest4(PowerPluginBase):
    def test_lock_on_lid_close(self):
        '''Check that we do lock on lid closing, if the machine will not suspend'''

        self.settings_screensaver['lock-enabled'] = True

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND),
            dbus_interface='org.gnome.SessionManager')

        time.sleep (gsdpowerconstants.LID_CLOSE_SAFETY_TIMEOUT)

        # Close the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check that we've blanked
        time.sleep(2)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')
        self.check_blank(2)

        # Drop the inhibit and see whether we suspend
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')
        # At this point logind should suspend for us
        self.settings_screensaver['lock-enabled'] = False

    def test_blank_on_lid_close(self):
        '''Check that we do blank on lid closing, if the machine will not suspend'''

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND),
            dbus_interface='org.gnome.SessionManager')

        time.sleep (gsdpowerconstants.LID_CLOSE_SAFETY_TIMEOUT)

        # Close the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')

        # Check that we've blanked
        self.check_blank(4)

        # Drop the inhibit and see whether we suspend
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')
        # At this point logind should suspend for us

    def test_unblank_on_lid_open(self):
        '''Check that we do unblank on lid opening, if the machine will not suspend'''

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_SUSPEND),
            dbus_interface='org.gnome.SessionManager')

        time.sleep (gsdpowerconstants.LID_CLOSE_SAFETY_TIMEOUT)

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
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')

class PowerPluginTest5(PowerPluginBase):
    def test_dim(self):
        '''Check that we do go to dim'''

        # Wait and flush log
        time.sleep (gsdpowerconstants.LID_CLOSE_SAFETY_TIMEOUT + 1)
        self.plugin_log.read()

        idle_delay = math.ceil(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)
        self.reset_idle_timer()

        self.settings_session['idle-delay'] = idle_delay
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = idle_delay + 1
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'
        # This is an absolute percentage, and our brightness is 0..100
        dim_level = self.settings_gsd_power['idle-brightness'];

        # Check that we're not idle
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

        # Wait and check we're not idle, but dimmed
        self.check_dim(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY + 1)
        # Give time for the brightness to change
        time.sleep(2)
        level = self.get_brightness();
        self.assertTrue(level == dim_level, 'incorrect dim brightness (%d != %d)' % (level, dim_level))

        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

        # Bring down the screensaver
        self.obj_screensaver.SetActive(True)
        self.assertTrue(self.obj_screensaver.GetActive(), 'screensaver not turned on')

        # Check that we blank
        self.check_blank(2)

        # Go to sleep
        self.logind_obj.EmitSignal('', 'PrepareForSleep', 'b', [True], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # Wake up
        self.logind_obj.EmitSignal('', 'PrepareForSleep', 'b', [False], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # And check that we have the pre-dim brightness
        self.assertTrue(self.get_brightness() == gsdpowerconstants.GSD_MOCK_DEFAULT_BRIGHTNESS , 'incorrect unblanked brightness (%d != %d)' % (self.get_brightness(), gsdpowerconstants.GSD_MOCK_DEFAULT_BRIGHTNESS))

    def test_lid_close_inhibition(self):
        '''Check that we correctly inhibit suspend with an external monitor'''

        # Wait and flush log
        time.sleep (gsdpowerconstants.LID_CLOSE_SAFETY_TIMEOUT + 1)
        self.plugin_log.read()

        # Add an external monitor
        self.set_has_external_monitor(True)
        self.check_for_lid_inhibited(1)

        # Check that we do not uninhibit with the external monitor attached
        self.check_no_lid_uninhibited(gsdpowerconstants.LID_CLOSE_SAFETY_TIMEOUT + 1)

        # Close the lid
        self.obj_upower.Set('org.freedesktop.UPower', 'LidIsClosed', True)
        self.obj_upower.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(0.5)

        # Unplug the external monitor
        self.set_has_external_monitor(False)

        # Check that no action happens during the safety time minus 1 second
        self.check_no_lid_uninhibited(gsdpowerconstants.LID_CLOSE_SAFETY_TIMEOUT - 1)
        # Check that we're uninhibited after the safety time
        self.check_for_lid_uninhibited(4)

class PowerPluginTest6(PowerPluginBase):
    def test_notify_critical_battery(self):
        '''action on critical battery'''

        self.set_composite_battery_discharging()

        time.sleep(2)

        self.set_composite_battery_critical()

        # Check that it was picked up
        self.check_plugin_log('EMIT: charge-critical', 2)

        # Wait a bit longer to ensure event has been fired
        time.sleep(0.5)
        # we should have gotten a notification now
        notify_log = self.p_notify.stdout.read()

        # verify notification
        self.assertRegex(notify_log, b'[0-9.]+ Notify "Power" 0 "battery-caution-symbolic" ".*battery critical.*"')

    def test_notify_critical_battery_on_start(self):
        '''action on critical battery on startup'''

        self.set_composite_battery_critical()

        # Check that it was picked up
        self.check_plugin_log('EMIT: charge-critical', 2)

        time.sleep(0.5)

        # we should have gotten a notification by now
        notify_log = self.p_notify.stdout.read()

        # verify notification
        self.assertRegex(notify_log, b'[0-9.]+ Notify "Power" 0 "battery-caution-symbolic" ".*battery critical.*"')

    def test_notify_device_battery(self):
        '''critical power level notification for device batteries'''

        # Set internal battery to discharging
        self.set_composite_battery_discharging()

        # Add a device battery
        bat2_path = '/org/freedesktop/UPower/devices/' + 'mock_MOUSE_BAT1'
        self.obj_upower.AddObject(bat2_path,
                                  'org.freedesktop.UPower.Device',
                                  {
                                      'PowerSupply': dbus.Boolean(False, variant_level=1),
                                      'IsPresent': dbus.Boolean(True, variant_level=1),
                                      'Model': dbus.String('Bat1', variant_level=1),
                                      'Percentage': dbus.Double(40.0, variant_level=1),
                                      'TimeToEmpty': dbus.Int64(1600, variant_level=1),
                                      'EnergyFull': dbus.Double(100.0, variant_level=1),
                                      'Energy': dbus.Double(40.0, variant_level=1),
                                      'State': dbus.UInt32(UPowerGlib.DeviceState.DISCHARGING, variant_level=1),
                                      'Type': dbus.UInt32(UPowerGlib.DeviceKind.MOUSE, variant_level=1),
                                      'WarningLevel': dbus.UInt32(UPowerGlib.DeviceLevel.NONE, variant_level=1),
                                   }, dbus.Array([], signature='(ssss)'))

        obj_bat2 = self.system_bus_con.get_object('org.freedesktop.UPower', bat2_path)
        self.obj_upower.EmitSignal('', 'DeviceAdded', 'o', [bat2_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')
        time.sleep(1)

        # now change the mouse battery to critical charge
        obj_bat2.Set('org.freedesktop.UPower.Device', 'TimeToEmpty',
                     dbus.Int64(30, variant_level=1),
                     dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat2.Set('org.freedesktop.UPower.Device', 'Energy',
                     dbus.Double(0.5, variant_level=1),
                     dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat2.Set('org.freedesktop.UPower.Device', 'WarningLevel',
                     dbus.UInt32(UPowerGlib.DeviceLevel.CRITICAL, variant_level=1),
                     dbus_interface=dbus.PROPERTIES_IFACE)
        obj_bat2.EmitSignal('', 'Changed', '', [], dbus_interface='org.freedesktop.DBus.Mock')
        self.obj_upower.EmitSignal('', 'DeviceChanged', 'o', [bat2_path],
                                   dbus_interface='org.freedesktop.DBus.Mock')

        self.check_plugin_log('EMIT: charge-critical', 2)
        time.sleep(0.5)

        # we should have gotten a notification by now
        notify_log = self.p_notify.stdout.read()

        # verify notification
        self.assertRegex(notify_log, b'[0-9.]+ Notify "Power" 0 ".*" ".*Wireless mouse .*low.* power.*"')

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
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_LOGOUT),
            dbus_interface='org.gnome.SessionManager')

        self.check_no_logout(idle_delay + 3)

        # Drop inhibitor
        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')

class PowerPluginTest7(PowerPluginBase):
    def test_check_missing_kbd_brightness(self):
        ''' https://bugzilla.gnome.org/show_bug.cgi?id=793512 '''

        obj_gsd_power_kbd = self.session_bus_con.get_object(
            'org.gnome.SettingsDaemon.Power', '/org/gnome/SettingsDaemon/Power')
        obj_gsd_power_kbd_props = dbus.Interface(obj_gsd_power_kbd, dbus.PROPERTIES_IFACE)

        # Will return -1 if gsd-power crashed, and an exception if the code caught the problem
        with self.assertRaises(dbus.DBusException) as exc:
            kbd_brightness = obj_gsd_power_kbd_props.Get('org.gnome.SettingsDaemon.Power.Keyboard', 'Brightness')

            # We should not have arrived here, if we did then the test failed, let's print this to help debugging
            print('Got keyboard brightness: {}'.format(kbd_brightness))

        self.assertEqual(exc.exception.get_dbus_message(), 'Failed to get property Brightness on interface org.gnome.SettingsDaemon.Power.Keyboard')

    def test_inhibitor_idletime(self):
        ''' https://bugzilla.gnome.org/show_bug.cgi?id=705942 '''

        idle_delay = round(gsdpowerconstants.MINIMUM_IDLE_DIM_DELAY / gsdpowerconstants.IDLE_DELAY_TO_IDLE_DIM_MULTIPLIER)

        self.settings_session['idle-delay'] = idle_delay
        self.settings_gsd_power['sleep-inactive-battery-timeout'] = 5
        self.settings_gsd_power['sleep-inactive-battery-type'] = 'suspend'

        # create inhibitor
        inhibit_id = self.obj_session_mgr.Inhibit(
            'testsuite', dbus.UInt32(0), 'for testing',
            dbus.UInt32(gsdpowerenums.GSM_INHIBITOR_FLAG_IDLE),
            dbus_interface='org.gnome.SessionManager')
        self.check_no_suspend(idle_delay + 2)
        self.check_no_dim(0)

        # Check that we didn't go to idle either
        self.assertEqual(self.get_status(), gsdpowerenums.GSM_PRESENCE_STATUS_AVAILABLE)

        self.obj_session_mgr.Uninhibit(dbus.UInt32(inhibit_id),
                dbus_interface='org.gnome.SessionManager')

        self.check_no_suspend(2)
        self.check_no_dim(0)

        time.sleep(5)

        self.check_suspend_no_hibernate(7)

    def disabled_test_unindle_on_ac_plug(self):
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

class PowerPluginTest8(PowerPluginBase):
    def test_brightness_stepping(self):
        '''Check that stepping the backlight works as expected'''

        obj_gsd_power = self.session_bus_con.get_object(
            'org.gnome.SettingsDaemon.Power', '/org/gnome/SettingsDaemon/Power')
        obj_gsd_power_screen_iface = dbus.Interface(obj_gsd_power, 'org.gnome.SettingsDaemon.Power.Screen')

        # Each of the step calls will only return when the value was written
        start = time.time()
        # We start at 50% and step by 5% each time
        obj_gsd_power_screen_iface.StepUp()
        self.assertEqual(self.get_brightness(), 55)
        obj_gsd_power_screen_iface.StepUp()
        self.assertEqual(self.get_brightness(), 60)
        obj_gsd_power_screen_iface.StepUp()
        self.assertEqual(self.get_brightness(), 65)
        obj_gsd_power_screen_iface.StepUp()
        self.assertEqual(self.get_brightness(), 70)
        stop = time.time()
        # This needs to take more than 0.8 seconds as each write is delayed by
        # 0.2 seconds by the test backlight helper
        self.assertGreater(stop - start, 0.8)

        # Now, the same thing should work fine if we step multiple times,
        # even if we are so quick that compression will happen.
        # Use a list to keep rack of replies (as integer is immutable and would
        # not be modified in the outer scope)
        replies = [0]

        def handle_reply(*args):
            replies[0] += 1

        def last_reply(*args):
            replies[0] += 1
            loop.quit()

        def error_handler(*args):
            loop.quit()

        start = time.time()
        obj_gsd_power_screen_iface.StepDown(reply_handler=handle_reply, error_handler=error_handler)
        obj_gsd_power_screen_iface.StepDown(reply_handler=handle_reply, error_handler=error_handler)
        obj_gsd_power_screen_iface.StepDown(reply_handler=handle_reply, error_handler=error_handler)
        obj_gsd_power_screen_iface.StepDown(reply_handler=last_reply, error_handler=error_handler)
        loop = GLib.MainLoop()
        loop.run()
        stop = time.time()

        # The calls need to be returned in order. As we got the last reply, all
        # others must have been received too.
        self.assertEqual(replies[0], 4)
        # Four steps down, so back at 50%
        self.assertEqual(self.get_brightness(), 50)
        # And compression must have happened, so it should take less than 0.8s
        self.assertLess(stop - start, 0.8)

    def test_brightness_compression(self):
        '''Check that compression also happens when setting the property'''
        # Now test that the compression works correctly.
        # NOTE: Relies on the implementation detail, that the property setter
        #       returns immediately rather than waiting for the brightness to
        #       be updated.
        # Should this ever be fixed, then this will need to be changed to use
        # async dbus calls similar to the stepping code

        obj_gsd_power = self.session_bus_con.get_object(
            'org.gnome.SettingsDaemon.Power', '/org/gnome/SettingsDaemon/Power')
        obj_gsd_power_prop_iface = dbus.Interface(obj_gsd_power, dbus.PROPERTIES_IFACE)

        # Quickly ramp the brightness up
        for brightness in range(70, 91):
            obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', brightness)

        # The brightness of 80 should be in effect after slightly more than
        # 0.4 seconds. If compression does not work as expected, this would take
        # more than 5 seconds for the 20 steps.
        time.sleep(2.0)
        self.assertEqual(self.get_brightness(), 90)

    def test_brightness_uevent(self):
        obj_gsd_power = self.session_bus_con.get_object(
            'org.gnome.SettingsDaemon.Power', '/org/gnome/SettingsDaemon/Power')
        obj_gsd_power_prop_iface = dbus.Interface(obj_gsd_power, dbus.PROPERTIES_IFACE)

        brightness = obj_gsd_power_prop_iface.Get('org.gnome.SettingsDaemon.Power.Screen', 'Brightness')
        self.assertEqual(50, brightness)

        # Check that the brightness is updated if it was changed through some
        # other mechanism (e.g. firmware).
        # Set to 80+1 because of the GSD offset (see add_backlight).
        self.testbed.set_attribute(self.backlight, 'brightness', '81')
        self.testbed.uevent(self.backlight, 'change')

        self.check_plugin_log('GsdBacklight: Got uevent', 1, 'gsd-power did not process uevent')
        time.sleep(0.2)

        brightness = obj_gsd_power_prop_iface.Get('org.gnome.SettingsDaemon.Power.Screen', 'Brightness')
        self.assertEqual(80, brightness)

    def test_brightness_step(self):
        # We cannot use check_plugin_log here because the startup check already
        # read the relevant message.
        log = open(self.plugin_log_write.name, 'rb').read()
        self.assertIn(b'Step size for backlight is 5.', log)

    def test_legacy_brightness_step(self):
        # We cannot use check_plugin_log here because the startup check already
        # read the relevant message.
        log = open(self.plugin_log_write.name, 'rb').read()
        self.assertIn(b'Step size for backlight is 1.', log)

    def test_legacy_brightness_rounding(self):
        obj_gsd_power = self.session_bus_con.get_object(
            'org.gnome.SettingsDaemon.Power', '/org/gnome/SettingsDaemon/Power')
        obj_gsd_power_prop_iface = dbus.Interface(obj_gsd_power, dbus.PROPERTIES_IFACE)

        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 0)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 0)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 10)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 1)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 20)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 3)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 25)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 4)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 50)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 7)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 52)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 8)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 56)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 8)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 57)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 9)
        obj_gsd_power_prop_iface.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 98)
        time.sleep(0.4)
        self.assertEqual(self.get_brightness(), 15)

    def test_no_backlight(self):
        '''Check that backlight brightness DBus api without a backlight'''

        obj_gsd_power = self.session_bus_con.get_object(
            'org.gnome.SettingsDaemon.Power', '/org/gnome/SettingsDaemon/Power')
        obj_gsd_power_props = dbus.Interface(obj_gsd_power, dbus.PROPERTIES_IFACE)
        obj_gsd_power_screen = dbus.Interface(obj_gsd_power, 'org.gnome.SettingsDaemon.Power.Screen')

        # We expect -1 to be returned
        brightness = obj_gsd_power_props.Get('org.gnome.SettingsDaemon.Power.Screen', 'Brightness')
        self.assertEqual(brightness, -1)

        # Trying to set the brightness
        with self.assertRaises(dbus.DBusException) as exc:
            obj_gsd_power_props.Set('org.gnome.SettingsDaemon.Power.Screen', 'Brightness', 1)

        self.assertEqual(exc.exception.get_dbus_message(), 'No usable backlight could be found!')

        with self.assertRaises(dbus.DBusException) as exc:
            obj_gsd_power_screen.StepUp()

        self.assertEqual(exc.exception.get_dbus_message(), 'No usable backlight could be found!')

        with self.assertRaises(dbus.DBusException) as exc:
            obj_gsd_power_screen.StepDown()

        self.assertEqual(exc.exception.get_dbus_message(), 'No usable backlight could be found!')

# avoid writing to stderr
unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
