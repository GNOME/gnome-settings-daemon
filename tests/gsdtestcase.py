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
import signal

from output_checker import OutputChecker

from gi.repository import GLib

try:
    import dbusmock
except ImportError:
    sys.stderr.write('You need python-dbusmock (http://pypi.python.org/pypi/python-dbusmock) for this test suite.\n')
    sys.exit(77)

from dbusmock import DBusTestCase

try:
    from gi.repository import Gio
except ImportError:
    sys.stderr.write('You need pygobject and the Gio GIR for this test suite.\n')
    sys.exit(77)

if subprocess.call(['which', 'gnome-session'], stdout=subprocess.DEVNULL) != 0:
    sys.stderr.write('You need gnome-session for this test suite.\n')
    sys.exit(77)


top_builddir = os.environ.get('TOP_BUILDDIR',
                              os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def set_nonblock(fd):
    '''Set a file object to non-blocking'''

    flags = fcntl.fcntl(fd, fcntl.F_GETFL)
    fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)


class GSDTestCase(DBusTestCase):
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
        klass.addClassCleanup(shutil.rmtree, klass.workdir)

        # X11 display tracking
        klass.display_name_fifo = None
        klass.x11_display = None

        # Prevent applications from accessing an outside session manager
        os.environ['SESSION_MANAGER'] = ''

        os.environ['XDG_SESSION_TYPE'] = 'wayland'
        os.environ['G_MESSAGES_DEBUG'] = 'all'

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

        # Starts dbus busses
        DBusTestCase.setUpClass()
        klass.start_system_bus()
        klass.start_session_bus()

        # Make dconf discoverable (requires newer dbusmock API, is not needed otherwise)
        if hasattr(klass, 'enable_service'):
            klass.enable_service('ca.desrt.dconf')

        klass.system_bus_con = klass.get_dbus(True)
        klass.session_bus_con = klass.get_dbus(False)
        klass.addClassCleanup(klass.system_bus_con.close)
        klass.addClassCleanup(klass.session_bus_con.close)

        # we never want to cause notifications on the actual GUI
        klass.p_notify_log = OutputChecker()
        klass.p_notify = klass.spawn_server_template(
            'notification_daemon', {}, stdout=klass.p_notify_log.fd)[0]
        klass.p_notify_log.writer_attached()
        klass.addClassCleanup(klass.stop_process, klass.p_notify)

        klass.configure_session()
        klass.start_monitor()
        klass.addClassCleanup(klass.stop_monitor)

        # Reset between tests in tearDown
        klass.settings_session = Gio.Settings(schema_id='org.gnome.desktop.session')

        # Make sure we get a backtrace when meson kills after a timeout
        def r(*args):
            raise KeyboardInterrupt()
        signal.signal(signal.SIGTERM, r)

    @classmethod
    def tearDownClass(klass):
        #if hasattr(klass, 'disable_service'):
        #    klass.disable_service('ca.desrt.dconf')

        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        DBusTestCase.tearDownClass()
        signal.signal(signal.SIGTERM, signal.SIG_DFL)

    def setUp(self):
        self.daemon_death_expected = False

    def tearDown(self):
        # we check this at the end so that the other cleanup always happens
        daemon_running = self.daemon.poll() == None
        self.assertTrue(daemon_running or self.daemon_death_expected, 'daemon died during the test')

        self.reset_settings(self.settings_session)

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
    def configure_session(klass):
        '''Configure minimal GNOME session'''

        # create dummy session type and component
        d = os.path.join(klass.workdir, 'config', 'gnome-session', 'sessions')
        if not os.path.isdir(d):
            os.makedirs(d)
        shutil.copy(os.path.join(os.path.dirname(__file__), 'dummy.session'), d)

        d = os.path.join(klass.workdir, 'data', 'applications')
        if not os.path.isdir(d):
            os.makedirs(d)
        shutil.copy(os.path.join(os.path.dirname(__file__), 'dummyapp.desktop'), d)

    def start_session(self):
        self.session_log = OutputChecker()
        self.session = subprocess.Popen(['gnome-session', '-f',
                                         '-a', os.path.join(self.workdir, 'autostart'),
                                         '--session=dummy', '--debug'],
                                        stdout=self.session_log.fd,
                                        stderr=subprocess.STDOUT)
        self.session_log.writer_attached()

        # wait until the daemon is on the bus
        self.wait_for_bus_object('org.gnome.SessionManager',
                                 '/org/gnome/SessionManager',
                                 timeout=100)

        self.session_log.check_line(b'fill: *** Done adding required components')

    def stop_session(self):
        '''Stop GNOME session'''

        assert self.session
        self.stop_process(self.session)
        # dummyapp.desktop survives the session. This keeps the FD open in the
        # CI environment when gnome-session fails to redirect the child output
        # to journald.
        # Though, gnome-session should probably kill the child anyway.
        #self.session_log.assert_closed()
        self.session_log.force_close()

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
        klass.stop_process(klass.monitor)

        klass.monitor_log.flush()
        klass.monitor_log.close()

    def start_logind(self, parameters=None):
        '''start mock logind'''

        if parameters is None:
            parameters = {}
        self.logind_log = OutputChecker()
        self.logind, self.logind_obj = self.spawn_server_template('logind',
                                                                  parameters,
                                                                  stdout=self.logind_log.fd)
        self.logind_log.writer_attached()

        # Monkey patch SuspendThenHibernate functions in for dbusmock <= 0.17.2
        # This should be removed once we can depend on dbusmock 0.17.3
        self.logind_obj.AddMethod('org.freedesktop.login1.Manager', 'SuspendThenHibernate', 'b', '', '')
        self.logind_obj.AddMethod('org.freedesktop.login1.Manager', 'CanSuspendThenHibernate', '', 's', 'ret = "%s"' % parameters.get('CanSuspendThenHibernate', 'yes'))

        self.logind_obj.AddMethod('org.freedesktop.login1.Session', 'SetBrightness', 'ssu', '', '')

    def stop_logind(self):
        '''stop mock logind'''

        self.stop_process(self.logind)
        self.logind_log.assert_closed()

    @classmethod
    def start_mutter(klass, needs_x11=False):
        ''' start mutter '''

        if needs_x11:
            tempdir = tempfile.mkdtemp()
            display_name_fifo = os.path.join(tempdir, 'display-name')
            os.mkfifo(display_name_fifo)

            extra_mutter_args = [
                '--',
                os.path.join(project_root, 'tests', 'get-display-name.sh'),
                display_name_fifo,
            ]
        else:
            extra_mutter_args = []

        os.environ['MUTTER_DEBUG_RESET_IDLETIME']='1'
        klass.mutter = subprocess.Popen(['mutter', '--headless',
                                         '--virtual-monitor', '800x600'] +
                                        extra_mutter_args)

        if needs_x11:
            with open(display_name_fifo) as f:
                klass.display_name_fifo = display_name_fifo
                klass.x11_display = f.readline().strip()
                klass.xauth = f.readline().strip()
                print("Using X11 display %s" % (klass.x11_display))

        klass.wait_for_bus_object('org.gnome.Mutter.IdleMonitor',
                                 '/org/gnome/Mutter/IdleMonitor/Core',
                                 timeout=100)

    @classmethod
    def stop_mutter(klass):
        '''stop mutter'''

        assert klass.monitor
        if klass.display_name_fifo:
            with open(klass.display_name_fifo, 'w') as f:
                f.write('\n')
            os.remove(klass.display_name_fifo)
            os.rmdir(os.path.dirname(klass.display_name_fifo))
        klass.stop_process(klass.mutter, timeout=2)

    def start_plugin(self, env):
        self.plugin_death_expected = False

        # We need to redirect stdout to grab the debug messages.
        # stderr is not needed by the testing infrastructure but is useful to
        # see warnings and errors.
        self.plugin_log = OutputChecker()
        if self.__class__.x11_display:
            klass = self.__class__

            extra_env = {
                'GNOME_SETUP_DISPLAY': klass.x11_display,
                'XAUTHORITY': klass.xauth,
            }
        else:
            extra_env = {}

        self.daemon = subprocess.Popen(
            [os.path.join(top_builddir, 'plugins', self.gsd_plugin, 'gsd-' + self.gsd_plugin), '--verbose'],
            stdout=self.plugin_log.fd,
            stderr=subprocess.STDOUT,
            env=env | extra_env)
        self.plugin_log.writer_attached()


        bus = self.get_dbus(False)

        timeout = 100
        while timeout > 0:
            if bus.name_has_owner('org.gnome.SettingsDaemon.' + self.gsd_plugin_case):
                break

            timeout -= 1
            time.sleep(0.1)
        if timeout <= 0:
            assert timeout > 0, 'timed out waiting for plugin startup: %s' % (self.gsd_plugin_case)

    def stop_plugin(self):
        daemon_running = self.daemon.poll() == None
        if daemon_running:
            self.stop_process(self.daemon)
        self.plugin_log.assert_closed()

    def reset_settings(self, schema):
        # reset all changed gsettings, so that tests are independent from each
        # other
        for k in schema.list_keys():
            schema.reset(k)
        Gio.Settings.sync()

    @classmethod
    def stop_process(cls, proc, timeout=1):
        proc.terminate()
        try:
            proc.wait(timeout)
        except:
            print("Killing %d (%s) after timeout of %f seconds" % (proc.pid, proc.args[0], timeout))
            proc.kill()
            proc.wait()

    @classmethod
    def reset_idle_timer(klass):
        '''trigger activity to reset idle timer'''

        obj_mutter_idlemonitor = klass.session_bus_con.get_object(
            'org.gnome.Mutter.IdleMonitor', '/org/gnome/Mutter/IdleMonitor/Core')

        obj_mutter_idlemonitor.ResetIdletime(dbus_interface='org.gnome.Mutter.IdleMonitor')
