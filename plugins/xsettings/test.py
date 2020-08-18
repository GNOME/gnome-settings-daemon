#!/usr/bin/python3
'''GNOME settings daemon tests for xsettings plugin.'''

__author__ = 'Bastien Nocera <hadess@hadess.net>'
__copyright__ = '(C) 2018 Red Hat, Inc.'
__license__ = 'GPL v2 or later'

import unittest
import subprocess
import sys
import time
import os
import os.path
import signal
import shutil

project_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
builddir = os.environ.get('BUILDDIR', os.path.dirname(__file__))

sys.path.insert(0, os.path.join(project_root, 'tests'))
sys.path.insert(0, builddir)
import gsdtestcase
import dbus
import dbusmock

from gi.repository import Gio
from gi.repository import GLib

class XsettingsPluginTest(gsdtestcase.GSDTestCase):
    '''Test the xsettings plugin'''

    def setUp(self):
        self.start_logind()

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

        self.start_mutter()

        Gio.Settings.sync()
        self.plugin_log_write = open(os.path.join(self.workdir, 'plugin_xsettings.log'), 'wb', buffering=0)
        os.environ['GSD_ignore_llvmpipe'] = '1'

        # Setup fontconfig config path before starting the daemon
        self.fc_dir = os.path.join(self.workdir, 'fontconfig')
        os.environ['FONTCONFIG_PATH'] = self.fc_dir
        try:
            os.makedirs(self.fc_dir)
        except:
            pass
        shutil.copy(os.path.join(os.path.dirname(__file__), 'fontconfig-test/fonts.conf'),
                os.path.join(self.fc_dir, 'fonts.conf'))

        # Setup GTK+ modules before starting the daemon
        modules_dir = os.path.join(self.workdir, 'gtk-modules')
        os.environ['GSD_gtk_modules_dir'] = modules_dir
        try:
            os.makedirs(modules_dir)
        except:
            pass
        shutil.copy(os.path.join(os.path.dirname(__file__), 'gtk-modules-test/canberra-gtk-module.desktop'),
                os.path.join(modules_dir, 'canberra-gtk-module.desktop'))
        shutil.copy(os.path.join(os.path.dirname(__file__), 'gtk-modules-test/pk-gtk-module.desktop'),
                os.path.join(modules_dir, 'pk-gtk-module.desktop'))

        self.settings_sound = Gio.Settings.new('org.gnome.desktop.sound')

        env = os.environ.copy()
        self.daemon = subprocess.Popen(
            [os.path.join(builddir, 'gsd-xsettings'), '--verbose'],
            # comment out this line if you want to see the logs in real time
            stdout=self.plugin_log_write,
            stderr=subprocess.STDOUT,
            env=env)

        # you can use this for reading the current daemon log in tests
        self.plugin_log = open(self.plugin_log_write.name, 'rb', buffering=0)

        # flush notification log
        try:
            self.p_notify.stdout.read()
        except IOError:
            pass

        time.sleep(3)
        obj_xsettings = self.session_bus_con.get_object(
            'org.gtk.Settings', '/org/gtk/Settings')
        self.obj_xsettings_props = dbus.Interface(obj_xsettings, dbus.PROPERTIES_IFACE)

    def tearDown(self):

        daemon_running = self.daemon.poll() == None
        if daemon_running:
            self.daemon.terminate()
            self.daemon.wait()
        self.plugin_log.close()
        self.plugin_log_write.flush()
        self.plugin_log_write.close()

        self.stop_session()
        self.stop_mutter()
        self.stop_logind()

        # reset all changed gsettings, so that tests are independent from each
        # other
        for schema in [self.settings_sound]:
            for k in schema.list_keys():
                schema.reset(k)
        Gio.Settings.sync()

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

    def test_gtk_modules(self):
        # Turn off event sounds
        self.settings_sound['event-sounds'] = False
        time.sleep(2)

        # Verify that only the PackageKit plugin is enabled
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'Modules'),
                dbus.String('pk-gtk-module', variant_level=1))

        # Turn on sounds
        self.settings_sound['event-sounds'] = True
        time.sleep(2)

        # Check that both PK and canberra plugin are enabled
        retval = self.obj_xsettings_props.Get('org.gtk.Settings', 'Modules')
        values = sorted(str(retval).split(':'))
        self.assertEqual(values, ['canberra-gtk-module', 'pk-gtk-module'])

    def test_fontconfig_timestamp(self):
        # Initially, the value is zero
        before = self.obj_xsettings_props.Get('org.gtk.Settings', 'FontconfigTimestamp')
        self.assertEqual(before, 0)

        # Copy the fonts.conf again
        shutil.copy(os.path.join(os.path.dirname(__file__), 'fontconfig-test/fonts.conf'),
                os.path.join(self.fc_dir, 'fonts.conf'))

        # Wait for gsd-xsettings to pick up the change (and process it)
        self.check_plugin_log("Fontconfig update successful", timeout=5, failmsg="Fontconfig was not updated!")

        # Sleep a bit to ensure that the setting is updated
        time.sleep(1)

        after = self.obj_xsettings_props.Get('org.gtk.Settings', 'FontconfigTimestamp')
        self.assertTrue(after > before)

# avoid writing to stderr
unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
