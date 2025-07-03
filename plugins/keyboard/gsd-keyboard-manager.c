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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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

#include "gnome-settings-bus.h"
#include "gnome-settings-profile.h"
#include "gsd-keyboard-manager.h"
#include "gnome-settings-daemon/gsd-enums.h"
#include "gsd-settings-migrate.h"

#define GNOME_DESKTOP_INTERFACE_DIR "org.gnome.desktop.interface"

#define GNOME_DESKTOP_INPUT_SOURCES_DIR "org.gnome.desktop.input-sources"

#define KEY_INPUT_SOURCES        "sources"
#define KEY_KEYBOARD_OPTIONS     "xkb-options"

#define INPUT_SOURCE_TYPE_XKB  "xkb"
#define INPUT_SOURCE_TYPE_IBUS "ibus"

#define DEFAULT_LAYOUT "us"

#define SETTINGS_PORTED_FILE ".gsd-keyboard.settings-ported"

struct _GsdKeyboardManager
{
        GsdApplication parent;

        guint      start_idle_id;
        GSettings *input_sources_settings;
        GDBusProxy *localed;
        GCancellable *cancellable;
};

static void     gsd_keyboard_manager_class_init  (GsdKeyboardManagerClass *klass);
static void     gsd_keyboard_manager_init        (GsdKeyboardManager      *keyboard_manager);

static void     migrate_keyboard_settings (void);

G_DEFINE_TYPE (GsdKeyboardManager, gsd_keyboard_manager, GSD_TYPE_APPLICATION)

static void
init_builder_with_sources (GVariantBuilder *builder,
                           GSettings       *settings)
{
        const gchar *type;
        const gchar *id;
        GVariantIter iter;
        GVariant *sources;

        sources = g_settings_get_value (settings, KEY_INPUT_SOURCES);

        g_variant_builder_init (builder, G_VARIANT_TYPE ("a(ss)"));

        g_variant_iter_init (&iter, sources);
        while (g_variant_iter_next (&iter, "(&s&s)", &type, &id))
                g_variant_builder_add (builder, "(ss)", type, id);

        g_variant_unref (sources);
}

static gboolean
schema_is_installed (const char *schema)
{
        GSettingsSchemaSource *source = NULL;
        gchar **non_relocatable = NULL;
        gchar **relocatable = NULL;
        gboolean installed = FALSE;

        source = g_settings_schema_source_get_default ();
        if (!source)
                return FALSE;

        g_settings_schema_source_list_schemas (source, TRUE, &non_relocatable, &relocatable);

        if (g_strv_contains ((const gchar * const *)non_relocatable, schema) ||
            g_strv_contains ((const gchar * const *)relocatable, schema))
                installed = TRUE;

        g_strfreev (non_relocatable);
        g_strfreev (relocatable);
        return installed;
}

static void
get_sources_from_xkb_config (GsdKeyboardManager *manager)
{
        GVariantBuilder builder;
        GVariant *v;
        gint i, n;
        gchar **layouts = NULL;
        gchar **variants = NULL;

        v = g_dbus_proxy_get_cached_property (manager->localed, "X11Layout");
        if (v) {
                const gchar *s = g_variant_get_string (v, NULL);
                if (*s)
                        layouts = g_strsplit (s, ",", -1);
                g_variant_unref (v);
        }

        init_builder_with_sources (&builder, manager->input_sources_settings);

        if (!layouts) {
                g_variant_builder_add (&builder, "(ss)", INPUT_SOURCE_TYPE_XKB, DEFAULT_LAYOUT);
                goto out;
	}

        v = g_dbus_proxy_get_cached_property (manager->localed, "X11Variant");
        if (v) {
                const gchar *s = g_variant_get_string (v, NULL);
                if (*s)
                        variants = g_strsplit (s, ",", -1);
                g_variant_unref (v);
        }

        if (variants && variants[0])
                n = MIN (g_strv_length (layouts), g_strv_length (variants));
        else
                n = g_strv_length (layouts);

        for (i = 0; i < n && layouts[i][0]; ++i) {
                gchar *id;

                if (variants && variants[i] && variants[i][0])
                        id = g_strdup_printf ("%s+%s", layouts[i], variants[i]);
                else
                        id = g_strdup (layouts[i]);

                g_variant_builder_add (&builder, "(ss)", INPUT_SOURCE_TYPE_XKB, id);
                g_free (id);
        }

out:
        g_settings_set_value (manager->input_sources_settings, KEY_INPUT_SOURCES, g_variant_builder_end (&builder));

        g_strfreev (layouts);
        g_strfreev (variants);
}

static void
get_options_from_xkb_config (GsdKeyboardManager *manager)
{
        GVariant *v;
        gchar **options = NULL;

        v = g_dbus_proxy_get_cached_property (manager->localed, "X11Options");
        if (v) {
                const gchar *s = g_variant_get_string (v, NULL);
                if (*s)
                        options = g_strsplit (s, ",", -1);
                g_variant_unref (v);
        }

        if (!options)
                return;

        g_settings_set_strv (manager->input_sources_settings, KEY_KEYBOARD_OPTIONS, (const gchar * const*) options);

        g_strfreev (options);
}

static void
convert_libgnomekbd_options (GSettings *settings)
{
        GPtrArray *opt_array;
        GSettings *libgnomekbd_settings;
        gchar **options, **o;

        if (!schema_is_installed ("org.gnome.libgnomekbd.keyboard"))
                return;

        opt_array = g_ptr_array_new_with_free_func (g_free);

        libgnomekbd_settings = g_settings_new ("org.gnome.libgnomekbd.keyboard");
        options = g_settings_get_strv (libgnomekbd_settings, "options");

        for (o = options; *o; ++o) {
                gchar **strv;

                strv = g_strsplit (*o, "\t", 2);
                if (strv[0] && strv[1])
                        g_ptr_array_add (opt_array, g_strdup (strv[1]));
                g_strfreev (strv);
        }
        g_ptr_array_add (opt_array, NULL);

        g_settings_set_strv (settings, KEY_KEYBOARD_OPTIONS, (const gchar * const*) opt_array->pdata);

        g_strfreev (options);
        g_object_unref (libgnomekbd_settings);
        g_ptr_array_free (opt_array, TRUE);
}

static void
convert_libgnomekbd_layouts (GSettings *settings)
{
        GVariantBuilder builder;
        GSettings *libgnomekbd_settings;
        gchar **layouts, **l;

        if (!schema_is_installed ("org.gnome.libgnomekbd.keyboard"))
                return;

        init_builder_with_sources (&builder, settings);

        libgnomekbd_settings = g_settings_new ("org.gnome.libgnomekbd.keyboard");
        layouts = g_settings_get_strv (libgnomekbd_settings, "layouts");

        for (l = layouts; *l; ++l) {
                gchar *id;
                gchar **strv;

                strv = g_strsplit (*l, "\t", 2);
                if (strv[0] && !strv[1])
                        id = g_strdup (strv[0]);
                else if (strv[0] && strv[1])
                        id = g_strdup_printf ("%s+%s", strv[0], strv[1]);
                else
                        id = NULL;

                if (id)
                        g_variant_builder_add (&builder, "(ss)", INPUT_SOURCE_TYPE_XKB, id);

                g_free (id);
                g_strfreev (strv);
        }

        g_settings_set_value (settings, KEY_INPUT_SOURCES, g_variant_builder_end (&builder));

        g_strfreev (layouts);
        g_object_unref (libgnomekbd_settings);
}

static void
maybe_convert_old_settings (GSettings *settings)
{
        GVariant *sources;
        gchar **options;
        gchar *stamp_dir_path = NULL;
        gchar *stamp_file_path = NULL;
        GError *error = NULL;

        stamp_dir_path = g_build_filename (g_get_user_data_dir (), PACKAGE_NAME, NULL);
        if (g_mkdir_with_parents (stamp_dir_path, 0755)) {
                g_warning ("Failed to create directory %s: %s", stamp_dir_path, g_strerror (errno));
                goto out;
        }

        stamp_file_path = g_build_filename (stamp_dir_path, "input-sources-converted", NULL);
        if (g_file_test (stamp_file_path, G_FILE_TEST_EXISTS))
                goto out;

        sources = g_settings_get_value (settings, KEY_INPUT_SOURCES);
        if (g_variant_n_children (sources) < 1) {
                convert_libgnomekbd_layouts (settings);
        }
        g_variant_unref (sources);

        options = g_settings_get_strv (settings, KEY_KEYBOARD_OPTIONS);
        if (g_strv_length (options) < 1)
                convert_libgnomekbd_options (settings);
        g_strfreev (options);

        if (!g_file_set_contents (stamp_file_path, "", 0, &error)) {
                g_warning ("%s", error->message);
                g_error_free (error);
        }
out:
        g_free (stamp_file_path);
        g_free (stamp_dir_path);
}

static void
maybe_create_initial_settings (GsdKeyboardManager *manager)
{
        GSettings *settings;
        GVariant *sources;
        GVariant *options;

        settings = manager->input_sources_settings;

        if (g_getenv ("RUNNING_UNDER_GDM"))
                return;

        maybe_convert_old_settings (settings);

        /* if we still don't have anything do some educated guesses */
        sources = g_settings_get_value (settings, KEY_INPUT_SOURCES);
        if (g_variant_n_children (sources) < 1)
                get_sources_from_xkb_config (manager);
        g_variant_unref (sources);

        options = g_settings_get_user_value (settings, KEY_KEYBOARD_OPTIONS);
        if (options == NULL)
                get_options_from_xkb_config (manager);
        g_variant_unref (options);
}

static void
localed_proxy_ready (GObject      *source,
                     GAsyncResult *res,
                     gpointer      data)
{
        GsdKeyboardManager *manager = data;
        GDBusProxy *proxy;
        GError *error = NULL;

        proxy = g_dbus_proxy_new_finish (res, &error);
        if (!proxy) {
                if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                        g_error_free (error);
                        return;
                }
                g_warning ("Failed to contact localed: %s", error->message);
                g_error_free (error);
        }

        manager->localed = proxy;
        maybe_create_initial_settings (manager);
}

static gboolean
start_keyboard_idle_cb (GsdKeyboardManager *manager)
{
        gnome_settings_profile_start (NULL);

        g_debug ("Starting keyboard manager");

        manager->input_sources_settings = g_settings_new (GNOME_DESKTOP_INPUT_SOURCES_DIR);

        manager->cancellable = g_cancellable_new ();

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  "org.freedesktop.locale1",
                                  "/org/freedesktop/locale1",
                                  "org.freedesktop.locale1",
                                  manager->cancellable,
                                  localed_proxy_ready,
                                  manager);

        gnome_settings_profile_end (NULL);

        manager->start_idle_id = 0;

        return FALSE;
}

static void
gsd_keyboard_manager_startup (GApplication *app)
{
        GsdKeyboardManager *manager = GSD_KEYBOARD_MANAGER (app);

        gnome_settings_profile_start (NULL);

        manager->start_idle_id = g_idle_add ((GSourceFunc) start_keyboard_idle_cb, manager);
        g_source_set_name_by_id (manager->start_idle_id, "[gnome-settings-daemon] start_keyboard_idle_cb");

        G_APPLICATION_CLASS (gsd_keyboard_manager_parent_class)->startup (app);

        gnome_settings_profile_end (NULL);
}

static void
gsd_keyboard_manager_shutdown (GApplication *app)
{
        GsdKeyboardManager *manager = GSD_KEYBOARD_MANAGER (app);

        g_debug ("Stopping keyboard manager");

        g_cancellable_cancel (manager->cancellable);
        g_clear_object (&manager->cancellable);

        g_clear_object (&manager->input_sources_settings);
        g_clear_object (&manager->localed);

        g_clear_handle_id (&manager->start_idle_id, g_source_remove);

        G_APPLICATION_CLASS (gsd_keyboard_manager_parent_class)->shutdown (app);
}

static void
gsd_keyboard_manager_class_init (GsdKeyboardManagerClass *klass)
{
        GApplicationClass *application_class = G_APPLICATION_CLASS (klass);

        application_class->startup = gsd_keyboard_manager_startup;
        application_class->shutdown = gsd_keyboard_manager_shutdown;
}

static void
gsd_keyboard_manager_init (GsdKeyboardManager *manager)
{
        migrate_keyboard_settings ();
}

static GVariant *
reset_gtk_im_module (GVariant *variant,
                     GVariant *old_default,
                     GVariant *new_default)
{
        return NULL;
}

static void
migrate_keyboard_settings (void)
{
        GsdSettingsMigrateEntry entries[] = {
                { "repeat",                 "repeat",                 NULL },
                { "repeat-interval",        "repeat-interval",        NULL },
                { "delay",                  "delay",                  NULL },
                { "remember-numlock-state", "remember-numlock-state", NULL },
        };
        g_autofree char *filename = NULL;

        gsd_settings_migrate_check ("org.gnome.settings-daemon.peripherals.keyboard.deprecated",
                                    "/org/gnome/settings-daemon/peripherals/keyboard/",
                                    "org.gnome.desktop.peripherals.keyboard",
                                    "/org/gnome/desktop/peripherals/keyboard/",
                                    entries, G_N_ELEMENTS (entries));

        /* In prior versions to GNOME 42, the gtk-im-module setting was
         * owned by gsd-keyboard. Reset it once before giving it back
         * to the user.
         */
        filename = g_build_filename (g_get_user_config_dir (),
                                     SETTINGS_PORTED_FILE,
                                     NULL);

        if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
                GsdSettingsMigrateEntry im_entry[] = {
                        { "gtk-im-module", "gtk-im-module", reset_gtk_im_module },
                };
                g_autoptr(GError) error = NULL;

                gsd_settings_migrate_check ("org.gnome.desktop.interface",
                                            "/org/gnome/desktop/interface/",
                                            "org.gnome.desktop.interface",
                                            "/org/gnome/desktop/interface/",
                                            im_entry, G_N_ELEMENTS (im_entry));

                if (!g_file_set_contents (filename, "", -1, &error))
                        g_warning ("Error migrating gtk-im-module: %s", error->message);
        }
}
