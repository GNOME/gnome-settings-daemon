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

#ifndef __HAL_DEVICE_H
#define __HAL_DEVICE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define HAL_TYPE_DEVICE		(hal_device_get_type ())
#define HAL_DEVICE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), HAL_TYPE_DEVICE, HalDevice))
#define HAL_DEVICE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), HAL_TYPE_DEVICE, HalDeviceClass))
#define HAL_IS_DEVICE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), HAL_TYPE_DEVICE))
#define HAL_IS_DEVICE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), HAL_TYPE_DEVICE))
#define HAL_DEVICE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), HAL_TYPE_DEVICE, HalDeviceClass))

typedef struct HalDevicePrivate HalDevicePrivate;

typedef struct
{
	GObject		     parent;
	HalDevicePrivate *priv;
} HalDevice;

/* Signals emitted from HalDevice are:
 *
 * device-property-modified
 * device-condition
 */

typedef struct
{
	GObjectClass	parent_class;
	void		(* device_property_modified)	(HalDevice	*device,
							 const gchar	*key,
							 gboolean	 is_added,
							 gboolean	 is_removed,
							 gboolean	 finally);
	void		(* device_condition)		(HalDevice	*device,
							 const gchar	*condition,
							 const gchar	*details);
} HalDeviceClass;

GType		 hal_device_get_type			(void);
HalDevice	*hal_device_new				(void);

gboolean	 hal_device_set_udi			(HalDevice	*device,
							 const gchar	*udi);
const gchar	*hal_device_get_udi			(HalDevice	*device);
gboolean	 hal_device_get_bool			(HalDevice	*device,
							 const gchar	*key,
							 gboolean	*value,
							 GError		**error);
gboolean	 hal_device_get_string			(HalDevice	*device,
							 const gchar	*key,
							 gchar		**value,
							 GError		**error);
gboolean	 hal_device_get_int			(HalDevice	*device,
							 const gchar	*key,
							 gint		*value,
							 GError		**error);
gboolean	 hal_device_get_uint			(HalDevice	*device,
							 const gchar	*key,
							 guint		*value,
							 GError		**error);
gboolean	 hal_device_query_capability		(HalDevice	*device,
							 const gchar	*capability,
							 gboolean	*has_capability,
							 GError		**error);
gboolean	 hal_device_watch_condition		(HalDevice	*device);
gboolean	 hal_device_watch_property_modified	(HalDevice	*device);
gboolean	 hal_device_remove_condition		(HalDevice	*device);
gboolean	 hal_device_remove_property_modified	(HalDevice	*device);

G_END_DECLS

#endif	/* __HAL_DEVICE_H */
