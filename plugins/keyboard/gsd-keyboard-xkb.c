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

#include <libgnomekbd/gkbd-status.h>
#include <libgnomekbd/gkbd-keyboard-drawing.h>
#include <libgnomekbd/gkbd-desktop-config.h>
#include <libgnomekbd/gkbd-keyboard-config.h>
#include <libgnomekbd/gkbd-util.h>

#include "gsd-keyboard-xkb.h"
#include "delayed-dialog.h"
#include "gnome-settings-profile.h"

#define SETTINGS_KEYBOARD_DIR "org.gnome.settings-daemon.plugins.keyboard"

#define DISABLE_INDICATOR_KEY "disable-indicator"
#define SHOW_KEYBOARD_LEDS_INDICATOR_KEY "show-keyboard-leds-indicator"

static GsdKeyboardManager *manager = NULL;

static XklEngine *xkl_engine;
static XklConfigRegistry *xkl_registry = NULL;

static GkbdDesktopConfig current_config;
static GkbdKeyboardConfig current_kbd_config;

/* never terminated */
static GkbdKeyboardConfig initial_sys_kbd_config;

static gboolean inited_ok = FALSE;

static GSettings *settings_plugin = NULL;
static GSettings *settings_desktop = NULL;
static GSettings *settings_keyboard = NULL;

static PostActivationCallback pa_callback = NULL;
static void *pa_callback_user_data = NULL;

static const char *gdm_keyboard_layout = NULL;

static GtkStatusIcon *icon = NULL;

static GHashTable *preview_dialogs = NULL;

static Atom caps_lock;
static Atom num_lock;
static Atom scroll_lock;

static GtkStatusIcon *indicator_icons[3];
static const gchar *indicator_on_icon_names[] = {
	"kbd-scrolllock-on",
	"kbd-numlock-on",
	"kbd-capslock-on"
};

static const gchar *indicator_off_icon_names[] = {
	"kbd-scrolllock-off",
	"kbd-numlock-off",
	"kbd-capslock-off"
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
	char const *vendor;
	int release;
	GtkWidget *dialog;

	vendor = ServerVendor (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
	release = VendorRelease (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));

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
						      " • a bug in libxklavier library\n"
						      " • a bug in X server (xkbcomp, xmodmap utilities)\n"
						      " • X server with incompatible libxkbfile implementation\n\n"
						      "X server version data:\n%s\n%d\n"
						      "If you report this situation as a bug, please include:\n"
						      " • The result of <b>%s</b>\n"
						      " • The result of <b>%s</b>"),
						     vendor,
						     release,
						     "xprop -root | grep XKB",
						     "gconftool-2 -R /desktop/gnome/peripherals/keyboard/kbd");
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gtk_widget_destroy), NULL);
	gsd_delayed_show_dialog (dialog);
}

static void
apply_desktop_settings (void)
{
	gboolean show_leds;
	int i;
	if (!inited_ok)
		return;

	gsd_keyboard_manager_apply_settings (manager);
	gkbd_desktop_config_load (&current_config);
	/* again, probably it would be nice to compare things
	   before activating them */
	gkbd_desktop_config_activate (&current_config);

	show_leds = g_settings_get_boolean (settings_plugin, SHOW_KEYBOARD_LEDS_INDICATOR_KEY);
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
show_layout_destroy (GtkWidget * dialog, gint group)
{
	g_hash_table_remove (preview_dialogs, GINT_TO_POINTER (group));
}

static void
popup_menu_show_layout ()
{
	GtkWidget *dialog;
	XklEngine *engine =
	    xkl_engine_get_instance (GDK_DISPLAY_XDISPLAY
				     (gdk_display_get_default ()));
	XklState *xkl_state = xkl_engine_get_current_state (engine);
	gpointer p = g_hash_table_lookup (preview_dialogs,
					  GINT_TO_POINTER
					  (xkl_state->group));
	gchar **group_names = gkbd_status_get_group_names ();

	if (xkl_state->group < 0
	    || xkl_state->group >= g_strv_length (group_names)) {
		return;
	}

	if (p != NULL) {
		/* existing window */
		gtk_window_present (GTK_WINDOW (p));
		return;
	}

	dialog =
	    gkbd_keyboard_drawing_new_dialog (xkl_state->group,
					      group_names
					      [xkl_state->group]);
	g_signal_connect (dialog, "destroy",
			  G_CALLBACK (show_layout_destroy),
			  GINT_TO_POINTER (xkl_state->group));
	g_hash_table_insert (preview_dialogs,
			     GINT_TO_POINTER (xkl_state->group), dialog);
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
	if (g_strv_length (current_kbd_config.layouts_variants) > 1) {
		if (icon == NULL) {
			gboolean disable =
			    g_settings_get_boolean (settings_plugin,
						    DISABLE_INDICATOR_KEY);
			if (disable)
				return;

			xkl_debug (150, "Creating new icon\n");
			icon = gkbd_status_new ();
			gtk_status_icon_set_name (icon, "keyboard");
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
	gchar **lv;
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
	while (*lv) {
		xkl_debug (100, "Checking [%s]\n", *lv);
		if (gkbd_keyboard_config_split_items (*lv, &lname, &vname)) {
			gboolean should_be_dropped = FALSE;
			g_snprintf (item->name, sizeof (item->name), "%s",
				    lname);
			if (!xkl_config_registry_find_layout
			    (xkl_registry, item)) {
				xkl_debug (100, "Bad layout [%s]\n",
					   lname);
				should_be_dropped = TRUE;
			} else if (vname) {
				g_snprintf (item->name,
					    sizeof (item->name), "%s",
					    vname);
				if (!xkl_config_registry_find_variant
				    (xkl_registry, lname, item)) {
					xkl_debug (100,
						   "Bad variant [%s(%s)]\n",
						   lname, vname);
					should_be_dropped = TRUE;
				}
			}
			if (should_be_dropped) {
				gkbd_strv_behead (lv);
				any_change = TRUE;
				continue;
			}
		}
		lv++;
	}
	g_object_unref (item);
	return any_change;
}

static void
apply_xkb_settings (void)
{
	GkbdKeyboardConfig current_sys_kbd_config;
	int group_to_activate = -1;
	char *gdm_layout;
	char *s;

	if (!inited_ok)
		return;

	/* With GDM the user can already set a layout from the login
	 * screen. Try to keep that setting.
	 * We clear gdm_keyboard_layout early, so we don't risk
	 * recursion from conf notification.
	 */
	gdm_layout = g_strdup (gdm_keyboard_layout);
	gdm_keyboard_layout = NULL;

	/* gdm's configuration and $GDM_KEYBOARD_LAYOUT separates layout and
	 * variant with a space, but gsettings use tabs; so convert to be robust
	 * with both */
	for (s = gdm_layout; s && *s; ++s) {
		if (*s == ' ') {
			*s = '\t';
		}
	}

	if (gdm_layout != NULL) {
		gchar **layouts;
		gboolean found_node;
		int max_groups;
		gchar **i;

		max_groups =
		    MAX (xkl_engine_get_max_num_groups (xkl_engine), 1);
		layouts =
		    g_settings_get_strv (settings_keyboard,
					 GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS);

		/* Use system layouts as a default if we do not have
		 * user configuration */
		if (layouts == NULL) {
			int len;

			i = layouts =
			    g_strdupv
			    (initial_sys_kbd_config.layouts_variants);
			if (i != NULL) {
				while (*i != NULL) {
					s = *i;

					/* chop off empty variants to avoid duplicates */
					len = strlen (s);
					if (s[len - 1] == '\t')
						s[len - 1] = '\0';
					i++;
				}
			}
		}

		/* Add the layout if it doesn't already exist. XKB limits the
		 * total number of layouts. If we already have the maximum
		 * number of layouts configured, we replace the last one. This
		 * prevents the list from becoming full if the user has a habit
		 * of selecting many different keyboard layouts in GDM. */

		found_node = FALSE;
		i = layouts;
		if (i != NULL) {
			while (*i != NULL) {
				if (!g_strcmp0 (*i, gdm_layout)) {
					found_node = TRUE;
					break;
				}
				i++;
			}
		}

		if (!found_node) {
			/* Insert at the last valid place, or at the end of
			 * list, whichever comes first */
			gint old_length = g_strv_length (layouts);
			if (old_length >= max_groups) {
				layouts[max_groups - 1] =
				    g_strdup (gdm_layout);
			} else {
				layouts =
				    gkbd_strv_append (layouts,
						      g_strdup
						      (gdm_layout));
			}

			g_settings_set_strv (settings_keyboard,
					     GKBD_KEYBOARD_CONFIG_KEY_LAYOUTS,
					     (const gchar *
					      const *) layouts);
		}

		g_strfreev (layouts);
	}

	gkbd_keyboard_config_init (&current_sys_kbd_config, xkl_engine);

	gkbd_keyboard_config_load (&current_kbd_config,
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
		gchar **l;
		int i;
		size_t len = strlen (gdm_layout);
		l = current_kbd_config.layouts_variants;
		if (l != NULL) {
			i = 0;
			while (*l != NULL) {
				char *lv = *l;
				if (strncmp (lv, gdm_layout, len) == 0
				    && (lv[len] == '\0'
					|| lv[len] == '\t')) {
					group_to_activate = i;
					break;
				}
				i++;
				l++;
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
	if (!inited_ok)
		return;

	gkbd_keyboard_config_init (&initial_sys_kbd_config, xkl_engine);
	gkbd_keyboard_config_load_from_x_initial (&initial_sys_kbd_config,
						  NULL);
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
	Display *display =
	    GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
	XkbGetNamedIndicator (display, caps_lock, NULL, &state, NULL,
			      NULL);
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
gsd_keyboard_xkb_init (GsdKeyboardManager * kbd_manager)
{
	int i;
	Display *display =
	    GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
	gnome_settings_profile_start (NULL);

	settings_plugin = g_settings_new (SETTINGS_KEYBOARD_DIR);

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

		gkbd_desktop_config_init (&current_config, xkl_engine);
		gkbd_keyboard_config_init (&current_kbd_config,
					   xkl_engine);
		xkl_engine_backup_names_prop (xkl_engine);
		gsd_keyboard_xkb_analyze_sysconfig ();

		settings_desktop = g_settings_new (GKBD_DESKTOP_SCHEMA);
		settings_keyboard = g_settings_new (GKBD_KEYBOARD_SCHEMA);
		g_signal_connect (settings_desktop, "changed",
				  (GCallback) apply_desktop_settings,
				  NULL);
		g_signal_connect (settings_keyboard, "changed",
				  (GCallback) apply_xkb_settings, NULL);

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

	g_object_unref (settings_desktop);
	settings_desktop = NULL;
	g_object_unref (settings_keyboard);
	settings_keyboard = NULL;
	g_object_unref (settings_plugin);
	settings_plugin = NULL;

	if (xkl_registry) {
		g_object_unref (xkl_registry);
	}

	g_object_unref (xkl_engine);

	xkl_engine = NULL;
	inited_ok = FALSE;
}
