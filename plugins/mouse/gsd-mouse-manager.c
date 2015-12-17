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

#define GSD_MOUSE_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_MOUSE_MANAGER, GsdMouseManagerPrivate))

#define GSD_SETTINGS_MOUSE_SCHEMA  "org.gnome.settings-daemon.peripherals.mouse"
#define GSETTINGS_MOUSE_SCHEMA     "org.gnome.desktop.peripherals.mouse"
#define GSETTINGS_TOUCHPAD_SCHEMA  "org.gnome.desktop.peripherals.touchpad"

/* Mouse settings */
#define KEY_LOCATE_POINTER               "locate-pointer"
#define KEY_DWELL_CLICK_ENABLED          "dwell-click-enabled"
#define KEY_SECONDARY_CLICK_ENABLED      "secondary-click-enabled"

struct GsdMouseManagerPrivate
{
        guint start_idle_id;
        GSettings *touchpad_settings;
        GSettings *mouse_a11y_settings;
        GSettings *mouse_settings;
        GSettings *gsd_mouse_settings;
        gboolean mousetweaks_daemon_running;
        gboolean locate_pointer_spawned;
        GPid locate_pointer_pid;
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

        g_type_class_add_private (klass, sizeof (GsdMouseManagerPrivate));
}

static void
set_locate_pointer (GsdMouseManager *manager,
                    gboolean         state)
{
        if (state) {
                GError *error = NULL;
                char *args[2];

                if (manager->priv->locate_pointer_spawned)
                        return;

                args[0] = LIBEXECDIR "/gsd-locate-pointer";
                args[1] = NULL;

                g_spawn_async (NULL, args, NULL,
                               0, NULL, NULL,
                               &manager->priv->locate_pointer_pid, &error);

                manager->priv->locate_pointer_spawned = (error == NULL);

                if (error) {
                        g_settings_set_boolean (manager->priv->gsd_mouse_settings, KEY_LOCATE_POINTER, FALSE);
                        g_error_free (error);
                }

        } else if (manager->priv->locate_pointer_spawned) {
                kill (manager->priv->locate_pointer_pid, SIGHUP);
                g_spawn_close_pid (manager->priv->locate_pointer_pid);
                manager->priv->locate_pointer_spawned = FALSE;
        }
}

static void
set_mousetweaks_daemon (GsdMouseManager *manager,
                        gboolean         dwell_click_enabled,
                        gboolean         secondary_click_enabled)
{
        GError *error = NULL;
        gchar *comm;
        gboolean run_daemon = dwell_click_enabled || secondary_click_enabled;

        if (run_daemon || manager->priv->mousetweaks_daemon_running)
                comm = g_strdup_printf ("mousetweaks %s",
                                        run_daemon ? "" : "-s");
        else
                return;

        if (run_daemon)
                manager->priv->mousetweaks_daemon_running = TRUE;

        if (! g_spawn_command_line_async (comm, &error)) {
                if (error->code == G_SPAWN_ERROR_NOENT && run_daemon) {
                        if (dwell_click_enabled) {
                                g_settings_set_boolean (manager->priv->mouse_a11y_settings,
                                                        KEY_DWELL_CLICK_ENABLED, FALSE);
                        } else if (secondary_click_enabled) {
                                g_settings_set_boolean (manager->priv->mouse_a11y_settings,
                                                        KEY_SECONDARY_CLICK_ENABLED, FALSE);
                        }

                        g_warning ("Error enabling mouse accessibility features (mousetweaks is not installed)");
                }
                g_error_free (error);
        }
        g_free (comm);
}

static void
mouse_callback (GSettings       *settings,
                const gchar     *key,
                GsdMouseManager *manager)
{
        if (g_str_equal (key, KEY_DWELL_CLICK_ENABLED) ||
            g_str_equal (key, KEY_SECONDARY_CLICK_ENABLED)) {
                set_mousetweaks_daemon (manager,
                                        g_settings_get_boolean (settings, KEY_DWELL_CLICK_ENABLED),
                                        g_settings_get_boolean (settings, KEY_SECONDARY_CLICK_ENABLED));
        } else if (g_str_equal (key, KEY_LOCATE_POINTER)) {
                set_locate_pointer (manager, g_settings_get_boolean (settings, KEY_LOCATE_POINTER));
        }
}

static void
gsd_mouse_manager_init (GsdMouseManager *manager)
{
        manager->priv = GSD_MOUSE_MANAGER_GET_PRIVATE (manager);
}

static gboolean
gsd_mouse_manager_idle_cb (GsdMouseManager *manager)
{
        gnome_settings_profile_start (NULL);

        manager->priv->gsd_mouse_settings = g_settings_new (GSD_SETTINGS_MOUSE_SCHEMA);
        g_signal_connect (manager->priv->gsd_mouse_settings, "changed",
                          G_CALLBACK (mouse_callback), manager);

        manager->priv->mouse_a11y_settings = g_settings_new ("org.gnome.desktop.a11y.mouse");
        g_signal_connect (manager->priv->mouse_a11y_settings, "changed",
                          G_CALLBACK (mouse_callback), manager);
#if 0
        manager->priv->mouse_settings = g_settings_new (GSETTINGS_MOUSE_SCHEMA);
        g_signal_connect (manager->priv->mouse_settings, "changed",
                          G_CALLBACK (mouse_callback), manager);
#endif

        set_locate_pointer (manager, g_settings_get_boolean (manager->priv->gsd_mouse_settings, KEY_LOCATE_POINTER));
        set_mousetweaks_daemon (manager,
                                g_settings_get_boolean (manager->priv->mouse_a11y_settings, KEY_DWELL_CLICK_ENABLED),
                                g_settings_get_boolean (manager->priv->mouse_a11y_settings, KEY_SECONDARY_CLICK_ENABLED));

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_mouse_manager_start (GsdMouseManager *manager,
                         GError         **error)
{
        gnome_settings_profile_start (NULL);

        migrate_mouse_settings ();

        if (gnome_settings_is_wayland ())
                return TRUE;

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) gsd_mouse_manager_idle_cb, manager);
        g_source_set_name_by_id (manager->priv->start_idle_id, "[gnome-settings-daemon] gsd_mouse_manager_idle_cb");

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_mouse_manager_stop (GsdMouseManager *manager)
{
        GsdMouseManagerPrivate *p = manager->priv;

        g_debug ("Stopping mouse manager");

        if (manager->priv->start_idle_id != 0) {
                g_source_remove (manager->priv->start_idle_id);
                manager->priv->start_idle_id = 0;
        }

        g_clear_object (&p->mouse_a11y_settings);
        g_clear_object (&p->mouse_settings);
        g_clear_object (&p->touchpad_settings);
        g_clear_object (&p->gsd_mouse_settings);

        set_locate_pointer (manager, FALSE);
}

static void
gsd_mouse_manager_finalize (GObject *object)
{
        GsdMouseManager *mouse_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_MOUSE_MANAGER (object));

        mouse_manager = GSD_MOUSE_MANAGER (object);

        g_return_if_fail (mouse_manager->priv != NULL);

        gsd_mouse_manager_stop (mouse_manager);

        G_OBJECT_CLASS (gsd_mouse_manager_parent_class)->finalize (object);
}

static GVariant *
map_speed (GVariant *variant)
{
        gdouble value;

        value = g_variant_get_double (variant);

        /* Remap from [0..10] to [-1..1] */
        value = (value / 5) - 1;

        return g_variant_new_double (value);
}

static GVariant *
map_send_events (GVariant *variant)
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
map_edge_scrolling_enabled (GVariant *variant)
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
