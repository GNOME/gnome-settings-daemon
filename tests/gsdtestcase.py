'''GNOME settings daemon test base class'''

__author__ = 'Martin Pitt <martin.pitt@ubuntu.com>'
__copyright__ = '(C) 2013 Canonical Ltd.'
__license__ = 'GPL v2 or later'

import subprocess
import time
import os
import os.path
import tempfile
import fcntl
import shutil
import sys
from glob import glob

from gi.repository import GLib

try:
    import dbusmock
except ImportError:
    sys.stderr.write('You need python-dbusmock (http://pypi.python.org/pypi/python-dbusmock) for this test suite.\n')
    sys.exit(1)

from x11session import X11SessionTestCase

try:
    from gi.repository import Gio
except ImportError:
    sys.stderr.write('You need pygobject and the Gio GIR for this test suite.\n')
    sys.exit(0)

if subprocess.call(['which', 'gnome-session'], stdout=subprocess.PIPE) != 0:
    sys.stderr.write('You need gnome-session for this test suite.\n')
    sys.exit(0)


top_builddir = os.environ.get('TOP_BUILDDIR',
                              os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def set_nonblock(fd):
    '''Set a file object to non-blocking'''

    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)


class GSDTestCase(X11SessionTestCase):
    '''Base class for settings daemon tests

    This redirects the XDG directories to temporary directories, and runs local
    session and system D-BUSes with a minimal GNOME session and a mock
    notification daemon. It also provides common functionality for plugin
    tests.
    '''
    @classmethod
    def setUpClass(klass):
        os.environ['GIO_USE_VFS'] = 'local'
        os.environ['GVFS_DISABLE_FUSE'] = '1'
        # we do some string checks, disable translations
        os.environ['LC_MESSAGES'] = 'C'
        klass.workdir = tempfile.mkdtemp(prefix='gsd-plugin-test')

        # Prevent applications from accessing an outside session manager
        os.environ['SESSION_MANAGER'] = ''

        # Signal to mutter and gnome-session that we are using X11
        os.environ['XDG_SESSION_TYPE'] = 'x11'

        # tell dconf and friends to use our config/runtime directories
        os.environ['XDG_CONFIG_HOME'] = os.path.join(klass.workdir, 'config')
        os.environ['XDG_DATA_HOME'] = os.path.join(klass.workdir, 'data')
        os.environ['XDG_RUNTIME_DIR'] = os.path.join(klass.workdir, 'runtime')

        # Copy gschema file into XDG_DATA_HOME
        gschema_dir = os.path.join(os.environ['XDG_DATA_HOME'], 'glib-2.0', 'schemas')
        os.makedirs(gschema_dir)
        shutil.copy(os.path.join(top_builddir, 'data', 'gschemas.compiled'), gschema_dir)

        # work around https://bugzilla.gnome.org/show_bug.cgi?id=689136
        os.makedirs(os.path.join(os.environ['XDG_CONFIG_HOME'], 'dconf'))
        os.makedirs(os.environ['XDG_RUNTIME_DIR'], mode=0o700)

        # Starts Xvfb and dbus busses
        X11SessionTestCase.setUpClass()

        klass.system_bus_con = klass.get_dbus(True)
        klass.session_bus_con = klass.get_dbus(False)

        # we never want to cause notifications on the actual GUI
        klass.p_notify = klass.spawn_server_template(
            'notification_daemon', {}, stdout=subprocess.PIPE)[0]
        set_nonblock(klass.p_notify.stdout)

        klass.start_session()
        klass.start_monitor()

        klass.settings_session = Gio.Settings(schema_id='org.gnome.desktop.session')

    @classmethod
    def tearDownClass(klass):
        klass.p_notify.terminate()
        klass.p_notify.wait()
        klass.stop_monitor()
        X11SessionTestCase.tearDownClass()
        shutil.rmtree(klass.workdir)

    def run(self, result=None):
        '''Show log files on failed tests

        If the environment variable $SHELL_ON_FAIL is set, this runs bash in
        the work directory; exit the shell to continue the tests. Otherwise it
        shows all log files.
        '''
        if result:
            orig_err_fail = len(result.errors) + len(result.failures)
        super(GSDTestCase, self).run(result)
        if result and len(result.errors) + len(result.failures) > orig_err_fail:
            if 'SHELL_ON_FAIL' in os.environ:
                subprocess.call(['bash', '-i'], cwd=self.workdir)
            else:
                for log_file in glob(os.path.join(self.workdir, '*.log')):
                    with open(log_file) as f:
                        print('\n----- %s -----\n%s\n------\n'
                              % (log_file, f.read()))

    @classmethod
    def start_session(klass):
        '''Start minimal GNOME session'''

        # create dummy session type and component
        d = os.path.join(klass.workdir, 'config', 'gnome-session', 'sessions')
        if not os.path.isdir(d):
            os.makedirs(d)
        shutil.copy(os.path.join(os.path.dirname(__file__), 'dummy.session'), d)

        d = os.path.join(klass.workdir, 'data', 'applications')
        if not os.path.isdir(d):
            os.makedirs(d)
        shutil.copy(os.path.join(os.path.dirname(__file__), 'dummyapp.desktop'), d)

    @classmethod
    def start_monitor(klass):
        '''Start dbus-monitor'''

        # You can rename the log file to *.log if you want to see it on test
        # case failures
        klass.monitor_log = open(os.path.join(klass.workdir, 'dbus-monitor.out'), 'wb', buffering=0)
        klass.monitor = subprocess.Popen(['dbus-monitor', '--monitor'],
                                         stdout=klass.monitor_log,
                                         stderr=subprocess.STDOUT)

    @classmethod
    def stop_monitor(klass):
        '''Stop dbus-monitor'''

        assert klass.monitor
        klass.monitor.terminate()
        klass.monitor.wait()

        klass.monitor_log.flush()
        klass.monitor_log.close()

    def start_logind(self, parameters=None):
        '''start mock logind'''

        if parameters is None:
            parameters = {}
        self.logind, self.logind_obj = self.spawn_server_template('logind',
                                                                  parameters,
                                                                  stdout=subprocess.PIPE)

        # Monkey patch SuspendThenHibernate functions in for dbusmock <= 0.17.2
        # This should be removed once we can depend on dbusmock 0.17.3
        self.logind_obj.AddMethod('org.freedesktop.login1.Manager', 'SuspendThenHibernate', 'b', '', '')
        self.logind_obj.AddMethod('org.freedesktop.login1.Manager', 'CanSuspendThenHibernate', '', 's', 'ret = "%s"' % parameters.get('CanSuspendThenHibernate', 'yes'))

        self.logind_obj.AddMethod('org.freedesktop.login1.Session', 'SetBrightness', 'ssu', '', '')

        # set log to nonblocking
        set_nonblock(self.logind.stdout)

    def stop_logind(self):
        '''stop mock logind'''

        self.logind.terminate()
        self.logind.wait()

    def start_mutter(klass):
        ''' start mutter '''

        os.environ['MUTTER_DEBUG_RESET_IDLETIME']='1'
        klass.mutter_log = open(os.path.join(klass.workdir, 'mutter.log'), 'wb', buffering=0)
        # See https://gitlab.gnome.org/GNOME/mutter/merge_requests/15
        klass.mutter = subprocess.Popen(['mutter', '--x11'],
                                         stdout=klass.mutter_log,
                                         stderr=subprocess.STDOUT)

    def stop_mutter(klass):
        '''stop mutter'''

        assert klass.monitor
        klass.mutter.terminate()
        klass.mutter.wait()

        klass.mutter_log.flush()
        klass.mutter_log.close()

    @classmethod
    def reset_idle_timer(klass):
        '''trigger activity to reset idle timer'''

        obj_mutter_idlemonitor = klass.session_bus_con.get_object(
            'org.gnome.Mutter.IdleMonitor', '/org/gnome/Mutter/IdleMonitor/Core')

        obj_mutter_idlemonitor.ResetIdletime(dbus_interface='org.gnome.Mutter.IdleMonitor')
