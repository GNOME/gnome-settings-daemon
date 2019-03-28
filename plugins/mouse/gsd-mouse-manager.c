/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include <gdesktop-enums.h>

#include "gnome-settings-bus.h"
#include "gnome-settings-profile.h"
#include "gsd-mouse-manager.h"
#include "gsd-enums.h"
#include "gsd-settings-migrate.h"

#define GSD_SETTINGS_MOUSE_SCHEMA  "org.gnome.settings-daemon.peripherals.mouse"
#define GSETTINGS_MOUSE_SCHEMA     "org.gnome.desktop.peripherals.mouse"
#define GSETTINGS_TOUCHPAD_SCHEMA  "org.gnome.desktop.peripherals.touchpad"

struct _GsdMouseManager
{
        GObject parent;

        GSettings *touchpad_settings;
        GSettings *mouse_settings;
        GSettings *gsd_mouse_settings;
};

static void     gsd_mouse_manager_class_init  (GsdMouseManagerClass *klass);
static void     gsd_mouse_manager_init        (GsdMouseManager      *mouse_manager);
static void     gsd_mouse_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdMouseManager, gsd_mouse_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void migrate_mouse_settings (void);

static void
gsd_mouse_manager_class_init (GsdMouseManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_mouse_manager_finalize;
}

static void
gsd_mouse_manager_init (GsdMouseManager *manager)
{
}

gboolean
gsd_mouse_manager_start (GsdMouseManager *manager,
                         GError         **error)
{
        gnome_settings_profile_start (NULL);

        migrate_mouse_settings ();

        if (gnome_settings_is_wayland ())
                return TRUE;

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_mouse_manager_stop (GsdMouseManager *manager)
{
        g_debug ("Stopping mouse manager");

        g_clear_object (&manager->mouse_settings);
        g_clear_object (&manager->touchpad_settings);
        g_clear_object (&manager->gsd_mouse_settings);
}

static void
gsd_mouse_manager_finalize (GObject *object)
{
        GsdMouseManager *mouse_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_MOUSE_MANAGER (object));

        mouse_manager = GSD_MOUSE_MANAGER (object);

        g_return_if_fail (mouse_manager != NULL);

        gsd_mouse_manager_stop (mouse_manager);

        G_OBJECT_CLASS (gsd_mouse_manager_parent_class)->finalize (object);
}

static GVariant *
map_speed (GVariant *variant, GVariant *old_default, GVariant *new_default)
{
        gdouble value;

        value = g_variant_get_double (variant);

        /* Remap from [0..10] to [-1..1] */
        value = (value / 5) - 1;

        return g_variant_new_double (value);
}

static GVariant *
map_send_events (GVariant *variant, GVariant *old_default, GVariant *new_default)
{
        gboolean enabled;

        enabled = g_variant_get_boolean (variant);

        if (enabled) {
                return g_variant_new_string ("enabled");
        } else {
                return g_variant_new_string ("disabled");
        }
}

static GVariant *
map_edge_scrolling_enabled (GVariant *variant, GVariant *old_default, GVariant *new_default)
{
	GsdTouchpadScrollMethod  method;

	method = g_variant_get_uint32 (variant);
	if (method == GSD_TOUCHPAD_SCROLL_METHOD_EDGE_SCROLLING)
		return g_variant_new_boolean (TRUE);
	else
		return g_variant_new_boolean (FALSE);
}

static void
migrate_mouse_settings (void)
{
        GsdSettingsMigrateEntry trackball_entries[] = {
                { "scroll-wheel-emulation-button", "scroll-wheel-emulation-button", NULL }
        };
        GsdSettingsMigrateEntry mouse_entries[] = {
                { "left-handed",           "left-handed", NULL },
                { "motion-acceleration",   "speed",       map_speed },
                { "motion-threshold",      NULL,          NULL },
                { "middle-button-enabled", NULL,          NULL },
        };
        GsdSettingsMigrateEntry touchpad_entries[] = {
                { "disable-while-typing", NULL,             NULL },
                { "horiz-scroll-enabled", NULL,             NULL },
                { "scroll-method",        "edge-scrolling-enabled", map_edge_scrolling_enabled },
                { "tap-to-click",         "tap-to-click",   NULL },
                { "touchpad-enabled",     "send-events",    map_send_events },
                { "left-handed",          "left-handed",    NULL },
                { "motion-acceleration",  "speed",          map_speed },
                { "motion-threshold",     NULL,             NULL },
                { "natural-scroll",       "natural-scroll", NULL }
        };

        gsd_settings_migrate_check ("org.gnome.settings-daemon.peripherals.trackball.deprecated",
                                    "/org/gnome/settings-daemon/peripherals/trackball/",
                                    "org.gnome.desktop.peripherals.trackball",
                                    "/org/gnome/desktop/peripherals/trackball/",
                                    trackball_entries, G_N_ELEMENTS (trackball_entries));
        gsd_settings_migrate_check ("org.gnome.settings-daemon.peripherals.mouse.deprecated",
                                    "/org/gnome/settings-daemon/peripherals/mouse/",
                                    "org.gnome.desktop.peripherals.mouse",
                                    "/org/gnome/desktop/peripherals/mouse/",
                                    mouse_entries, G_N_ELEMENTS (mouse_entries));
        gsd_settings_migrate_check ("org.gnome.settings-daemon.peripherals.touchpad.deprecated",
                                    "/org/gnome/settings-daemon/peripherals/touchpad/",
                                    "org.gnome.desktop.peripherals.touchpad",
                                    "/org/gnome/desktop/peripherals/touchpad/",
                                    touchpad_entries, G_N_ELEMENTS (touchpad_entries));
}

GsdMouseManager *
gsd_mouse_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_MOUSE_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_MOUSE_MANAGER (manager_object);
}
