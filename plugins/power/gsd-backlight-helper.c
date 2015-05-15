/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010-2011 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <glib-object.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <gudev/gudev.h>

#include "gsd-backlight-linux.h"

#define GSD_BACKLIGHT_HELPER_EXIT_CODE_SUCCESS			0
#define GSD_BACKLIGHT_HELPER_EXIT_CODE_FAILED			1
#define GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID	3
#define GSD_BACKLIGHT_HELPER_EXIT_CODE_INVALID_USER		4
#define GSD_BACKLIGHT_HELPER_EXIT_CODE_NO_DEVICES		5

static gboolean
gsd_backlight_helper_write (const gchar *filename, gint value, GError **error)
{
	gchar *filename_path = NULL;
	gchar *text = NULL;
	gint retval;
	gint length;
	gint fd = -1;
	gboolean ret = TRUE;

	filename_path = g_build_filename (filename, "brightness", NULL);

	fd = open (filename_path, O_WRONLY);
	if (fd < 0) {
		ret = FALSE;
		g_set_error (error, 1, 0, "failed to open filename: %s", filename);
		goto out;
	}

	/* convert to text */
	text = g_strdup_printf ("%i", value);
	length = strlen (text);

	/* write to device file */
	retval = write (fd, text, length);
	if (retval != length) {
		ret = FALSE;
		g_set_error (error, 1, 0, "writing '%s' to %s failed", text, filename);
		goto out;
	}
out:
	if (fd >= 0)
		close (fd);
	g_free (text);
	g_free (filename_path);
	return ret;
}

static gint
gsd_backlight_helper_read_value (const gchar *filename, GError **error)
{
	gchar *contents = NULL;
	gint value;

	if (g_file_get_contents (filename, &contents, NULL, error))
		value = atoi (contents);
	else
		value = -1;
	g_free (contents);

	if (value < 0 && *error == NULL)
		g_set_error (error, 1, 0, "got invalid backlight value from %s", filename);

	return value;
}

static gint
gsd_backlight_helper_get (const gchar *filename, GError **error)
{
	gchar *filename_path = NULL;
	gint value;

	filename_path = g_build_filename (filename, "brightness", NULL);
	value = gsd_backlight_helper_read_value (filename_path, error);
	g_free (filename_path);
	return value;
}

static gint
gsd_backlight_helper_get_max (const gchar *filename, GError **error)
{
	gchar *filename_path = NULL;
	gint value;

	filename_path = g_build_filename (filename, "max_brightness", NULL);
	value = gsd_backlight_helper_read_value (filename_path, error);
	g_free (filename_path);
	return value;
}

static gint
clamp_minimum (gint max, gint value)
{
	gint minimum;
	/* If the interface has less than 100 possible values, it's
	 * likely that 0 doesn't turn the backlight off so we let 0 be
	 * set in that case. */
	if (max > 99)
		minimum = 1;
	else
		minimum = 0;

	return MAX (value, minimum);
}

int
main (int argc, char *argv[])
{
	GOptionContext *context;
	gint uid;
	gint euid;
	guint retval = 0;
	GError *error = NULL;
	gint set_brightness = -1;
	gboolean get_brightness = FALSE;
	gboolean get_max_brightness = FALSE;
	gchar *filename = NULL;
	GsdBacklightType type;

	const GOptionEntry options[] = {
		{ "set-brightness", '\0', 0, G_OPTION_ARG_INT, &set_brightness,
		   /* command line argument */
		  "Set the current brightness", NULL },
		{ "get-brightness", '\0', 0, G_OPTION_ARG_NONE, &get_brightness,
		   /* command line argument */
		  "Get the current brightness", NULL },
		{ "get-max-brightness", '\0', 0, G_OPTION_ARG_NONE, &get_max_brightness,
		   /* command line argument */
		  "Get the number of brightness levels supported", NULL },
		{ NULL}
	};

	context = g_option_context_new (NULL);
	g_option_context_set_summary (context, "GNOME Settings Daemon Backlight Helper");
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

#ifndef __linux__
	/* the g-s-d plugin should only call this helper on linux */
	g_critical ("Attempting to call gsb-backlight-helper on non-Linux");
	g_assert_not_reached ();
#endif

	/* no input */
	if (set_brightness == -1 && !get_brightness && !get_max_brightness) {
		g_print ("%s\n", "No valid option was specified");
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
		goto out;
	}

	/* find device */
	filename = gsd_backlight_helper_get_best_backlight (&type);
	if (filename == NULL) {
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_NO_DEVICES;
		g_print ("%s: %s\n",
			 "Could not get or set the value of the backlight",
			 "No backlight devices present");
		goto out;
	}

	/* GetBrightness */
	if (get_brightness) {
		gint value;
		value = gsd_backlight_helper_get (filename, &error);
		if (value < 0) {
			g_print ("%s: %s\n",
				 "Could not get the value of the backlight",
				 error->message);
			g_error_free (error);
			retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
			goto out;
		}

		/* just print the contents to stdout */
		g_print ("%d", value);
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_SUCCESS;
		goto out;
	}

	/* GetSteps */
	if (get_max_brightness) {
		gint value;
		value = gsd_backlight_helper_get_max (filename, &error);
		if (value < 0) {
			g_print ("%s: %s\n",
				 "Could not get the maximum value of the backlight",
				 error->message);
			g_error_free (error);
			retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
			goto out;
		}

		/* just print the contents to stdout */
		g_print ("%d", value);
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_SUCCESS;
		goto out;
	}

	/* check calling UID */
	uid = getuid ();
	euid = geteuid ();
	if (uid != 0 || euid != 0) {
		g_print ("%s\n",
			 "This program can only be used by the root user");
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
		goto out;
	}

	/* SetBrightness */
	if (set_brightness != -1) {
		gboolean ret = FALSE;
		gint max = gsd_backlight_helper_get_max (filename, &error);

		if (max < 0) {
			g_print ("%s: %s\n",
				 "Could not get the maximum value of the backlight",
				 error->message);
			g_error_free (error);
			retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
			goto out;
		}

		if (type == GSD_BACKLIGHT_TYPE_RAW)
			set_brightness = clamp_minimum (max, set_brightness);

		ret = gsd_backlight_helper_write (filename, set_brightness, &error);
		if (!ret) {
			g_print ("%s: %s\n",
				 "Could not set the value of the backlight",
				 error->message);
			g_error_free (error);
			retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
			goto out;
		}
	}

	/* success */
	retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_SUCCESS;
out:
	g_free (filename);
	return retval;
}

