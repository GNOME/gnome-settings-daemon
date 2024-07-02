#!/usr/bin/python3 -u
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
from output_checker import OutputChecker

from gi.repository import Gio
from gi.repository import GLib

class XsettingsPluginTest(gsdtestcase.GSDTestCase):
    '''Test the xsettings plugin'''

    gsd_plugin = 'xsettings'
    gsd_plugin_case = 'XSettings'

    def setUp(self):
        self.start_logind()
        self.addCleanup(self.stop_logind)

        self.start_session()
        self.addCleanup(self.stop_session)

        self.obj_session_mgr = self.session_bus_con.get_object(
            'org.gnome.SessionManager', '/org/gnome/SessionManager')

        self.start_mutter(needs_x11=True)
        self.addCleanup(self.stop_mutter)

        Gio.Settings.sync()
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
        self.addCleanup(self.reset_settings, self.settings_sound)
        Gio.Settings.sync()

        env = os.environ.copy()
        self.start_plugin(os.environ.copy())
        self.addCleanup(self.stop_plugin)

        # flush notification log
        self.p_notify_log.clear()

        self.plugin_log.check_line(b'GsdXSettingsGtk initializing', timeout=10)

        obj_xsettings = self.session_bus_con.get_object(
            'org.gtk.Settings', '/org/gtk/Settings')
        self.obj_xsettings_props = dbus.Interface(obj_xsettings, dbus.PROPERTIES_IFACE)

    def check_plugin_log(self, needle, timeout=0, failmsg=None):
        '''Check that needle is found in the log within the given timeout.
        Returns immediately when found.

        Fail after the given timeout.
        '''
        self.plugin_log.check_line(needle, timeout=timeout, failmsg=failmsg)

    def test_gtk_modules(self):
        # Turn off event sounds
        self.settings_sound['event-sounds'] = False
        Gio.Settings.sync()
        time.sleep(2)

        # Verify that only the PackageKit plugin is enabled
        self.assertEqual(self.obj_xsettings_props.Get('org.gtk.Settings', 'Modules'),
                dbus.String('pk-gtk-module', variant_level=1))

        # Turn on sounds
        self.settings_sound['event-sounds'] = True
        Gio.Settings.sync()
        time.sleep(2)

        # Check that both PK and canberra plugin are enabled
        retval = self.obj_xsettings_props.Get('org.gtk.Settings', 'Modules')
        values = sorted(str(retval).split(':'))
        self.assertEqual(values, ['canberra-gtk-module', 'pk-gtk-module'])

    def test_fontconfig_timestamp(self):
        # Initially, the value is zero
        before = self.obj_xsettings_props.Get('org.gtk.Settings', 'FontconfigTimestamp')
        self.assertEqual(before, 0)

        # Make sure the seconds changed
        time.sleep(1)

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
