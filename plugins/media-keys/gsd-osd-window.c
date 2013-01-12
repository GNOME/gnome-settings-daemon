/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * On-screen-display (OSD) window for gnome-settings-daemon's plugins
 *
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu> 
 * Copyright (C) 2009 Novell, Inc
 *
 * Authors:
 *   William Jon McCann <mccann@jhu.edu>
 *   Federico Mena-Quintero <federico@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "gsd-osd-window.h"
#include "gsd-osd-window-private.h"

#define ICON_SCALE 0.55           /* size of the icon compared to the whole OSD */
#define LABEL_HSCALE 0.80         /* size of the volume label compared to the whole OSD */
#define VOLUME_HSCALE 0.65        /* hsize of the volume bar compared to the whole OSD */
#define VOLUME_VSCALE 0.04        /* vsize of the volume bar compared to the whole OSD */
#define FG_ALPHA 1.0              /* Alpha value to be used for foreground objects drawn in an OSD window */

#define GSD_OSD_WINDOW_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_OSD_WINDOW, GsdOsdWindowPrivate))

struct GsdOsdWindowPrivate
{
        guint                    hide_timeout_id;
        guint                    fade_timeout_id;
        double                   fade_out_alpha;

        gint                     screen_width;
        gint                     screen_height;
        gint                     primary_monitor;
        guint                    monitors_changed_id;
        guint                    monitor_changed : 1;

        GsdOsdWindowAction       action;
        GIcon                   *icon;
        gboolean                 show_level;

        int                      volume_level;
        char                    *volume_label;
        guint                    volume_muted : 1;
};

G_DEFINE_TYPE (GsdOsdWindow, gsd_osd_window, GTK_TYPE_WINDOW)

static void gsd_osd_window_update_and_hide (GsdOsdWindow *window);

static void
gsd_osd_window_draw_rounded_rectangle (cairo_t* cr,
                                       gdouble  aspect,
                                       gdouble  x,
                                       gdouble  y,
                                       gdouble  corner_radius,
                                       gdouble  width,
                                       gdouble  height)
{
        gdouble radius = corner_radius / aspect;

        cairo_move_to (cr, x + radius, y);

        cairo_line_to (cr,
                       x + width - radius,
                       y);
        cairo_arc (cr,
                   x + width - radius,
                   y + radius,
                   radius,
                   -90.0f * G_PI / 180.0f,
                   0.0f * G_PI / 180.0f);
        cairo_line_to (cr,
                       x + width,
                       y + height - radius);
        cairo_arc (cr,
                   x + width - radius,
                   y + height - radius,
                   radius,
                   0.0f * G_PI / 180.0f,
                   90.0f * G_PI / 180.0f);
        cairo_line_to (cr,
                       x + radius,
                       y + height);
        cairo_arc (cr,
                   x + radius,
                   y + height - radius,
                   radius,
                   90.0f * G_PI / 180.0f,
                   180.0f * G_PI / 180.0f);
        cairo_line_to (cr,
                       x,
                       y + radius);
        cairo_arc (cr,
                   x + radius,
                   y + radius,
                   radius,
                   180.0f * G_PI / 180.0f,
                   270.0f * G_PI / 180.0f);
        cairo_close_path (cr);
}

static gboolean
fade_timeout (GsdOsdWindow *window)
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
                GtkAllocation allocation;

                window->priv->fade_out_alpha -= 0.10;

                rect.x = 0;
                rect.y = 0;
                gtk_widget_get_allocation (win, &allocation);
                rect.width = allocation.width;
                rect.height = allocation.height;

                gtk_widget_realize (win);
                gdk_window_invalidate_rect (gtk_widget_get_window (win), &rect, FALSE);
        }

        return TRUE;
}

static gboolean
hide_timeout (GsdOsdWindow *window)
{
	window->priv->hide_timeout_id = 0;
	window->priv->fade_timeout_id = g_timeout_add (FADE_FRAME_TIMEOUT,
						       (GSourceFunc) fade_timeout,
						       window);

        return FALSE;
}

static void
remove_hide_timeout (GsdOsdWindow *window)
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
add_hide_timeout (GsdOsdWindow *window)
{
        window->priv->hide_timeout_id = g_timeout_add (DIALOG_FADE_TIMEOUT,
                                                       (GSourceFunc) hide_timeout,
                                                       window);
}

static GIcon *
get_image_name_for_volume (gboolean muted,
                           int volume)
{
        static const char *icon_names[] = {
                "audio-volume-muted-symbolic",
                "audio-volume-low-symbolic",
                "audio-volume-medium-symbolic",
                "audio-volume-high-symbolic",
                NULL
        };
        int n;

        if (muted) {
                n = 0;
        } else {
                /* select image */
                n = 3 * volume / 100 + 1;
                if (n < 1) {
                        n = 1;
                } else if (n > 3) {
                        n = 3;
                }
        }

	return g_themed_icon_new_with_default_fallbacks (icon_names[n]);
}

static void
action_changed (GsdOsdWindow *window)
{
        gsd_osd_window_update_and_hide (GSD_OSD_WINDOW (window));
}

static void
volume_level_changed (GsdOsdWindow *window)
{
        gsd_osd_window_update_and_hide (GSD_OSD_WINDOW (window));
}

static void
volume_muted_changed (GsdOsdWindow *window)
{
        gsd_osd_window_update_and_hide (GSD_OSD_WINDOW (window));
}

void
gsd_osd_window_set_action (GsdOsdWindow      *window,
                           GsdOsdWindowAction action)
{
        g_return_if_fail (GSD_IS_OSD_WINDOW (window));
        g_return_if_fail (action == GSD_OSD_WINDOW_ACTION_VOLUME);

        if (window->priv->action != action) {
                window->priv->action = action;
                action_changed (window);
        } else {
                gsd_osd_window_update_and_hide (GSD_OSD_WINDOW (window));
        }
}

void
gsd_osd_window_set_action_custom (GsdOsdWindow      *window,
                                  const char        *icon_name,
                                  gboolean           show_level)
{
        GIcon *icon;

        g_return_if_fail (GSD_IS_OSD_WINDOW (window));
        g_return_if_fail (icon_name != NULL);

        icon = g_themed_icon_new_with_default_fallbacks (icon_name);
        gsd_osd_window_set_action_custom_gicon (window, icon, show_level);
}

void
gsd_osd_window_set_action_custom_gicon (GsdOsdWindow *window,
                                        GIcon        *icon,
                                        gboolean      show_level)
{
        if (window->priv->action != GSD_OSD_WINDOW_ACTION_CUSTOM ||
            g_icon_equal (window->priv->icon, icon) == FALSE ||
            window->priv->show_level != show_level) {
                window->priv->action = GSD_OSD_WINDOW_ACTION_CUSTOM;
                g_clear_object (&window->priv->icon);
                window->priv->icon = g_object_ref (icon);
                window->priv->show_level = show_level;
                action_changed (window);
        } else {
                gsd_osd_window_update_and_hide (GSD_OSD_WINDOW (window));
        }
}

void
gsd_osd_window_set_volume_muted (GsdOsdWindow *window,
                                 gboolean      muted)
{
        g_return_if_fail (GSD_IS_OSD_WINDOW (window));

        if (window->priv->volume_muted != muted) {
                window->priv->volume_muted = muted;
                volume_muted_changed (window);
        }
}

void
gsd_osd_window_set_volume_level (GsdOsdWindow *window,
                                 int           level)
{
        g_return_if_fail (GSD_IS_OSD_WINDOW (window));

        if (window->priv->volume_level != level) {
                window->priv->volume_level = level;
                volume_level_changed (window);
        }
}

void
gsd_osd_window_set_volume_label (GsdOsdWindow *window,
                                 const char   *label)
{
        g_return_if_fail (GSD_IS_OSD_WINDOW (window));

        if (g_strcmp0 (window->priv->volume_label, label) != 0) {
                g_free (window->priv->volume_label);
                window->priv->volume_label = g_strdup (label);
        }
}

static GdkPixbuf *
load_pixbuf (GsdOsdDrawContext *ctx,
             GIcon             *icon,
             int                icon_size)
{
        GtkIconInfo     *info;
        GdkPixbuf       *pixbuf;

        info = gtk_icon_theme_lookup_by_gicon (ctx->theme,
                                               icon,
                                               icon_size,
                                               GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_GENERIC_FALLBACK);

        if (info == NULL) {
                g_warning ("Failed to load '%s'", g_icon_to_string (icon));
                return NULL;
        }

        pixbuf = gtk_icon_info_load_symbolic_for_context (info,
                                                          ctx->style,
                                                          NULL,
                                                          NULL);
        gtk_icon_info_free (info);

        return pixbuf;
}

static gboolean
render_speaker (GsdOsdDrawContext *ctx,
                cairo_t           *cr,
                GdkRectangle      *icon_box)
{
        GdkPixbuf  *pixbuf;
        GIcon      *icon;

        icon = get_image_name_for_volume (ctx->volume_muted,
                                          ctx->volume_level);
        pixbuf = load_pixbuf (ctx, icon, icon_box->width);

        if (pixbuf == NULL) {
                return FALSE;
        }

        gtk_render_icon (ctx->style, cr,
                         pixbuf, icon_box->x, icon_box->y);

        g_object_unref (pixbuf);

        return TRUE;
}

static void
draw_volume_boxes (GsdOsdDrawContext *ctx,
                   cairo_t           *cr,
                   double             percentage,
                   GdkRectangle      *volume_box)
{
        gdouble   x1, radius;
        GdkRGBA  acolor;

        x1 = round ((volume_box->width - 1) * percentage);
        radius = MAX (round (volume_box->height / 6), 4);

        /* bar background */
        gtk_style_context_save (ctx->style);
        gtk_style_context_add_class (ctx->style, GTK_STYLE_CLASS_TROUGH);
        gtk_style_context_get_background_color (ctx->style, GTK_STATE_FLAG_NORMAL, &acolor);

        gsd_osd_window_draw_rounded_rectangle (cr, 1.0,
                                               volume_box->x, volume_box->y, radius,
                                               volume_box->width, volume_box->height);
        gdk_cairo_set_source_rgba (cr, &acolor);
        cairo_fill (cr);

        gtk_style_context_restore (ctx->style);

        /* bar progress */
        if (percentage < 0.01)
                return;
        gtk_style_context_save (ctx->style);
        gtk_style_context_add_class (ctx->style, GTK_STYLE_CLASS_PROGRESSBAR);
        gtk_style_context_get_background_color (ctx->style, GTK_STATE_FLAG_NORMAL, &acolor);

        gsd_osd_window_draw_rounded_rectangle (cr, 1.0,
                                               volume_box->x, volume_box->y, radius,
                                               x1, volume_box->height);
        gdk_cairo_set_source_rgba (cr, &acolor);
        cairo_fill (cr);

        gtk_style_context_restore (ctx->style);
}

static void
draw_volume_label (GsdOsdDrawContext *ctx,
                   cairo_t           *cr,
                   GdkRectangle      *label_box)
{
        PangoLayout *layout;
        PangoAttrList *attrs;
        PangoAttribute *attr;
        gint width, height;
        gdouble scale;

        if (!ctx->volume_label)
                return;

        layout = pango_layout_new (ctx->pango_context);
        pango_layout_set_text (layout, ctx->volume_label, -1);
        pango_layout_get_pixel_size (layout, &width, &height);

        scale = MIN ((gdouble) label_box->width / width, (gdouble) label_box->height / height);
        attrs = pango_attr_list_new ();

        attr = pango_attr_scale_new (scale);
        pango_attr_list_insert (attrs, attr);

        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        pango_attr_list_insert (attrs, attr);

        pango_layout_set_attributes (layout, attrs);

        pango_layout_get_pixel_size (layout, &width, &height);

        gtk_render_layout (ctx->style, cr,
                           label_box->x + ((label_box->width - width) / 2),
                           label_box->y + ((label_box->height - height) / 2),
                           layout);

        g_object_unref (layout);
        pango_attr_list_unref (attrs);
}

static void
get_bounding_boxes (GsdOsdDrawContext *ctx,
                    GdkRectangle *icon_box_out,
                    GdkRectangle *label_box_out,
                    GdkRectangle *volume_box_out)
{
        int window_width;
        int window_height;
        GdkRectangle icon_box, label_box, volume_box;

	window_width = window_height = ctx->size;

        icon_box.width = round (window_width * ICON_SCALE);
        icon_box.height = round (window_height * ICON_SCALE);
        volume_box.width = round (window_width * VOLUME_HSCALE);
        volume_box.height = round (window_height * VOLUME_VSCALE);

        icon_box.x = round ((window_width - icon_box.width) / 2);
        icon_box.y = round ((window_height - icon_box.height - 6 * volume_box.height) / 2);
        volume_box.x = round ((window_width - volume_box.width) / 2);
        volume_box.y = round (window_height - 4 * volume_box.height);

        label_box.width = round (window_width * LABEL_HSCALE);
        label_box.x = round ((window_width - label_box.width) / 2);
        label_box.y = (icon_box.y + icon_box.height + volume_box.height);
        label_box.height = (volume_box.y - volume_box.height - label_box.y);

        if (icon_box_out)
                *icon_box_out = icon_box;
        if (label_box_out)
                *label_box_out = label_box;
        if (volume_box_out)
                *volume_box_out = volume_box;
}

static void
draw_action_volume (GsdOsdDrawContext *ctx,
                    cairo_t           *cr)
{
        GdkRectangle icon_box, label_box, volume_box;

        get_bounding_boxes (ctx, &icon_box, &label_box, &volume_box);

#if 0
        g_message ("icon box: w=%d h=%d _x0=%d _y0=%d",
                   icon_box.width,
                   icon_box.height,
                   icon_box.x,
                   icon_box.y);
        g_message ("volume box: w=%d h=%d _x0=%d _y0=%d",
                   volume_box.width,
                   volume_box.height,
                   volume_box.x,
                   volume_box.y);
#endif

        render_speaker (ctx, cr, &icon_box);

        /* draw volume label */
        draw_volume_label (ctx, cr, &label_box);

        /* draw volume meter */
        draw_volume_boxes (ctx, cr, (double) ctx->volume_level / 100.0, &volume_box);
}

static gboolean
render_custom (GsdOsdDrawContext  *ctx,
               cairo_t            *cr,
               GdkRectangle       *icon_box)
{
        GdkPixbuf         *pixbuf;

        pixbuf = load_pixbuf (ctx, ctx->icon, icon_box->width);

        if (pixbuf == NULL) {
/* FIXME */
#if 0
                char *name;
                if (ctx->direction == GTK_TEXT_DIR_RTL)
                        name = g_strdup_printf ("%s-rtl", ctx->icon_name);
                else
                        name = g_strdup_printf ("%s-ltr", ctx->icon_name);
                pixbuf = load_pixbuf (ctx, name, icon_box->width);
                g_free (name);
                if (pixbuf == NULL)
#endif
                        return FALSE;
        }

        gtk_render_icon (ctx->style, cr,
                         pixbuf, icon_box->x, icon_box->y);

        g_object_unref (pixbuf);

        return TRUE;
}

static void
draw_action_custom (GsdOsdDrawContext  *ctx,
                    cairo_t            *cr)
{
        GdkRectangle icon_box, bright_box, label_box;

        get_bounding_boxes (ctx, &icon_box, &label_box, &bright_box);

#if 0
        g_message ("icon box: w=%d h=%d _x0=%d _y0=%d",
                   icon_box.width,
                   icon_box.height,
                   icon_box.x,
                   icon_box.y);
        g_message ("brightness box: w=%d h=%d _x0=%d _y0=%d",
                   bright_box.width,
                   bright_box.height,
                   bright_box.x,
                   bright_box.y);
#endif

        render_custom (ctx, cr, &icon_box);

        /* draw label */
        draw_volume_label (ctx, cr, &label_box);

        if (ctx->show_level != FALSE) {
                /* draw volume meter */
                draw_volume_boxes (ctx, cr, (double) ctx->volume_level / 100.0, &bright_box);
        }
}

void
gsd_osd_window_draw (GsdOsdDrawContext *ctx,
                     cairo_t           *cr)
{
        gdouble          corner_radius;
        GdkRGBA          acolor;

        /* draw a box */
        corner_radius = ctx->size / 10;
        gsd_osd_window_draw_rounded_rectangle (cr, 1.0, 0.0, 0.0, corner_radius, ctx->size - 1, ctx->size - 1);

        gtk_style_context_get_background_color (ctx->style, GTK_STATE_FLAG_NORMAL, &acolor);
        gdk_cairo_set_source_rgba (cr, &acolor);
        cairo_fill (cr);

        switch (ctx->action) {
        case GSD_OSD_WINDOW_ACTION_VOLUME:
                draw_action_volume (ctx, cr);
                break;
        case GSD_OSD_WINDOW_ACTION_CUSTOM:
                draw_action_custom (ctx, cr);
                break;
        default:
                break;
        }
}

static gboolean
gsd_osd_window_obj_draw (GtkWidget *widget,
                         cairo_t   *orig_cr)
{
        GsdOsdWindow      *window;
        cairo_t           *cr;
        cairo_surface_t   *surface;
        GtkStyleContext   *context;
        GsdOsdDrawContext  ctx;
        int                width, height, size;

        window = GSD_OSD_WINDOW (widget);
        gtk_window_get_size (GTK_WINDOW (widget), &width, &height);
        size = MIN (width, height);

        context = gtk_widget_get_style_context (widget);
        gtk_style_context_save (context);
        gtk_style_context_add_class (context, "osd");

        cairo_set_operator (orig_cr, CAIRO_OPERATOR_SOURCE);

        surface = cairo_surface_create_similar (cairo_get_target (orig_cr),
                                                CAIRO_CONTENT_COLOR_ALPHA,
                                                size,
                                                size);

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

        ctx.size = size;
        ctx.style = context;
        ctx.pango_context = gtk_widget_get_pango_context (widget);
        ctx.volume_level = window->priv->volume_level;
        ctx.volume_muted = window->priv->volume_muted;
        ctx.volume_label = window->priv->volume_label;
        ctx.icon = window->priv->icon;
        ctx.direction = gtk_widget_get_direction (GTK_WIDGET (window));
        ctx.show_level = window->priv->show_level;
        ctx.action = window->priv->action;
        if (window != NULL && gtk_widget_has_screen (GTK_WIDGET (window))) {
                ctx.theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (window)));
        } else {
                ctx.theme = gtk_icon_theme_get_default ();
        }
        gsd_osd_window_draw (&ctx, cr);

        cairo_destroy (cr);
        gtk_style_context_restore (context);

        /* Make sure we have a transparent background */
        cairo_rectangle (orig_cr, 0, 0, size, size);
        cairo_set_source_rgba (orig_cr, 0.0, 0.0, 0.0, 0.0);
        cairo_fill (orig_cr);

        cairo_set_source_surface (orig_cr, surface, 0, 0);
        cairo_paint_with_alpha (orig_cr, window->priv->fade_out_alpha);

 done:
        if (surface != NULL) {
                cairo_surface_destroy (surface);
        }

        return FALSE;
}

static void
gsd_osd_window_real_show (GtkWidget *widget)
{
        GsdOsdWindow *window;

        if (GTK_WIDGET_CLASS (gsd_osd_window_parent_class)->show) {
                GTK_WIDGET_CLASS (gsd_osd_window_parent_class)->show (widget);
        }

        window = GSD_OSD_WINDOW (widget);
        remove_hide_timeout (window);
        add_hide_timeout (window);
}

static void
gsd_osd_window_real_hide (GtkWidget *widget)
{
        GsdOsdWindow *window;

        if (GTK_WIDGET_CLASS (gsd_osd_window_parent_class)->hide) {
                GTK_WIDGET_CLASS (gsd_osd_window_parent_class)->hide (widget);
        }

        window = GSD_OSD_WINDOW (widget);
        remove_hide_timeout (window);
}

static void
gsd_osd_window_real_realize (GtkWidget *widget)
{
        cairo_region_t *region;
        GdkScreen *screen;
        GdkVisual *visual;

        screen = gtk_widget_get_screen (widget);
        visual = gdk_screen_get_rgba_visual (screen);
        if (visual == NULL) {
                visual = gdk_screen_get_system_visual (screen);
        }

        gtk_widget_set_visual (widget, visual);

        if (GTK_WIDGET_CLASS (gsd_osd_window_parent_class)->realize) {
                GTK_WIDGET_CLASS (gsd_osd_window_parent_class)->realize (widget);
        }

        /* make the whole window ignore events */
        region = cairo_region_create ();
        gtk_widget_input_shape_combine_region (widget, region);
        cairo_region_destroy (region);
}

static GObject *
gsd_osd_window_constructor (GType                  type,
                            guint                  n_construct_properties,
                            GObjectConstructParam *construct_params)
{
        GObject *object;

        object = G_OBJECT_CLASS (gsd_osd_window_parent_class)->constructor (type, n_construct_properties, construct_params);

        g_object_set (object,
                      "type", GTK_WINDOW_POPUP,
                      "type-hint", GDK_WINDOW_TYPE_HINT_NOTIFICATION,
                      "skip-taskbar-hint", TRUE,
                      "skip-pager-hint", TRUE,
                      "focus-on-map", FALSE,
                      NULL);

        return object;
}

static void
gsd_osd_window_finalize (GObject *object)
{
	GsdOsdWindow *window;

	window = GSD_OSD_WINDOW (object);

	g_clear_object (&window->priv->icon);
	g_clear_pointer (&window->priv->volume_label, g_free);

	if (window->priv->monitors_changed_id > 0) {
		GdkScreen *screen;
		screen = gtk_widget_get_screen (GTK_WIDGET (object));
		g_signal_handler_disconnect (G_OBJECT (screen), window->priv->monitors_changed_id);
		window->priv->monitors_changed_id = 0;
	}

	G_OBJECT_CLASS (gsd_osd_window_parent_class)->finalize (object);
}

static void
gsd_osd_window_class_init (GsdOsdWindowClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        gobject_class->constructor = gsd_osd_window_constructor;
        gobject_class->finalize = gsd_osd_window_finalize;

        widget_class->show = gsd_osd_window_real_show;
        widget_class->hide = gsd_osd_window_real_hide;
        widget_class->realize = gsd_osd_window_real_realize;
        widget_class->draw = gsd_osd_window_obj_draw;

        g_type_class_add_private (klass, sizeof (GsdOsdWindowPrivate));
}

/**
 * gsd_osd_window_is_valid:
 * @window: a #GsdOsdWindow
 *
 * Return value: TRUE if the @window's idea of the screen geometry is the
 * same as the current screen's.
 */
gboolean
gsd_osd_window_is_valid (GsdOsdWindow *window)
{
	return window->priv->monitor_changed;
}

static void
monitors_changed_cb (GdkScreen    *screen,
		     GsdOsdWindow *window)
{
        gint primary_monitor;
        GdkRectangle mon_rect;

	primary_monitor = gdk_screen_get_primary_monitor (screen);
	if (primary_monitor != window->priv->primary_monitor) {
		window->priv->monitor_changed = TRUE;
		return;
	}

	gdk_screen_get_monitor_geometry (screen, primary_monitor, &mon_rect);

        if (window->priv->screen_width != mon_rect.width ||
            window->priv->screen_height != mon_rect.height)
                window->priv->monitor_changed = TRUE;
}

static void
gsd_osd_window_init (GsdOsdWindow *window)
{
        GdkScreen *screen;
        gdouble scalew, scaleh, scale;
        GdkRectangle monitor;
        int size;

        window->priv = GSD_OSD_WINDOW_GET_PRIVATE (window);

        screen = gtk_widget_get_screen (GTK_WIDGET (window));
        window->priv->monitors_changed_id = g_signal_connect (G_OBJECT (screen), "monitors-changed",
                                                              G_CALLBACK (monitors_changed_cb), window);

        window->priv->primary_monitor = gdk_screen_get_primary_monitor (screen);
        gdk_screen_get_monitor_geometry (screen, window->priv->primary_monitor, &monitor);
        window->priv->screen_width = monitor.width;
        window->priv->screen_height = monitor.height;

        gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
        gtk_widget_set_app_paintable (GTK_WIDGET (window), TRUE);

        /* assume 110x110 on a 640x480 display and scale from there */
        scalew = monitor.width / 640.0;
        scaleh = monitor.height / 480.0;
        scale = MIN (scalew, scaleh);
        size = 110 * MAX (1, scale);
        gtk_window_set_default_size (GTK_WINDOW (window), size, size);

        window->priv->fade_out_alpha = 1.0;
}

GtkWidget *
gsd_osd_window_new (void)
{
        return g_object_new (GSD_TYPE_OSD_WINDOW, NULL);
}

/**
 * gsd_osd_window_update_and_hide:
 * @window: a #GsdOsdWindow
 *
 * Queues the @window for immediate drawing, and queues a timer to hide the window.
 */
static void
gsd_osd_window_update_and_hide (GsdOsdWindow *window)
{
        remove_hide_timeout (window);
        add_hide_timeout (window);

        gtk_widget_queue_draw (GTK_WIDGET (window));
}
