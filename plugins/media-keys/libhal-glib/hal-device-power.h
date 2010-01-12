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

#ifndef __HALDEVICEPOWER_H
#define __HALDEVICEPOWER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define HAL_TYPE_DEVICE_POWER		(hal_device_power_get_type ())
#define HAL_DEVICE_POWER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), HAL_TYPE_DEVICE_POWER, HalDevicePower))
#define HAL_DEVICE_POWER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), HAL_TYPE_DEVICE_POWER, HalDevicePowerClass))
#define HAL_IS_DEVICE_POWER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), HAL_TYPE_DEVICE_POWER))
#define HAL_IS_DEVICE_POWER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), HAL_TYPE_DEVICE_POWER))
#define HAL_DEVICE_POWER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), HAL_TYPE_DEVICE_POWER, HalDevicePowerClass))

typedef struct HalDevicePowerPrivate HalDevicePowerPrivate;

typedef struct
{
	GObject		  parent;
	HalDevicePowerPrivate *priv;
} HalDevicePower;

typedef struct
{
	GObjectClass	parent_class;
} HalDevicePowerClass;

GType		 hal_device_power_get_type		(void);
HalDevicePower	*hal_device_power_new			(void);

gboolean	 hal_device_power_has_support		(HalDevicePower	*power);
gboolean	 hal_device_power_can_suspend		(HalDevicePower	*power);
gboolean	 hal_device_power_can_hibernate		(HalDevicePower	*power);
gboolean	 hal_device_power_suspend		(HalDevicePower	*power,
							 guint		 wakeup,
							 GError		**error);
gboolean	 hal_device_power_hibernate		(HalDevicePower	*power,
							 GError		**error);
gboolean	 hal_device_power_shutdown		(HalDevicePower	*power,
							 GError		**error);
gboolean	 hal_device_power_reboot		(HalDevicePower	*power,
							 GError		**error);
gboolean	 hal_device_power_has_suspend_error	(HalDevicePower	*power,
							 gboolean	*state);
gboolean	 hal_device_power_has_hibernate_error	(HalDevicePower	*power,
							 gboolean	*state);
gboolean	 hal_device_power_clear_suspend_error	(HalDevicePower	*power,
							 GError		**error);
gboolean	 hal_device_power_clear_hibernate_error	(HalDevicePower	*power,
							 GError		**error);
gboolean	 hal_device_power_enable_power_save	(HalDevicePower	*power,
							 gboolean	 enable);

G_END_DECLS

#endif	/* __HALDEVICEPOWER_H */
