/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2007 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <time.h>

#include "egg-debug.h"
#include "egg-dbus-proxy.h"

#include "hal-marshal.h"
#include "hal-device-power.h"
#include "hal-device.h"
#include "hal-manager.h"

static void     hal_device_power_class_init (HalDevicePowerClass *klass);
static void     hal_device_power_init       (HalDevicePower      *power);
static void     hal_device_power_finalize   (GObject	      *object);

#define HAL_DEVICE_POWER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), HAL_TYPE_DEVICE_POWER, HalDevicePowerPrivate))

struct HalDevicePowerPrivate
{
	HalDevice		*computer;
	EggDbusProxy		*gproxy;
};

static gpointer hal_device_power_object = NULL;
G_DEFINE_TYPE (HalDevicePower, hal_device_power, G_TYPE_OBJECT)

/**
 * hal_device_power_class_init:
 * @klass: This class instance
 **/
static void
hal_device_power_class_init (HalDevicePowerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hal_device_power_finalize;
	g_type_class_add_private (klass, sizeof (HalDevicePowerPrivate));
}

/**
 * hal_device_power_init:
 *
 * @power: This class instance
 **/
static void
hal_device_power_init (HalDevicePower *power)
{
	DBusGConnection *connection;
	power->priv = HAL_DEVICE_POWER_GET_PRIVATE (power);

	/* get the power connection */
	power->priv->gproxy = egg_dbus_proxy_new ();
	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	egg_dbus_proxy_assign (power->priv->gproxy, connection, HAL_DBUS_SERVICE,
			       HAL_ROOT_COMPUTER, HAL_DBUS_INTERFACE_POWER);
	if (power->priv->gproxy == NULL)
		egg_warning ("HAL does not support power management!");

	power->priv->computer = hal_device_new ();
	hal_device_set_udi (power->priv->computer, HAL_ROOT_COMPUTER);
}

/**
 * hal_device_power_is_laptop:
 *
 * @power: This class instance
 * Return value: TRUE is computer is identified as a laptop
 *
 * Returns true if system.formfactor is "laptop"
 **/
gboolean
hal_device_power_is_laptop (HalDevicePower *power)
{
	gboolean ret = TRUE;
	gchar *formfactor = NULL;

	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	/* always present */
	hal_device_get_string (power->priv->computer, "system.formfactor", &formfactor, NULL);
	if (formfactor == NULL) {
		/* no need to free */
		return FALSE;
	}
	if (strcmp (formfactor, "laptop") != 0) {
		egg_debug ("This machine is not identified as a laptop."
			   "system.formfactor is %s.", formfactor);
		ret = FALSE;
	}
	g_free (formfactor);
	return ret;
}

/**
 * hal_device_power_has_support:
 *
 * @power: This class instance
 * Return value: TRUE if haldaemon has power management capability
 *
 * Finds out if power management functions are running (only ACPI, PMU, APM)
 **/
gboolean
hal_device_power_has_support (HalDevicePower *power)
{
	gchar *type = NULL;

	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	hal_device_get_string (power->priv->computer, "power_management.type", &type, NULL);
	/* this key only has to exist to be pm okay */
	if (type != NULL) {
		g_free (type);
		return TRUE;
	}
	return FALSE;
}

/**
 * hal_device_power_can_suspend:
 *
 * @power: This class instance
 * Return value: TRUE if kernel suspend support is compiled in
 *
 * Finds out if HAL indicates that we can suspend
 **/
gboolean
hal_device_power_can_suspend (HalDevicePower *power)
{
	gboolean exists;
	gboolean can_suspend;

	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	/* TODO: Change to can_suspend when rely on newer HAL */
	exists = hal_device_get_bool (power->priv->computer,
					  "power_management.can_suspend",
					  &can_suspend, NULL);
	if (exists == FALSE) {
		egg_warning ("Key can_suspend missing");
		return FALSE;
	}
	return can_suspend;
}

/**
 * hal_device_power_can_hibernate:
 *
 * @power: This class instance
 * Return value: TRUE if kernel hibernation support is compiled in
 *
 * Finds out if HAL indicates that we can hibernate
 **/
gboolean
hal_device_power_can_hibernate (HalDevicePower *power)
{
	gboolean exists;
	gboolean can_hibernate;

	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	/* TODO: Change to can_hibernate when rely on newer HAL */
	exists = hal_device_get_bool (power->priv->computer,
					  "power_management.can_hibernate",
					  &can_hibernate, NULL);
	if (exists == FALSE) {
		egg_warning ("Key can_hibernate missing");
		return FALSE;
	}
	return can_hibernate;
}

/**
 * hal_device_power_filter_error:
 *
 * We have to ignore dbus timeouts sometimes
 **/
static gboolean
hal_device_power_filter_error (GError **error)
{
	/* short cut for speed, no error */
	if (error == NULL || *error == NULL)
		return FALSE;

	/* DBUS might time out, which is okay. We can remove this code
	   when the dbus glib bindings are fixed. See #332888 */
	if (g_error_matches (*error, DBUS_GERROR, DBUS_GERROR_NO_REPLY)) {
		egg_warning ("DBUS timed out, but recovering");
		g_error_free (*error);
		*error = NULL;
		return TRUE;
	}
	egg_warning ("Method failed\n(%s)",  (*error)->message);
	return FALSE;
}

/**
 * hal_device_power_suspend:
 *
 * @power: This class instance
 * @wakeup: Seconds to wakeup, currently unsupported
 * Return value: Success, true if we suspended OK
 *
 * Uses org.freedesktop.Hal.Device.SystemPowerManagement.Suspend ()
 **/
gboolean
hal_device_power_suspend (HalDevicePower *power, guint wakeup, GError **error)
{
	time_t start;
	time_t end;
	gint retval = 0;
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	proxy = egg_dbus_proxy_get_proxy (power->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}

	time (&start);
	ret = dbus_g_proxy_call (proxy, "Suspend", error,
				 G_TYPE_INT, wakeup,
				 G_TYPE_INVALID,
				 G_TYPE_INT, &retval,
				 G_TYPE_INVALID);
	/* we might have to ignore the error */
	if (error != NULL && hal_device_power_filter_error (error))
		return TRUE;
	if (retval != 0)
		egg_warning ("Suspend failed without error message");

	/* compare the amount of time that has passed - if it's more than 6 hours
	 * then the dbus call timed out (dbus-pending-call.c) */
	if (ret != 0) {
		time (&end);
		if (difftime (start, end) >= 6*60*60*1000)
			return TRUE;
	}

	return ret;
}

/**
 * hal_device_power_pm_method_void:
 *
 * @power: This class instance
 * @method: The method name, e.g. "Hibernate"
 * Return value: Success, true if we did OK
 *
 * Do a method on org.freedesktop.Hal.Device.SystemPowerManagement.*
 * with no arguments.
 **/
static gboolean
hal_device_power_pm_method_void (HalDevicePower *power, const gchar *method, GError **error)
{
	time_t start;
	time_t end;
	guint retval = 0;
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);
	g_return_val_if_fail (method != NULL, FALSE);

	proxy = egg_dbus_proxy_get_proxy (power->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("not connected");
		return FALSE;
	}

	time (&start);
	ret = dbus_g_proxy_call (proxy, method, error,
				 G_TYPE_INVALID,
				 G_TYPE_INT, &retval,
				 G_TYPE_INVALID);
	/* we might have to ignore the error */
	if (error != NULL && hal_device_power_filter_error (error))
		return TRUE;
	if (retval != 0)
		egg_warning ("%s failed in a horrible way!", method);

	/* compare the amount of time that has passed - if it's more than 6 hours
	 * then the dbus call timed out (dbus-pending-call.c) */
	if (ret != 0) {
		time (&end);
		if (difftime (start,end) >= 6*60*60*1000)
			return TRUE;
	}

	return ret;
}

/**
 * hal_device_power_hibernate:
 *
 * @power: This class instance
 * Return value: Success, true if we hibernated OK
 *
 * Uses org.freedesktop.Hal.Device.SystemPowerManagement.Hibernate ()
 **/
gboolean
hal_device_power_hibernate (HalDevicePower *power, GError **error)
{
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);
	return hal_device_power_pm_method_void (power, "Hibernate", error);
}

/**
 * hal_device_power_shutdown:
 *
 * Return value: Success, true if we shutdown OK
 *
 * Uses org.freedesktop.Hal.Device.SystemPowerManagement.Shutdown ()
 **/
gboolean
hal_device_power_shutdown (HalDevicePower *power, GError **error)
{
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);
	return hal_device_power_pm_method_void (power, "Shutdown", error);
}

/**
 * hal_device_power_reboot:
 *
 * @power: This class instance
 * Return value: Success, true if we shutdown OK
 *
 * Uses org.freedesktop.Hal.Device.SystemPowerManagement.Reboot ()
 **/
gboolean
hal_device_power_reboot (HalDevicePower *power, GError **error)
{
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);
	return hal_device_power_pm_method_void (power, "Reboot", error);
}

/**
 * hal_device_power_enable_power_save:
 *
 * @power: This class instance
 * @enable: True to enable low power mode
 * Return value: Success, true if we set the mode
 *
 * Uses org.freedesktop.Hal.Device.SystemPowerManagement.SetPowerSave ()
 **/
gboolean
hal_device_power_enable_power_save (HalDevicePower *power, gboolean enable)
{
	gint retval = 0;
	GError *error = NULL;
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (power != NULL, FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	proxy = egg_dbus_proxy_get_proxy (power->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}

	/* abort if we are not a "qualified" laptop */
	if (hal_device_power_is_laptop (power) == FALSE) {
		egg_debug ("We are not a laptop, so not even trying");
		return FALSE;
	}

	ret = dbus_g_proxy_call (proxy, "SetPowerSave", &error,
				 G_TYPE_BOOLEAN, enable,
				 G_TYPE_INVALID,
				 G_TYPE_INT, &retval,
				 G_TYPE_INVALID);
	if (retval != 0)
		egg_warning ("SetPowerSave failed in a horrible way!");
	return ret;
}

/**
 * hal_device_power_has_suspend_error:
 *
 * @power: This class instance
 * @enable: Return true if there was a suspend error
 * Return value: Success
 *
 * TODO: should call a method on HAL and also return the ouput of the file
 **/
gboolean
hal_device_power_has_suspend_error (HalDevicePower *power, gboolean *state)
{
	g_return_val_if_fail (power != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);
	*state = g_file_test ("/var/lib/hal/system-power-suspend-output", G_FILE_TEST_EXISTS);
	return TRUE;
}

/**
 * hal_device_power_has_hibernate_error:
 *
 * @power: This class instance
 * @enable: Return true if there was a hibernate error
 * Return value: Success
 *
 * TODO: should call a method on HAL and also return the ouput of the file
 **/
gboolean
hal_device_power_has_hibernate_error (HalDevicePower *power, gboolean *state)
{
	g_return_val_if_fail (power != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);
	*state = g_file_test ("/var/lib/hal/system-power-hibernate-output", G_FILE_TEST_EXISTS);
	return TRUE;
}

/**
 * hal_device_power_clear_suspend_error:
 *
 * @power: This class instance
 * Return value: Success
 *
 * Tells HAL to try and clear the suspend error as we appear to be okay
 **/
gboolean
hal_device_power_clear_suspend_error (HalDevicePower *power, GError **error)
{
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (power != NULL, FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	proxy = egg_dbus_proxy_get_proxy (power->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}

	ret = dbus_g_proxy_call (proxy, "SuspendClearError", error,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * hal_device_power_clear_hibernate_error:
 *
 * @power: This class instance
 * Return value: Success
 *
 * Tells HAL to try and clear the hibernate error as we appear to be okay
 **/
gboolean
hal_device_power_clear_hibernate_error (HalDevicePower *power, GError **error)
{
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (power != NULL, FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE_POWER (power), FALSE);

	proxy = egg_dbus_proxy_get_proxy (power->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}

	ret = dbus_g_proxy_call (proxy, "HibernateClearError", error,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	return ret;
}

/**
 * hal_device_power_finalize:
 * @object: This class instance
 **/
static void
hal_device_power_finalize (GObject *object)
{
	HalDevicePower *power;
	g_return_if_fail (object != NULL);
	g_return_if_fail (HAL_IS_DEVICE_POWER (object));

	power = HAL_DEVICE_POWER (object);
	power->priv = HAL_DEVICE_POWER_GET_PRIVATE (power);

	g_object_unref (power->priv->gproxy);
	g_object_unref (power->priv->computer);

	G_OBJECT_CLASS (hal_device_power_parent_class)->finalize (object);
}

/**
 * hal_device_power_new:
 * Return value: new HalDevicePower instance.
 **/
HalDevicePower *
hal_device_power_new (void)
{
	if (hal_device_power_object != NULL) {
		g_object_ref (hal_device_power_object);
	} else {
		hal_device_power_object = g_object_new (HAL_TYPE_DEVICE_POWER, NULL);
		g_object_add_weak_pointer (hal_device_power_object, &hal_device_power_object);
	}
	return HAL_DEVICE_POWER (hal_device_power_object);
}

