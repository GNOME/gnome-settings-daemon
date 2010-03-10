/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2001 Udaltsoft
 *
 * Written by Sergey V. Oudaltsov <svu@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <libgnomekbd/gkbd-desktop-config.h>
#include <libgnomekbd/gkbd-keyboard-config.h>

#include "gsd-xmodmap.h"
#include "gsd-keyboard-xkb.h"
#include "delayed-dialog.h"
#include "gnome-settings-profile.h"

static GsdKeyboardManager *manager = NULL;

static XklEngine *xkl_engine;
static XklConfigRegistry *xkl_registry = NULL;

static GkbdDesktopConfig current_config;
static GkbdKeyboardConfig current_kbd_config;

/* never terminated */
static GkbdKeyboardConfig initial_sys_kbd_config;

static gboolean inited_ok = FALSE;

static guint notify_desktop = 0;
static guint notify_keyboard = 0;

static PostActivationCallback pa_callback = NULL;
static void *pa_callback_user_data = NULL;

static const char KNOWN_FILES_KEY[] =
    "/desktop/gnome/peripherals/keyboard/general/known_file_list";

static const char *gdm_keyboard_layout = NULL;

#define noGSDKX

#ifdef GSDKX
static FILE *logfile;

static void
gsd_keyboard_log_appender (const char file[],
			   const char function[],
			   int level, const char format[], va_list args)
{
	time_t now = time (NULL);
	fprintf (logfile, "[%08ld,%03d,%s:%s/] \t", now,
		 level, file, function);
	vfprintf (logfile, format, args);
	fflush (logfile);
}
#endif

static void
activation_error (void)
{
	char const *vendor = ServerVendor (GDK_DISPLAY ());
	int release = VendorRelease (GDK_DISPLAY ());
	GtkWidget *dialog;
	gboolean badXFree430Release;

	badXFree430Release = (vendor != NULL)
	    && (0 == strcmp (vendor, "The XFree86 Project, Inc"))
	    && (release / 100000 == 403);

	/* VNC viewers will not work, do not barrage them with warnings */
	if (NULL != vendor && NULL != strstr (vendor, "VNC"))
		return;

	dialog = gtk_message_dialog_new_with_markup (NULL,
						     0,
						     GTK_MESSAGE_ERROR,
						     GTK_BUTTONS_CLOSE,
						     _
						     ("Error activating XKB configuration.\n"
						      "It can happen under various circumstances:\n"
						      "- a bug in libxklavier library\n"
						      "- a bug in X server (xkbcomp, xmodmap utilities)\n"
						      "- X server with incompatible libxkbfile implementation\n\n"
						      "X server version data:\n%s\n%d\n%s\n"
						      "If you report this situation as a bug, please include:\n"
						      "- The result of <b>%s</b>\n"
						      "- The result of <b>%s</b>"),
						     vendor,
						     release,
						     badXFree430Release
						     ?
						     _
						     ("You are using XFree 4.3.0.\n"
						      "There are known problems with complex XKB configurations.\n"
						      "Try using a simpler configuration or taking a fresher version of XFree software.")
						     : "",
						     "xprop -root | grep XKB",
						     "gconftool-2 -R /desktop/gnome/peripherals/keyboard/kbd");
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gtk_widget_destroy), NULL);
	gsd_delayed_show_dialog (dialog);
}

static void
apply_desktop_settings (void)
{
	if (!inited_ok)
		return;

	gsd_keyboard_manager_apply_settings (manager);
	gkbd_desktop_config_load_from_gconf (&current_config);
	/* again, probably it would be nice to compare things
	   before activating them */
	gkbd_desktop_config_activate (&current_config);
}

static gboolean
try_activating_xkb_config_if_new (GkbdKeyboardConfig *current_sys_kbd_config)
{
	/* Activate - only if different! */
	if (!gkbd_keyboard_config_equals
	    (&current_kbd_config, current_sys_kbd_config)) {
		if (gkbd_keyboard_config_activate (&current_kbd_config)) {
			if (pa_callback != NULL) {
				(*pa_callback) (pa_callback_user_data);
				return TRUE;
			}
		} else {
		       return FALSE;
		}
	}
	return TRUE;
}

static gboolean
filter_xkb_config (void)
{
	XklConfigItem *item;
	gchar *lname;
	gchar *vname;
	GSList *lv;
	GSList *filtered;
	gboolean any_change = FALSE;

	xkl_debug (100, "Filtering configuration against the registry\n");
	if (!xkl_registry) {
		xkl_registry = xkl_config_registry_get_instance (xkl_engine);
		/* load all materials, unconditionally! */
		if (!xkl_config_registry_load (xkl_registry, TRUE)) {
			g_object_unref (xkl_registry);
			xkl_registry = NULL;
			return FALSE;
		}
	}
	lv = current_kbd_config.layouts_variants;
	item = xkl_config_item_new ();
	while (lv) {
		xkl_debug (100, "Checking [%s]\n", lv->data);
		if (gkbd_keyboard_config_split_items(lv->data, &lname, &vname)) {
			g_snprintf (item->name, sizeof (item->name), "%s", lname);
			if (!xkl_config_registry_find_layout (xkl_registry, item)) {
			xkl_debug (100, "Bad layout [%s]\n", lname);
				filtered = lv;
				lv = lv->next;
				g_free (filtered->data);
				current_kbd_config.layouts_variants = g_slist_delete_link (current_kbd_config.layouts_variants,
											   filtered);
				any_change = TRUE;
				continue;
			}
			if (vname) {
				g_snprintf (item->name, sizeof (item->name), "%s", vname);
				if (!xkl_config_registry_find_variant (xkl_registry, lname, item)) {
				xkl_debug(100, "Bad variant [%s(%s)]\n", lname, vname);
					filtered = lv;
					lv = lv->next;
					g_free (filtered->data);
					current_kbd_config.layouts_variants = g_slist_delete_link (current_kbd_config.layouts_variants,
												   filtered);
					any_change = TRUE;
					continue;
				}
			}
		}
		lv = lv->next;
	}
	g_object_unref(item);
	return any_change;
}

static void
apply_xkb_settings (void)
{
	GConfClient *conf_client;
	GkbdKeyboardConfig current_sys_kbd_config;
	int group_to_activate = -1;
	char *gdm_layout;
	char *s;

	if (!inited_ok)
		return;

	conf_client = gconf_client_get_default ();

	/* With GDM the user can already set a layout from the login
	 * screen. Try to keep that setting.
	 * We clear gdm_keyboard_layout early, so we don't risk
	 * recursion from gconf notification.
	 */
	gdm_layout = g_strdup (gdm_keyboard_layout);
	gdm_keyboard_layout = NULL;

	/* gdm's configuration and $GDM_KEYBOARD_LAYOUT separates layout and
	 * variant with a space, but gconf uses tabs; so convert to be robust
	 * with both */
	for (s = gdm_layout; s && *s; ++s) {
		if (*s == ' ') {
			*s = '\t';
		}
	}

	if (gdm_layout != NULL) {
		GSList *layouts;
		GSList *found_node;
		int     max_groups;

		max_groups = MAX (xkl_engine_get_max_num_groups (xkl_engine), 1);
		layouts = gconf_client_get_list (conf_client,
						 GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS,
						 GCONF_VALUE_STRING, NULL);

		/* Add the layout if it doesn't already exist. XKB limits the
		 * total number of layouts. If we already have the maximum
		 * number of layouts configured, we replace the last one. This
		 * prevents the list from becoming full if the user has a habit
		 * of selecting many different keyboard layouts in GDM. */

		found_node = g_slist_find_custom (layouts, gdm_layout, (GCompareFunc) g_strcmp0);

		if (!found_node) {
			/* Insert at the last valid place, or at the end of
			 * list, whichever comes first */
			layouts = g_slist_insert (layouts, g_strdup (gdm_layout), max_groups - 1);
			if (g_slist_length (layouts) > max_groups) {
				GSList *last;
				GSList *free_layouts;

				last = g_slist_nth (layouts, max_groups - 1);
				free_layouts = last->next;
				last->next = NULL;

				g_slist_foreach (free_layouts, (GFunc) g_free, NULL);
				g_slist_free (free_layouts);
			}

			gconf_client_set_list (conf_client,
					       GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS,
					       GCONF_VALUE_STRING, layouts,
					       NULL);
		}

		g_slist_foreach (layouts, (GFunc) g_free, NULL);
		g_slist_free (layouts);
	}

	gkbd_keyboard_config_init (&current_sys_kbd_config,
				   conf_client, xkl_engine);

	gkbd_keyboard_config_load_from_gconf (&current_kbd_config,
					      &initial_sys_kbd_config);

	gkbd_keyboard_config_load_from_x_current (&current_sys_kbd_config,
						  NULL);

	if (gdm_layout != NULL) {
		/* If there are multiple layouts,
		 * try to find the one closest to the gdm layout
		 */
		GSList *l;
		int i;
		size_t len = strlen (gdm_layout);
		for (i = 0, l = current_kbd_config.layouts_variants; l;
		     i++, l = l->next) {
			char *lv = l->data;
			if (strncmp (lv, gdm_layout, len) == 0
			    && (lv[len] == '\0' || lv[len] == '\t')) {
				group_to_activate = i;
				break;
			}
		}
	}

	g_free (gdm_layout);

	if (!try_activating_xkb_config_if_new (&current_sys_kbd_config)) {
		if (filter_xkb_config ()) {
			if (!try_activating_xkb_config_if_new (&current_sys_kbd_config)) {
				g_warning ("Could not activate the filtered XKB configuration");
				activation_error ();
			}
		} else {	
			g_warning ("Could not activate the XKB configuration");
			activation_error ();
		}
	} else
		xkl_debug (100,
			   "Actual KBD configuration was not changed: redundant notification\n");

	if (group_to_activate != -1)
		xkl_engine_lock_group (current_config.engine,
				       group_to_activate);
	gkbd_keyboard_config_term (&current_sys_kbd_config);
}

static void
gsd_keyboard_xkb_analyze_sysconfig (void)
{
	GConfClient *conf_client;

	if (!inited_ok)
		return;

	conf_client = gconf_client_get_default ();
	gkbd_keyboard_config_init (&initial_sys_kbd_config,
				   conf_client, xkl_engine);
	gkbd_keyboard_config_load_from_x_initial (&initial_sys_kbd_config,
						  NULL);
	g_object_unref (conf_client);
}

static gboolean
gsd_chk_file_list (void)
{
	GDir *home_dir;
	const char *fname;
	GSList *file_list = NULL;
	GSList *last_login_file_list = NULL;
	GSList *tmp = NULL;
	GSList *tmp_l = NULL;
	gboolean new_file_exist = FALSE;
	GConfClient *conf_client;

	home_dir = g_dir_open (g_get_home_dir (), 0, NULL);
	while ((fname = g_dir_read_name (home_dir)) != NULL) {
		if (g_strrstr (fname, "modmap")) {
			file_list =
			    g_slist_append (file_list, g_strdup (fname));
		}
	}
	g_dir_close (home_dir);

	conf_client = gconf_client_get_default ();

	last_login_file_list = gconf_client_get_list (conf_client,
						      KNOWN_FILES_KEY,
						      GCONF_VALUE_STRING,
						      NULL);

	/* Compare between the two file list, currently available modmap files
	   and the files available in the last log in */
	tmp = file_list;
	while (tmp != NULL) {
		tmp_l = last_login_file_list;
		new_file_exist = TRUE;
		while (tmp_l != NULL) {
			if (strcmp (tmp->data, tmp_l->data) == 0) {
				new_file_exist = FALSE;
				break;
			} else {
				tmp_l = tmp_l->next;
			}
		}
		if (new_file_exist) {
			break;
		} else {
			tmp = tmp->next;
		}
	}

	if (new_file_exist) {
		gconf_client_set_list (conf_client,
				       KNOWN_FILES_KEY,
				       GCONF_VALUE_STRING,
				       file_list, NULL);
	}

	g_object_unref (conf_client);

	g_slist_foreach (file_list, (GFunc) g_free, NULL);
	g_slist_free (file_list);

	g_slist_foreach (last_login_file_list, (GFunc) g_free, NULL);
	g_slist_free (last_login_file_list);

	return new_file_exist;

}

static void
gsd_keyboard_xkb_chk_lcl_xmm (void)
{
	if (gsd_chk_file_list ()) {
		gsd_modmap_dialog_call ();
	}
	gsd_load_modmap_files ();
}

void
gsd_keyboard_xkb_set_post_activation_callback (PostActivationCallback fun,
					       void *user_data)
{
	pa_callback = fun;
	pa_callback_user_data = user_data;
}

static GdkFilterReturn
gsd_keyboard_xkb_evt_filter (GdkXEvent * xev, GdkEvent * event)
{
	XEvent *xevent = (XEvent *) xev;
	xkl_engine_filter_events (xkl_engine, xevent);
	return GDK_FILTER_CONTINUE;
}

static guint
register_config_callback (GConfClient * client,
			  const char *path, GConfClientNotifyFunc func)
{
	gconf_client_add_dir (client, path, GCONF_CLIENT_PRELOAD_ONELEVEL,
			      NULL);
	return gconf_client_notify_add (client, path, func, NULL, NULL,
					NULL);
}

/* When new Keyboard is plugged in - reload the settings */
static void
gsd_keyboard_new_device (XklEngine * engine)
{
	apply_desktop_settings ();
	apply_xkb_settings ();
}

void
gsd_keyboard_xkb_init (GConfClient * client,
		       GsdKeyboardManager * kbd_manager)
{
	gnome_settings_profile_start (NULL);
#ifdef GSDKX
	xkl_set_debug_level (200);
	logfile = fopen ("/tmp/gsdkx.log", "a");
	xkl_set_log_appender (gsd_keyboard_log_appender);
#endif
	manager = kbd_manager;
	gnome_settings_profile_start ("xkl_engine_get_instance");
	xkl_engine = xkl_engine_get_instance (GDK_DISPLAY ());
	gnome_settings_profile_end ("xkl_engine_get_instance");
	if (xkl_engine) {
		inited_ok = TRUE;

		gdm_keyboard_layout = g_getenv ("GDM_KEYBOARD_LAYOUT");

		gkbd_desktop_config_init (&current_config,
					  client, xkl_engine);
		gkbd_keyboard_config_init (&current_kbd_config,
					   client, xkl_engine);
		xkl_engine_backup_names_prop (xkl_engine);
		gsd_keyboard_xkb_analyze_sysconfig ();
		gnome_settings_profile_start
		    ("gsd_keyboard_xkb_chk_lcl_xmm");
		gsd_keyboard_xkb_chk_lcl_xmm ();
		gnome_settings_profile_end
		    ("gsd_keyboard_xkb_chk_lcl_xmm");

		notify_desktop =
		    register_config_callback (client,
					      GKBD_DESKTOP_CONFIG_DIR,
					      (GConfClientNotifyFunc)
					      apply_desktop_settings);

		notify_keyboard =
		    register_config_callback (client,
					      GKBD_KEYBOARD_CONFIG_DIR,
					      (GConfClientNotifyFunc)
					      apply_xkb_settings);

		gdk_window_add_filter (NULL, (GdkFilterFunc)
				       gsd_keyboard_xkb_evt_filter, NULL);

		if (xkl_engine_get_features (xkl_engine) &
		    XKLF_DEVICE_DISCOVERY)
			g_signal_connect (xkl_engine, "X-new-device",
					  G_CALLBACK
					  (gsd_keyboard_new_device), NULL);

		gnome_settings_profile_start ("xkl_engine_start_listen");
		xkl_engine_start_listen (xkl_engine,
					 XKLL_MANAGE_LAYOUTS |
					 XKLL_MANAGE_WINDOW_STATES);
		gnome_settings_profile_end ("xkl_engine_start_listen");

		gnome_settings_profile_start ("apply_desktop_settings");
		apply_desktop_settings ();
		gnome_settings_profile_end ("apply_desktop_settings");
		gnome_settings_profile_start ("apply_xkb_settings");
		apply_xkb_settings ();
		gnome_settings_profile_end ("apply_xkb_settings");
	}
	gnome_settings_profile_end (NULL);
}

void
gsd_keyboard_xkb_shutdown (void)
{
	GConfClient *client;

	pa_callback = NULL;
	pa_callback_user_data = NULL;
	manager = NULL;

	if (!inited_ok)
		return;

	xkl_engine_stop_listen (xkl_engine);

	gdk_window_remove_filter (NULL,
				  (GdkFilterFunc)
				  gsd_keyboard_xkb_evt_filter, NULL);

	client = gconf_client_get_default ();

	if (notify_desktop != 0) {
		gconf_client_remove_dir (client, GKBD_DESKTOP_CONFIG_DIR,
					 NULL);
		gconf_client_notify_remove (client, notify_desktop);
		notify_desktop = 0;
	}

	if (notify_keyboard != 0) {
		gconf_client_remove_dir (client, GKBD_KEYBOARD_CONFIG_DIR,
					 NULL);
		gconf_client_notify_remove (client, notify_keyboard);
		notify_keyboard = 0;
	}

	if (xkl_registry) {
		g_object_unref (xkl_registry);
	}

	g_object_unref (client);
	g_object_unref (xkl_engine);

	xkl_engine = NULL;
	inited_ok = FALSE;
}
