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

#include <unistd.h>
#include <glib-object.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#define GSD_BACKLIGHT_HELPER_EXIT_CODE_SUCCESS			0
#define GSD_BACKLIGHT_HELPER_EXIT_CODE_FAILED			1
#define GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID	3
#define GSD_BACKLIGHT_HELPER_EXIT_CODE_INVALID_USER		4

#define GSD_BACKLIGHT_HELPER_SYSFS_LOCATION			"/sys/class/backlight"

static gchar *
gsd_backlight_helper_get_best_backlight ()
{
	gchar *filename;
	guint i;
	gboolean ret;
	GDir *dir = NULL;
	GError *error = NULL;
	const gchar *first_device;

	/* available kernel interfaces in priority order */
	static const gchar *backlight_interfaces[] = {
		"nv_backlight",
		"asus_laptop",
		"toshiba",
		"eeepc",
		"thinkpad_screen",
		"dell_backlight",
		"acpi_video1",
		"mbp_backlight",
		"acpi_video0",
		"fujitsu-laptop",
		"sony",
		"samsung",
		NULL,
	};

	/* search each one */
	for (i=0; backlight_interfaces[i] != NULL; i++) {
		filename = g_build_filename (GSD_BACKLIGHT_HELPER_SYSFS_LOCATION,
					     backlight_interfaces[i], NULL);
		ret = g_file_test (filename, G_FILE_TEST_EXISTS);
		if (ret)
			goto out;
		g_free (filename);
	}

	/* nothing found in the ordered list */
	filename = NULL;

	/* find any random ones */
	dir = g_dir_open (GSD_BACKLIGHT_HELPER_SYSFS_LOCATION, 0, &error);
	if (dir == NULL) {
		g_warning ("failed to find any devices: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get first device if any */
	first_device = g_dir_read_name (dir);
	if (first_device != NULL) {
		filename = g_build_filename (GSD_BACKLIGHT_HELPER_SYSFS_LOCATION,
					     first_device, NULL);
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	return filename;
}

static gboolean
gsd_backlight_helper_write (const gchar *filename, gint value, GError **error)
{
	gchar *text = NULL;
	gint retval;
	gint length;
	gint fd = -1;
	gboolean ret = TRUE;

	fd = open (filename, O_WRONLY);
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
	return ret;
}

int
main (int argc, char *argv[])
{
	GOptionContext *context;
	gint uid;
	gint euid;
	guint retval = 0;
	const gchar *pkexec_uid_str;
	GError *error = NULL;
	gboolean ret = FALSE;
	gint set_brightness = -1;
	gboolean get_brightness = FALSE;
	gboolean get_max_brightness = FALSE;
	gchar *filename = NULL;
	gchar *filename_file = NULL;
	gchar *contents = NULL;

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

	/* setup type system */
	g_type_init ();

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
	filename = gsd_backlight_helper_get_best_backlight ();
	if (filename == NULL) {
		g_print ("%s\n", "No backlights were found on your system");
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_INVALID_USER;
		goto out;
	}

	/* GetBrightness */
	if (get_brightness) {
		filename_file = g_build_filename (filename, "brightness", NULL);
		ret = g_file_get_contents (filename_file, &contents, NULL, &error);
		if (!ret) {
			g_print ("%s: %s\n",
				 "Could not get the value of the backlight",
				 error->message);
			g_error_free (error);
			retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
			goto out;
		}

		/* just print the contents to stdout */
		g_print ("%s", contents);
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_SUCCESS;
		goto out;
	}

	/* GetSteps */
	if (get_max_brightness) {
		filename_file = g_build_filename (filename, "max_brightness", NULL);
		ret = g_file_get_contents (filename_file, &contents, NULL, &error);
		if (!ret) {
			g_print ("%s: %s\n",
				 "Could not get the maximum value of the backlight",
				 error->message);
			g_error_free (error);
			retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
			goto out;
		}

		/* just print the contents to stdout */
		g_print ("%s", contents);
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_SUCCESS;
		goto out;
	}

	/* get calling process */
	uid = getuid ();
	euid = geteuid ();
	if (uid != 0 || euid != 0) {
		g_print ("%s\n",
			 "This program can only be used by the root user");
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_ARGUMENTS_INVALID;
		goto out;
	}

	/* check we're not being spoofed */
	pkexec_uid_str = g_getenv ("PKEXEC_UID");
	if (pkexec_uid_str == NULL) {
		g_print ("%s\n",
			 "This program must only be run through pkexec");
		retval = GSD_BACKLIGHT_HELPER_EXIT_CODE_INVALID_USER;
		goto out;
	}

	/* SetBrightness */
	if (set_brightness != -1) {
		filename_file = g_build_filename (filename, "brightness", NULL);
		ret = gsd_backlight_helper_write (filename_file, set_brightness, &error);
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
	g_free (filename_file);
	g_free (contents);
	return retval;
}

