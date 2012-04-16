/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright © 2001 Ximian, Inc.
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Written by Sergey V. Oudaltsov <svu@users.sourceforge.net>
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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include <libxklavier/xklavier.h>
#include <libgnomekbd/gkbd-status.h>
#include <libgnomekbd/gkbd-keyboard-drawing.h>
#include <libgnomekbd/gkbd-desktop-config.h>
#include <libgnomekbd/gkbd-keyboard-config.h>
#include <libgnomekbd/gkbd-util.h>

#include "gnome-settings-profile.h"
#include "gsd-keyboard-manager.h"
#include "gsd-input-helper.h"
#include "gsd-enums.h"
#include "delayed-dialog.h"

#define GSD_KEYBOARD_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_KEYBOARD_MANAGER, GsdKeyboardManagerPrivate))

#define GSD_KEYBOARD_DIR "org.gnome.settings-daemon.peripherals.keyboard"

#define KEY_REPEAT         "repeat"
#define KEY_CLICK          "click"
#define KEY_INTERVAL       "repeat-interval"
#define KEY_DELAY          "delay"
#define KEY_CLICK_VOLUME   "click-volume"
#define KEY_NUMLOCK_STATE  "numlock-state"

#define KEY_BELL_VOLUME    "bell-volume"
#define KEY_BELL_PITCH     "bell-pitch"
#define KEY_BELL_DURATION  "bell-duration"
#define KEY_BELL_MODE      "bell-mode"

struct GsdKeyboardManagerPrivate
{
	guint      start_idle_id;
        GSettings *settings;
        gint       xkb_event_base;
        GsdNumLockState old_state;
        GdkDeviceManager *device_manager;
        guint device_added_id;
        guint device_removed_id;

        /* XKB */
	XklEngine *xkl_engine;
	XklConfigRegistry *xkl_registry;

	GkbdDesktopConfig current_config;
	GkbdKeyboardConfig current_kbd_config;

	GkbdKeyboardConfig initial_sys_kbd_config;
	GSettings *settings_desktop;
	GSettings *settings_keyboard;

	GtkStatusIcon *icon;
	GtkMenu *popup_menu;
};

static void     gsd_keyboard_manager_class_init  (GsdKeyboardManagerClass *klass);
static void     gsd_keyboard_manager_init        (GsdKeyboardManager      *keyboard_manager);
static void     gsd_keyboard_manager_finalize    (GObject                 *object);

G_DEFINE_TYPE (GsdKeyboardManager, gsd_keyboard_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static gboolean try_activating_xkb_config_if_new (GsdKeyboardManager *manager,
						  GkbdKeyboardConfig *current_sys_kbd_config);
static gboolean filter_xkb_config (GsdKeyboardManager *manager);
static void show_hide_icon (GsdKeyboardManager *manager);

static void
activation_error (void)
{
	char const *vendor;
	GtkWidget *dialog;

	vendor =
	    ServerVendor (GDK_DISPLAY_XDISPLAY
			  (gdk_display_get_default ()));

	/* VNC viewers will not work, do not barrage them with warnings */
	if (NULL != vendor && NULL != strstr (vendor, "VNC"))
		return;

	dialog = gtk_message_dialog_new_with_markup (NULL,
						     0,
						     GTK_MESSAGE_ERROR,
						     GTK_BUTTONS_CLOSE,
						     _
						     ("Error activating XKB configuration.\n"
						      "There can be various reasons for that.\n\n"
						      "If you report this situation as a bug, include the results of\n"
						      " • <b>%s</b>\n"
						      " • <b>%s</b>\n"
						      " • <b>%s</b>\n"
						      " • <b>%s</b>"),
						     "xprop -root | grep XKB",
						     "gsettings get org.gnome.libgnomekbd.keyboard model",
						     "gsettings get org.gnome.libgnomekbd.keyboard layouts",
						     "gsettings get org.gnome.libgnomekbd.keyboard options");
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gtk_widget_destroy), NULL);
	gsd_delayed_show_dialog (dialog);
}

static gboolean
ensure_manager_xkl_registry (GsdKeyboardManager *manager)
{
	if (!manager->priv->xkl_registry) {
		manager->priv->xkl_registry =
		    xkl_config_registry_get_instance (manager->priv->xkl_engine);
		/* load all materials, unconditionally! */
		if (!xkl_config_registry_load (manager->priv->xkl_registry, TRUE)) {
			g_object_unref (manager->priv->xkl_registry);
			manager->priv->xkl_registry = NULL;
			return FALSE;
		}
	}

	return TRUE;
}

static void
apply_desktop_settings (GsdKeyboardManager *manager)
{
	if (manager->priv->xkl_engine == NULL)
		return;

	gsd_keyboard_manager_apply_settings (manager);
	gkbd_desktop_config_load (&manager->priv->current_config);
	/* again, probably it would be nice to compare things
	   before activating them */
	gkbd_desktop_config_activate (&manager->priv->current_config);
}

static void
apply_xkb_settings (GsdKeyboardManager *manager)
{
	GkbdKeyboardConfig current_sys_kbd_config;

	if (manager->priv->xkl_engine == NULL)
		return;

	gkbd_keyboard_config_init (&current_sys_kbd_config, manager->priv->xkl_engine);

	gkbd_keyboard_config_load (&manager->priv->current_kbd_config,
				   &manager->priv->initial_sys_kbd_config);

	gkbd_keyboard_config_load_from_x_current (&current_sys_kbd_config,
						  NULL);

	if (!try_activating_xkb_config_if_new (manager, &current_sys_kbd_config)) {
		if (filter_xkb_config (manager)) {
			if (!try_activating_xkb_config_if_new
			    (manager, &current_sys_kbd_config)) {
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
		g_debug (
			   "Actual KBD configuration was not changed: redundant notification\n");

	gkbd_keyboard_config_term (&current_sys_kbd_config);
	show_hide_icon (manager);
}

static void
desktop_settings_changed (GSettings          *settings,
			  gchar              *key,
			  GsdKeyboardManager *manager)
{
	apply_desktop_settings (manager);
}

static void
xkb_settings_changed (GSettings          *settings,
		      gchar              *key,
		      GsdKeyboardManager *manager)
{
	apply_xkb_settings (manager);
}

static void
popup_menu_launch_capplet (void)
{
	GAppInfo *info;
	GdkAppLaunchContext *ctx;
	GError *error = NULL;

	info = g_app_info_create_from_commandline ("gnome-control-center region", NULL, 0, NULL);
	if (info == NULL)
		return;

	ctx = gdk_display_get_app_launch_context (gdk_display_get_default ());

	if (g_app_info_launch (info, NULL,
			       G_APP_LAUNCH_CONTEXT (ctx), &error) == FALSE) {
		g_warning ("Could not execute keyboard properties capplet: [%s]\n",
			   error->message);
		g_error_free (error);
	}

	g_object_unref (info);
	g_object_unref (ctx);
}

static void
popup_menu_show_layout (GtkMenuItem *menuitem,
			GsdKeyboardManager *manager)
{
	XklState *xkl_state;
	char *command;

	xkl_state = xkl_engine_get_current_state (manager->priv->xkl_engine);
	if (xkl_state->group < 0)
		return;

	command = g_strdup_printf ("gkbd-keyboard-display -g %d", xkl_state->group + 1);
	g_spawn_command_line_async (command, NULL);
	g_free (command);
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
		g_debug ("Enforcing the state %d for window %lx\n",
			   st.group, cur);
		xkl_engine_save_state (engine,
				       xkl_engine_get_current_window
				       (engine), &st);
/*    XSetInputFocus( GDK_DISPLAY(), cur, RevertToNone, CurrentTime );*/
	} else {
		g_debug (
			   "??? Enforcing the state %d for unknown window\n",
			   st.group);
		/* strange situation - bad things can happen */
	}
	xkl_engine_lock_group (engine, st.group);
}

static void
ensure_popup_menu (GsdKeyboardManager *manager)
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

	item = gtk_menu_item_new_with_mnemonic (_("Show _Keyboard Layout..."));
	gtk_widget_show (item);
	g_signal_connect (item, "activate", G_CALLBACK (popup_menu_show_layout), manager);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item);

	/* translators note:
	 * This is the name of the gnome-control-center "region" panel */
	item = gtk_menu_item_new_with_mnemonic (_("Region and Language Settings"));
	gtk_widget_show (item);
	g_signal_connect (item, "activate", popup_menu_launch_capplet, NULL);
	gtk_menu_shell_append (GTK_MENU_SHELL (popup_menu), item);

	for (i = 0; *current_name; i++, current_name++) {
		item = gtk_menu_item_new_with_label (*current_name);
		gtk_widget_show (item);
		gtk_menu_shell_append (GTK_MENU_SHELL (groups_menu), item);
		g_signal_connect (item, "activate",
				  G_CALLBACK (popup_menu_set_group),
				  GINT_TO_POINTER (i));
	}

	if (manager->priv->popup_menu != NULL)
		gtk_widget_destroy (GTK_WIDGET (manager->priv->popup_menu));
	manager->priv->popup_menu = popup_menu;
}

static void
status_icon_popup_menu_cb (GtkStatusIcon      *icon,
			   guint               button,
			   guint               time,
			   GsdKeyboardManager *manager)
{
	ensure_popup_menu (manager);
	gtk_menu_popup (manager->priv->popup_menu, NULL, NULL,
			gtk_status_icon_position_menu,
			(gpointer) icon, button, time);
}

static void
show_hide_icon (GsdKeyboardManager *manager)
{
	if (g_strv_length (manager->priv->current_kbd_config.layouts_variants) > 1) {
		if (manager->priv->icon == NULL) {
			g_debug ("Creating keyboard status icon\n");
			manager->priv->icon = gkbd_status_new ();
			g_signal_connect (manager->priv->icon, "popup-menu",
					  G_CALLBACK
					  (status_icon_popup_menu_cb),
					  manager);

		}
	} else {
		if (manager->priv->icon != NULL) {
			g_debug ("Destroying icon\n");
			g_object_unref (manager->priv->icon);
			manager->priv->icon = NULL;
		}
	}
}

static gboolean
try_activating_xkb_config_if_new (GsdKeyboardManager *manager,
				  GkbdKeyboardConfig *current_sys_kbd_config)
{
	/* Activate - only if different! */
	if (!gkbd_keyboard_config_equals
	    (&manager->priv->current_kbd_config, current_sys_kbd_config)) {
		if (gkbd_keyboard_config_activate (&manager->priv->current_kbd_config)) {
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
filter_xkb_config (GsdKeyboardManager *manager)
{
	XklConfigItem *item;
	gchar *lname;
	gchar *vname;
	gchar **lv;
	gboolean any_change = FALSE;

	g_debug ("Filtering configuration against the registry\n");
	if (!ensure_manager_xkl_registry (manager))
		return FALSE;

	lv = manager->priv->current_kbd_config.layouts_variants;
	item = xkl_config_item_new ();
	while (*lv) {
		g_debug ("Checking [%s]\n", *lv);
		if (gkbd_keyboard_config_split_items (*lv, &lname, &vname)) {
			gboolean should_be_dropped = FALSE;
			g_snprintf (item->name, sizeof (item->name), "%s",
				    lname);
			if (!xkl_config_registry_find_layout
			    (manager->priv->xkl_registry, item)) {
				g_debug ("Bad layout [%s]\n",
					   lname);
				should_be_dropped = TRUE;
			} else if (vname) {
				g_snprintf (item->name,
					    sizeof (item->name), "%s",
					    vname);
				if (!xkl_config_registry_find_variant
				    (manager->priv->xkl_registry, lname, item)) {
					g_debug (
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
gsd_keyboard_xkb_init (GsdKeyboardManager *manager)
{
	Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

	manager->priv->xkl_engine = xkl_engine_get_instance (dpy);
	if (!manager->priv->xkl_engine)
		return;

	gkbd_desktop_config_init (&manager->priv->current_config, manager->priv->xkl_engine);
	gkbd_keyboard_config_init (&manager->priv->current_kbd_config,
				   manager->priv->xkl_engine);
	xkl_engine_backup_names_prop (manager->priv->xkl_engine);
	gkbd_keyboard_config_init (&manager->priv->initial_sys_kbd_config, manager->priv->xkl_engine);
	gkbd_keyboard_config_load_from_x_initial (&manager->priv->initial_sys_kbd_config,
						  NULL);

	gnome_settings_profile_start ("xkl_engine_start_listen");
	xkl_engine_start_listen (manager->priv->xkl_engine,
				 XKLL_MANAGE_LAYOUTS |
				 XKLL_MANAGE_WINDOW_STATES);
	gnome_settings_profile_end ("xkl_engine_start_listen");

	gnome_settings_profile_start ("apply_desktop_settings");
	apply_desktop_settings (manager);
	gnome_settings_profile_end ("apply_desktop_settings");
	gnome_settings_profile_start ("apply_xkb_settings");
	apply_xkb_settings (manager);
	gnome_settings_profile_end ("apply_xkb_settings");

	gnome_settings_profile_end (NULL);
}

static gboolean
xkb_set_keyboard_autorepeat_rate (guint delay, guint interval)
{
        return XkbSetAutoRepeatRate (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                     XkbUseCoreKbd,
                                     delay,
                                     interval);
}

static gboolean
check_xkb_extension (GsdKeyboardManager *manager)
{
        Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        int opcode, error_base, major, minor;
        gboolean have_xkb;

        have_xkb = XkbQueryExtension (dpy,
                                      &opcode,
                                      &manager->priv->xkb_event_base,
                                      &error_base,
                                      &major,
                                      &minor);

        return have_xkb;
}

static void
numlock_xkb_init (GsdKeyboardManager *manager)
{
        Display *dpy;

        dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        XkbSelectEventDetails (dpy,
                               XkbUseCoreKbd,
                               XkbStateNotify,
                               XkbModifierLockMask,
                               XkbModifierLockMask);
}

static unsigned
numlock_NumLock_modifier_mask (void)
{
        Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        return XkbKeysymToModifiers (dpy, XK_Num_Lock);
}

static void
numlock_set_xkb_state (GsdNumLockState new_state)
{
        unsigned int num_mask;
        Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        if (new_state != GSD_NUM_LOCK_STATE_ON && new_state != GSD_NUM_LOCK_STATE_OFF)
                return;
        num_mask = numlock_NumLock_modifier_mask ();
        XkbLockModifiers (dpy, XkbUseCoreKbd, num_mask, new_state == GSD_NUM_LOCK_STATE_ON ? num_mask : 0);
}

static GdkFilterReturn
xkb_events_filter (GdkXEvent *xev_,
		   GdkEvent  *gdkev_,
		   gpointer   user_data)
{
        XEvent *xev = (XEvent *) xev_;
	XkbEvent *xkbev = (XkbEvent *) xev;
        GsdKeyboardManager *manager = (GsdKeyboardManager *) user_data;

	/* libxklavier's events first */
	if (manager->priv->xkl_engine != NULL)
		xkl_engine_filter_events (manager->priv->xkl_engine, xev);

	/* Then XKB specific events */
        if (xev->type != manager->priv->xkb_event_base)
		return GDK_FILTER_CONTINUE;

	if (xkbev->any.xkb_type != XkbStateNotify)
		return GDK_FILTER_CONTINUE;

	if (xkbev->state.changed & XkbModifierLockMask) {
		unsigned num_mask = numlock_NumLock_modifier_mask ();
		unsigned locked_mods = xkbev->state.locked_mods;
		GsdNumLockState numlock_state;

		numlock_state = (num_mask & locked_mods) ? GSD_NUM_LOCK_STATE_ON : GSD_NUM_LOCK_STATE_OFF;

		if (numlock_state != manager->priv->old_state) {
			g_settings_set_enum (manager->priv->settings,
					     KEY_NUMLOCK_STATE,
					     numlock_state);
			manager->priv->old_state = numlock_state;
		}
	}

        return GDK_FILTER_CONTINUE;
}

static void
install_xkb_filter (GsdKeyboardManager *manager)
{
        gdk_window_add_filter (NULL,
                               xkb_events_filter,
                               manager);
}

static void
remove_xkb_filter (GsdKeyboardManager *manager)
{
        gdk_window_remove_filter (NULL,
                                  xkb_events_filter,
                                  manager);
}

static void
apply_settings (GSettings          *settings,
                const char         *key,
                GsdKeyboardManager *manager)
{
        XKeyboardControl kbdcontrol;
        gboolean         repeat;
        gboolean         click;
        guint            interval;
        guint            delay;
        int              click_volume;
        int              bell_volume;
        int              bell_pitch;
        int              bell_duration;
        GsdBellMode      bell_mode;
        gboolean         rnumlock;

        repeat        = g_settings_get_boolean  (settings, KEY_REPEAT);
        click         = g_settings_get_boolean  (settings, KEY_CLICK);
        interval      = g_settings_get_uint  (settings, KEY_INTERVAL);
        delay         = g_settings_get_uint  (settings, KEY_DELAY);
        click_volume  = g_settings_get_int   (settings, KEY_CLICK_VOLUME);
        bell_pitch    = g_settings_get_int   (settings, KEY_BELL_PITCH);
        bell_duration = g_settings_get_int   (settings, KEY_BELL_DURATION);

        bell_mode = g_settings_get_enum (settings, KEY_BELL_MODE);
        bell_volume   = (bell_mode == GSD_BELL_MODE_ON) ? 50 : 0;

        rnumlock      = g_settings_get_boolean  (settings, "remember-numlock-state");

        gdk_error_trap_push ();
        if (repeat) {
                gboolean rate_set = FALSE;

                XAutoRepeatOn (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
                /* Use XKB in preference */
                rate_set = xkb_set_keyboard_autorepeat_rate (delay, interval);

                if (!rate_set)
                        g_warning ("Neither XKeyboard not Xfree86's keyboard extensions are available,\n"
                                   "no way to support keyboard autorepeat rate settings");
        } else {
                XAutoRepeatOff (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
        }

        /* as percentage from 0..100 inclusive */
        if (click_volume < 0) {
                click_volume = 0;
        } else if (click_volume > 100) {
                click_volume = 100;
        }
        kbdcontrol.key_click_percent = click ? click_volume : 0;
        kbdcontrol.bell_percent = bell_volume;
        kbdcontrol.bell_pitch = bell_pitch;
        kbdcontrol.bell_duration = bell_duration;
        XChangeKeyboardControl (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                KBKeyClickPercent | KBBellPercent | KBBellPitch | KBBellDuration,
                                &kbdcontrol);

        manager->priv->old_state = g_settings_get_enum (manager->priv->settings, KEY_NUMLOCK_STATE);

        if (rnumlock)
                numlock_set_xkb_state (manager->priv->old_state);

        XSync (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), FALSE);
        gdk_error_trap_pop_ignored ();
}

void
gsd_keyboard_manager_apply_settings (GsdKeyboardManager *manager)
{
        apply_settings (manager->priv->settings, NULL, manager);
}

static void
device_added_cb (GdkDeviceManager   *device_manager,
                 GdkDevice          *device,
                 GsdKeyboardManager *manager)
{
        GdkInputSource source;

        source = gdk_device_get_source (device);
        if (source == GDK_SOURCE_KEYBOARD) {
                apply_desktop_settings (manager);
                apply_xkb_settings (manager);
                run_custom_command (device, COMMAND_DEVICE_ADDED);
        }
}

static void
device_removed_cb (GdkDeviceManager   *device_manager,
                   GdkDevice          *device,
                   GsdKeyboardManager *manager)
{
        GdkInputSource source;

        source = gdk_device_get_source (device);
        if (source == GDK_SOURCE_KEYBOARD) {
                run_custom_command (device, COMMAND_DEVICE_REMOVED);
        }
}

static void
set_devicepresence_handler (GsdKeyboardManager *manager)
{
        GdkDeviceManager *device_manager;

        device_manager = gdk_display_get_device_manager (gdk_display_get_default ());

        manager->priv->device_added_id = g_signal_connect (G_OBJECT (device_manager), "device-added",
                                                           G_CALLBACK (device_added_cb), manager);
        manager->priv->device_removed_id = g_signal_connect (G_OBJECT (device_manager), "device-removed",
                                                             G_CALLBACK (device_removed_cb), manager);
        manager->priv->device_manager = device_manager;
}

static gboolean
start_keyboard_idle_cb (GsdKeyboardManager *manager)
{
        gnome_settings_profile_start (NULL);

        g_debug ("Starting keyboard manager");

        manager->priv->settings = g_settings_new (GSD_KEYBOARD_DIR);
	manager->priv->settings_desktop = g_settings_new (GKBD_DESKTOP_SCHEMA);
	manager->priv->settings_keyboard = g_settings_new (GKBD_KEYBOARD_SCHEMA);

	gsd_keyboard_xkb_init (manager);
	numlock_xkb_init (manager);

	set_devicepresence_handler (manager);

        /* apply current settings before we install the callback */
        gsd_keyboard_manager_apply_settings (manager);

        g_signal_connect (G_OBJECT (manager->priv->settings), "changed",
                          G_CALLBACK (apply_settings), manager);
	g_signal_connect (manager->priv->settings_desktop, "changed",
			  (GCallback) desktop_settings_changed, manager);
	g_signal_connect (manager->priv->settings_keyboard, "changed",
			  (GCallback) xkb_settings_changed, manager);

	install_xkb_filter (manager);

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_keyboard_manager_start (GsdKeyboardManager *manager,
                            GError            **error)
{
        gnome_settings_profile_start (NULL);

	if (check_xkb_extension (manager) == FALSE) {
		g_debug ("XKB is not supported, not applying any settings");
		return TRUE;
	}

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) start_keyboard_idle_cb, manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_keyboard_manager_stop (GsdKeyboardManager *manager)
{
        GsdKeyboardManagerPrivate *p = manager->priv;

        g_debug ("Stopping keyboard manager");

        if (p->settings != NULL) {
                g_object_unref (p->settings);
                p->settings = NULL;
        }
        if (p->settings_desktop != NULL) {
		g_object_unref (p->settings_desktop);
		p->settings_desktop = NULL;
	}
	if (p->settings_keyboard != NULL) {
		g_object_unref (p->settings_keyboard);
		p->settings_keyboard = NULL;
	}

        if (p->device_manager != NULL) {
                g_signal_handler_disconnect (p->device_manager, p->device_added_id);
                g_signal_handler_disconnect (p->device_manager, p->device_removed_id);
                p->device_manager = NULL;
        }

        if (p->popup_menu != NULL) {
                gtk_widget_destroy (GTK_WIDGET (p->popup_menu));
                p->popup_menu = NULL;
	}

	remove_xkb_filter (manager);

	if (p->xkl_registry != NULL) {
		g_object_unref (p->xkl_registry);
		p->xkl_registry = NULL;
	}

	if (p->xkl_engine != NULL) {
		xkl_engine_stop_listen (p->xkl_engine,
					XKLL_MANAGE_LAYOUTS | XKLL_MANAGE_WINDOW_STATES);
		g_object_unref (p->xkl_engine);
		p->xkl_engine = NULL;
	}
}

static GObject *
gsd_keyboard_manager_constructor (GType                  type,
                                  guint                  n_construct_properties,
                                  GObjectConstructParam *construct_properties)
{
        GsdKeyboardManager      *keyboard_manager;

        keyboard_manager = GSD_KEYBOARD_MANAGER (G_OBJECT_CLASS (gsd_keyboard_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (keyboard_manager);
}

static void
gsd_keyboard_manager_class_init (GsdKeyboardManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gsd_keyboard_manager_constructor;
        object_class->finalize = gsd_keyboard_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdKeyboardManagerPrivate));
}

static void
gsd_keyboard_manager_init (GsdKeyboardManager *manager)
{
        manager->priv = GSD_KEYBOARD_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_keyboard_manager_finalize (GObject *object)
{
        GsdKeyboardManager *keyboard_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_KEYBOARD_MANAGER (object));

        keyboard_manager = GSD_KEYBOARD_MANAGER (object);

        g_return_if_fail (keyboard_manager->priv != NULL);

        if (keyboard_manager->priv->start_idle_id != 0)
                g_source_remove (keyboard_manager->priv->start_idle_id);

        G_OBJECT_CLASS (gsd_keyboard_manager_parent_class)->finalize (object);
}

GsdKeyboardManager *
gsd_keyboard_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_KEYBOARD_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_KEYBOARD_MANAGER (manager_object);
}
