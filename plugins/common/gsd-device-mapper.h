/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Carlos Garnacho <carlosg@gnome.org>
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

#ifndef __GSD_DEVICE_MAPPER_H__
#define __GSD_DEVICE_MAPPER_H__

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>
#undef GNOME_DESKTOP_USE_UNSTABLE_API
#include <gdk/gdk.h>

#include "gsd-device-manager.h"

G_BEGIN_DECLS

#define GSD_TYPE_DEVICE_MAPPER	       (gsd_device_mapper_get_type ())
#define GSD_DEVICE_MAPPER(o)	       (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_DEVICE_MAPPER, GsdDeviceMapper))
#define GSD_DEVICE_MAPPER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_DEVICE_MAPPER, GsdDeviceMapperClass))
#define GSD_IS_DEVICE_MAPPER(o)	       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_DEVICE_MAPPER))
#define GSD_IS_DEVICE_MAPPER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_DEVICE_MAPPER))
#define GSD_DEVICE_MAPPER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_DEVICE_MAPPER, GsdDeviceMapperClass))

typedef struct _GsdDeviceMapper GsdDeviceMapper;
typedef struct _GsdDeviceMapperClass GsdDeviceMapperClass;

GType		  gsd_device_mapper_get_type	      (void) G_GNUC_CONST;
GsdDeviceMapper * gsd_device_mapper_get		      (void);

void		  gsd_device_mapper_add_input	      (GsdDeviceMapper *mapper,
						       GsdDevice       *device);
void		  gsd_device_mapper_remove_input      (GsdDeviceMapper *mapper,
						       GsdDevice       *device);
void		  gsd_device_mapper_add_output	      (GsdDeviceMapper *mapper,
						       GnomeRROutput   *output);
void		  gsd_device_mapper_remove_output     (GsdDeviceMapper *mapper,
						       GnomeRROutput   *output);

GnomeRROutput	* gsd_device_mapper_get_device_output (GsdDeviceMapper *mapper,
						       GsdDevice       *device);

void		  gsd_device_mapper_set_device_output (GsdDeviceMapper *mapper,
						       GsdDevice       *device,
						       GnomeRROutput   *output);

gint		  gsd_device_mapper_get_device_monitor (GsdDeviceMapper *mapper,
							GsdDevice	*device);
void		  gsd_device_mapper_set_device_monitor (GsdDeviceMapper *mapper,
							GsdDevice	*device,
							gint		 monitor_num);

G_END_DECLS

#endif /* __GSD_DEVICE_MAPPER_H__ */
