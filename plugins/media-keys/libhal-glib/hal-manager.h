/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2006 Richard Hughes <richard@hughsie.com>
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

#ifndef __HAL_MANAGER_H
#define __HAL_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define	HAL_DBUS_SERVICE		 	"org.freedesktop.Hal"
#define	HAL_DBUS_PATH_MANAGER		 	"/org/freedesktop/Hal/Manager"
#define	HAL_DBUS_INTERFACE_MANAGER	 	"org.freedesktop.Hal.Manager"
#define	HAL_DBUS_INTERFACE_DEVICE	 	"org.freedesktop.Hal.Device"
#define	HAL_DBUS_INTERFACE_LAPTOP_PANEL	 	"org.freedesktop.Hal.Device.LaptopPanel"
#define	HAL_DBUS_INTERFACE_POWER	 	"org.freedesktop.Hal.Device.SystemPowerManagement"
#define	HAL_DBUS_INTERFACE_CPUFREQ	 	"org.freedesktop.Hal.Device.CPUFreq"
#define	HAL_DBUS_INTERFACE_KBD_BACKLIGHT 	"org.freedesktop.Hal.Device.KeyboardBacklight"
#define	HAL_DBUS_INTERFACE_LIGHT_SENSOR	 	"org.freedesktop.Hal.Device.LightSensor"
#define HAL_ROOT_COMPUTER		 	"/org/freedesktop/Hal/devices/computer"

#define HAL_TYPE_MANAGER		(hal_manager_get_type ())
#define HAL_MANAGER(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), HAL_TYPE_MANAGER, HalManager))
#define HAL_MANAGER_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), HAL_TYPE_MANAGER, HalManagerClass))
#define HAL_IS_MANAGER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), HAL_TYPE_MANAGER))
#define HAL_IS_MANAGER_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), HAL_TYPE_MANAGER))
#define HAL_MANAGER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), HAL_TYPE_MANAGER, HalManagerClass))

typedef struct HalManagerPrivate HalManagerPrivate;

typedef struct
{
	GObject			 parent;
	HalManagerPrivate	*priv;
} HalManager;

/* Signals emitted from HalManager are:
 *
 * device-added
 * device-removed
 * new-capability
 * lost-capability
 * daemon-start
 * daemon-stop
 */

typedef struct
{
	GObjectClass	parent_class;
	void		(* device_added)		(HalManager	*manager,
							 const gchar	*udi);
	void		(* device_removed)		(HalManager	*manager,
							 const gchar	*udi);
	void		(* new_capability)		(HalManager	*manager,
							 const gchar	*udi,
							 const gchar	*capability);
	void		(* lost_capability)		(HalManager	*manager,
							 const gchar	*udi,
							 const gchar	*capability);
	void		(* daemon_start)		(HalManager	*manager);
	void		(* daemon_stop)			(HalManager	*manager);
} HalManagerClass;

GType		 hal_manager_get_type			(void);
HalManager	*hal_manager_new			(void);

gboolean	 hal_manager_is_running			(HalManager	*manager);
gint		 hal_manager_num_devices_of_capability	(HalManager	*manager,
							 const gchar	*capability);
gint		 hal_manager_num_devices_of_capability_with_value (HalManager *manager,
							 const gchar	*capability,
							 const gchar	*key,
							 const gchar	*value);
gboolean	 hal_manager_find_capability		(HalManager	*manager,
							 const gchar	*capability,
							 gchar     	***value,
							 GError		**error);
gboolean	 hal_manager_find_device_string_match	(HalManager	*manager,
							 const gchar	*key,
							 const gchar	*value,
							 gchar		***devices,
							 GError		**error);
void		 hal_manager_free_capability		(gchar		**value);
gboolean	 hal_manager_is_laptop			(HalManager	*manager);

G_END_DECLS

#endif	/* __HAL_MANAGER_H */
