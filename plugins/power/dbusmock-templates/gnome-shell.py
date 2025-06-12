'''gnome-shell proxy mock template
'''

# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU Lesser General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option) any
# later version.  See http://www.gnu.org/copyleft/lgpl.html for the full text
# of the license.


__author__ = 'Sebastian Wick'
__copyright__ = '(c) 2025 Red Hat Inc.'


import dbus
from dbusmock import MOCK_IFACE, mockobject


BUS_NAME = 'org.gnome.Shell.Brightness'
MAIN_OBJ = '/org/gnome/Shell/Brightness'
MAIN_IFACE = 'org.gnome.Shell.Brightness'
SYSTEM_BUS = False


def load(mock, parameters):
    mock.AddMethod(MAIN_IFACE, 'SetDimming', 'b', '', '');
    mock.AddMethod(MAIN_IFACE, 'SetAutoBrightnessTarget', 'd', '', '');

    mock.AddProperty(MAIN_IFACE, 'HasBrightnessControl', True)
