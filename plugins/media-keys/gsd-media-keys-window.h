/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef GSD_MEDIA_KEYS_WINDOW_H
#define GSD_MEDIA_KEYS_WINDOW_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CPU_GOVERNOR_ONDEMAND                   "ondemand"
#define CPU_GOVERNOR_POWERSAVE                  "powersave"
#define CPU_GOVERNOR_PERFORMANCE                "performance"

#define GSD_TYPE_MEDIA_KEYS_WINDOW            (gsd_media_keys_window_get_type ())
#define GSD_MEDIA_KEYS_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  GSD_TYPE_MEDIA_KEYS_WINDOW, GsdMediaKeysWindow))
#define GSD_MEDIA_KEYS_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),   GSD_TYPE_MEDIA_KEYS_WINDOW, GsdMediaKeysWindowClass))
#define GSD_IS_MEDIA_KEYS_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  GSD_TYPE_MEDIA_KEYS_WINDOW))
#define GSD_IS_MEDIA_KEYS_WINDOW_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), GSD_TYPE_MEDIA_KEYS_WINDOW))

typedef struct GsdMediaKeysWindow                   GsdMediaKeysWindow;
typedef struct GsdMediaKeysWindowClass              GsdMediaKeysWindowClass;
typedef struct GsdMediaKeysWindowPrivate            GsdMediaKeysWindowPrivate;

struct GsdMediaKeysWindow {
        GtkWindow                   parent;

        GsdMediaKeysWindowPrivate  *priv;
};

struct GsdMediaKeysWindowClass {
        GtkWindowClass parent_class;
};

typedef enum {
	GSD_MEDIA_KEYS_WINDOW_ACTION_VOLUME,
	GSD_MEDIA_KEYS_WINDOW_ACTION_EJECT,
        GSD_MEDIA_KEYS_WINDOW_ACTION_SLEEP,
        GSD_MEDIA_KEYS_WINDOW_ACTION_NUM_LOCK,
        GSD_MEDIA_KEYS_WINDOW_ACTION_SCROLL_LOCK,
	GSD_MEDIA_KEYS_WINDOW_ACTION_WIFI,
	GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_ON,
	GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_OFF,
	GSD_MEDIA_KEYS_WINDOW_ACTION_XRANDR,
	GSD_MEDIA_KEYS_WINDOW_ACTION_CPU_GOVERNOR,
	GSD_MEDIA_KEYS_WINDOW_ACTION_BACKLIGHT,
	GSD_MEDIA_KEYS_WINDOW_ACTION_PERFORMANCE,
	GSD_MEDIA_KEYS_WINDOW_ACTION_WEBCAM,
	GSD_MEDIA_KEYS_WINDOW_ACTION_BLUETOOTH
} GsdMediaKeysWindowAction;

typedef enum {
	GSD_MEDIA_KEYS_XRANDR_LCD_ONLY = 0,
	GSD_MEDIA_KEYS_XRANDR_LCD,
	GSD_MEDIA_KEYS_XRANDR_CRT,
	GSD_MEDIA_KEYS_XRANDR_CLONE,
	GSD_MEDIA_KEYS_XRANDR_DUAL
} GsdMediaKeysXrandr;

GType                 gsd_media_keys_window_get_type          (void);

GtkWidget *           gsd_media_keys_window_new               (void);
void                  gsd_media_keys_window_set_action        (GsdMediaKeysWindow      *window,
                                                               GsdMediaKeysWindowAction action);
void                  gsd_media_keys_window_set_volume_muted  (GsdMediaKeysWindow      *window,
                                                               gboolean                 muted);
void                  gsd_media_keys_window_set_volume_level  (GsdMediaKeysWindow      *window,
                                                               int                      level);
gboolean              gsd_media_keys_window_is_valid          (GsdMediaKeysWindow      *window);
void		      gsd_media_keys_window_set_draw_osd_box  (GsdMediaKeysWindow      *window,
                                        		       gboolean            draw_osd_box_in);
void		      gsd_media_keys_window_set_osd_window_size (GsdMediaKeysWindow *window,
                                           			 int               osd_window_size_in);
void		      gsd_media_keys_window_set_cust_theme (GsdMediaKeysWindow *window,
                                      			    GtkIconTheme       *cust_theme_in);

void                  gsd_media_keys_window_set_volume_step_icons (GsdMediaKeysWindow *window,
                                                                   gboolean            volume_step_icons_in);

void                  gsd_media_keys_window_set_volume_step (GsdMediaKeysWindow *window,
                                                             int                 vol_step_in);

void gsd_media_keys_window_set_is_mute_key (GsdMediaKeysWindow *window,
                                            gboolean            is_mute_key_in);

void gsd_media_keys_window_set_bluetooth_enabled (GsdMediaKeysWindow *window,
                                                  gboolean	      bluetooth_enabled_in);

void gsd_media_keys_window_set_num_locked (GsdMediaKeysWindow *window,
                                           unsigned int	       state);

void gsd_media_keys_window_set_scroll_locked (GsdMediaKeysWindow *window,
                                              unsigned int        state);

void gsd_media_keys_window_set_wifi_enabled (GsdMediaKeysWindow *window,
                                             gboolean	         wifi_enabled_in);

void gsd_media_keys_window_set_webcam_enabled (GsdMediaKeysWindow *window,
                                               gboolean	           webcam_enabled_in);

void gsd_media_keys_window_set_xrandr (GsdMediaKeysWindow *window,
                                       GsdMediaKeysXrandr  xrandr_in);

void gsd_media_keys_window_set_cpu_governor (GsdMediaKeysWindow *window,
                                             gchar*              cpu_governor_in);

void gsd_media_keys_window_set_backlight (GsdMediaKeysWindow *window,
                                          guint	              backlight_in);

void gsd_media_keys_window_set_performance (GsdMediaKeysWindow *window,
                                            guint	        performance_in);

G_END_DECLS

#endif
