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

#include <libgnomekbd/gkbd-status.h>
#include <libgnomekbd/gkbd-keyboard-drawing.h>
#include <libgnomekbd/gkbd-desktop-config.h>
#include <libgnomekbd/gkbd-keyboard-config.h>
#include <libgnomekbd/gkbd-util.h>

#include "gsd-xmodmap.h"
#include "gsd-keyboard-xkb.h"
#include "delayed-dialog.h"
#include "gnome-settings-profile.h"

#define GTK_RESPONSE_PRINT 2

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

static const char DISABLE_INDICATOR_KEY[] =
    "/desktop/gnome/peripherals/keyboard/general/disable_indicator";

static const char DUPLICATE_LEDS_KEY[] =
    "/desktop/gnome/peripherals/keyboard/general/duplicate_leds";

static const char *gdm_keyboard_layout = NULL;

static GtkStatusIcon *icon = NULL;

static GHashTable *preview_dialogs = NULL;

static Atom caps_lock;
static Atom num_lock;
static Atom scroll_lock;

static GtkStatusIcon *indicator_icons[3];
static const gchar *indicator_on_icon_names[] = {
	"scrolllock_on",
	"numlock_on",
	"capslock_on"
};

static const gchar *indicator_off_icon_names[] = {
	"scrolllock_off",
	"numlock_off",
	"capslock_off"
};

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
						      "Try using a simpler configuration or using a later version of the XFree software.")
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
	GConfClient *conf_client;
	gboolean show_leds;
	int i;
	if (!inited_ok)
		return;

	gsd_keyboard_manager_apply_settings (manager);
	gkbd_desktop_config_load_from_gconf (&current_config);
	/* again, probably it would be nice to compare things
	   before activating them */
	gkbd_desktop_config_activate (&current_config);

	conf_client = gconf_client_get_default ();
	show_leds =
	    gconf_client_get_bool (conf_client, DUPLICATE_LEDS_KEY, NULL);
	g_object_unref (conf_client);
	for (i = sizeof (indicator_icons) / sizeof (indicator_icons[0]);
	     --i >= 0;) {
		gtk_status_icon_set_visible (indicator_icons[i],
					     show_leds);
	}
}

static void
popup_menu_launch_capplet ()
{
	GError *error = NULL;

	gdk_spawn_command_line_on_screen (gdk_screen_get_default (),
					  "gnome-keyboard-properties",
					  &error);

	if (error != NULL) {
		g_warning
		    ("Could not execute keyboard properties capplet: [%s]\n",
		     error->message);
		g_error_free (error);
	}
}

static void
show_layout_response (GtkWidget * dialog, gint resp)
{
	GdkRectangle rect;
	GtkWidget *kbdraw;
	const gchar *groupName;

	switch (resp) {
	case GTK_RESPONSE_HELP:
		gtk_show_uri (gtk_widget_get_screen (GTK_WIDGET (dialog)),
			      "ghelp:gswitchit?layout-view",
			      gtk_get_current_event_time (), NULL);
		return;
	case GTK_RESPONSE_CLOSE:
		gtk_window_get_position (GTK_WINDOW (dialog), &rect.x,
					 &rect.y);
		gtk_window_get_size (GTK_WINDOW (dialog), &rect.width,
				     &rect.height);
		gkbd_preview_save_position (&rect);
		gtk_widget_destroy (dialog);
		break;
	case GTK_RESPONSE_PRINT:
		kbdraw =
		    GTK_WIDGET (g_object_get_data
				(G_OBJECT (dialog), "kbdraw"));
		groupName =
		    (const gchar *) g_object_get_data (G_OBJECT (dialog),
						       "groupName");
		gkbd_keyboard_drawing_print (GKBD_KEYBOARD_DRAWING
					     (kbdraw), GTK_WINDOW (dialog),
					     groupName ? groupName :
					     _("Unknown"));
	}
}

static void
show_layout_destroy (GtkWidget * dialog, gint group)
{
	GtkBuilder *builder =
	    GTK_BUILDER (g_object_get_data
			 (G_OBJECT (dialog), "builderData"));
	g_object_unref (G_OBJECT (builder));
	g_hash_table_remove (preview_dialogs, GINT_TO_POINTER (group));
}

static void
popup_menu_show_layout ()
{
	static GkbdKeyboardDrawingGroupLevel groupsLevels[] = { {
								 0, 1}, {
									 0,
									 3},
	{
	 0, 0}, {
		 0, 2}
	};
	static GkbdKeyboardDrawingGroupLevel *pGroupsLevels[] = {
		groupsLevels, groupsLevels + 1, groupsLevels + 2,
		groupsLevels + 3
	};

	GtkBuilder *builder;
	GtkWidget *dialog, *kbdraw;
	XkbComponentNamesRec component_names;
	XklConfigRec *xkl_data;
	GdkRectangle *rect;
	GError *error = NULL;

	XklEngine *engine = xkl_engine_get_instance (GDK_DISPLAY ());
	XklState *xkl_state = xkl_engine_get_current_state (engine);
	gchar **group_names = gkbd_status_get_group_names ();
	gpointer p = g_hash_table_lookup (preview_dialogs,
					  GINT_TO_POINTER
					  (xkl_state->group));
	if (p != NULL) {
		/* existing window */
		gtk_window_present (GTK_WINDOW (p));
		return;
	}

	builder = gtk_builder_new ();
	gtk_builder_add_from_file (builder, DATADIR "/show-layout.ui",
				   &error);

	if (error) {
		g_error ("building ui from %s failed: %s",
			 DATADIR "/show-layout.ui", error->message);
		g_clear_error (&error);
	}


	dialog =
	    GTK_WIDGET (gtk_builder_get_object
			(builder, "gswitchit_layout_view"));
	kbdraw = gkbd_keyboard_drawing_new ();

	if (xkl_state->group >= 0 &&
	    xkl_state->group < g_strv_length (group_names)) {
		char title[128] = "";
		snprintf (title, sizeof (title),
			  _("Keyboard Layout \"%s\""),
			  group_names[xkl_state->group]);
		gtk_window_set_title (GTK_WINDOW (dialog), title);
		g_object_set_data_full (G_OBJECT (dialog), "group_name",
					g_strdup (group_names
						  [xkl_state->group]),
					g_free);
	}

	gkbd_keyboard_drawing_set_groups_levels (GKBD_KEYBOARD_DRAWING
						 (kbdraw), pGroupsLevels);

	xkl_data = xkl_config_rec_new ();
	if (xkl_config_rec_get_from_server (xkl_data, engine)) {
		int num_layouts = g_strv_length (xkl_data->layouts);
		int num_variants = g_strv_length (xkl_data->variants);
		if (xkl_state->group >= 0 &&
		    xkl_state->group < num_layouts &&
		    xkl_state->group < num_variants) {
			char *l =
			    g_strdup (xkl_data->layouts[xkl_state->group]);
			char *v =
			    g_strdup (xkl_data->variants
				      [xkl_state->group]);
			char **p;
			int i;

			if ((p = xkl_data->layouts) != NULL)
				for (i = num_layouts; --i >= 0;)
					g_free (*p++);

			if ((p = xkl_data->variants) != NULL)
				for (i = num_variants; --i >= 0;)
					g_free (*p++);

			xkl_data->layouts =
			    g_realloc (xkl_data->layouts,
				       sizeof (char *) * 2);
			xkl_data->variants =
			    g_realloc (xkl_data->variants,
				       sizeof (char *) * 2);
			xkl_data->layouts[0] = l;
			xkl_data->variants[0] = v;
			xkl_data->layouts[1] = xkl_data->variants[1] =
			    NULL;
		}

		if (xkl_xkb_config_native_prepare
		    (engine, xkl_data, &component_names)) {
			gkbd_keyboard_drawing_set_keyboard
			    (GKBD_KEYBOARD_DRAWING (kbdraw),
			     &component_names);
			xkl_xkb_config_native_cleanup (engine,
						       &component_names);
		}
	}
	g_object_unref (G_OBJECT (xkl_data));

	g_object_set_data (G_OBJECT (dialog), "builderData", builder);
	g_signal_connect (GTK_OBJECT (dialog),
			  "destroy", G_CALLBACK (show_layout_destroy),
			  GINT_TO_POINTER (xkl_state->group));
	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (show_layout_response), NULL);

	rect = gkbd_preview_load_position ();
	if (rect != NULL) {
		gtk_window_move (GTK_WINDOW (dialog), rect->x, rect->y);
		gtk_window_resize (GTK_WINDOW (dialog), rect->width,
				   rect->height);
		g_free (rect);
	} else
		gtk_window_resize (GTK_WINDOW (dialog), 700, 400);

	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

	gtk_container_add (GTK_CONTAINER
			   (gtk_builder_get_object
			    (builder, "preview_vbox")), kbdraw);

	g_object_set_data (G_OBJECT (dialog), "kbdraw", kbdraw);

	g_hash_table_insert (preview_dialogs,
			     GINT_TO_POINTER (xkl_state->group), dialog);

	gtk_widget_show_all (GTK_WIDGET (dialog));
}

static void
popup_menu_set_group (GtkMenuItem * item, gpointer param)
{
	gint group_number = GPOINTER_TO_INT (param);
	XklEngine *engine = gkbd_status_get_xkl_engine ();
	XklState st;
	Window cur;

	st.group = group_number;
	xkl_engine_allow_one_switch_to_secondary_group (engine);
	cur = xkl_engine_get_current_window (engine);
	if (cur != (Window) NULL) {
		xkl_debug (150, "Enforcing the state %d for window %lx\n",
			   st.group, cur);
		xkl_engine_save_state (engine,
				       xkl_engine_get_current_window
				       (engine), &st);
/*    XSetInputFocus( GDK_DISPLAY(), cur, RevertToNone, CurrentTime );*/
	} else {
		xkl_debug (150,
			   "??? Enforcing the state %d for unknown window\n",
			   st.group);
		/* strange situation - bad things can happen */
	}
	xkl_engine_lock_group (engine, st.group);
}

static void
status_icon_popup_menu_cb (GtkStatusIcon * icon, guint button, guint time)
{
	GtkMenu *popup_menu = GTK_MENU (gtk_menu_new ());
	GtkMenu *groups_menu = GTK_MENU (gtk_menu_new ());
	int i = 0;
	gchar **current_name = gkbd_status_get_group_names ();

	GtkWidget *item = gtk_menu_item_new_with_mnemonic (_("_Layouts"));
	gtk_widget_show (item);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item),
				   GTK_WIDGET (groups_menu));

	item =
	    gtk_menu_item_new_with_mnemonic (_("Keyboard _Preferences"));
	gtk_widget_show (item);
	g_signal_connect (item, "activate", popup_menu_launch_capplet,
			  NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item);

	item = gtk_menu_item_new_with_mnemonic (_("Show _Current Layout"));
	gtk_widget_show (item);
	g_signal_connect (item, "activate", popup_menu_show_layout, NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item);

	for (i = 0; *current_name; i++, current_name++) {
		gchar *image_file = gkbd_status_get_image_filename (i);

		if (image_file == NULL) {
			item =
			    gtk_menu_item_new_with_label (*current_name);
		} else {
			GdkPixbuf *pixbuf =
			    gdk_pixbuf_new_from_file_at_size (image_file,
							      24, 24,
							      NULL);
			GtkWidget *img =
			    gtk_image_new_from_pixbuf (pixbuf);
			item =
			    gtk_image_menu_item_new_with_label
			    (*current_name);
			gtk_widget_show (img);
			gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM
						       (item), img);
			gtk_image_menu_item_set_always_show_image
			    (GTK_IMAGE_MENU_ITEM (item), TRUE);
			g_free (image_file);
		}
		gtk_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (groups_menu), item);
		g_signal_connect (item, "activate",
				  G_CALLBACK (popup_menu_set_group),
				  GINT_TO_POINTER (i));
	}

	gtk_menu_popup (popup_menu, NULL, NULL,
			gtk_status_icon_position_menu,
			(gpointer) icon, button, time);
}

static void
show_hide_icon ()
{
	if (g_slist_length (current_kbd_config.layouts_variants) > 1) {
		if (icon == NULL) {
			GConfClient *conf_client =
			    gconf_client_get_default ();
			gboolean disable =
			    gconf_client_get_bool (conf_client,
						   DISABLE_INDICATOR_KEY,
						   NULL);
			g_object_unref (conf_client);
			if (disable)
				return;

			xkl_debug (150, "Creating new icon\n");
			icon = gkbd_status_new ();
			g_signal_connect (icon, "popup-menu",
					  G_CALLBACK
					  (status_icon_popup_menu_cb),
					  NULL);

		}
	} else {
		if (icon != NULL) {
			xkl_debug (150, "Destroying icon\n");
			g_object_unref (icon);
			icon = NULL;
		}
	}
}

static gboolean
try_activating_xkb_config_if_new (GkbdKeyboardConfig *
				  current_sys_kbd_config)
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
		xkl_registry =
		    xkl_config_registry_get_instance (xkl_engine);
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
		if (gkbd_keyboard_config_split_items
		    (lv->data, &lname, &vname)) {
			g_snprintf (item->name, sizeof (item->name), "%s",
				    lname);
			if (!xkl_config_registry_find_layout
			    (xkl_registry, item)) {
				xkl_debug (100, "Bad layout [%s]\n",
					   lname);
				filtered = lv;
				lv = lv->next;
				g_free (filtered->data);
				current_kbd_config.layouts_variants =
				    g_slist_delete_link
				    (current_kbd_config.layouts_variants,
				     filtered);
				any_change = TRUE;
				continue;
			}
			if (vname) {
				g_snprintf (item->name,
					    sizeof (item->name), "%s",
					    vname);
				if (!xkl_config_registry_find_variant
				    (xkl_registry, lname, item)) {
					xkl_debug (100,
						   "Bad variant [%s(%s)]\n",
						   lname, vname);
					filtered = lv;
					lv = lv->next;
					g_free (filtered->data);
					current_kbd_config.layouts_variants
					    =
					    g_slist_delete_link
					    (current_kbd_config.
					     layouts_variants, filtered);
					any_change = TRUE;
					continue;
				}
			}
		}
		lv = lv->next;
	}
	g_object_unref (item);
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
		int max_groups;

		max_groups =
		    MAX (xkl_engine_get_max_num_groups (xkl_engine), 1);
		layouts =
		    gconf_client_get_list (conf_client,
					   GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS,
					   GCONF_VALUE_STRING, NULL);

		/* Use system layouts as a default if we do not have
		 * user configuration */
		if (layouts == NULL) {
			GSList *i;
			int len;

			for (i = initial_sys_kbd_config.layouts_variants;
			     i; i = g_slist_next (i)) {
				s = g_strdup (i->data);

				/* chop off empty variants to avoid duplicates */
				len = strlen (s);
				if (s[len - 1] == '\t')
					s[len - 1] = '\0';
				layouts = g_slist_append (layouts, s);
			}
		}

		/* Add the layout if it doesn't already exist. XKB limits the
		 * total number of layouts. If we already have the maximum
		 * number of layouts configured, we replace the last one. This
		 * prevents the list from becoming full if the user has a habit
		 * of selecting many different keyboard layouts in GDM. */

		found_node =
		    g_slist_find_custom (layouts, gdm_layout,
					 (GCompareFunc) g_strcmp0);

		if (!found_node) {
			/* Insert at the last valid place, or at the end of
			 * list, whichever comes first */
			layouts =
			    g_slist_insert (layouts, g_strdup (gdm_layout),
					    max_groups - 1);
			if (g_slist_length (layouts) > max_groups) {
				GSList *last;
				GSList *free_layouts;

				last =
				    g_slist_nth (layouts, max_groups - 1);
				free_layouts = last->next;
				last->next = NULL;

				g_slist_foreach (free_layouts,
						 (GFunc) g_free, NULL);
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

	if (!try_activating_xkb_config_if_new (&current_sys_kbd_config)) {
		if (filter_xkb_config ()) {
			if (!try_activating_xkb_config_if_new
			    (&current_sys_kbd_config)) {
				g_warning
				    ("Could not activate the filtered XKB configuration");
				activation_error ();
			}
		} else {
			g_warning
			    ("Could not activate the XKB configuration");
			activation_error ();
		}
	} else
		xkl_debug (100,
			   "Actual KBD configuration was not changed: redundant notification\n");

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

	if (group_to_activate != -1)
		xkl_engine_lock_group (current_config.engine,
				       group_to_activate);
	gkbd_keyboard_config_term (&current_sys_kbd_config);
	show_hide_icon ();
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

static void
gsd_keyboard_update_indicator_icons ()
{
	Bool state;
	int new_state, i;
	Display *display = GDK_DISPLAY ();
	XkbGetNamedIndicator (display, caps_lock, NULL, &state,
			      NULL, NULL);
	new_state = state ? 1 : 0;
	XkbGetNamedIndicator (display, num_lock, NULL, &state, NULL, NULL);
	new_state <<= 1;
	new_state |= (state ? 1 : 0);
	XkbGetNamedIndicator (display, scroll_lock, NULL, &state,
			      NULL, NULL);
	new_state <<= 1;
	new_state |= (state ? 1 : 0);
	xkl_debug (160, "Indicators state: %d\n", new_state);


	for (i = sizeof (indicator_icons) / sizeof (indicator_icons[0]);
	     --i >= 0;) {
		gtk_status_icon_set_from_icon_name (indicator_icons[i],
						    (new_state & (1 << i))
						    ?
						    indicator_on_icon_names
						    [i] :
						    indicator_off_icon_names
						    [i]);
	}
}

static void
gsd_keyboard_state_changed (XklEngine * engine, XklEngineStateChange type,
			    gint new_group, gboolean restore)
{
	xkl_debug (160,
		   "State changed: type %d, new group: %d, restore: %d.\n",
		   type, new_group, restore);
	if (type == INDICATORS_CHANGED) {
		gsd_keyboard_update_indicator_icons ();
	}
}

void
gsd_keyboard_xkb_init (GConfClient * client,
		       GsdKeyboardManager * kbd_manager)
{
	int i;
	Display *display = GDK_DISPLAY ();
	gnome_settings_profile_start (NULL);

	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   DATADIR G_DIR_SEPARATOR_S
					   "icons");

	caps_lock = XInternAtom (display, "Caps Lock", False);
	num_lock = XInternAtom (display, "Num Lock", False);
	scroll_lock = XInternAtom (display, "Scroll Lock", False);

	for (i = sizeof (indicator_icons) / sizeof (indicator_icons[0]);
	     --i >= 0;) {
		indicator_icons[i] =
		    gtk_status_icon_new_from_icon_name
		    (indicator_off_icon_names[i]);
	}

	gsd_keyboard_update_indicator_icons ();

#ifdef GSDKX
	xkl_set_debug_level (200);
	logfile = fopen ("/tmp/gsdkx.log", "a");
	xkl_set_log_appender (gsd_keyboard_log_appender);
#endif
	manager = kbd_manager;
	gnome_settings_profile_start ("xkl_engine_get_instance");
	xkl_engine = xkl_engine_get_instance (display);
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
		g_signal_connect (xkl_engine, "X-state-changed",
				  G_CALLBACK
				  (gsd_keyboard_state_changed), NULL);

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
	preview_dialogs = g_hash_table_new (g_direct_hash, g_direct_equal);

	gnome_settings_profile_end (NULL);
}

void
gsd_keyboard_xkb_shutdown (void)
{
	GConfClient *client;
	int i;

	pa_callback = NULL;
	pa_callback_user_data = NULL;
	manager = NULL;

	for (i = sizeof (indicator_icons) / sizeof (indicator_icons[0]);
	     --i >= 0;) {
		g_object_unref (G_OBJECT (indicator_icons[i]));
		indicator_icons[i] = NULL;
	}

	g_hash_table_destroy (preview_dialogs);

	if (!inited_ok)
		return;

	xkl_engine_stop_listen (xkl_engine,
				XKLL_MANAGE_LAYOUTS |
				XKLL_MANAGE_WINDOW_STATES);

	gdk_window_remove_filter (NULL, (GdkFilterFunc)
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
