/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Kalev Lember <kalevlember@gmail.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __GSD_TIMEZONE_MONITOR_H
#define __GSD_TIMEZONE_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_TIMEZONE_MONITOR                  (gsd_timezone_monitor_get_type ())
#define GSD_TIMEZONE_MONITOR(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSD_TYPE_TIMEZONE_MONITOR, GsdTimezoneMonitor))
#define GSD_IS_TIMEZONE_MONITOR(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSD_TYPE_TIMEZONE_MONITOR))
#define GSD_TIMEZONE_MONITOR_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GSD_TYPE_TIMEZONE_MONITOR, GsdTimezoneMonitorClass))
#define GSD_IS_TIMEZONE_MONITOR_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GSD_TYPE_TIMEZONE_MONITOR))
#define GSD_TIMEZONE_MONITOR_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GSD_TYPE_TIMEZONE_MONITOR, GsdTimezoneMonitorClass))

typedef struct _GsdTimezoneMonitor        GsdTimezoneMonitor;
typedef struct _GsdTimezoneMonitorClass   GsdTimezoneMonitorClass;

struct _GsdTimezoneMonitor
{
	GObject parent_instance;
};

struct _GsdTimezoneMonitorClass
{
	GObjectClass parent_class;

	void (*timezone_changed) (GsdTimezoneMonitor *monitor, gchar *timezone_id);
};

GType gsd_timezone_monitor_get_type (void) G_GNUC_CONST;

GsdTimezoneMonitor *gsd_timezone_monitor_new (void);

G_END_DECLS

#endif /* __GSD_TIMEZONE_MONITOR_H */
