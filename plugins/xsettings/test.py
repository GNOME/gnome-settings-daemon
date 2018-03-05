#!/usr/bin/env python
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
        self.plugin_log_write = open(os.path.join(self.workdir, 'plugin_xsettings.log'), 'wb')
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

        self.settings_sound = Gio.Settings('org.gnome.desktop.sound')

        env = os.environ.copy()
        self.daemon = subprocess.Popen(
            [os.path.join(builddir, 'gsd-xsettings'), '--verbose'],
            # comment out this line if you want to see the logs in real time
            stdout=self.plugin_log_write,
            stderr=subprocess.STDOUT,
            env=env)

        # you can use this for reading the current daemon log in tests
        self.plugin_log = open(self.plugin_log_write.name)

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

    def test_gtk_modules(self):
        # Turn off event sounds
        self.settings_sound['event-sounds'] = False
        time.sleep(1)

        # Verify that only the PackageKit plugin is enabled
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'Modules'),
                dbus.String('pk-gtk-module', variant_level=1))

        # Turn on sounds
        self.settings_sound['event-sounds'] = True
        time.sleep(1)

        # Check that both PK and canberra plugin are enabled
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'Modules'),
                dbus.String('pk-gtk-module:canberra-gtk-module', variant_level=1))

    def test_enable_animations(self):
        # Check that "Enable animations" is off
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'EnableAnimations'),
                dbus.Boolean(True, variant_level=1))

        # Make vino appear
        vino = self.spawn_server('org.gnome.Vino',
                '/org/gnome/vino/screens/0',
                'org.gnome.VinoScreen')

        dbus_con = self.get_dbus()
        obj_vino = dbus_con.get_object('org.gnome.Vino', '/org/gnome/vino/screens/0')
        mock_vino = dbus.Interface(obj_vino, dbusmock.MOCK_IFACE)
        mock_vino.AddProperty('', 'Connected', dbus.Boolean(False, variant_level=1))

        time.sleep(0.1)

        # Check animations are still enabled
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'EnableAnimations'),
                dbus.Boolean(True, variant_level=1))

        # Connect a remote user
        mock_vino.EmitSignal('org.freedesktop.DBus.Properties',
                             'PropertiesChanged',
                            'sa{sv}as',
                            ['org.gnome.VinoScreen',
                            dbus.Dictionary({'Connected': dbus.Boolean(True, variant_level=1)}, signature='sv'),
                            dbus.Array([], signature='s')
                            ])

        time.sleep(0.1)

        # gdbus debug output
        # gdbus_log_write = open(os.path.join(self.workdir, 'gdbus.log'), 'wb')
        # process = subprocess.Popen(['gdbus', 'introspect', '--session', '--dest', 'org.gnome.Vino', '--object-path', '/org/gnome/vino/screens/0'],
        #         stdout=gdbus_log_write, stderr=subprocess.STDOUT)
        # time.sleep(1)

        # Check that "Enable animations" is off
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'EnableAnimations'),
                dbus.Boolean(False, variant_level=1))

        vino.terminate()
        vino.wait()
        time.sleep(0.1)

        # Check animations are back enabled
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'EnableAnimations'),
                dbus.Boolean(True, variant_level=1))

    def test_fontconfig_timestamp(self):
        # gdbus_log_write = open(os.path.join(self.workdir, 'gdbus.log'), 'wb')
        # process = subprocess.Popen(['gdbus', 'introspect', '--session', '--dest', 'org.gtk.Settings', '--object-path', '/org/gtk/Settings'],
        #        stdout=gdbus_log_write, stderr=subprocess.STDOUT)
        # time.sleep(1)

        before = self.obj_xsettings_props.Get('org.gtk.Settings', 'FontconfigTimestamp')
        self.assertEqual(before, 0)

        # Copy the fonts.conf again
        shutil.copy(os.path.join(os.path.dirname(__file__), 'fontconfig-test/fonts.conf'),
                os.path.join(self.fc_dir, 'fonts.conf'))
        time.sleep(1)

        # gdbus_log_write = open(os.path.join(self.workdir, 'gdbus-after.log'), 'wb')
        # process = subprocess.Popen(['gdbus', 'introspect', '--session', '--dest', 'org.gtk.Settings', '--object-path', '/org/gtk/Settings'],
        #         stdout=gdbus_log_write, stderr=subprocess.STDOUT)
        time.sleep(1)

        after = self.obj_xsettings_props.Get('org.gtk.Settings', 'FontconfigTimestamp')
        self.assertTrue(after > before)

# avoid writing to stderr
unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
