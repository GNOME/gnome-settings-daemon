/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
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
 *
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gsd-media-keys-window.h"

#define DIALOG_TIMEOUT 2000     /* dialog timeout in ms */
#define DIALOG_FADE_TIMEOUT 1500 /* timeout before fade starts */
#define FADE_TIMEOUT 10        /* timeout in ms between each frame of the fade */

#define BG_ALPHA 0.50
#define FG_ALPHA 1.00

static void     gsd_media_keys_window_class_init (GsdMediaKeysWindowClass *klass);
static void     gsd_media_keys_window_init       (GsdMediaKeysWindow      *fade);

#define GSD_MEDIA_KEYS_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_MEDIA_KEYS_WINDOW, GsdMediaKeysWindowPrivate))

struct GsdMediaKeysWindowPrivate
{
        guint                    is_composited : 1;
        guint                    hide_timeout_id;
        guint                    fade_timeout_id;
        double                   fade_out_alpha;
        GsdMediaKeysWindowAction action;

        guint                    volume_muted : 1;
        int                      volume_level;
	/* volume per-step percentage */
	int      		 vol_step;
        guint                    is_mute_key : 1;

        GtkImage                *image;
        GtkWidget               *progress;

	/* OSD parameter */
        guint                    draw_osd_box : 1;
	int			 osd_window_size;
	guint 		 	 volume_step_icons : 1;

	/* customer OSD theme */
	GtkIconTheme 		 *cust_theme;

	unsigned int		 num_locked;
	unsigned int		 scroll_locked;
	gchar*		 	 cpu_governor;
	gboolean		 wifi_enabled;
	GsdMediaKeysXrandr	 xrandr;
	guint 		 	 backlight;
	guint			 performance;
	gboolean		 webcam_enabled;
	gboolean		 bluetooth_enabled;
};

G_DEFINE_TYPE (GsdMediaKeysWindow, gsd_media_keys_window, GTK_TYPE_WINDOW)

static gboolean
fade_timeout (GsdMediaKeysWindow *window)
{
        if (window->priv->fade_out_alpha <= 0.0) {
                gtk_widget_hide (GTK_WIDGET (window));

                /* Reset it for the next time */
                window->priv->fade_out_alpha = 1.0;
                window->priv->fade_timeout_id = 0;
                return FALSE;
        } else {
                GdkRectangle rect;
                GtkWidget *win = GTK_WIDGET (window);

                window->priv->fade_out_alpha -= 0.10;

                rect.x = 0;
                rect.y = 0;
                rect.width = win->allocation.width;
                rect.height = win->allocation.height;

                gdk_window_invalidate_rect (win->window, &rect, FALSE);
        }

        return TRUE;
}

static gboolean
hide_timeout (GsdMediaKeysWindow *window)
{
        if (window->priv->is_composited) {
                window->priv->hide_timeout_id = 0;
                window->priv->fade_timeout_id = g_timeout_add (FADE_TIMEOUT,
                                                               (GSourceFunc) fade_timeout,
                                                               window);
        } else {
                gtk_widget_hide (GTK_WIDGET (window));
        }
        return FALSE;
}

static void
remove_hide_timeout (GsdMediaKeysWindow *window)
{
        if (window->priv->hide_timeout_id != 0) {
                g_source_remove (window->priv->hide_timeout_id);
                window->priv->hide_timeout_id = 0;
        }

        if (window->priv->fade_timeout_id != 0) {
                g_source_remove (window->priv->fade_timeout_id);
                window->priv->fade_timeout_id = 0;
                window->priv->fade_out_alpha = 1.0;
        }
}

static void
add_hide_timeout (GsdMediaKeysWindow *window)
{
        int timeout;

        if (window->priv->is_composited) {
                timeout = DIALOG_FADE_TIMEOUT;
        } else {
                timeout = DIALOG_TIMEOUT;
        }
        window->priv->hide_timeout_id = g_timeout_add (timeout,
                                                       (GSourceFunc) hide_timeout,
                                                       window);
}

static void
update_window (GsdMediaKeysWindow *window)
{
        remove_hide_timeout (window);
        add_hide_timeout (window);

        if (window->priv->is_composited) {
                gtk_widget_queue_draw (GTK_WIDGET (window));
        }
}

static void
volume_controls_set_visible (GsdMediaKeysWindow *window,
                             gboolean            visible)
{
        if (window->priv->progress == NULL)
                return;

        if (visible) {
                gtk_widget_show (window->priv->progress);
        } else {
                gtk_widget_hide (window->priv->progress);
        }
}

static void
window_set_icon_name (GsdMediaKeysWindow *window,
                      const char         *name)
{
        if (window->priv->image == NULL)
                return;

        gtk_image_set_from_icon_name (window->priv->image,
                                      name, GTK_ICON_SIZE_DIALOG);
}

static void
action_changed (GsdMediaKeysWindow *window)
{
        if (! window->priv->is_composited) {
                switch (window->priv->action) {
                case GSD_MEDIA_KEYS_WINDOW_ACTION_VOLUME:
                        volume_controls_set_visible (window, TRUE);

                        if (window->priv->volume_muted) {
                                window_set_icon_name (window, "audio-volume-muted");
                        } else {
                                window_set_icon_name (window, "audio-volume-high");
                        }

                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_EJECT:
                        volume_controls_set_visible (window, FALSE);
                        window_set_icon_name (window, "media-eject");
                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_WIFI:
                        volume_controls_set_visible (window, FALSE);

			if (window->priv->wifi_enabled) {
                        	window_set_icon_name (window, "wireless-on");
			} else {
                        	window_set_icon_name (window, "wireless-off");
			}

                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_ON:
                        volume_controls_set_visible (window, FALSE);
                        window_set_icon_name (window, "touchpad-on");
                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_OFF:
                        volume_controls_set_visible (window, FALSE);
                        window_set_icon_name (window, "touchpad-off");
                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_XRANDR:
                        volume_controls_set_visible (window, FALSE);
			switch (window->priv->xrandr) {
			case GSD_MEDIA_KEYS_XRANDR_LCD_ONLY:
				window_set_icon_name (window, "display-internal-only");
				break;
			case GSD_MEDIA_KEYS_XRANDR_LCD:
				window_set_icon_name (window, "display-internal");
				break;
			case GSD_MEDIA_KEYS_XRANDR_CRT:
				window_set_icon_name (window, "display-external");
				break;
			case GSD_MEDIA_KEYS_XRANDR_CLONE:
				window_set_icon_name (window, "display-mirror");
				break;
			case GSD_MEDIA_KEYS_XRANDR_DUAL:
				window_set_icon_name (window, "display-extended-desktop");
				break;
			}
                        break;
		case GSD_MEDIA_KEYS_WINDOW_ACTION_CPU_GOVERNOR:
                        volume_controls_set_visible (window, FALSE);

	                if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_POWERSAVE)) {
        	                window_set_icon_name (window, "cpu-governor-low");
                	}
                	else if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_ONDEMAND)) {
                        	window_set_icon_name (window, "cpu-governor-normal");
                	}
                	else if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_PERFORMANCE)) {
                        	window_set_icon_name (window, "cpu-governor-speed");
                	}

                        break;
        	case GSD_MEDIA_KEYS_WINDOW_ACTION_NUM_LOCK:
                        volume_controls_set_visible (window, FALSE);

			if (window->priv->num_locked) {
                            window_set_icon_name (window, "numlock-on");
			} else {
                            window_set_icon_name (window, "numlock-off");
			}

                	break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_SCROLL_LOCK:
                        volume_controls_set_visible (window, FALSE);

                        if (window->priv->scroll_locked) {
                            window_set_icon_name (window, "scrolllock-on");
                        } else {
                            window_set_icon_name (window, "scrolllock-off");
                        }

                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_BACKLIGHT:
                        volume_controls_set_visible (window, FALSE);

                        if (window->priv->backlight) {
                            window_set_icon_name (window, "lcd-backlight-1");
                        } else {
                            window_set_icon_name (window, "lcd-backlight-0");
                        }

                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_PERFORMANCE:
                        volume_controls_set_visible (window, FALSE);

                        if (window->priv->performance >= 0) {
			    gchar		   icon_name_tmp [25];
                            g_snprintf (icon_name_tmp, 25, "performance-%01d",
					window->priv->performance);
                            window_set_icon_name (window, icon_name_tmp);
                        }

                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_WEBCAM:
                        volume_controls_set_visible (window, FALSE);

			if (window->priv->webcam_enabled) {
                        	window_set_icon_name (window, "camera-web-on");
			} else {
                        	window_set_icon_name (window, "camera-web-off");
			}

                        break;
                case GSD_MEDIA_KEYS_WINDOW_ACTION_BLUETOOTH:
                        volume_controls_set_visible (window, FALSE);

			if (window->priv->bluetooth_enabled) {
                        	window_set_icon_name (window, "bluetooth-on");
			} else {
                        	window_set_icon_name (window, "bluetooth-off");
			}

                        break;
                default:
                        break;
                }
        }

        update_window (window);
}

static void
volume_level_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (!window->priv->is_composited && window->priv->progress != NULL) {
                double fraction;

                fraction = (double) window->priv->volume_level / 100.0;

                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (window->priv->progress),
                                               fraction);
        }
}

static void
vol_step_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (!window->priv->is_composited) {
		;//TODO: change the icon
        }
}

static void
is_mute_key_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (!window->priv->is_composited) {
                ;//TODO: change the icon
        }
}

static void
volume_muted_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
                if (window->priv->volume_muted) {
                        window_set_icon_name (window, "audio-volume-muted");
                } else {
                        window_set_icon_name (window, "audio-volume-high");
                }
        }
}

static void
num_locked_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
                if (window->priv->num_locked) {
                        window_set_icon_name (window, "numlock-on");
                } else {
                        window_set_icon_name (window, "numlock-off");
                }
        }
}

static void
scroll_locked_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
                if (window->priv->scroll_locked) {
                        window_set_icon_name (window, "scrolllock-on");
                } else {
                        window_set_icon_name (window, "scrolllock-off");
                }
        }
}

static void
cpu_governor_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
		if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_POWERSAVE)) {
                        window_set_icon_name (window, "cpu-governor-low");
		}
		else if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_ONDEMAND)) {
                        window_set_icon_name (window, "cpu-governor-normal");
		}
		else if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_PERFORMANCE)) {
                        window_set_icon_name (window, "cpu-governor-speed");
		}
        }
}

static void
xrandr_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
		switch (window->priv->xrandr) {
		case GSD_MEDIA_KEYS_XRANDR_LCD_ONLY:
			window_set_icon_name (window, "display-internal-only");
			break;
		case GSD_MEDIA_KEYS_XRANDR_LCD:
			window_set_icon_name (window, "display-internal");
			break;
		case GSD_MEDIA_KEYS_XRANDR_CRT:
			window_set_icon_name (window, "display-external");
			break;
		case GSD_MEDIA_KEYS_XRANDR_CLONE:
			window_set_icon_name (window, "display-mirror");
			break;
		case GSD_MEDIA_KEYS_XRANDR_DUAL:
			window_set_icon_name (window, "display-extended-desktop");
			break;
		}
	}
}

static void
wifi_enabled_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
                if (window->priv->wifi_enabled) {
                        window_set_icon_name (window, "wireless-on");
                }
                else {
                        window_set_icon_name (window, "wireless-off");
		}
        }
}

static void
webcam_enabled_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
                if (window->priv->webcam_enabled) {
                        window_set_icon_name (window, "camera-web-on");
                }
                else {
                        window_set_icon_name (window, "camera-web-off");
		}
        }
}

static void
bluetooth_enabled_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
                if (window->priv->bluetooth_enabled) {
                        window_set_icon_name (window, "bluetooth-on");
                }
                else {
                        window_set_icon_name (window, "bluetooth-off");
		}
        }
}

static void
backlight_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
		if (window->priv->backlight) {
		    window_set_icon_name (window, "lcd-backlight-1");
		} else {
		    window_set_icon_name (window, "lcd-backlight-0");
		}
        }
}

static void
performance_changed (GsdMediaKeysWindow *window)
{
        update_window (window);

        if (! window->priv->is_composited) {
		if (window->priv->performance >= 0) {
			gchar		   icon_name_tmp [25];
               		g_snprintf (icon_name_tmp, 25, "performance-%01d",
			    		window->priv->performance);
                	window_set_icon_name (window, icon_name_tmp);
		}
        }
}

void
gsd_media_keys_window_set_action (GsdMediaKeysWindow      *window,
                                  GsdMediaKeysWindowAction action)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->action != action) {
                window->priv->action = action;
                action_changed (window);
        }
}

void
gsd_media_keys_window_set_volume_muted (GsdMediaKeysWindow *window,
                                        gboolean            muted)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->volume_muted != muted) {
                window->priv->volume_muted = muted;
                volume_muted_changed (window);
        }
}

void
gsd_media_keys_window_set_draw_osd_box (GsdMediaKeysWindow *window,
                                        gboolean            draw_osd_box_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->draw_osd_box != draw_osd_box_in) {
                window->priv->draw_osd_box = draw_osd_box_in;
        }
}

void
gsd_media_keys_window_set_osd_window_size (GsdMediaKeysWindow *window,
                                           int            osd_window_size_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        gint size;

        window->priv->osd_window_size = osd_window_size_in;
        size = window->priv->osd_window_size;

        gtk_window_set_default_size (GTK_WINDOW (window), size, size);
}

void
gsd_media_keys_window_set_volume_step_icons (GsdMediaKeysWindow *window,
                                             gboolean            volume_step_icons_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->volume_step_icons != volume_step_icons_in) {
                window->priv->volume_step_icons = volume_step_icons_in;
        }
}

void
gsd_media_keys_window_set_is_mute_key (GsdMediaKeysWindow *window,
                                       gboolean            is_mute_key_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->is_mute_key != is_mute_key_in) {
                window->priv->is_mute_key = is_mute_key_in;
                is_mute_key_changed (window);
        }
}

void
gsd_media_keys_window_set_cust_theme (GsdMediaKeysWindow *window,
                                      GtkIconTheme       *cust_theme_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        window->priv->cust_theme = cust_theme_in;
}

void
gsd_media_keys_window_set_num_locked (GsdMediaKeysWindow *window,
                                      unsigned int	  state)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->num_locked != state) {
        	window->priv->num_locked = state;
                num_locked_changed (window);
        }
}

void
gsd_media_keys_window_set_scroll_locked (GsdMediaKeysWindow *window,
                                         unsigned int        state)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->scroll_locked != state) {
                window->priv->scroll_locked = state;
                scroll_locked_changed (window);
        }
}

void
gsd_media_keys_window_set_cpu_governor (GsdMediaKeysWindow *window,
                                        gchar*         cpu_governor_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (g_strcmp0(window->priv->cpu_governor, cpu_governor_in)) {
                window->priv->cpu_governor = cpu_governor_in;
                cpu_governor_changed (window);
        }
}

void
gsd_media_keys_window_set_xrandr (GsdMediaKeysWindow *window,
                                  GsdMediaKeysXrandr  xrandr_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->xrandr != xrandr_in) {
                window->priv->xrandr = xrandr_in;
                xrandr_changed (window);
        }
}

void
gsd_media_keys_window_set_wifi_enabled (GsdMediaKeysWindow *window,
                                        gboolean	    wifi_enabled_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->wifi_enabled != wifi_enabled_in) {
                window->priv->wifi_enabled = wifi_enabled_in;
                wifi_enabled_changed (window);
        }
}

void
gsd_media_keys_window_set_webcam_enabled (GsdMediaKeysWindow *window,
                                          gboolean	     webcam_enabled_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->webcam_enabled != webcam_enabled_in) {
                window->priv->webcam_enabled = webcam_enabled_in;
                webcam_enabled_changed (window);
        }
}

void
gsd_media_keys_window_set_bluetooth_enabled (GsdMediaKeysWindow *window,
                                          gboolean	     bluetooth_enabled_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->bluetooth_enabled != bluetooth_enabled_in) {
                window->priv->bluetooth_enabled = bluetooth_enabled_in;
                webcam_enabled_changed (window);
        }
}

void
gsd_media_keys_window_set_backlight (GsdMediaKeysWindow *window,
                                     guint	         backlight_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->backlight != backlight_in) {
                window->priv->backlight = backlight_in;
                backlight_changed (window);
        }
}

void
gsd_media_keys_window_set_performance (GsdMediaKeysWindow *window,
                                       guint	           performance_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->performance != performance_in) {
                window->priv->performance = performance_in;
                performance_changed (window);
        }
}

void
gsd_media_keys_window_set_volume_level (GsdMediaKeysWindow *window,
                                        int                 level)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->volume_level != level) {
                window->priv->volume_level = level;
                volume_level_changed (window);
        }
}

void
gsd_media_keys_window_set_volume_step (GsdMediaKeysWindow *window,
                                       int                 vol_step_in)
{
        g_return_if_fail (GSD_IS_MEDIA_KEYS_WINDOW (window));

        if (window->priv->vol_step != vol_step_in) {
                window->priv->vol_step = vol_step_in;
                vol_step_changed (window);
        }
}

static void
curved_rectangle (cairo_t *cr,
                  double   x0,
                  double   y0,
                  double   width,
                  double   height,
                  double   radius)
{
        double x1;
        double y1;

        x1 = x0 + width;
        y1 = y0 + height;

        if (!width || !height) {
                return;
        }

        if (width / 2 < radius) {
                if (height / 2 < radius) {
                        cairo_move_to  (cr, x0, (y0 + y1) / 2);
                        cairo_curve_to (cr, x0 ,y0, x0, y0, (x0 + x1) / 2, y0);
                        cairo_curve_to (cr, x1, y0, x1, y0, x1, (y0 + y1) / 2);
                        cairo_curve_to (cr, x1, y1, x1, y1, (x1 + x0) / 2, y1);
                        cairo_curve_to (cr, x0, y1, x0, y1, x0, (y0 + y1) / 2);
                } else {
                        cairo_move_to  (cr, x0, y0 + radius);
                        cairo_curve_to (cr, x0, y0, x0, y0, (x0 + x1) / 2, y0);
                        cairo_curve_to (cr, x1, y0, x1, y0, x1, y0 + radius);
                        cairo_line_to (cr, x1, y1 - radius);
                        cairo_curve_to (cr, x1, y1, x1, y1, (x1 + x0) / 2, y1);
                        cairo_curve_to (cr, x0, y1, x0, y1, x0, y1 - radius);
                }
        } else {
                if (height / 2 < radius) {
                        cairo_move_to  (cr, x0, (y0 + y1) / 2);
                        cairo_curve_to (cr, x0, y0, x0 , y0, x0 + radius, y0);
                        cairo_line_to (cr, x1 - radius, y0);
                        cairo_curve_to (cr, x1, y0, x1, y0, x1, (y0 + y1) / 2);
                        cairo_curve_to (cr, x1, y1, x1, y1, x1 - radius, y1);
                        cairo_line_to (cr, x0 + radius, y1);
                        cairo_curve_to (cr, x0, y1, x0, y1, x0, (y0 + y1) / 2);
                } else {
                        cairo_move_to  (cr, x0, y0 + radius);
                        cairo_curve_to (cr, x0 , y0, x0 , y0, x0 + radius, y0);
                        cairo_line_to (cr, x1 - radius, y0);
                        cairo_curve_to (cr, x1, y0, x1, y0, x1, y0 + radius);
                        cairo_line_to (cr, x1, y1 - radius);
                        cairo_curve_to (cr, x1, y1, x1, y1, x1 - radius, y1);
                        cairo_line_to (cr, x0 + radius, y1);
                        cairo_curve_to (cr, x0, y1, x0, y1, x0, y1 - radius);
                }
        }

        cairo_close_path (cr);
}

static GdkPixbuf *
load_pixbuf (GsdMediaKeysWindow *window,
             const char         *name,
             int                 icon_size)
{
        GtkIconTheme *theme;
        GdkPixbuf    *pixbuf;

	if ((window != NULL) && (window->priv->cust_theme != NULL)) {
		theme = window->priv->cust_theme;
	} else if (gtk_widget_has_screen (GTK_WIDGET (window))) {
                theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (window)));
        } else {
                theme = gtk_icon_theme_get_default ();
        }

	pixbuf = gtk_icon_theme_load_icon (theme,
                                           name,
                                           icon_size,
                                           GTK_ICON_LOOKUP_FORCE_SVG,
                                           NULL);

        /* make sure the pixbuf is close to the requested size
         * this is necessary because GTK_ICON_LOOKUP_FORCE_SVG
         * seems to be broken */
        if (pixbuf != NULL) {
                int width;
		int height;

                width = gdk_pixbuf_get_width (pixbuf);
        	height = gdk_pixbuf_get_height (pixbuf);

                if (width < (float)icon_size * 0.75) {
                        g_object_unref (pixbuf);
                        pixbuf = NULL;
                }

		/* if pixbuf out of range, rescale it*/
        	if (width > icon_size || height > icon_size)
    		{
			int new_width;
			int new_height;

			new_width = icon_size;
			new_height = icon_size;
			if (width > height)
			{
			    new_height = icon_size * height / width;
			}
			else
			{
			    new_width = icon_size * width / height;
			}

        		GdkPixbuf *temp = pixbuf;
        		pixbuf = gdk_pixbuf_scale_simple (temp,
                                      new_width,
                                      new_height,
                                      GDK_INTERP_HYPER);
                	g_object_unref (temp);
    		}
        }

        return pixbuf;
}

static gboolean
render_eject (GsdMediaKeysWindow *window,
              cairo_t            *cr,
              double              x0,
              double              y0,
              double              width,
              double              height)
{
        GdkPixbuf  *pixbuf;
        int         icon_size;
        const char *icon_name;

        icon_name = "media-eject";

        icon_size = (int)width;

        pixbuf = load_pixbuf (window, icon_name, icon_size);

        if (pixbuf == NULL) {
                return FALSE;
        }

        gdk_cairo_set_source_pixbuf (cr, pixbuf, x0, y0);
        cairo_paint_with_alpha (cr, FG_ALPHA);

        g_object_unref (pixbuf);

        return TRUE;
}

static gboolean
render_common (GsdMediaKeysWindow *window,
              cairo_t            *cr,
              double              x0,
              double              y0,
              double              width,
              double              height,
	      const char	  *icon_name)
{
        GdkPixbuf  *pixbuf;
        int         icon_size;

        icon_size = (int)width;

        pixbuf = load_pixbuf (window, icon_name, icon_size);

        if (pixbuf == NULL) {
                return FALSE;
        }

        gdk_cairo_set_source_pixbuf (cr, pixbuf, x0, y0);
        cairo_paint_with_alpha (cr, FG_ALPHA);

        g_object_unref (pixbuf);

        return TRUE;
}

static void
draw_eject (cairo_t *cr,
            double   x0,
            double   y0,
            double   width,
            double   height)
{
        int box_height;
        int tri_height;
        int separation;

        box_height = height * 0.2;
        separation = box_height / 3;
        tri_height = height - box_height - separation;

        cairo_rectangle (cr, x0, y0 + height - box_height, width, box_height);

        cairo_move_to (cr, x0, y0 + tri_height);
        cairo_rel_line_to (cr, width, 0);
        cairo_rel_line_to (cr, -width / 2, -tri_height);
        cairo_rel_line_to (cr, -width / 2, tri_height);
        cairo_close_path (cr);
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, FG_ALPHA);
        cairo_fill_preserve (cr);

        cairo_set_source_rgba (cr, 0.6, 0.6, 0.6, FG_ALPHA / 2);
        cairo_set_line_width (cr, 2);
        cairo_stroke (cr);
}

static void
draw_action_eject (GsdMediaKeysWindow *window,
                   cairo_t            *cr)
{
        int      window_width;
        int      window_height;
        double   width;
        double   height;
        double   x0;
        double   y0;
        gboolean res;

        gtk_window_get_size (GTK_WINDOW (window), &window_width, &window_height);

        width = window_width * 0.65;		//TODO: add gconf key for setup percent
        height = window_height * 0.65;
        x0 = (window_width - width) / 2;
        y0 = (window_height - height) / 2;

#if 0
        g_message ("eject box: w=%f h=%f x0=%f y0=%f",
                   width,
                   height,
                   x0,
                   y0);
#endif

        res = render_eject (window,
                            cr,
                            x0, y0,
                            width, height);
        if (! res) {
                /* draw eject symbol */
                draw_eject (cr, x0, y0, width, height);
        }
}

static gboolean
draw_action_common (GsdMediaKeysWindow *window,
                    cairo_t            *cr,
		    const char 	       *icon_name)
{
        int      window_width;
        int      window_height;
        double   width;
        double   height;
        double   x0;
        double   y0;
        gboolean res;

        gtk_window_get_size (GTK_WINDOW (window), &window_width, &window_height);

        width = window_width * 0.65;
        height = window_height * 0.65;
        x0 = (window_width - width) / 2;
        y0 = (window_height - height) / 2;

        res = render_common (window,
                            cr,
                            x0, y0,
                            width, height, icon_name);

	return res;
}

static void
draw_action_num_lock (GsdMediaKeysWindow *window,
                      cairo_t            *cr)
{
	if (window->priv->num_locked) {
        	draw_action_common (window, cr, "numlock-on");
	} else {
        	draw_action_common (window, cr, "numlock-off");
	}
}

static void
draw_action_webcam (GsdMediaKeysWindow *window,
                      cairo_t            *cr)
{
	if (window->priv->webcam_enabled) {
        	draw_action_common (window, cr, "camera-web-on");
	} else {
        	draw_action_common (window, cr, "camera-web-off");
	}
}

static void
draw_action_bluetooth (GsdMediaKeysWindow *window,
                      cairo_t            *cr)
{
	if (window->priv->bluetooth_enabled) {
        	draw_action_common (window, cr, "bluetooth-on");
	} else {
        	draw_action_common (window, cr, "bluetooth-off");
	}
}

static void
draw_action_scroll_lock (GsdMediaKeysWindow *window,
                         cairo_t            *cr)
{
        if (window->priv->scroll_locked) {
                draw_action_common (window, cr, "scrolllock-on");
        } else {
                draw_action_common (window, cr, "scrolllock-off");
        }
}

static void
draw_action_cpu_governor (GsdMediaKeysWindow *window,
                          cairo_t            *cr)
{
	if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_POWERSAVE)) {
        	draw_action_common (window, cr, "cpu-governor-low");
        }
        else if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_ONDEMAND)) {
        	draw_action_common (window, cr, "cpu-governor-normal");
       	}
        else if (!g_strcmp0(window->priv->cpu_governor, CPU_GOVERNOR_PERFORMANCE)) {
        	draw_action_common (window, cr, "cpu-governor-speed");
        }
}

static void
draw_action_xrandr (GsdMediaKeysWindow *window,
                    cairo_t            *cr)
{
	switch (window->priv->xrandr) {
	case GSD_MEDIA_KEYS_XRANDR_LCD_ONLY:
		if (!draw_action_common (window, cr, "display-internal-only"))
			draw_action_common (window, cr, "display-internal");
		break;
	case GSD_MEDIA_KEYS_XRANDR_LCD:
		draw_action_common (window, cr, "display-internal");
		break;
	case GSD_MEDIA_KEYS_XRANDR_CRT:
		draw_action_common (window, cr, "display-external");
		break;
	case GSD_MEDIA_KEYS_XRANDR_CLONE:
		draw_action_common (window, cr, "display-mirror");
		break;
	case GSD_MEDIA_KEYS_XRANDR_DUAL:
		draw_action_common (window, cr, "display-extended-desktop");
		break;
	}
}

static void
draw_action_wifi_enabled (GsdMediaKeysWindow *window,
                          cairo_t            *cr)
{
        if (window->priv->wifi_enabled) {
                draw_action_common (window, cr, "wireless-on");
        }
        else {
                draw_action_common (window, cr, "wireless-off");
        }
}

static void
draw_action_backlight (GsdMediaKeysWindow *window,
                       cairo_t            *cr)
{
        if (window->priv->backlight) {
                draw_action_common (window, cr, "lcd-backlight-1");
        }
        else {
                draw_action_common (window, cr, "lcd-backlight-0");
        }
}

static void
draw_action_performance (GsdMediaKeysWindow *window,
                         cairo_t            *cr)
{
        if (window->priv->performance >= 0) {
		gchar		   icon_name_tmp [25];
                g_snprintf (icon_name_tmp, 25, "performance-%01d",
			    window->priv->performance);
        	draw_action_common (window, cr, icon_name_tmp);
	}
}

static void
draw_waves (cairo_t *cr,
            double   cx,
            double   cy,
            double   max_radius,
            int      volume_level)
{
        const int n_waves = 3;
        int last_wave;
        int i;

        last_wave = n_waves * volume_level / 100;

        for (i = 0; i < n_waves; i++) {
                double angle1;
                double angle2;
                double radius;
                double alpha;

                angle1 = -M_PI / 4;
                angle2 = M_PI / 4;

                if (i < last_wave)
                        alpha = 1.0;
                else if (i > last_wave)
                        alpha = 0.1;
                else alpha = 0.1 + 0.9 * (n_waves * volume_level % 100) / 100.0;

                radius = (i + 1) * (max_radius / n_waves);
                cairo_arc (cr, cx, cy, radius, angle1, angle2);
                cairo_set_source_rgba (cr, 0.6, 0.6, 0.6, alpha / 2);
                cairo_set_line_width (cr, 14);
                cairo_set_line_cap  (cr, CAIRO_LINE_CAP_ROUND);
                cairo_stroke_preserve (cr);

                cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, alpha);
                cairo_set_line_width (cr, 10);
                cairo_set_line_cap  (cr, CAIRO_LINE_CAP_ROUND);
                cairo_stroke (cr);
        }
}

static void
draw_cross (cairo_t *cr,
            double   cx,
            double   cy,
            double   size)
{
        cairo_move_to (cr, cx, cy - size/2.0);
        cairo_rel_line_to (cr, size, size);

        cairo_move_to (cr, cx, cy + size/2.0);
        cairo_rel_line_to (cr, size, -size);

        cairo_set_source_rgba (cr, 0.6, 0.6, 0.6, FG_ALPHA / 2);
        cairo_set_line_width (cr, 14);
        cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
        cairo_stroke_preserve (cr);

        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, FG_ALPHA);
        cairo_set_line_width (cr, 10);
        cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
        cairo_stroke (cr);
}

static void
draw_speaker (cairo_t *cr,
              double   cx,
              double   cy,
              double   width,
              double   height)
{
        double box_width;
        double box_height;
        double x0;
        double y0;

        box_width = width / 3;
        box_height = height / 3;

        x0 = cx - (width / 2) + box_width;
        y0 = cy - box_height / 2;

        cairo_move_to (cr, x0, y0);
        cairo_rel_line_to (cr, - box_width, 0);
        cairo_rel_line_to (cr, 0, box_height);
        cairo_rel_line_to (cr, box_width, 0);

        cairo_line_to (cr, cx + box_width, cy + height / 2);
        cairo_rel_line_to (cr, 0, -height);
        cairo_line_to (cr, x0, y0);
        cairo_close_path (cr);

        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, FG_ALPHA);
        cairo_fill_preserve (cr);

        cairo_set_source_rgba (cr, 0.6, 0.6, 0.6, FG_ALPHA / 2);
        cairo_set_line_width (cr, 2);
        cairo_stroke (cr);
}

static gboolean
render_speaker (GsdMediaKeysWindow *window,
                cairo_t            *cr,
                double              x0,
                double              y0,
                double              width,
                double              height)
{
        GdkPixbuf         *pixbuf;
        int                icon_size;
        int                n;
	const char        *icon_name;
	gchar		   icon_name_tmp [25];
        static const char *icon_names[] = {
                "audio-volume-muted",
                "audio-volume-low",
                "audio-volume-medium",
                "audio-volume-high",
                NULL
        };

        if (window->priv->volume_step_icons) {
                if (window->priv->is_mute_key) {
                        if (window->priv->volume_muted) {
                                icon_name = "audio-volume-muted";
                        } else {
                                icon_name = "audio-volume-unmuted";
                        }
                } else {
                        n = window->priv->volume_level / window->priv->vol_step;
			n = (n == 0 && window->priv->volume_level > 0)? 1:n;
                        g_snprintf (icon_name_tmp, 25, "audio-volume-%02d", n);
                        icon_name = icon_name_tmp;
                }
	} else {
		if (window->priv->volume_muted) {
			n = 0;
		} else {
			/* select image */
			n = 3 * window->priv->volume_level / 100 + 1;
			if (n < 1) {
				n = 1;
			} else if (n > 3) {
				n = 3;
			}
		}
		icon_name = icon_names[n];
	}

        icon_size = (int)width;

        pixbuf = load_pixbuf (window, icon_name, icon_size);

        if (pixbuf == NULL) {
                return FALSE;
        }

        gdk_cairo_set_source_pixbuf (cr, pixbuf, x0, y0);
        cairo_paint_with_alpha (cr, FG_ALPHA);

        g_object_unref (pixbuf);

        return TRUE;
}

static void
draw_volume_boxes (GsdMediaKeysWindow *window,
                   cairo_t            *cr,
                   double              percentage,
                   double              x0,
                   double              y0,
                   double              width,
                   double              height)
{
        gdouble  x1;
        GdkColor color;
        double   r, g, b;

        x1 = width * percentage;

        /* bar background */
        color = GTK_WIDGET (window)->style->dark [GTK_STATE_NORMAL];
        r = (float)color.red / 65535.0;
        g = (float)color.green / 65535.0;
        b = (float)color.blue / 65535.0;
        cairo_rectangle (cr, x0, y0, width, height);
        cairo_set_source_rgba (cr, r, g, b, FG_ALPHA);
        cairo_fill (cr);

        /* bar border */
        color = GTK_WIDGET (window)->style->dark [GTK_STATE_SELECTED];
        r = (float)color.red / 65535.0;
        g = (float)color.green / 65535.0;
        b = (float)color.blue / 65535.0;
        cairo_rectangle (cr, x0, y0, width, height);
        cairo_set_source_rgba (cr, r, g, b, FG_ALPHA);
        cairo_set_line_width (cr, 1);
        cairo_stroke (cr);

        /* bar progress */
        color = GTK_WIDGET (window)->style->bg [GTK_STATE_SELECTED];
        r = (float)color.red / 65535.0;
        g = (float)color.green / 65535.0;
        b = (float)color.blue / 65535.0;
        cairo_rectangle (cr, x0, y0, x1, height);
        cairo_set_source_rgba (cr, r, g, b, FG_ALPHA);
        cairo_fill (cr);
}

static void
draw_action_volume (GsdMediaKeysWindow *window,
                    cairo_t            *cr)
{
        int window_width;
        int window_height;
        double icon_box_width;
        double icon_box_height;
        double icon_box_x0;
        double icon_box_y0;
        double volume_box_x0;
        double volume_box_y0;
        double volume_box_width;
        double volume_box_height;
        gboolean res;

        gtk_window_get_size (GTK_WINDOW (window), &window_width, &window_height);

        icon_box_width = window_width * 0.65;
        icon_box_height = window_height * 0.65;
        volume_box_width = icon_box_width;
        volume_box_height = window_height * 0.05;

        icon_box_x0 = (window_width - icon_box_width) / 2;
        if (!window->priv->volume_step_icons)
        {
        	icon_box_y0 = (window_height - icon_box_height - volume_box_height) / 2;
	} else {
        	icon_box_y0 = (window_height - icon_box_height) / 2;
	}
        volume_box_x0 = icon_box_x0;
        volume_box_y0 = icon_box_height + icon_box_y0;

#if 0
        g_message ("icon box: w=%f h=%f x0=%f y0=%f",
                   icon_box_width,
                   icon_box_height,
                   icon_box_x0,
                   icon_box_y0);
        g_message ("volume box: w=%f h=%f x0=%f y0=%f",
                   volume_box_width,
                   volume_box_height,
                   volume_box_x0,
                   volume_box_y0);
#endif

        res = render_speaker (window,
                              cr,
                              icon_box_x0, icon_box_y0,
                              icon_box_width, icon_box_height);
        if (! res) {
                double speaker_width;
                double speaker_height;
                double speaker_cx;
                double speaker_cy;

                speaker_width = icon_box_width * 0.5;
                speaker_height = icon_box_height * 0.75;
                speaker_cx = icon_box_x0 + speaker_width / 2;
                speaker_cy = icon_box_y0 + speaker_height / 2;

#if 0
                g_message ("speaker box: w=%f h=%f cx=%f cy=%f",
                           speaker_width,
                           speaker_height,
                           speaker_cx,
                           speaker_cy);
#endif

                /* draw speaker symbol */
                draw_speaker (cr, speaker_cx, speaker_cy, speaker_width, speaker_height);

                if (! window->priv->volume_muted) {
                        /* draw sound waves */
                        double wave_x0;
                        double wave_y0;
                        double wave_radius;

                        wave_x0 = window_width / 2;
                        wave_y0 = speaker_cy;
                        wave_radius = icon_box_width / 2;

                        draw_waves (cr, wave_x0, wave_y0, wave_radius, window->priv->volume_level);
                } else {
                        /* draw 'mute' cross */
                        double cross_x0;
                        double cross_y0;
                        double cross_size;

                        cross_size = speaker_width * 3 / 4;
                        cross_x0 = icon_box_x0 + icon_box_width - cross_size;
                        cross_y0 = speaker_cy;

                        draw_cross (cr, cross_x0, cross_y0, cross_size);
                }
        }

        /* draw volume meter */
        if (!window->priv->volume_step_icons)
        {
		draw_volume_boxes (window,
				   cr,
				   (double)window->priv->volume_level / 100.0,
				   volume_box_x0,
				   volume_box_y0,
				   volume_box_width,
				   volume_box_height);
	}
}

static void
draw_action (GsdMediaKeysWindow *window,
             cairo_t            *cr)
{
        switch (window->priv->action) {
        case GSD_MEDIA_KEYS_WINDOW_ACTION_VOLUME:
                draw_action_volume (window, cr);
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_EJECT:
                draw_action_eject (window, cr);
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_WIFI:
		draw_action_wifi_enabled (window, cr);
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_ON:
                draw_action_common (window, cr, "touchpad-on");
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_OFF:
                draw_action_common (window, cr, "touchpad-off");
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_XRANDR:
		draw_action_xrandr (window, cr);
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_CPU_GOVERNOR:
		draw_action_cpu_governor (window, cr);
                break;
	case GSD_MEDIA_KEYS_WINDOW_ACTION_NUM_LOCK:
		draw_action_num_lock (window, cr);
		break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_SCROLL_LOCK:
                draw_action_scroll_lock (window, cr);
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_BACKLIGHT:
                draw_action_backlight (window, cr);
                break;
        case GSD_MEDIA_KEYS_WINDOW_ACTION_PERFORMANCE:
                draw_action_performance (window, cr);
                break;
	case GSD_MEDIA_KEYS_WINDOW_ACTION_WEBCAM:
		draw_action_webcam (window, cr);
		break;
	case GSD_MEDIA_KEYS_WINDOW_ACTION_BLUETOOTH:
		draw_action_bluetooth (window, cr);
		break;
        default:
                break;
        }
}

static gboolean
on_expose_event (GtkWidget          *widget,
                 GdkEventExpose     *event,
                 GsdMediaKeysWindow *window)
{
        cairo_t         *context;
        cairo_t         *cr;
        cairo_surface_t *surface;
        int              width;
        int              height;
        GdkColor         color;
        double           r, g, b;

        context = gdk_cairo_create (GTK_WIDGET (window)->window);

        cairo_set_operator (context, CAIRO_OPERATOR_SOURCE);
        gtk_window_get_size (GTK_WINDOW (widget), &width, &height);

        surface = cairo_surface_create_similar (cairo_get_target (context),
                                                CAIRO_CONTENT_COLOR_ALPHA,
                                                width,
                                                height);

        if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS) {
                goto done;
        }

        cr = cairo_create (surface);
        if (cairo_status (cr) != CAIRO_STATUS_SUCCESS) {
                goto done;
        }
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.0);
        cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
        cairo_paint (cr);

        /* draw a box */
        if (window->priv->draw_osd_box) { 
                curved_rectangle (cr, 0.5, 0.5, width-1, height-1, height / 10);
                color = GTK_WIDGET (window)->style->bg [GTK_STATE_NORMAL];
                r = (float)color.red / 65535.0;
                g = (float)color.green / 65535.0;
                b = (float)color.blue / 65535.0;
                cairo_set_source_rgba (cr, r, g, b, BG_ALPHA);
                cairo_fill_preserve (cr);

                color = GTK_WIDGET (window)->style->fg [GTK_STATE_NORMAL];
                r = (float)color.red / 65535.0;
                g = (float)color.green / 65535.0;
                b = (float)color.blue / 65535.0;
                cairo_set_source_rgba (cr, r, g, b, BG_ALPHA);
                cairo_set_line_width (cr, 1);
                cairo_stroke (cr);
        }

        /* draw action */
        draw_action (window, cr);

        cairo_destroy (cr);

        /* Make sure we have a transparent background */
        cairo_rectangle (context, 0, 0, width, height);
        cairo_set_source_rgba (context, 0.0, 0.0, 0.0, 0.0);
        cairo_fill (context);

        cairo_set_source_surface (context, surface, 0, 0);
        cairo_paint_with_alpha (context, window->priv->fade_out_alpha);

 done:
        if (surface != NULL) {
                cairo_surface_destroy (surface);
        }
        cairo_destroy (context);

        return FALSE;
}

static void
gsd_media_keys_window_real_show (GtkWidget *widget)
{
        GsdMediaKeysWindow *window;

        if (GTK_WIDGET_CLASS (gsd_media_keys_window_parent_class)->show) {
                GTK_WIDGET_CLASS (gsd_media_keys_window_parent_class)->show (widget);
        }

        window = GSD_MEDIA_KEYS_WINDOW (widget);
        remove_hide_timeout (window);
        add_hide_timeout (window);
}

static void
gsd_media_keys_window_real_hide (GtkWidget *widget)
{
        GsdMediaKeysWindow *window;

        if (GTK_WIDGET_CLASS (gsd_media_keys_window_parent_class)->hide) {
                GTK_WIDGET_CLASS (gsd_media_keys_window_parent_class)->hide (widget);
        }

	window = GSD_MEDIA_KEYS_WINDOW (widget);
        remove_hide_timeout (window);
}

static void
gsd_media_keys_window_real_realize (GtkWidget *widget)
{
        GdkColormap *colormap;
        GdkBitmap *mask;
        cairo_t *cr;

        colormap = gdk_screen_get_rgba_colormap (gtk_widget_get_screen (widget));

        if (colormap != NULL) {
                gtk_widget_set_colormap (widget, colormap);
        }

        if (GTK_WIDGET_CLASS (gsd_media_keys_window_parent_class)->realize) {
                GTK_WIDGET_CLASS (gsd_media_keys_window_parent_class)->realize (widget);
        }

        mask = gdk_pixmap_new (widget->window,
                               widget->allocation.width,
                               widget->allocation.height,
                               1);
        cr = gdk_cairo_create (mask);

        cairo_set_source_rgba (cr, 1., 1., 1., 0.);
        cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint (cr);

        /* make the whole window ignore events */
        gdk_window_input_shape_combine_mask (widget->window, mask, 0, 0);
        g_object_unref (mask);
        cairo_destroy (cr);
}

static void
gsd_media_keys_window_class_init (GsdMediaKeysWindowClass *klass)
{
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        widget_class->show = gsd_media_keys_window_real_show;
        widget_class->hide = gsd_media_keys_window_real_hide;
        widget_class->realize = gsd_media_keys_window_real_realize;

        g_type_class_add_private (klass, sizeof (GsdMediaKeysWindowPrivate));
}

gboolean
gsd_media_keys_window_is_valid (GsdMediaKeysWindow *window)
{
        GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (window));
        return gdk_screen_is_composited (screen) == window->priv->is_composited;
}

static void
gsd_media_keys_window_init (GsdMediaKeysWindow *window)
{
        GdkScreen *screen;

        window->priv = GSD_MEDIA_KEYS_WINDOW_GET_PRIVATE (window);

        screen = gtk_widget_get_screen (GTK_WIDGET (window));

        window->priv->is_composited = gdk_screen_is_composited (screen);

        if (window->priv->is_composited) {
                gdouble scalew, scaleh, scale;
                gint size;

                gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
                gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);

                /* assume 130x130 on a 640x480 display and scale from there */
                scalew = gdk_screen_get_width (screen) / 640.0;
                scaleh = gdk_screen_get_height (screen) / 480.0;
                scale = MIN (scalew, scaleh);
                size = 130 * MAX (1, scale);

                gtk_window_set_default_size (GTK_WINDOW (window), size, size);
                g_signal_connect (window, "expose-event", G_CALLBACK (on_expose_event), window);

                window->priv->fade_out_alpha = 1.0;
        } else {
                GtkBuilder *builder;
                gchar *objects[] = {"acme_frame", NULL};
                GtkWidget *frame;

                builder = gtk_builder_new ();
                gtk_builder_add_objects_from_file (builder,
                                                   GTKBUILDERDIR "/acme.ui",
                                                   objects,
                                                   NULL);

                window->priv->image = GTK_IMAGE (gtk_builder_get_object (builder, "acme_image"));
                window->priv->progress = GTK_WIDGET (gtk_builder_get_object (builder, "acme_volume_progressbar"));
                frame = GTK_WIDGET (gtk_builder_get_object (builder,
                                                            "acme_frame"));

                if (frame != NULL) {
                        gtk_container_add (GTK_CONTAINER (window), frame);
                        gtk_widget_show_all (frame);
                }

                /* The builder needs to stay alive until the window
                   takes ownership of the frame (and its children)  */
                g_object_unref (builder);
        }
}

GtkWidget *
gsd_media_keys_window_new (void)
{
        GObject *object;

        object = g_object_new (GSD_TYPE_MEDIA_KEYS_WINDOW,
                               "type", GTK_WINDOW_POPUP,
                               "type-hint", GDK_WINDOW_TYPE_HINT_NOTIFICATION,
                               "skip-taskbar-hint", TRUE,
                               "skip-pager-hint", TRUE,
                               "focus-on-map", FALSE,
                               NULL);

        return GTK_WIDGET (object);
}
