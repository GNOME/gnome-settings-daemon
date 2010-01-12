/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#include "egg-debug.h"

#include "hal-marshal.h"
#include "hal-device.h"
#include "hal-device-store.h"

static void     hal_device_store_class_init (HalDeviceStoreClass *klass);
static void     hal_device_store_init       (HalDeviceStore      *device_store);
static void     hal_device_store_finalize   (GObject	          *object);

#define HAL_DEVICE_STORE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), HAL_TYPE_DEVICE_STORE, HalDeviceStorePrivate))

struct HalDeviceStorePrivate
{
	GPtrArray		*array;		/* the device array */
};

enum {
	DEVICE_REMOVED,				/* is not expected to work yet */
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (HalDeviceStore, hal_device_store, G_TYPE_OBJECT)

/**
 * hal_device_store_index_udi:
 *
 * Returns -1 if not found
 *
 * @device_store: This store instance
 * @device: The device
 */
static gint
hal_device_store_index_udi (HalDeviceStore *device_store, const gchar *udi)
{
	gint i;
	guint length;
	HalDevice *d;

	length = device_store->priv->array->len;
	for (i=0;i<length;i++) {
		d = (HalDevice *) g_ptr_array_index (device_store->priv->array, i);
		if (strcmp (hal_device_get_udi (d), udi) == 0) {
			return i;
		}
	}
	return -1;
}

/**
 * hal_device_store_index:
 *
 * Returns -1 if not found
 *
 * @device_store: This store instance
 * @device: The device
 */
static gint
hal_device_store_index (HalDeviceStore *device_store, HalDevice *device)
{
	HalDevice *d;
	gint i;
	guint length;
	const gchar *udi;

	g_return_val_if_fail (HAL_IS_DEVICE_STORE (device_store), FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);

	length = device_store->priv->array->len;
	udi = hal_device_get_udi (device);

	/* trivial check, is instance the same (FAST) */
	for (i=0;i<length;i++) {
		d = (HalDevice *) g_ptr_array_index (device_store->priv->array, i);
		if (d == device) {
			return i;
		}
	}

	/* non trivial check, is udi the same (SLOW) */
	return hal_device_store_index_udi (device_store, udi);
}

/**
 * hal_device_store_find_udi:
 *
 * NULL return value is not found
 *
 * @device_store: This store instance
 * @device: The device
 */
HalDevice *
hal_device_store_find_udi (HalDeviceStore *device_store, const gchar *udi)
{
	gint index;

	g_return_val_if_fail (HAL_IS_DEVICE_STORE (device_store), NULL);
	g_return_val_if_fail (udi != NULL, NULL);

	index = hal_device_store_index_udi (device_store, udi);
	if (index == -1) {
		return NULL;
	}

	/* return the device */
	return (HalDevice *) g_ptr_array_index (device_store->priv->array, index);
}

/**
 * hal_device_store_present:
 *
 * @device_store: This store instance
 * @device: The device
 */
gboolean
hal_device_store_present (HalDeviceStore *device_store, HalDevice *device)
{
	g_return_val_if_fail (HAL_IS_DEVICE_STORE (device_store), FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);

	/* if we have an index, we have the device */
	if (hal_device_store_index (device_store, device) != -1) {
		return TRUE;
	}
	return FALSE;
}

/**
 * hal_device_store_insert:
 *
 * @device_store: This store instance
 * @device: The device
 */
gboolean
hal_device_store_insert (HalDeviceStore *device_store, HalDevice *device)
{
	g_return_val_if_fail (HAL_IS_DEVICE_STORE (device_store), FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);

	if (hal_device_store_present (device_store, device)) {
		return FALSE;
	}

	g_ptr_array_add (device_store->priv->array, (gpointer) device);
	return TRUE;
}

/**
 * hal_device_store_remove:
 *
 * @device_store: This store instance
 * @device: The device
 */
gboolean
hal_device_store_remove (HalDeviceStore *device_store, HalDevice *device)
{
	gint index;
	HalDevice *d;

	g_return_val_if_fail (HAL_IS_DEVICE_STORE (device_store), FALSE);
	g_return_val_if_fail (HAL_IS_DEVICE (device), FALSE);

	index = hal_device_store_index (device_store, device);
	if (index == -1) {
		return FALSE;
	}

	/* we unref because this may be the only pointer to this instance */
	d = (HalDevice *) g_ptr_array_index (device_store->priv->array, index);
	g_object_unref (d);

	/* remove from the device_store */
	g_ptr_array_remove_index (device_store->priv->array, index);

	return TRUE;
}

/**
 * hal_device_store_print:
 *
 * @device_store: This store instance
 */
gboolean
hal_device_store_print (HalDeviceStore *device_store)
{
	HalDevice *d;
	guint i;
	guint length;

	g_return_val_if_fail (HAL_IS_DEVICE_STORE (device_store), FALSE);

	length = device_store->priv->array->len;
	g_print ("Printing device list in %p\n", device_store);
	for (i=0;i<length;i++) {
		d = (HalDevice *) g_ptr_array_index (device_store->priv->array, i);
		g_print ("%i: %s\n", i, hal_device_get_udi (d));
	}

	return TRUE;
}

/**
 * hal_device_store_class_init:
 * @klass: This class instance
 **/
static void
hal_device_store_class_init (HalDeviceStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = hal_device_store_finalize;
	g_type_class_add_private (klass, sizeof (HalDeviceStorePrivate));

	signals [DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (HalDeviceStoreClass, device_removed),
			      NULL,
			      NULL,
			      hal_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, G_TYPE_STRING);
}

/**
 * hal_device_store_init:
 *
 * @hal_device_store: This class instance
 **/
static void
hal_device_store_init (HalDeviceStore *device_store)
{
	device_store->priv = HAL_DEVICE_STORE_GET_PRIVATE (device_store);

	device_store->priv->array = g_ptr_array_new ();
}

/**
 * hal_device_store_finalize:
 * @object: This class instance
 **/
static void
hal_device_store_finalize (GObject *object)
{
	HalDeviceStore *device_store;
	HalDevice *d;
	gint i;
	guint length;

	g_return_if_fail (object != NULL);
	g_return_if_fail (HAL_IS_DEVICE_STORE (object));

	device_store = HAL_DEVICE_STORE (object);
	device_store->priv = HAL_DEVICE_STORE_GET_PRIVATE (device_store);

	length = device_store->priv->array->len;

	/* unref all */
	for (i=0;i<length;i++) {
		d = (HalDevice *) g_ptr_array_index (device_store->priv->array, i);
		g_object_unref (d);
	}
	g_ptr_array_free (device_store->priv->array, TRUE);

	G_OBJECT_CLASS (hal_device_store_parent_class)->finalize (object);
}

/**
 * hal_device_store_new:
 * Return value: new HalDeviceStore instance.
 **/
HalDeviceStore *
hal_device_store_new (void)
{
	HalDeviceStore *device_store = g_object_new (HAL_TYPE_DEVICE_STORE, NULL);
	return HAL_DEVICE_STORE (device_store);
}

