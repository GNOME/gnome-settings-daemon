'''mutter proxy mock template
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


BUS_NAME = 'org.gnome.Mutter.DisplayConfig'
MAIN_OBJ = '/org/gnome/Mutter/DisplayConfig'
MAIN_IFACE = 'org.gnome.Mutter.DisplayConfig'
MOCK_IFACE = 'org.gnome.Mutter.DisplayConfig.Mock'
SYSTEM_BUS = False


def _wrap_in_dbus_variant(value):
    dbus_types = [
        dbus.types.ByteArray,
        dbus.types.Int16,
        dbus.types.ObjectPath,
        dbus.types.Struct,
        dbus.types.UInt64,
        dbus.types.Boolean,
        dbus.types.Dictionary,
        dbus.types.Int32,
        dbus.types.Signature,
        dbus.types.UInt16,
        dbus.types.UnixFd,
        dbus.types.Byte,
        dbus.types.Double,
        dbus.types.Int64,
        dbus.types.String,
        dbus.types.UInt32,
    ]
    if isinstance(value, dbus.String):
        return dbus.String(str(value), variant_level=1)
    if isinstance(value, dbus.types.Array) or isinstance(value, dbus.types.Struct):
        return value
    if type(value) in dbus_types:
        return type(value)(value.conjugate(), variant_level=1)
    if isinstance(value, str):
        return dbus.String(value, variant_level=1)
    raise dbus.exceptions.DBusException(f"could not wrap type {type(value)}")


# this is a fixed up version of the dbusmock method which works for Struct types
def UpdateProperties(mock, interface, properties):
    changed_props = {}

    for name, value in properties.items():
        if not interface:
            interface = mock.interface
        if name not in mock.props.get(interface, {}):
            raise dbus.exceptions.DBusException(f"property {name} not found", name=interface + ".NoSuchProperty")

        mock._set_property(interface, name, value)
        changed_props[name] = _wrap_in_dbus_variant(value)

    mock.EmitSignal(dbus.PROPERTIES_IFACE, "PropertiesChanged", "sa{sv}as", [interface, changed_props, []])


def load(mock, parameters):
    mock.serial = 1
    mock.backlights = {}

    mock.AddProperty(MAIN_IFACE, 'Backlight', backlights_to_dbus({}))
    mock.AddProperty(MAIN_IFACE, 'PowerSaveMode', dbus.Int32(0))
    mock.AddProperty(MAIN_IFACE, 'HasExternalMonitor', True)


def backlights_to_dbus(backlights):
    out = dbus.Array([], signature='a{sv}')
    for connector, backlight in backlights.items():
        b = dbus.Dictionary(backlight.copy(), signature='sv')
        b['connector'] = dbus.String(connector)
        out.append(b)
    return out


@dbus.service.method(
    MAIN_IFACE,
    in_signature='usi',
    out_signature=''
)
def SetBacklight(self, serial, connector, value):
    if serial != self.serial:
        raise dbus.exceptions.DBusException("Invalid backlight serial")

    if not connector in self.backlights:
        raise dbus.exceptions.DBusException("Unknown backlight")

    backlight = self.backlights[connector]

    if value < backlight['min'] or value > backlight['max']:
        raise dbus.exceptions.DBusException("Invalid backlight value")

    backlight['value'] = value

    UpdateProperties(self, MAIN_IFACE, {
        'Backlight': dbus.Struct(
            (dbus.UInt32(self.serial), backlights_to_dbus(self.backlights)),
            signature='uaa{sv}',
            variant_level=1
        ),
    })


@dbus.service.method(
    MOCK_IFACE,
    in_signature='sbiii',
    out_signature='',
)
def MockSetBacklight(self, connector, active, min, max, value):
    self.backlights[connector] = {
        'active': dbus.Boolean(active),
        'min': dbus.Int32(min),
        'max': dbus.Int32(max),
        'value': dbus.Int32(value),
    }

    self.serial += 1
    UpdateProperties(self, MAIN_IFACE, {
        'Backlight': dbus.Struct(
            (dbus.UInt32(self.serial), backlights_to_dbus(self.backlights)),
            signature='uaa{sv}',
            variant_level=1
        ),
    })
