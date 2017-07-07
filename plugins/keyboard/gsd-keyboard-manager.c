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
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include "gnome-settings-bus.h"
#include "gnome-settings-profile.h"
#include "gsd-keyboard-manager.h"
#include "gsd-input-helper.h"
#include "gsd-enums.h"
#include "gsd-settings-migrate.h"

#define GSD_KEYBOARD_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_KEYBOARD_MANAGER, GsdKeyboardManagerPrivate))

#define GSD_KEYBOARD_DIR "org.gnome.settings-daemon.peripherals.keyboard"

#define KEY_CLICK          "click"
#define KEY_CLICK_VOLUME   "click-volume"
#define KEY_REMEMBER_NUMLOCK_STATE "remember-numlock-state"
#define KEY_NUMLOCK_STATE  "numlock-state"

#define KEY_BELL_VOLUME    "bell-volume"
#define KEY_BELL_PITCH     "bell-pitch"
#define KEY_BELL_DURATION  "bell-duration"
#define KEY_BELL_MODE      "bell-mode"
#define KEY_BELL_CUSTOM_FILE "bell-custom-file"

#define GNOME_DESKTOP_INTERFACE_DIR "org.gnome.desktop.interface"

#define KEY_GTK_IM_MODULE    "gtk-im-module"
#define GTK_IM_MODULE_SIMPLE "gtk-im-context-simple"
#define GTK_IM_MODULE_IBUS   "ibus"

#define GNOME_DESKTOP_INPUT_SOURCES_DIR "org.gnome.desktop.input-sources"

#define KEY_INPUT_SOURCES        "sources"
#define KEY_KEYBOARD_OPTIONS     "xkb-options"

#define INPUT_SOURCE_TYPE_XKB  "xkb"
#define INPUT_SOURCE_TYPE_IBUS "ibus"

#define DEFAULT_LAYOUT "us"

struct GsdKeyboardManagerPrivate
{
	guint      start_idle_id;
        GSettings *settings;
        GSettings *input_sources_settings;
        GDBusProxy *localed;
        GCancellable *cancellable;

        gint       xkb_event_base;
        GsdNumLockState old_state;
        GdkDeviceManager *device_manager;
        guint device_added_id;
};

static void     gsd_keyboard_manager_class_init  (GsdKeyboardManagerClass *klass);
static void     gsd_keyboard_manager_init        (GsdKeyboardManager      *keyboard_manager);
static void     gsd_keyboard_manager_finalize    (GObject                 *object);

G_DEFINE_TYPE (GsdKeyboardManager, gsd_keyboard_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

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

static const char *
num_lock_state_to_string (GsdNumLockState numlock_state)
{
	switch (numlock_state) {
	case GSD_NUM_LOCK_STATE_UNKNOWN:
		return "GSD_NUM_LOCK_STATE_UNKNOWN";
	case GSD_NUM_LOCK_STATE_ON:
		return "GSD_NUM_LOCK_STATE_ON";
	case GSD_NUM_LOCK_STATE_OFF:
		return "GSD_NUM_LOCK_STATE_OFF";
	default:
		return "UNKNOWN";
	}
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
			g_debug ("New num-lock state '%s' != Old num-lock state '%s'",
				 num_lock_state_to_string (numlock_state),
				 num_lock_state_to_string (manager->priv->old_state));
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
apply_bell (GsdKeyboardManager *manager)
{
	GSettings       *settings;
        XKeyboardControl kbdcontrol;
        gboolean         click;
        int              bell_volume;
        int              bell_pitch;
        int              bell_duration;
        GsdBellMode      bell_mode;
        int              click_volume;

        g_debug ("Applying the bell settings");
        settings      = manager->priv->settings;
        click         = g_settings_get_boolean  (settings, KEY_CLICK);
        click_volume  = g_settings_get_int   (settings, KEY_CLICK_VOLUME);

        bell_pitch    = g_settings_get_int   (settings, KEY_BELL_PITCH);
        bell_duration = g_settings_get_int   (settings, KEY_BELL_DURATION);

        bell_mode = g_settings_get_enum (settings, KEY_BELL_MODE);
        bell_volume   = (bell_mode == GSD_BELL_MODE_ON) ? 50 : 0;

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

        gdk_error_trap_push ();
        XChangeKeyboardControl (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                                KBKeyClickPercent | KBBellPercent | KBBellPitch | KBBellDuration,
                                &kbdcontrol);

        XSync (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), FALSE);
        gdk_error_trap_pop_ignored ();
}

static void
apply_numlock (GsdKeyboardManager *manager)
{
	GSettings *settings;
        gboolean rnumlock;

        g_debug ("Applying the num-lock settings");
        settings = manager->priv->settings;
        rnumlock = g_settings_get_boolean  (settings, KEY_REMEMBER_NUMLOCK_STATE);
        manager->priv->old_state = g_settings_get_enum (manager->priv->settings, KEY_NUMLOCK_STATE);

        gdk_error_trap_push ();
        if (rnumlock) {
                g_debug ("Remember num-lock is set, so applying setting '%s'",
                         num_lock_state_to_string (manager->priv->old_state));
                numlock_set_xkb_state (manager->priv->old_state);
        }

        XSync (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), FALSE);
        gdk_error_trap_pop_ignored ();
}

static void
apply_all_settings (GsdKeyboardManager *manager)
{
	apply_bell (manager);
	apply_numlock (manager);
}

static void
settings_changed (GSettings          *settings,
                  const char         *key,
                  GsdKeyboardManager *manager)
{
	if (g_strcmp0 (key, KEY_CLICK) == 0||
	    g_strcmp0 (key, KEY_CLICK_VOLUME) == 0 ||
	    g_strcmp0 (key, KEY_BELL_PITCH) == 0 ||
	    g_strcmp0 (key, KEY_BELL_DURATION) == 0 ||
	    g_strcmp0 (key, KEY_BELL_MODE) == 0) {
		g_debug ("Bell setting '%s' changed, applying bell settings", key);
		apply_bell (manager);
	} else if (g_strcmp0 (key, KEY_REMEMBER_NUMLOCK_STATE) == 0) {
		g_debug ("Remember Num-Lock state '%s' changed, applying num-lock settings", key);
		apply_numlock (manager);
	} else if (g_strcmp0 (key, KEY_NUMLOCK_STATE) == 0) {
		g_debug ("Num-Lock state '%s' changed, will apply at next startup", key);
	} else if (g_strcmp0 (key, KEY_BELL_CUSTOM_FILE) == 0){
		g_debug ("Ignoring '%s' setting change", KEY_BELL_CUSTOM_FILE);
	} else {
		g_warning ("Unhandled settings change, key '%s'", key);
	}

}

static void
device_added_cb (GdkDeviceManager   *device_manager,
                 GdkDevice          *device,
                 GsdKeyboardManager *manager)
{
        GdkInputSource source;

        source = gdk_device_get_source (device);
        if (source == GDK_SOURCE_KEYBOARD) {
                g_debug ("New keyboard plugged in, applying all settings");
                apply_numlock (manager);
        }
}

static void
set_devicepresence_handler (GsdKeyboardManager *manager)
{
        GdkDeviceManager *device_manager;

        if (gnome_settings_is_wayland ())
                return;

        device_manager = gdk_display_get_device_manager (gdk_display_get_default ());

        manager->priv->device_added_id = g_signal_connect (G_OBJECT (device_manager), "device-added",
                                                           G_CALLBACK (device_added_cb), manager);
        manager->priv->device_manager = device_manager;
}

static gboolean
need_ibus (GVariant *sources)
{
        GVariantIter iter;
        const gchar *type;

        g_variant_iter_init (&iter, sources);
        while (g_variant_iter_next (&iter, "(&s&s)", &type, NULL))
                if (g_str_equal (type, INPUT_SOURCE_TYPE_IBUS))
                        return TRUE;

        return FALSE;
}

static void
set_gtk_im_module (GSettings *settings,
                   GVariant  *sources)
{
        const gchar *new_module;
        gchar *current_module;

        if (need_ibus (sources))
                new_module = GTK_IM_MODULE_IBUS;
        else
                new_module = GTK_IM_MODULE_SIMPLE;

        current_module = g_settings_get_string (settings, KEY_GTK_IM_MODULE);
        if (!g_str_equal (current_module, new_module))
                g_settings_set_string (settings, KEY_GTK_IM_MODULE, new_module);
        g_free (current_module);
}

static void
input_sources_changed (GSettings          *settings,
                       const char         *key,
                       GsdKeyboardManager *manager)
{
        GSettings *interface_settings;
        GVariant *sources;
        /* Gtk+ uses the IM module advertised in XSETTINGS so, if we
         * have IBus input sources, we want it to load that
         * module. Otherwise we can use the default "simple" module
         * which is builtin gtk+
         */
        sources = g_settings_get_value (settings, KEY_INPUT_SOURCES);
        interface_settings = g_settings_new (GNOME_DESKTOP_INTERFACE_DIR);
        set_gtk_im_module (interface_settings, sources);
        g_object_unref (interface_settings);
        g_variant_unref (sources);
}

static void
get_sources_from_xkb_config (GsdKeyboardManager *manager)
{
        GsdKeyboardManagerPrivate *priv = manager->priv;
        GVariantBuilder builder;
        GVariant *v;
        gint i, n;
        gchar **layouts = NULL;
        gchar **variants = NULL;

        v = g_dbus_proxy_get_cached_property (priv->localed, "X11Layout");
        if (v) {
                const gchar *s = g_variant_get_string (v, NULL);
                if (*s)
                        layouts = g_strsplit (s, ",", -1);
                g_variant_unref (v);
        }

        init_builder_with_sources (&builder, priv->input_sources_settings);

        if (!layouts) {
                g_variant_builder_add (&builder, "(ss)", INPUT_SOURCE_TYPE_XKB, DEFAULT_LAYOUT);
                goto out;
	}

        v = g_dbus_proxy_get_cached_property (priv->localed, "X11Variant");
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
        g_settings_set_value (priv->input_sources_settings, KEY_INPUT_SOURCES, g_variant_builder_end (&builder));

        g_strfreev (layouts);
        g_strfreev (variants);
}

static void
get_options_from_xkb_config (GsdKeyboardManager *manager)
{
        GsdKeyboardManagerPrivate *priv = manager->priv;
        GVariant *v;
        gchar **options = NULL;

        v = g_dbus_proxy_get_cached_property (priv->localed, "X11Options");
        if (v) {
                const gchar *s = g_variant_get_string (v, NULL);
                if (*s)
                        options = g_strsplit (s, ",", -1);
                g_variant_unref (v);
        }

        if (!options)
                return;

        g_settings_set_strv (priv->input_sources_settings, KEY_KEYBOARD_OPTIONS, (const gchar * const*) options);

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
        gchar **options;

        settings = manager->priv->input_sources_settings;

        if (g_getenv ("RUNNING_UNDER_GDM"))
                return;

        maybe_convert_old_settings (settings);

        /* if we still don't have anything do some educated guesses */
        sources = g_settings_get_value (settings, KEY_INPUT_SOURCES);
        if (g_variant_n_children (sources) < 1)
                get_sources_from_xkb_config (manager);
        g_variant_unref (sources);

        options = g_settings_get_strv (settings, KEY_KEYBOARD_OPTIONS);
        if (g_strv_length (options) < 1)
                get_options_from_xkb_config (manager);
        g_strfreev (options);
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

        manager->priv->localed = proxy;
        maybe_create_initial_settings (manager);
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
        g_signal_connect (manager->priv->input_sources_settings, "changed::"KEY_INPUT_SOURCES,
                          G_CALLBACK (input_sources_changed), manager);

        manager->priv->cancellable = g_cancellable_new ();

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  "org.freedesktop.locale1",
                                  "/org/freedesktop/locale1",
                                  "org.freedesktop.locale1",
                                  manager->priv->cancellable,
                                  localed_proxy_ready,
                                  manager);

        if (!gnome_settings_is_wayland ()) {
                /* apply current settings before we install the callback */
                g_debug ("Started the keyboard plugin, applying all settings");
                apply_all_settings (manager);

                g_signal_connect (G_OBJECT (manager->priv->settings), "changed",
                                  G_CALLBACK (settings_changed), manager);
        }

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
        g_source_set_name_by_id (manager->priv->start_idle_id, "[gnome-settings-daemon] start_keyboard_idle_cb");

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_keyboard_manager_stop (GsdKeyboardManager *manager)
{
        GsdKeyboardManagerPrivate *p = manager->priv;

        g_debug ("Stopping keyboard manager");

        g_cancellable_cancel (p->cancellable);
        g_clear_object (&p->cancellable);

        g_clear_object (&p->settings);
        g_clear_object (&p->input_sources_settings);
        g_clear_object (&p->localed);

        if (p->device_manager != NULL) {
                g_signal_handler_disconnect (p->device_manager, p->device_added_id);
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

        gsd_keyboard_manager_stop (keyboard_manager);

        if (keyboard_manager->priv->start_idle_id != 0)
                g_source_remove (keyboard_manager->priv->start_idle_id);

        G_OBJECT_CLASS (gsd_keyboard_manager_parent_class)->finalize (object);
}

static void
migrate_keyboard_settings (void)
{
        GsdSettingsMigrateEntry entries[] = {
                { "repeat",          "repeat",          NULL },
                { "repeat-interval", "repeat-interval", NULL },
                { "delay",           "delay",           NULL }
        };

        gsd_settings_migrate_check ("org.gnome.settings-daemon.peripherals.keyboard.deprecated",
                                    "/org/gnome/settings-daemon/peripherals/keyboard/",
                                    "org.gnome.desktop.peripherals.keyboard",
                                    "/org/gnome/desktop/peripherals/keyboard/",
                                    entries, G_N_ELEMENTS (entries));
}

GsdKeyboardManager *
gsd_keyboard_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                migrate_keyboard_settings ();
                manager_object = g_object_new (GSD_TYPE_KEYBOARD_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_KEYBOARD_MANAGER (manager_object);
}
