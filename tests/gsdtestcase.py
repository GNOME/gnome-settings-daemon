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

try:
    import dbusmock
except ImportError:
    sys.stderr.write('You need python-dbusmock (http://pypi.python.org/pypi/python-dbusmock) for this test suite.\n')
    sys.exit(0)

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


class GSDTestCase(dbusmock.DBusTestCase):
    '''Base class for settings daemon tests

    This redirects the XDG directories to temporary directories, and runs local
    session and system D-BUSes with a minimal GNOME session and a mock
    notification daemon. It also provides common functionality for plugin
    tests.
    '''
    @classmethod
    def setUpClass(klass):
        os.environ['GIO_USE_VFS'] = 'local'
        # we do some string checks, disable translations
        os.environ['LC_MESSAGES'] = 'C'
        klass.workdir = tempfile.mkdtemp(prefix='gsd-power-test')

        # tell dconf and friends to use our config/runtime directories
        os.environ['XDG_CONFIG_HOME'] = os.path.join(klass.workdir, 'config')
        os.environ['XDG_DATA_HOME'] = os.path.join(klass.workdir, 'data')
        os.environ['XDG_RUNTIME_DIR'] = os.path.join(klass.workdir, 'runtime')

        # work around https://bugzilla.gnome.org/show_bug.cgi?id=689136
        os.makedirs(os.path.join(os.environ['XDG_CONFIG_HOME'], 'dconf'))

        klass.start_system_bus()
        klass.start_session_bus()
        klass.system_bus_con = klass.get_dbus(True)
        klass.session_bus_con = klass.get_dbus(False)

        # we never want to cause notifications on the actual GUI
        klass.p_notify = klass.spawn_server_template(
            'notification_daemon', {}, stdout=subprocess.PIPE)[0]
        set_nonblock(klass.p_notify.stdout)

        klass.start_session()

        klass.settings_session = Gio.Settings('org.gnome.desktop.session')

    @classmethod
    def tearDownClass(klass):
        klass.stop_session()
        dbusmock.DBusTestCase.tearDownClass()
        shutil.rmtree(klass.workdir)

    def run(self, result=None):
        '''Show log files on failed tests

        If the environment variable $SHELL_ON_FAIL is set, this runs bash in
        the work directory; exit the shell to continue the tests. Otherwise it
        shows all log files.
        '''
        if result:
            orig_err_fail = result.errors + result.failures
        super(GSDTestCase, self).run(result)
        if result and result.errors + result.failures > orig_err_fail:
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

        klass.session_log = open(os.path.join(klass.workdir, 'gnome-session.log'), 'wb')
        klass.session = subprocess.Popen(['gnome-session', '-f',
                                          '-a', os.path.join(klass.workdir, 'autostart'),
                                          '--session=dummy', '--debug'],
                                         stdout=klass.session_log,
                                         stderr=subprocess.STDOUT)

        # wait until the daemon is on the bus
        try:
            klass.wait_for_bus_object('org.gnome.SessionManager',
                                      '/org/gnome/SessionManager')
        except:
            # on failure, print log
            with open(klass.session_log.name) as f:
                print('----- session log -----\n%s\n------' % f.read())
            raise

        klass.obj_session_mgr = klass.session_bus_con.get_object(
            'org.gnome.SessionManager', '/org/gnome/SessionManager')

        # give g-session some time to lazily initialize idle timers, etc.
        time.sleep(3)

    @classmethod
    def stop_session(klass):
        '''Stop GNOME session'''

        assert klass.session
        klass.session.terminate()
        klass.session.wait()

        klass.session_log.flush()
        klass.session_log.close()

    def start_logind(self):
        '''start mock logind'''

        self.logind = self.spawn_server('org.freedesktop.login1',
                                        '/org/freedesktop/login1',
                                        'org.freedesktop.login1.Manager',
                                        system_bus=True,
                                        stdout=subprocess.PIPE)
        self.obj_logind = self.system_bus_con.get_object(
            'org.freedesktop.login1', '/org/freedesktop/login1')

        self.obj_logind.AddMethods('',
            [
                ('PowerOff', 'b', '', ''),
                ('Suspend', 'b', '', ''),
                ('Hibernate', 'b', '', ''),
                ('Inhibit', 'ssss', 'h', 'ret = 5'),
            ], dbus_interface='org.freedesktop.DBus.Mock')

        # set log to nonblocking
        set_nonblock(self.logind.stdout)

    def stop_logind(self):
        '''stop mock logind'''

        self.logind.terminate()
        self.logind.wait()

    @classmethod
    def reset_idle_timer(klass):
        '''trigger activity to reset idle timer'''

        subprocess.check_call([os.path.join(top_builddir, 'tests', 'shiftkey')])
