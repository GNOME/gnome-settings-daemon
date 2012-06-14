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
#include <X11/extensions/XKBrules.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-xkb-info.h>

#include "gnome-settings-profile.h"
#include "gsd-keyboard-manager.h"
#include "gsd-input-helper.h"
#include "gsd-enums.h"

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

#define GNOME_DESKTOP_INPUT_SOURCES_DIR "org.gnome.desktop.input-sources"

#define KEY_CURRENT_INPUT_SOURCE "current"
#define KEY_INPUT_SOURCES        "sources"

#define INPUT_SOURCE_TYPE_XKB "xkb"

#define DEFAULT_LANGUAGE "en_US"

struct GsdKeyboardManagerPrivate
{
	guint      start_idle_id;
        GSettings *settings;
        GSettings *input_sources_settings;
        GnomeXkbInfo *xkb_info;
        gint       xkb_event_base;
        GsdNumLockState old_state;
        GdkDeviceManager *device_manager;
        guint device_added_id;
        guint device_removed_id;
};

static void     gsd_keyboard_manager_class_init  (GsdKeyboardManagerClass *klass);
static void     gsd_keyboard_manager_init        (GsdKeyboardManager      *keyboard_manager);
static void     gsd_keyboard_manager_finalize    (GObject                 *object);

G_DEFINE_TYPE (GsdKeyboardManager, gsd_keyboard_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

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
xkb_init (GsdKeyboardManager *manager)
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

        if (xev->type != manager->priv->xkb_event_base ||
            xkbev->any.xkb_type != XkbStateNotify)
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
free_xkb_component_names (XkbComponentNamesRec *p)
{
        g_return_if_fail (p != NULL);

        free (p->keymap);
        free (p->keycodes);
        free (p->types);
        free (p->compat);
        free (p->symbols);
        free (p->geometry);

        g_free (p);
}

static void
upload_xkb_description (const gchar          *rules_file_path,
                        XkbRF_VarDefsRec     *var_defs,
                        XkbComponentNamesRec *comp_names)
{
        Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        XkbDescRec *xkb_desc;
        gchar *rules_file;

        /* Upload it to the X server using the same method as setxkbmap */
        xkb_desc = XkbGetKeyboardByName (display,
                                         XkbUseCoreKbd,
                                         comp_names,
                                         XkbGBN_AllComponentsMask,
                                         XkbGBN_AllComponentsMask &
                                         (~XkbGBN_GeometryMask), True);
        if (!xkb_desc) {
                g_warning ("Couldn't upload new XKB keyboard description");
                return;
        }

        XkbFreeKeyboard (xkb_desc, 0, True);

        rules_file = g_path_get_basename (rules_file_path);

        if (!XkbRF_SetNamesProp (display, rules_file, var_defs))
                g_warning ("Couldn't update the XKB root window property");

        g_free (rules_file);
}

static gchar *
language_code_from_locale (const gchar *locale)
{
        if (!locale || !locale[0] || !locale[1])
                return NULL;

        if (!locale[2] || locale[2] == '_' || locale[2] == '.')
                return g_strndup (locale, 2);

        if (!locale[3] || locale[3] == '_' || locale[3] == '.')
                return g_strndup (locale, 3);

        return NULL;
}

static gchar *
build_xkb_group_string (const gchar *user,
                        const gchar *locale,
                        const gchar *latin)
{
        gchar *string;
        gsize length = 0;
        guint commas = 2;

        if (latin)
                length += strlen (latin);
        else
                commas -= 1;

        if (locale)
                length += strlen (locale);
        else
                commas -= 1;

        length += strlen (user) + commas + 1;

        string = malloc (length);

        if (locale && latin)
                sprintf (string, "%s,%s,%s", user, locale, latin);
        else if (locale)
                sprintf (string, "%s,%s", user, locale);
        else if (latin)
                sprintf (string, "%s,%s", user, latin);
        else
                sprintf (string, "%s", user);

        return string;
}

static gboolean
layout_equal (const gchar *layout_a,
              const gchar *variant_a,
              const gchar *layout_b,
              const gchar *variant_b)
{
        return !g_strcmp0 (layout_a, layout_b) && !g_strcmp0 (variant_a, variant_b);
}

static void
replace_layout_and_variant (GsdKeyboardManager *manager,
                            XkbRF_VarDefsRec   *xkb_var_defs,
                            const gchar        *layout,
                            const gchar        *variant)
{
        /* Toolkits need to know about both a latin layout to handle
         * accelerators which are usually defined like Ctrl+C and a
         * layout with the symbols for the language used in UI strings
         * to handle mnemonics like Alt+Ф, so we try to find and add
         * them in XKB group slots after the layout which the user
         * actually intends to type with. */
        const gchar *latin_layout = "us";
        const gchar *latin_variant = "";
        const gchar *locale_layout = NULL;
        const gchar *locale_variant = NULL;
        const gchar *locale;
        gchar *language;

        locale = setlocale (LC_MESSAGES, NULL);
        /* If LANG is empty, default to en_US */
        if (!locale)
                language = g_strdup (DEFAULT_LANGUAGE);
        else
                language = language_code_from_locale (locale);

        if (!language)
                language = language_code_from_locale (DEFAULT_LANGUAGE);

        gnome_xkb_info_get_layout_info_for_language (manager->priv->xkb_info,
                                                     language,
                                                     NULL,
                                                     NULL,
                                                     NULL,
                                                     &locale_layout,
                                                     &locale_variant);
        g_free (language);

        /* We want to minimize the number of XKB groups if we have
         * duplicated layout+variant pairs.
         *
         * Also, if a layout doesn't have a variant we still have to
         * include it in the variants string because the number of
         * variants must agree with the number of layouts. For
         * instance:
         *
         * layouts:  "us,ru,us"
         * variants: "dvorak,,"
         */
        if (layout_equal (latin_layout, latin_variant, locale_layout, locale_variant) ||
            layout_equal (latin_layout, latin_variant, layout, variant)) {
                latin_layout = NULL;
                latin_variant = NULL;
        }

        if (layout_equal (locale_layout, locale_variant, layout, variant)) {
                locale_layout = NULL;
                locale_variant = NULL;
        }

        free (xkb_var_defs->layout);
        xkb_var_defs->layout = build_xkb_group_string (layout, locale_layout, latin_layout);

        free (xkb_var_defs->variant);
        xkb_var_defs->variant = build_xkb_group_string (variant, locale_variant, latin_variant);
}

static void
apply_xkb_layout (GsdKeyboardManager *manager,
                  const gchar        *layout,
                  const gchar        *variant)
{
        XkbRF_RulesRec *xkb_rules;
        XkbRF_VarDefsRec *xkb_var_defs;
        gchar *rules_file_path;

        gnome_xkb_info_get_var_defs (&rules_file_path, &xkb_var_defs);

        replace_layout_and_variant (manager, xkb_var_defs, layout, variant);

        gdk_error_trap_push ();

        xkb_rules = XkbRF_Load (rules_file_path, NULL, True, True);
        if (xkb_rules) {
                XkbComponentNamesRec *xkb_comp_names;
                xkb_comp_names = g_new0 (XkbComponentNamesRec, 1);

                XkbRF_GetComponents (xkb_rules, xkb_var_defs, xkb_comp_names);
                upload_xkb_description (rules_file_path, xkb_var_defs, xkb_comp_names);

                free_xkb_component_names (xkb_comp_names);
                XkbRF_Free (xkb_rules, True);
        } else {
                g_warning ("Couldn't load XKB rules");
        }

        if (gdk_error_trap_pop ())
                g_warning ("Error loading XKB rules");

        gnome_xkb_info_free_var_defs (xkb_var_defs);
        g_free (rules_file_path);
}

static void
apply_input_sources_settings (GSettings          *settings,
                              gchar              *key,
                              GsdKeyboardManager *manager)
{
        GsdKeyboardManagerPrivate *priv = manager->priv;
        GVariant *sources;
        guint current;
        const gchar *type = NULL;
        const gchar *id = NULL;
        const gchar *layout = NULL;
        const gchar *variant = NULL;

        sources = g_settings_get_value (priv->input_sources_settings, KEY_INPUT_SOURCES);
        current = g_settings_get_uint (priv->input_sources_settings, KEY_CURRENT_INPUT_SOURCE);

        if (g_variant_n_children (sources) < 1) {
                g_warning ("No input source configured, resetting");
                g_settings_reset (priv->input_sources_settings, KEY_INPUT_SOURCES);
                goto exit;
        }
        if (current >= g_variant_n_children (sources)) {
                g_settings_set_uint (priv->input_sources_settings,
                                     KEY_CURRENT_INPUT_SOURCE,
                                     g_variant_n_children (sources) - 1);
                goto exit;
        }

        g_variant_get_child (sources, current, "(&s&s)", &type, &id);

        if (!g_str_equal (type, INPUT_SOURCE_TYPE_XKB)) {
                g_warning ("Unknown input source type '%s'", type);
                goto exit;
        }

        gnome_xkb_info_get_layout_info (priv->xkb_info, id, NULL, NULL, &layout, &variant);

        if (!layout || !layout[0]) {
                g_warning ("Couldn't find XKB input source '%s'", id);
                goto exit;
        }

        apply_xkb_layout (manager, layout, variant);

 exit:
        g_variant_unref (sources);
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

static void
device_added_cb (GdkDeviceManager   *device_manager,
                 GdkDevice          *device,
                 GsdKeyboardManager *manager)
{
        GdkInputSource source;

        source = gdk_device_get_source (device);
        if (source == GDK_SOURCE_KEYBOARD) {
                apply_settings (manager->priv->settings, NULL, manager);
                apply_input_sources_settings (manager->priv->input_sources_settings, NULL, manager);
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

	xkb_init (manager);

	set_devicepresence_handler (manager);

        manager->priv->input_sources_settings = g_settings_new (GNOME_DESKTOP_INPUT_SOURCES_DIR);

        manager->priv->xkb_info = gnome_xkb_info_new ();

        /* apply current settings before we install the callback */
        apply_settings (manager->priv->settings, NULL, manager);
        apply_input_sources_settings (manager->priv->input_sources_settings, NULL, manager);

        g_signal_connect (G_OBJECT (manager->priv->settings), "changed",
                          G_CALLBACK (apply_settings), manager);
        g_signal_connect (G_OBJECT (manager->priv->input_sources_settings), "changed",
                          G_CALLBACK (apply_input_sources_settings), manager);

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

        g_clear_object (&p->settings);
        g_clear_object (&p->input_sources_settings);
        g_clear_object (&p->xkb_info);

        if (p->device_manager != NULL) {
                g_signal_handler_disconnect (p->device_manager, p->device_added_id);
                g_signal_handler_disconnect (p->device_manager, p->device_removed_id);
                p->device_manager = NULL;
        }

	remove_xkb_filter (manager);
}

static void
gsd_keyboard_manager_class_init (GsdKeyboardManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

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
