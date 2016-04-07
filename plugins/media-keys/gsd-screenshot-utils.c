/* gsd-screenshot-utils.c - utilities to take screenshots
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Adapted from gnome-screenshot code, which is
 *   Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu>
 *   Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
 *   Copyright (C) 2008-2012 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 */

#include <config.h>
#include <canberra-gtk.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <string.h>
#include <glib/gstdio.h>

#include "gsd-screenshot-utils.h"

#define SHELL_SCREENSHOT_BUS_NAME "org.gnome.Shell"
#define SHELL_SCREENSHOT_BUS_PATH "/org/gnome/Shell/Screenshot"
#define SHELL_SCREENSHOT_BUS_IFACE "org.gnome.Shell.Screenshot"

typedef enum {
  SCREENSHOT_TYPE_SCREEN,
  SCREENSHOT_TYPE_WINDOW,
  SCREENSHOT_TYPE_AREA
} ScreenshotType;

typedef struct {
  ScreenshotType type;
  gboolean copy_to_clipboard;

  GdkRectangle area_selection;
  gchar *save_filename;
  gchar *used_filename;

  GDBusConnection *connection;
} ScreenshotContext;

static void
screenshot_play_sound_effect (const gchar *event_id,
                              const gchar *event_desc)
{
  ca_context *c;

  c = ca_gtk_context_get ();
  ca_context_play (c, 0,
                   CA_PROP_EVENT_ID, event_id,
                   CA_PROP_EVENT_DESCRIPTION, event_desc,
                   CA_PROP_CANBERRA_CACHE_CONTROL, "permanent",
                   NULL);
}

static void
screenshot_context_free (ScreenshotContext *ctx)
{
  g_free (ctx->save_filename);
  g_free (ctx->used_filename);
  g_clear_object (&ctx->connection);
  g_slice_free (ScreenshotContext, ctx);
}

static void
screenshot_play_error_sound_effect (void)
{
  screenshot_play_sound_effect ("dialog-error", _("Unable to capture a screenshot"));
}

static void
screenshot_save_to_recent (ScreenshotContext *ctx)
{
  GFile *file = g_file_new_for_path (ctx->used_filename);
  gchar *uri = g_file_get_uri (file);

  gtk_recent_manager_add_item (gtk_recent_manager_get_default (), uri);

  g_free (uri);
  g_object_unref (file);
}

static void
screenshot_save_to_clipboard (ScreenshotContext *ctx)
{
  GdkPixbuf *screenshot;
  GtkClipboard *clipboard;
  GError *error = NULL;

  screenshot = gdk_pixbuf_new_from_file (ctx->used_filename, &error);
  if (error != NULL)
    {
      screenshot_play_error_sound_effect ();
      g_warning ("Failed to save a screenshot to clipboard: %s\n", error->message);
      g_error_free (error);
      return;
    }

  screenshot_play_sound_effect ("screen-capture", _("Screenshot taken"));
  clipboard = gtk_clipboard_get_for_display (gdk_display_get_default (),
                                             GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_image (clipboard, screenshot);

  /* remove the temporary file created by the shell */
  g_unlink (ctx->used_filename);
  g_object_unref (screenshot);
}

static void
bus_call_ready_cb (GObject *source,
                   GAsyncResult *res,
                   gpointer user_data)
{
  GError *error = NULL;
  ScreenshotContext *ctx = user_data;
  GVariant *variant;
  gboolean success;

  variant = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);

  if (error != NULL)
    {
      screenshot_play_error_sound_effect ();
      g_warning ("Failed to save a screenshot: %s\n", error->message);
      g_error_free (error);
      screenshot_context_free (ctx);

      return;
    }

  g_variant_get (variant, "(bs)", &success, &ctx->used_filename);

  if (success)
    {
      if (ctx->copy_to_clipboard)
        {
          screenshot_save_to_clipboard (ctx);
        }
      else
        {
          screenshot_play_sound_effect ("screen-capture", _("Screenshot taken"));
          screenshot_save_to_recent (ctx);
        }
    }

  screenshot_context_free (ctx);
  g_variant_unref (variant);
}

static void
screenshot_call_shell (ScreenshotContext *ctx)
{
  const gchar *method_name;
  GVariant *method_params;

  if (ctx->type == SCREENSHOT_TYPE_SCREEN)
    {
      method_name = "Screenshot";
      method_params = g_variant_new ("(bbs)",
                                     FALSE, /* include pointer */
                                     TRUE,  /* flash */
                                     ctx->save_filename);
    }
  else if (ctx->type == SCREENSHOT_TYPE_WINDOW)
    {
      method_name = "ScreenshotWindow";
      method_params = g_variant_new ("(bbbs)",
                                     TRUE,  /* include border */
                                     FALSE, /* include pointer */
                                     TRUE,  /* flash */
                                     ctx->save_filename);
    }
  else
    {
      method_name = "ScreenshotArea";
      method_params = g_variant_new ("(iiiibs)",
                                     ctx->area_selection.x, ctx->area_selection.y,
                                     ctx->area_selection.width, ctx->area_selection.height,
                                     TRUE, /* flash */
                                     ctx->save_filename);
    }

  g_dbus_connection_call (ctx->connection,
                          SHELL_SCREENSHOT_BUS_NAME,
                          SHELL_SCREENSHOT_BUS_PATH,
                          SHELL_SCREENSHOT_BUS_IFACE,
                          method_name,
                          method_params,
                          NULL,
                          G_DBUS_CALL_FLAGS_NO_AUTO_START,
                          -1,
                          NULL,
                          bus_call_ready_cb,
                          ctx);
}

static void
area_selection_ready_cb (GObject *source,
                         GAsyncResult *res,
                         gpointer user_data)
{
  GdkRectangle rectangle;
  ScreenshotContext *ctx = user_data;
  GVariant *geometry;

  geometry = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source),
                                            res, NULL);

  /* cancelled by the user */
  if (!geometry)
    {
      screenshot_context_free (ctx);
      return;
    }

  g_variant_get (geometry, "(iiii)",
                 &rectangle.x, &rectangle.y,
                 &rectangle.width, &rectangle.height);

  ctx->area_selection = rectangle;
  screenshot_call_shell (ctx);
  g_variant_unref (geometry);
}

static void
bus_connection_ready_cb (GObject *source,
                         GAsyncResult *res,
                         gpointer user_data)
{
  GError *error = NULL;
  ScreenshotContext *ctx = user_data;

  ctx->connection = g_bus_get_finish (res, &error);

  if (error != NULL)
    {
      screenshot_play_error_sound_effect ();
      g_warning ("Failed to save a screenshot: %s\n", error->message);
      g_error_free (error);
      screenshot_context_free (ctx);

      return;
    }

  if (ctx->type == SCREENSHOT_TYPE_AREA)
    g_dbus_connection_call (ctx->connection,
                            SHELL_SCREENSHOT_BUS_NAME,
                            SHELL_SCREENSHOT_BUS_PATH,
                            SHELL_SCREENSHOT_BUS_IFACE,
                            "SelectArea",
                            NULL,
                            NULL,
                            G_DBUS_CALL_FLAGS_NO_AUTO_START,
                            -1,
                            NULL,
                            area_selection_ready_cb,
                            ctx);
  else
    screenshot_call_shell (ctx);
}

static void
screenshot_take (ScreenshotContext *ctx)
{
  g_bus_get (G_BUS_TYPE_SESSION, NULL, bus_connection_ready_cb, ctx);
}

static gchar *
screenshot_build_tmp_path (void)
{
  gchar *path;
  gint fd;

  fd = g_file_open_tmp ("gnome-settings-daemon-screenshot-XXXXXX", &path, NULL);
  close (fd);

  return path;
}

static gchar *
screenshot_build_filename (void)
{
  char *file_name, *origin;
  GDateTime *d;

  d = g_date_time_new_now_local ();
  origin = g_date_time_format (d, "%Y-%m-%d %H-%M-%S");
  g_date_time_unref (d);

  /* translators: this is the name of the file that gets made up
   * with the screenshot */
  file_name = g_strdup_printf (_("Screenshot from %s"), origin);
  g_free (origin);

  return file_name;
}

static void
screenshot_check_name_ready (ScreenshotContext *ctx)
{
  if (ctx->copy_to_clipboard)
    ctx->save_filename = screenshot_build_tmp_path ();
  else
    ctx->save_filename = screenshot_build_filename ();

  screenshot_take (ctx);
}

void
gsd_screenshot_take (MediaKeyType key_type)
{
  ScreenshotContext *ctx = g_slice_new0 (ScreenshotContext);

  ctx->copy_to_clipboard = (key_type == SCREENSHOT_CLIP_KEY ||
                            key_type == WINDOW_SCREENSHOT_CLIP_KEY ||
                            key_type == AREA_SCREENSHOT_CLIP_KEY);

  switch (key_type)
    {
    case SCREENSHOT_KEY:
    case SCREENSHOT_CLIP_KEY:
      ctx->type = SCREENSHOT_TYPE_SCREEN;
      break;
    case WINDOW_SCREENSHOT_KEY:
    case WINDOW_SCREENSHOT_CLIP_KEY:
      ctx->type = SCREENSHOT_TYPE_WINDOW;
      break;
    case AREA_SCREENSHOT_KEY:
    case AREA_SCREENSHOT_CLIP_KEY:
      ctx->type = SCREENSHOT_TYPE_AREA;
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  screenshot_check_name_ready (ctx);
}
