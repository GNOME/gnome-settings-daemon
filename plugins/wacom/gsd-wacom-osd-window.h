/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Author: Olivier Fourdan <ofourdan@redhat.com>
 *
 */

#ifndef __GSD_WACOM_OSD_WINDOW_H
#define __GSD_WACOM_OSD_WINDOW_H

#include <gtk/gtk.h>
#include <glib-object.h>
#include "gsd-wacom-device.h"

#define GSD_TYPE_WACOM_OSD_WINDOW         (gsd_wacom_osd_window_get_type ())
#define GSD_WACOM_OSD_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_WACOM_OSD_WINDOW, GsdWacomOSDWindow))
#define GSD_WACOM_OSD_WINDOW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_WACOM_OSD_WINDOW, GsdWacomOSDWindowClass))
#define GSD_IS_WACOM_OSD_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_WACOM_OSD_WINDOW))
#define GSD_IS_WACOM_OSD_WINDOW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_WACOM_OSD_WINDOW))
#define GSD_WACOM_OSD_WINDOW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_WACOM_OSD_WINDOW, GsdWacomOSDWindowClass))

typedef struct GsdWacomOSDWindowPrivate GsdWacomOSDWindowPrivate;

typedef struct
{
        GtkWindow                 window;
        GsdWacomOSDWindowPrivate *priv;
} GsdWacomOSDWindow;

typedef struct
{
        GtkWindowClass            parent_class;
} GsdWacomOSDWindowClass;

GType                     gsd_wacom_osd_window_get_type        (void) G_GNUC_CONST;
GsdWacomDevice *          gsd_wacom_osd_window_get_device      (GsdWacomOSDWindow        *osd_window);
void                      gsd_wacom_osd_window_set_message     (GsdWacomOSDWindow        *osd_window,
                                                                const gchar              *str);
const char *              gsd_wacom_osd_window_get_message     (GsdWacomOSDWindow        *osd_window);
void                      gsd_wacom_osd_window_set_active      (GsdWacomOSDWindow        *osd_window,
                                                                GsdWacomTabletButton     *button,
                                                                GtkDirectionType          dir,
                                                                gboolean                  active);
void                      gsd_wacom_osd_window_set_mode        (GsdWacomOSDWindow        *osd_window,
                                                                gint                      group_id,
                                                                gint                      mode);
gboolean                  gsd_wacom_osd_window_get_edition_mode (GsdWacomOSDWindow        *osd_window);
void                      gsd_wacom_osd_window_set_edition_mode (GsdWacomOSDWindow        *osd_window,
                                                                 gboolean                  edition_mode);
GtkWidget *               gsd_wacom_osd_window_new             (GsdWacomDevice           *pad,
                                                                const gchar              *message);

#endif /* __GSD_WACOM_OSD_WINDOW_H */
