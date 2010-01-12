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

#include "egg-debug.h"
#include "egg-dbus-proxy.h"

#include "hal-marshal.h"
#include "hal-device-power.h"
#include "hal-device.h"
#include "hal-manager.h"

static void     hal_device_class_init (HalDeviceClass *klass);
static void     hal_device_init       (HalDevice      *device);
static void     hal_device_finalize   (GObject	     *object);

#define HAL_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), HAL_TYPE_DEVICE, HalDevicePrivate))

struct HalDevicePrivate
{
	DBusGConnection		*connection;
	gboolean		 use_property_modified;
	gboolean		 use_condition;
	EggDbusProxy		*gproxy;
	gchar			*udi;
};

/* Signals emitted from HalDevice are:
 *
 * device-added
 * device-removed
 * device-property-modified
 * device-condition
 * new-capability
 * lost-capability
 * daemon-start
 * daemon-stop
 */
enum {
	DEVICE_PROPERTY_MODIFIED,
	DEVICE_CONDITION,
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (HalDevice, hal_device, G_TYPE_OBJECT)

/**
 * hal_device_set_udi:
 *
 * Return value: TRUE for success, FALSE for failure
 **/
gboolean
hal_device_set_udi (HalDevice  *device, const gchar *udi)
{
	DBusGProxy *proxy;
	DBusGConnection *connection;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (udi != NULL, FALSE);

	if (device->priv->udi != NULL) {
		/* aready set UDI */
		return FALSE;
	}

	connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, NULL);
	proxy = egg_dbus_proxy_assign (device->priv->gproxy, connection,
				       HAL_DBUS_SERVICE, udi, HAL_DBUS_INTERFACE_DEVICE);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy failed");
		return FALSE;
	}
	device->priv->udi = g_strdup (udi);

	return TRUE;
}

/**
 * hal_device_get_udi:
 *
 * Return value: UDI
 **/
const gchar *
hal_device_get_udi (HalDevice *device)
{
	g_return_val_if_fail (HAL_IS_DEVICE (device), NULL);

	return device->priv->udi;
}

/**
 * hal_device_get_bool:
 *
 * @hal_device: This class instance
 * @key: The key to query
 * @value: return value, passed by ref
 * Return value: TRUE for success, FALSE for failure
 **/
gboolean
hal_device_get_bool (HalDevice  *device,
		      const gchar *key,
		      gboolean    *value,
		      GError     **error)
{
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (device->priv->udi != NULL, FALSE);

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	ret = dbus_g_proxy_call (proxy, "GetPropertyBoolean", error,
				 G_TYPE_STRING, key,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, value,
				 G_TYPE_INVALID);
	if (!ret) {
		*value = FALSE;
	}
	return ret;
}

/**
 * hal_device_get_string:
 *
 * @hal_device: This class instance
 * @key: The key to query
 * @value: return value, passed by ref
 * Return value: TRUE for success, FALSE for failure
 *
 * You must g_free () the return value.
 **/
gboolean
hal_device_get_string (HalDevice   *device,
			const gchar  *key,
			gchar       **value,
			GError      **error)
{
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (device->priv->udi != NULL, FALSE);

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	ret = dbus_g_proxy_call (proxy, "GetPropertyString", error,
				 G_TYPE_STRING, key,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, value,
				 G_TYPE_INVALID);
	if (!ret) {
		*value = NULL;
	}
	return ret;
}

/**
 * hal_device_get_int:
 *
 * @hal_device: This class instance
 * @key: The key to query
 * @value: return value, passed by ref
 * Return value: TRUE for success, FALSE for failure
 **/
gboolean
hal_device_get_int (HalDevice   *device,
		     const gchar  *key,
		     gint         *value,
		     GError      **error)
{
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (device->priv->udi != NULL, FALSE);

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	ret = dbus_g_proxy_call (proxy, "GetPropertyInteger", error,
				 G_TYPE_STRING, key,
				 G_TYPE_INVALID,
				 G_TYPE_INT, value,
				 G_TYPE_INVALID);
	if (!ret) {
		*value = 0;
	}
	return ret;
}

/**
 * hal_device_get_uint:
 *
 * HAL has no concept of a UINT, only INT
 **/
gboolean
hal_device_get_uint (HalDevice   *device,
		      const gchar  *key,
		      guint        *value,
		      GError      **error)
{
	gint tvalue;
	gboolean ret;

	/* bodge */
	ret = hal_device_get_int (device, key, &tvalue, error);
	*value = (guint) tvalue;
	return ret;
}

/**
 * hal_device_query_capability:
 *
 * @hal_device: This class instance
 * @capability: The capability, e.g. "battery"
 * @value: return value, passed by ref
 * Return value: TRUE for success, FALSE for failure
 **/
gboolean
hal_device_query_capability (HalDevice  *device,
			      const gchar *capability,
			      gboolean    *has_capability,
			      GError     **error)
{
	gboolean ret;
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (capability != NULL, FALSE);
	g_return_val_if_fail (has_capability != NULL, FALSE);
	g_return_val_if_fail (device->priv->udi != NULL, FALSE);

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	ret = dbus_g_proxy_call (proxy, "QueryCapability", error,
				 G_TYPE_STRING, capability,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, has_capability,
				 G_TYPE_INVALID);
	if (!ret) {
		*has_capability = FALSE;
	}
	return ret;
}

/**
 * watch_device_property_modified:
 *
 * @key: Property key
 * @is_added: If the key was added
 * @is_removed: If the key was removed
 *
 * Invoked when a property of a device in the Global Device List is
 * changed, and we have we have subscribed to changes for that device.
 */
static void
watch_device_property_modified (DBusGProxy  *proxy,
				const gchar *key,
				gboolean     is_added,
				gboolean     is_removed,
				gboolean     finally,
				HalDevice  *device)
{
	g_signal_emit (device, signals [DEVICE_PROPERTY_MODIFIED], 0,
		       key, is_added, is_removed, finally);
}

/**
 * watch_device_properties_modified_cb:
 *
 * @proxy: The org.freedesktop.Hal.Manager proxy
 * @device: This class instance
 *
 * Demultiplex the composite PropertyModified events here.
 */
static void
watch_device_properties_modified_cb (DBusGProxy *proxy,
				     gint	 type,
				     GPtrArray  *properties,
				     HalDevice *device)
{
	GValueArray *array;
	const gchar *udi;
	const gchar *key;
	gboolean     added;
	gboolean     removed;
	gboolean     finally = FALSE;
	guint	     i;

	udi = dbus_g_proxy_get_path (proxy);

	array = NULL;

	for (i = 0; i < properties->len; i++) {
		array = g_ptr_array_index (properties, i);
		if (array->n_values != 3) {
			egg_warning ("array->n_values invalid (!3)");
			return;
		}

		key = g_value_get_string (g_value_array_get_nth (array, 0));
		removed = g_value_get_boolean (g_value_array_get_nth (array, 1));
		added = g_value_get_boolean (g_value_array_get_nth (array, 2));

		/* Work out if this PropertyModified is the last to be sent as
		 * sometimes we only want to refresh caches when we have all
		 * the info from a UDI */
		if (i == properties->len - 1) {
			finally = TRUE;
		}

		watch_device_property_modified (proxy, key, added, removed, finally, device);
	}
}

/**
 * hal_device_watch_property_modified:
 *
 * Watch the specified device, so it emits device-property-modified and
 * adds to the gpm cache so we don't get asked to add it again.
 */
gboolean
hal_device_watch_property_modified (HalDevice *device)
{
	DBusGProxy *proxy;
	GType struct_array_type, struct_type;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (device->priv->udi != NULL, FALSE);

	if (device->priv->use_property_modified) {
		/* already watched */
		return FALSE;
	}

	device->priv->use_property_modified = TRUE;

	struct_type = dbus_g_type_get_struct ("GValueArray",
					      G_TYPE_STRING,
					      G_TYPE_BOOLEAN,
					      G_TYPE_BOOLEAN,
					      G_TYPE_INVALID);

	struct_array_type = dbus_g_type_get_collection ("GPtrArray", struct_type);

	dbus_g_object_register_marshaller (hal_marshal_VOID__INT_BOXED,
					   G_TYPE_NONE, G_TYPE_INT,
					   struct_array_type, G_TYPE_INVALID);

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	dbus_g_proxy_add_signal (proxy, "PropertyModified",
				 G_TYPE_INT, struct_array_type, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "PropertyModified",
				     G_CALLBACK (watch_device_properties_modified_cb), device, NULL);
	return TRUE;
}

/**
 * watch_device_condition_cb:
 *
 * @udi: Univerisal Device Id
 * @name: Name of condition
 * @details: D-BUS message with parameters
 *
 * Invoked when a property of a device in the Global Device List is
 * changed, and we have we have subscribed to changes for that device.
 */
static void
watch_device_condition_cb (DBusGProxy  *proxy,
			   const gchar *condition,
			   const gchar *details,
			   HalDevice  *device)
{
	g_signal_emit (device, signals [DEVICE_CONDITION], 0, condition, details);
}

/**
 * hal_device_watch_condition:
 *
 * Watch the specified device, so it emits a device-condition
 */
gboolean
hal_device_watch_condition (HalDevice *device)
{
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (device->priv->udi != NULL, FALSE);

	if (device->priv->use_condition) {
		/* already watched */
		return FALSE;
	}

	device->priv->use_condition = TRUE;

	dbus_g_object_register_marshaller (hal_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_INVALID);

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	dbus_g_proxy_add_signal (proxy, "Condition",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "Condition",
				     G_CALLBACK (watch_device_condition_cb), device, NULL);
	return TRUE;
}

/**
 * hal_device_remove_condition:
 *
 * Remove the specified device, so it does not emit device-condition signals.
 */
gboolean
hal_device_remove_condition (HalDevice *device)
{
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);

	if (device->priv->use_condition == FALSE) {
		/* already connected */
		return FALSE;
	}

	device->priv->use_condition = FALSE;

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	dbus_g_proxy_disconnect_signal (proxy, "Condition",
					G_CALLBACK (watch_device_condition_cb), device);
	return TRUE;
}

/**
 * hal_device_remove_property_modified:
 *
 * Remove the specified device, so it does not emit device-propery-modified.
 */
gboolean
hal_device_remove_property_modified (HalDevice *device)
{
	DBusGProxy *proxy;

	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);

	if (device->priv->use_property_modified == FALSE) {
		/* already disconnected */
		return FALSE;
	}

	device->priv->use_property_modified = FALSE;

	proxy = egg_dbus_proxy_get_proxy (device->priv->gproxy);
	if (DBUS_IS_G_PROXY (proxy) == FALSE) {
		egg_warning ("proxy NULL!!");
		return FALSE;
	}
	dbus_g_proxy_disconnect_signal (proxy, "PropertyModified",
				        G_CALLBACK (watch_device_properties_modified_cb), device);
	return TRUE;
}

/**
 * proxy_status_cb:
 * @proxy: The dbus raw proxy
 * @status: The status of the service, where TRUE is connected
 * @hal_manager: This class instance
 **/
static void
proxy_status_cb (DBusGProxy *proxy,
		 gboolean    status,
		 HalDevice *device)
{
	g_return_if_fail (HAL_IS_DEVICE (device));
	if (status) {
		/* should join */
	} else {
		/* should unjoin */
	}
}

/**
 * hal_device_class_init:
 * @klass: This class instance
 **/
static void
hal_device_class_init (HalDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hal_device_finalize;
	g_type_class_add_private (klass, sizeof (HalDevicePrivate));

	signals [DEVICE_PROPERTY_MODIFIED] =
		g_signal_new ("property-modified",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HalDeviceClass, device_property_modified),
			      NULL,
			      NULL,
			      hal_marshal_VOID__STRING_BOOLEAN_BOOLEAN_BOOLEAN,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	signals [DEVICE_CONDITION] =
		g_signal_new ("device-condition",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HalDeviceClass, device_condition),
			      NULL,
			      NULL,
			      hal_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, G_TYPE_STRING);
}

/**
 * hal_device_init:
 *
 * @hal_device: This class instance
 **/
static void
hal_device_init (HalDevice *device)
{
	GError *error = NULL;

	device->priv = HAL_DEVICE_GET_PRIVATE (device);

	device->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
	}

	device->priv->use_property_modified = FALSE;
	device->priv->use_condition = FALSE;

	/* get the manager connection */
	device->priv->gproxy = egg_dbus_proxy_new ();
	g_signal_connect (device->priv->gproxy, "proxy-status",
			  G_CALLBACK (proxy_status_cb), device);
}

/**
 * hal_device_finalize:
 * @object: This class instance
 **/
static void
hal_device_finalize (GObject *object)
{
	HalDevice *device;
	g_return_if_fail (object != NULL);
	g_return_if_fail (HAL_IS_DEVICE (object));

	device = HAL_DEVICE (object);
	device->priv = HAL_DEVICE_GET_PRIVATE (device);

	if (device->priv->use_property_modified) {
		hal_device_remove_property_modified (device);
	}
	if (device->priv->use_condition) {
		hal_device_remove_condition (device);
	}

	g_object_unref (device->priv->gproxy);
	g_free (device->priv->udi);

	G_OBJECT_CLASS (hal_device_parent_class)->finalize (object);
}

/**
 * hal_device_new:
 * Return value: new HalDevice instance.
 **/
HalDevice *
hal_device_new (void)
{
	HalDevice *device = g_object_new (HAL_TYPE_DEVICE, NULL);
	return HAL_DEVICE (device);
}
