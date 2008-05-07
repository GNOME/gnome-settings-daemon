/* gsd-locate-pointer.c
 *
 * Copyright (C) 2008 Carlos Garnacho  <carlos@imendio.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include "gsd-timeline.h"
#include "gsd-locate-pointer.h"

#define ANIMATION_LENGTH 750
#define WINDOW_SIZE 101
#define N_CIRCLES 4

/* All circles are supposed to be moving when progress
 * reaches 0.5, and each of them are supposed to long
 * for half of the progress, hence the need of 0.5 to
 * get the circles interval, and the multiplication
 * by 2 to know a circle progress */
#define CIRCLES_PROGRESS_INTERVAL (0.5 / N_CIRCLES)
#define CIRCLE_PROGRESS(p) (MIN (1., ((gdouble) (p) * 2.)))

typedef struct GsdLocatePointerData GsdLocatePointerData;

struct GsdLocatePointerData
{
  GsdTimeline *timeline;
  GtkWidget *widget; 
  GdkWindow *window;

  gdouble progress;
};

static GsdLocatePointerData *data = NULL;

static void
locate_pointer_paint (GsdLocatePointerData *data,
		      cairo_t              *cr,
		      gboolean              composite)
{
  GdkColor color;
  gdouble progress, circle_progress;
  gint width, height, i;

  progress = data->progress;
  gdk_drawable_get_size (data->window, &width, &height);
  color = data->widget->style->bg[GTK_STATE_SELECTED];

  cairo_set_source_rgba (cr, 1., 1., 1., 0.);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);

  for (i = 0; i <= N_CIRCLES; i++)
    {
      if (progress < 0.)
	break;

      circle_progress = MIN (1., (progress * 2));
      progress -= CIRCLES_PROGRESS_INTERVAL;

      if (circle_progress >= 1.)
	continue;

      if (composite)
	{
	  cairo_set_source_rgba (cr,
				 color.red / 65535.,
				 color.green / 65535.,
				 color.blue / 65535.,
				 1 - circle_progress);
	  cairo_arc (cr,
		     width / 2,
		     height / 2,
		     circle_progress * width / 2,
		     0, 2 * G_PI);

	  cairo_fill (cr);
	  cairo_stroke (cr);
	}
      else
	{
	  cairo_set_source_rgb (cr, 0., 0., 0.);
	  cairo_set_line_width (cr, 3.);
	  cairo_arc (cr,
		     width / 2,
		     height / 2,
		     circle_progress * width / 2,
		     0, 2 * G_PI);
	  cairo_stroke (cr);

	  cairo_set_source_rgb (cr, 1., 1., 1.);
	  cairo_set_line_width (cr, 1.);
	  cairo_arc (cr,
		     width / 2,
		     height / 2,
		     circle_progress * width / 2,
		     0, 2 * G_PI);
	  cairo_stroke (cr);
	}
    }
}

static gboolean
locate_pointer_expose (GtkWidget *widget,
		       GdkEventExpose *event,
		       gpointer        user_data)
{
  GsdLocatePointerData *data = (GsdLocatePointerData *) user_data;
  cairo_t *cr;
  GdkBitmap *mask;

  if (event->window != data->window)
    return FALSE;

  cr = gdk_cairo_create (data->window);

  if (gtk_widget_is_composited (data->widget))
    locate_pointer_paint (data, cr, TRUE);
  else
    {
      locate_pointer_paint (data, cr, FALSE);
      cairo_destroy (cr);

      /* create a bitmap for the shape, reuse the cairo_t to paint on it */
      mask = gdk_pixmap_new (data->window, WINDOW_SIZE, WINDOW_SIZE, 1);
      cr = gdk_cairo_create (mask);
      locate_pointer_paint (data, cr, FALSE);
      gdk_window_shape_combine_mask (data->window, mask, 0, 0);
      g_object_unref (mask);
    }

  cairo_destroy (cr);

  return TRUE;
}

static void
timeline_frame_cb (GsdTimeline *timeline,
		   gdouble      progress,
		   gpointer     user_data)
{
  GsdLocatePointerData *data = (GsdLocatePointerData *) user_data;

  if (gtk_widget_is_composited (data->widget))
    {
      gdk_window_invalidate_rect (data->window, NULL, FALSE);
      data->progress = progress;
    }
  else if (progress >= data->progress + CIRCLES_PROGRESS_INTERVAL)
    {
      /* only invalidate window each circle interval */
      gdk_window_invalidate_rect (data->window, NULL, FALSE);
      data->progress += CIRCLES_PROGRESS_INTERVAL;
    }
}

static void
set_transparent_shape (GdkWindow *window)
{
  GdkBitmap *mask;
  cairo_t *cr;

  mask = gdk_pixmap_new (data->window, WINDOW_SIZE, WINDOW_SIZE, 1);
  cr = gdk_cairo_create (mask);

  cairo_set_source_rgba (cr, 1., 1., 1., 0.);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint (cr);

  gdk_window_shape_combine_mask (data->window, mask, 0, 0);
  g_object_unref (mask);
  cairo_destroy (cr);
}

static void
timeline_finished_cb (GsdTimeline *timeline,
		      gpointer     user_data)
{
  GsdLocatePointerData *data = (GsdLocatePointerData *) user_data;

  /* set transparent shape and hide window */
  if (!gtk_widget_is_composited (data->widget))
    set_transparent_shape (data->window);

  gdk_window_hide (data->window);
}

static void
create_window (GsdLocatePointerData *data,
	       GdkScreen            *screen)
{
  GdkColormap *colormap;
  GdkVisual *visual;
  GdkWindowAttr attributes;

  colormap = gdk_screen_get_rgba_colormap (screen);
  visual = gdk_screen_get_rgba_visual (screen);

  if (!colormap)
    {
      colormap = gdk_screen_get_rgb_colormap (screen);
      visual = gdk_screen_get_rgb_visual (screen);
    }

  attributes.window_type = GDK_WINDOW_TEMP;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = visual;
  attributes.colormap = colormap;
  attributes.event_mask = GDK_VISIBILITY_NOTIFY_MASK | GDK_EXPOSURE_MASK;
  attributes.width = 1;
  attributes.height = 1;

  data->window = gdk_window_new (gdk_screen_get_root_window (screen),
				 &attributes,
				 GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP);

  gdk_window_set_user_data (data->window, data->widget);
}

static GsdLocatePointerData *
gsd_locate_pointer_data_new (GdkScreen *screen)
{
  GsdLocatePointerData *data;

  data = g_new0 (GsdLocatePointerData, 1);

  /* this widget will never be shown, it's
   * mainly used to get signals/events from
   */
  data->widget = gtk_window_new (GTK_WINDOW_POPUP);
  gtk_widget_realize (data->widget);

  g_signal_connect (G_OBJECT (data->widget), "expose_event",
                    G_CALLBACK (locate_pointer_expose),
                    data);

  data->timeline = gsd_timeline_new (ANIMATION_LENGTH);
  g_signal_connect (data->timeline, "frame",
		    G_CALLBACK (timeline_frame_cb), data);
  g_signal_connect (data->timeline, "finished",
		    G_CALLBACK (timeline_finished_cb), data);

  create_window (data, screen);

  return data;
}

static void
move_locate_pointer_window (GsdLocatePointerData *data,
			    GdkScreen            *screen)
{
  gint cursor_x, cursor_y;
  GdkBitmap *mask;
  GdkColor col;
  GdkGC *gc;

  gdk_window_get_pointer (gdk_screen_get_root_window (screen), &cursor_x, &cursor_y, NULL);

  gdk_window_move_resize (data->window,
                          cursor_x - WINDOW_SIZE / 2,
                          cursor_y - WINDOW_SIZE / 2,
                          WINDOW_SIZE, WINDOW_SIZE);

  col.pixel = 0;
  mask = gdk_pixmap_new (data->window, WINDOW_SIZE, WINDOW_SIZE, 1);

  gc = gdk_gc_new (mask);
  gdk_gc_set_foreground (gc, &col);
  gdk_draw_rectangle (mask, gc, TRUE, 0, 0, WINDOW_SIZE, WINDOW_SIZE);

  /* allow events to happen through the window */
  gdk_window_input_shape_combine_mask (data->window, mask, 0, 0);

  g_object_unref (mask);
  g_object_unref (gc);
}

void
gsd_locate_pointer (GdkScreen *screen)
{
  if (!data)
    data = gsd_locate_pointer_data_new (screen);

  gsd_timeline_pause (data->timeline);
  gsd_timeline_rewind (data->timeline);

  /* Create again the window if it is not for the current screen */
  if (gdk_screen_get_number (screen) != gdk_screen_get_number (gdk_drawable_get_screen (data->window)))
    {
      gdk_window_set_user_data (data->window, NULL);
      gdk_window_destroy (data->window);

      create_window (data, screen);
    }

  data->progress = 0.;

  if (!gtk_widget_is_composited (data->widget))
    set_transparent_shape (data->window);

  gdk_window_show (data->window);
  move_locate_pointer_window (data, screen);

  gsd_timeline_start (data->timeline);
}
