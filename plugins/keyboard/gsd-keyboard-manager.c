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

#ifdef HAVE_IBUS
#include <ibus.h>
#endif

#include "gnome-settings-profile.h"
#include "gsd-keyboard-manager.h"
#include "gsd-input-helper.h"
#include "gsd-enums.h"
#include "xkb-rules-db.h"

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

#define KEY_CURRENT_IS     "current"
#define KEY_INPUT_SOURCES  "sources"

#ifndef DFLT_XKB_CONFIG_ROOT
#define DFLT_XKB_CONFIG_ROOT "/usr/share/X11/xkb"
#endif
#ifndef DFLT_XKB_RULES_FILE
#define DFLT_XKB_RULES_FILE "base"
#endif
#ifndef DFLT_XKB_LAYOUT
#define DFLT_XKB_LAYOUT "us"
#endif
#ifndef DFLT_XKB_MODEL
#define DFLT_XKB_MODEL "pc105"
#endif

#ifdef HAVE_IBUS
#define GNOME_DESKTOP_INTERFACE_DIR "org.gnome.desktop.interface"

#define KEY_GTK_IM_MODULE "gtk-im-module"
#define GTK_IM_MODULE_SIMPLE "gtk-im-context-simple"
#define GTK_IM_MODULE_IBUS   "ibus"
#endif

struct GsdKeyboardManagerPrivate
{
	guint      start_idle_id;
        GSettings *settings;
        GSettings *is_settings;
#ifdef HAVE_IBUS
        GSettings *interface_settings;
#endif
        gulong     ignore_serial;
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
            xev->xany.serial == manager->priv->ignore_serial)
		return GDK_FILTER_CONTINUE;

	if (xkbev->any.xkb_type == XkbStateNotify &&
            xkbev->state.changed & XkbModifierLockMask) {
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
get_xkb_values (gchar            **rules,
                XkbRF_VarDefsRec  *var_defs)
{
        Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        *rules = NULL;

        /* Get it from the X property or fallback on defaults */
        if (!XkbRF_GetNamesProp (display, rules, var_defs) || !*rules) {
                *rules = strdup (DFLT_XKB_RULES_FILE);
                var_defs->model = strdup (DFLT_XKB_MODEL);
                var_defs->layout = strdup (DFLT_XKB_LAYOUT);
                var_defs->variant = NULL;
                var_defs->options = NULL;
        }
}

static void
free_xkb_var_defs (XkbRF_VarDefsRec *p)
{
        if (p->model)
                free (p->model);
        if (p->layout)
                free (p->layout);
        if (p->variant)
                free (p->variant);
        if (p->options)
                free (p->options);
        free (p);
}

static void
free_xkb_component_names (XkbComponentNamesRec *p)
{
        if (p->keymap)
                free (p->keymap);
        if (p->keycodes)
                free (p->keycodes);
        if (p->types)
                free (p->types);
        if (p->compat)
                free (p->compat);
        if (p->symbols)
                free (p->symbols);
        if (p->geometry)
                free (p->geometry);
        free (p);
}

static void
upload_xkb_description (gchar                *rules_file,
                        XkbRF_VarDefsRec     *var_defs,
                        XkbComponentNamesRec *comp_names)
{
        Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        XkbDescRec *xkb_desc;

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

        if (!XkbRF_SetNamesProp (display, rules_file, var_defs))
                g_warning ("Couldn't update the XKB root window property");
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

static void
replace_layout_and_variant (XkbRF_VarDefsRec *xkb_var_defs,
                            const gchar      *layout,
                            const gchar      *variant)
{
        /* Toolkits need to know about both a latin layout to handle
         * accelerators which are usually defined like Ctrl+C and a
         * layout with the symbols for the language used in UI strings
         * to handle mnemonics like Alt+Ф, so we try to find and add
         * them in XKB group slots after the layout which the user
         * actually intends to type with. */
        const gchar *latin_layout = "us";
        const gchar *latin_variant = "";
        const gchar *locale_layout;
        const gchar *locale_variant;
        const gchar *locale = setlocale (LC_MESSAGES, NULL);
        gchar *language = language_code_from_locale (locale);

        xkb_rules_db_get_layout_info_for_language (language,
                                                   NULL,
                                                   NULL,
                                                   &locale_layout,
                                                   &locale_variant);
        g_free (language);

        if ((g_strcmp0 (latin_layout, locale_layout) == 0 &&
             g_strcmp0 (latin_variant, locale_variant) == 0)
            ||
            (g_strcmp0 (latin_layout, layout) == 0 &&
             g_strcmp0 (latin_variant, variant) == 0)) {
                latin_layout = NULL;
                latin_variant = NULL;
        }

        if (g_strcmp0 (locale_layout, layout) == 0 &&
            g_strcmp0 (locale_variant, variant) == 0) {
                locale_layout = NULL;
                locale_variant = NULL;
        }

        if (xkb_var_defs->layout)
                free (xkb_var_defs->layout);

        xkb_var_defs->layout =
                locale_layout && latin_layout ?
                g_strdup_printf ("%s,%s,%s", layout, locale_layout, latin_layout) :
                locale_layout ?
                g_strdup_printf ("%s,%s", layout, locale_layout) :
                latin_layout ?
                g_strdup_printf ("%s,%s", layout, latin_layout) :
                g_strdup_printf ("%s", layout);

        if (xkb_var_defs->variant)
                free (xkb_var_defs->variant);

        xkb_var_defs->variant =
                locale_variant && latin_variant ?
                g_strdup_printf ("%s,%s,%s", variant, locale_variant, latin_variant) :
                locale_variant ?
                g_strdup_printf ("%s,%s", variant, locale_variant) :
                latin_variant ?
                g_strdup_printf ("%s,%s", variant, latin_variant) :
                g_strdup_printf ("%s", variant);
}

static void
apply_xkb_layout (GsdKeyboardManager *manager,
                  const gchar        *layout,
                  const gchar        *variant)
{
        Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        XkbRF_RulesRec *xkb_rules;
        XkbRF_VarDefsRec *xkb_var_defs;
        gchar *rules_file;
        gchar *rules_path;

        xkb_var_defs = calloc (1, sizeof (XkbRF_VarDefsRec));
        get_xkb_values (&rules_file, xkb_var_defs);

        if (rules_file[0] == '/')
                rules_path = g_strdup (rules_file);
        else
                rules_path = g_strdup_printf ("%s/rules/%s",
                                              DFLT_XKB_CONFIG_ROOT,
                                              rules_file);

        replace_layout_and_variant (xkb_var_defs, layout, variant);

        xkb_rules = XkbRF_Load (rules_path, "C", True, True);
        if (xkb_rules) {
                XkbComponentNamesRec *xkb_comp_names;
                xkb_comp_names = calloc (1, sizeof (XkbComponentNamesRec));

                XkbRF_GetComponents (xkb_rules, xkb_var_defs, xkb_comp_names);
                manager->priv->ignore_serial = XNextRequest (display);
                upload_xkb_description (rules_file, xkb_var_defs, xkb_comp_names);

                free_xkb_component_names (xkb_comp_names);
                XkbRF_Free (xkb_rules, True);
        } else {
                g_warning ("Couldn't load XKB rules");
        }

        free_xkb_var_defs (xkb_var_defs);
        free (rules_file);
        g_free (rules_path);
}

#ifdef HAVE_IBUS
static void
apply_ibus_engine (GsdKeyboardManager *manager,
                   const gchar        *engine)
{
        IBusEngineDesc *desc = NULL;
        IBusBus *ibus = ibus_bus_new ();

        if (!ibus_bus_set_global_engine (ibus, engine) ||
            !(desc = ibus_bus_get_global_engine (ibus)))
                g_warning ("Couldn't set IBus engine");
        else
                g_settings_set_string (manager->priv->interface_settings,
                                       KEY_GTK_IM_MODULE,
                                       GTK_IM_MODULE_IBUS);

        g_object_unref (ibus);
        if (desc)
                g_object_unref (desc);
}
#endif

static void
apply_input_sources_settings (GSettings          *settings,
                              gchar              *key,
                              GsdKeyboardManager *manager)
{
        const gchar *layout;
        const gchar *variant;
        const gchar *engine;

        g_settings_get (manager->priv->is_settings, KEY_CURRENT_IS,
                        "(&s&s&s&s&s)", NULL, NULL, &layout, &variant, &engine);

        apply_xkb_layout (manager, layout, variant);
#ifdef HAVE_IBUS
        if (engine && engine[0] != '\0')
                apply_ibus_engine (manager, engine);
        else
                g_settings_set_string (manager->priv->interface_settings,
                                       KEY_GTK_IM_MODULE,
                                       GTK_IM_MODULE_SIMPLE);
#endif
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
                apply_input_sources_settings (manager->priv->is_settings, NULL, manager);
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
        Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        gnome_settings_profile_start (NULL);

        g_debug ("Starting keyboard manager");

        manager->priv->settings = g_settings_new (GSD_KEYBOARD_DIR);

	xkb_init (manager);

	set_devicepresence_handler (manager);

        manager->priv->is_settings = g_settings_new (GNOME_DESKTOP_INPUT_SOURCES_DIR);
        manager->priv->ignore_serial = XLastKnownRequestProcessed (display) - 1;

#ifdef HAVE_IBUS
        ibus_init ();
        manager->priv->interface_settings = g_settings_new (GNOME_DESKTOP_INTERFACE_DIR);
#endif

        /* apply current settings before we install the callback */
        apply_settings (manager->priv->settings, NULL, manager);
        apply_input_sources_settings (manager->priv->is_settings, NULL, manager);

        g_signal_connect (G_OBJECT (manager->priv->settings), "changed",
                          G_CALLBACK (apply_settings), manager);
        g_signal_connect (G_OBJECT (manager->priv->is_settings), "changed::" KEY_CURRENT_IS,
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

        if (p->settings != NULL) {
                g_object_unref (p->settings);
                p->settings = NULL;
        }

        if (p->is_settings != NULL) {
                g_object_unref (p->is_settings);
                p->is_settings = NULL;
        }
#ifdef HAVE_IBUS
        if (p->interface_settings != NULL) {
                g_object_unref (p->interface_settings);
                p->interface_settings = NULL;
        }
#endif

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
