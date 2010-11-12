/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Rodrigo Moya
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
#include <time.h>

#include <X11/Xatom.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "gnome-settings-profile.h"
#include "gsd-enums.h"
#include "gsd-xsettings-manager.h"
#include "gsd-xsettings-gtk.h"
#include "xsettings-manager.h"
#include "fontconfig-monitor.h"

#define GNOME_XSETTINGS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNOME_TYPE_XSETTINGS_MANAGER, GnomeXSettingsManagerPrivate))

#define MOUSE_SETTINGS_SCHEMA     "org.gnome.settings-daemon.peripherals.mouse"
#define INTERFACE_SETTINGS_SCHEMA "org.gnome.desktop.interface"
#define SOUND_SETTINGS_SCHEMA     "org.gnome.desktop.sound"

#define XSETTINGS_PLUGIN_SCHEMA "org.gnome.settings-daemon.plugins.xsettings"

#define GTK_MODULES_DISABLED_KEY "disabled-gtk-modules"
#define GTK_MODULES_ENABLED_KEY  "enabled-gtk-modules"

#define FONT_ANTIALIASING_KEY "antialiasing"
#define FONT_HINTING_KEY      "hinting"
#define FONT_RGBA_ORDER_KEY   "rgba-order"
#define FONT_DPI_KEY          "dpi"

/* X servers sometimes lie about the screen's physical dimensions, so we cannot
 * compute an accurate DPI value.  When this happens, the user gets fonts that
 * are too huge or too tiny.  So, we see what the server returns:  if it reports
 * something outside of the range [DPI_LOW_REASONABLE_VALUE,
 * DPI_HIGH_REASONABLE_VALUE], then we assume that it is lying and we use
 * DPI_FALLBACK instead.
 *
 * See get_dpi_from_gsettings_or_x_server() below, and also
 * https://bugzilla.novell.com/show_bug.cgi?id=217790
 */
#define DPI_FALLBACK 96
#define DPI_LOW_REASONABLE_VALUE 50
#define DPI_HIGH_REASONABLE_VALUE 500

typedef struct _TranslationEntry TranslationEntry;
typedef void (* TranslationFunc) (GnomeXSettingsManager *manager,
                                  TranslationEntry      *trans,
                                  GVariant              *value);

struct _TranslationEntry {
        const char     *gsettings_schema;
        const char     *gsettings_key;
        const char     *xsetting_name;

        TranslationFunc translate;
};

struct GnomeXSettingsManagerPrivate
{
        XSettingsManager **managers;
        GHashTable        *settings;

        GSettings         *plugin_settings;
        fontconfig_monitor_handle_t *fontconfig_handle;

        GsdXSettingsGtk   *gtk;
};

#define GSD_XSETTINGS_ERROR gsd_xsettings_error_quark ()

enum {
        GSD_XSETTINGS_ERROR_INIT
};

static void     gnome_xsettings_manager_class_init  (GnomeXSettingsManagerClass *klass);
static void     gnome_xsettings_manager_init        (GnomeXSettingsManager      *xsettings_manager);
static void     gnome_xsettings_manager_finalize    (GObject                  *object);

G_DEFINE_TYPE (GnomeXSettingsManager, gnome_xsettings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static GQuark
gsd_xsettings_error_quark (void)
{
        return g_quark_from_static_string ("gsd-xsettings-error-quark");
}

static void
translate_bool_int (GnomeXSettingsManager *manager,
                    TranslationEntry      *trans,
                    GVariant              *value)
{
        int i;

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_int (manager->priv->managers [i], trans->xsetting_name,
                                           g_variant_get_boolean (value));
        }
}

static void
translate_int_int (GnomeXSettingsManager *manager,
                   TranslationEntry      *trans,
                   GVariant              *value)
{
        int i;

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_int (manager->priv->managers [i], trans->xsetting_name,
                                           g_variant_get_int32 (value));
        }
}

static void
translate_string_string (GnomeXSettingsManager *manager,
                         TranslationEntry      *trans,
                         GVariant              *value)
{
        int i;

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_string (manager->priv->managers [i],
                                              trans->xsetting_name,
                                              g_variant_get_string (value, NULL));
        }
}

static void
translate_string_string_toolbar (GnomeXSettingsManager *manager,
                                 TranslationEntry      *trans,
                                 GVariant              *value)
{
        int         i;
        const char *tmp;

        /* This is kind of a workaround since GNOME expects the key value to be
         * "both_horiz" and gtk+ wants the XSetting to be "both-horiz".
         */
        tmp = g_variant_get_string (value, NULL);
        if (tmp && strcmp (tmp, "both_horiz") == 0) {
                tmp = "both-horiz";
        }

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_string (manager->priv->managers [i],
                                              trans->xsetting_name,
                                              tmp);
        }
}

static TranslationEntry translations [] = {
        { "org.gnome.settings-daemon.peripherals.mouse", "double-click",   "Net/DoubleClickTime",  translate_int_int },
        { "org.gnome.settings-daemon.peripherals.mouse", "drag-threshold", "Net/DndDragThreshold", translate_int_int },

        { "org.gnome.desktop.interface", "gtk-color-palette",      "Gtk/ColorPalette",        translate_string_string },
        { "org.gnome.desktop.interface", "font-name",              "Gtk/FontName",            translate_string_string },
        { "org.gnome.desktop.interface", "gtk-key-theme",          "Gtk/KeyThemeName",        translate_string_string },
        { "org.gnome.desktop.interface", "toolbar-style",          "Gtk/ToolbarStyle",        translate_string_string_toolbar },
        { "org.gnome.desktop.interface", "toolbar-icons-size",     "Gtk/ToolbarIconSize",     translate_string_string },
        { "org.gnome.desktop.interface", "can-change-accels",      "Gtk/CanChangeAccels",     translate_bool_int },
        { "org.gnome.desktop.interface", "cursor-blink",           "Net/CursorBlink",         translate_bool_int },
        { "org.gnome.desktop.interface", "cursor-blink-time",      "Net/CursorBlinkTime",     translate_int_int },
        { "org.gnome.desktop.interface", "cursor-blink-timeout",   "Gtk/CursorBlinkTimeout",  translate_int_int },
        { "org.gnome.desktop.interface", "gtk-theme",              "Net/ThemeName",           translate_string_string },
        { "org.gnome.desktop.interface", "gtk-timeout-initial",    "Gtk/TimeoutInitial",      translate_int_int },
        { "org.gnome.desktop.interface", "gtk-timeout-repeat",     "Gtk/TimeoutRepeat",       translate_int_int },
        { "org.gnome.desktop.interface", "gtk-color-scheme",       "Gtk/ColorScheme",         translate_string_string },
        { "org.gnome.desktop.interface", "gtk-im-preedit-style",   "Gtk/IMPreeditStyle",      translate_string_string },
        { "org.gnome.desktop.interface", "gtk-im-status-style",    "Gtk/IMStatusStyle",       translate_string_string },
        { "org.gnome.desktop.interface", "gtk-im-module",          "Gtk/IMModule",            translate_string_string },
        { "org.gnome.desktop.interface", "icon-theme",             "Net/IconThemeName",       translate_string_string },
        { "org.gnome.desktop.interface", "menus-have-icons",       "Gtk/MenuImages",          translate_bool_int },
        { "org.gnome.desktop.interface", "buttons-have-icons",     "Gtk/ButtonImages",        translate_bool_int },
        { "org.gnome.desktop.interface", "menubar-accel",          "Gtk/MenuBarAccel",        translate_string_string },
        { "org.gnome.desktop.interface", "enable-animations",      "Gtk/EnableAnimations",    translate_bool_int },
        { "org.gnome.desktop.interface", "cursor-theme",           "Gtk/CursorThemeName",     translate_string_string },
        { "org.gnome.desktop.interface", "cursor-size",            "Gtk/CursorThemeSize",     translate_int_int },
        { "org.gnome.desktop.interface", "show-input-method-menu", "Gtk/ShowInputMethodMenu", translate_bool_int },
        { "org.gnome.desktop.interface", "show-unicode-menu",      "Gtk/ShowUnicodeMenu",     translate_bool_int },

        { "org.gnome.desktop.sound", "theme-name",                 "Net/SoundThemeName",            translate_string_string },
        { "org.gnome.desktop.sound", "event-sounds",               "Net/EnableEventSounds" ,        translate_bool_int },
        { "org.gnome.desktop.sound", "input-feedback-sounds",      "Net/EnableInputFeedbackSounds", translate_bool_int }
};

static double
dpi_from_pixels_and_mm (int pixels,
                        int mm)
{
        double dpi;

        if (mm >= 1)
                dpi = pixels / (mm / 25.4);
        else
                dpi = 0;

        return dpi;
}

static double
get_dpi_from_x_server (void)
{
        GdkScreen *screen;
        double     dpi;

        screen = gdk_screen_get_default ();
        if (screen != NULL) {
                double width_dpi, height_dpi;

                width_dpi = dpi_from_pixels_and_mm (gdk_screen_get_width (screen), gdk_screen_get_width_mm (screen));
                height_dpi = dpi_from_pixels_and_mm (gdk_screen_get_height (screen), gdk_screen_get_height_mm (screen));

                if (width_dpi < DPI_LOW_REASONABLE_VALUE || width_dpi > DPI_HIGH_REASONABLE_VALUE
                    || height_dpi < DPI_LOW_REASONABLE_VALUE || height_dpi > DPI_HIGH_REASONABLE_VALUE) {
                        dpi = DPI_FALLBACK;
                } else {
                        dpi = (width_dpi + height_dpi) / 2.0;
                }
        } else {
                /* Huh!?  No screen? */

                dpi = DPI_FALLBACK;
        }

        return dpi;
}

static double
get_dpi_from_gsettings_or_x_server (GnomeXSettingsManager *manager)
{
        double      dpi;

        dpi = g_settings_get_double (manager->priv->plugin_settings, FONT_DPI_KEY);
        if (dpi == 0.0)
                dpi = get_dpi_from_x_server ();

        return dpi;
}

typedef struct {
        gboolean    antialias;
        gboolean    hinting;
        int         dpi;
        const char *rgba;
        const char *hintstyle;
} GnomeXftSettings;

/* Read GSettings and determine the appropriate Xft settings based on them. */
static void
xft_settings_get (GnomeXSettingsManager *manager,
                  GnomeXftSettings      *settings)
{
        GsdFontAntialiasingMode antialiasing;
        GsdFontHinting hinting;
        GsdFontRgbaOrder rgba_order;
        double dpi;
        gboolean use_rgba = FALSE;

        antialiasing = g_settings_get_enum (manager->priv->plugin_settings, FONT_ANTIALIASING_KEY);
        hinting = g_settings_get_enum (manager->priv->plugin_settings, FONT_HINTING_KEY);
        rgba_order = g_settings_get_enum (manager->priv->plugin_settings, FONT_RGBA_ORDER_KEY);
        dpi = get_dpi_from_gsettings_or_x_server (manager);

        settings->antialias = (antialiasing != GSD_FONT_ANTIALIASING_MODE_NONE);
        settings->hinting = (hinting != GSD_FONT_HINTING_NONE);
        settings->dpi = dpi * 1024; /* Xft wants 1/1024ths of an inch */
        settings->rgba = "rgb";
        settings->hintstyle = "hintfull";

        switch (hinting) {
        case GSD_FONT_HINTING_NONE:
                settings->hintstyle = "hintnone";
                break;
        case GSD_FONT_HINTING_SLIGHT:
                settings->hintstyle = "hintslight";
                break;
        case GSD_FONT_HINTING_MEDIUM:
                settings->hintstyle = "hintmedium";
                break;
        case GSD_FONT_HINTING_FULL:
                settings->hintstyle = "hintfull";
                break;
        }

        switch (antialiasing) {
        case GSD_FONT_ANTIALIASING_MODE_NONE:
                settings->antialias = 0;
                break;
        case GSD_FONT_ANTIALIASING_MODE_GRAYSCALE:
                settings->antialias = 1;
                break;
        case GSD_FONT_ANTIALIASING_MODE_RGBA:
                settings->antialias = 1;
                use_rgba = TRUE;
        }

        if (!use_rgba) {
                settings->rgba = "none";
        }
}

static void
xft_settings_set_xsettings (GnomeXSettingsManager *manager,
                            GnomeXftSettings      *settings)
{
        int i;

        gnome_settings_profile_start (NULL);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_int (manager->priv->managers [i], "Xft/Antialias", settings->antialias);
                xsettings_manager_set_int (manager->priv->managers [i], "Xft/Hinting", settings->hinting);
                xsettings_manager_set_string (manager->priv->managers [i], "Xft/HintStyle", settings->hintstyle);
                xsettings_manager_set_int (manager->priv->managers [i], "Xft/DPI", settings->dpi);
                xsettings_manager_set_string (manager->priv->managers [i], "Xft/RGBA", settings->rgba);
                xsettings_manager_set_string (manager->priv->managers [i], "Xft/lcdfilter",
                                              g_str_equal (settings->rgba, "rgb") ? "lcddefault" : "none");
        }
        gnome_settings_profile_end (NULL);
}

static void
update_property (GString *props, const gchar* key, const gchar* value)
{
        gchar* needle;
        size_t needle_len;
        gchar* found = NULL;

        /* update an existing property */
        needle = g_strconcat (key, ":", NULL);
        needle_len = strlen (needle);
        if (g_str_has_prefix (props->str, needle))
                found = props->str;
        else 
            found = strstr (props->str, needle);

        if (found) {
                size_t value_index;
                gchar* end;

                end = strchr (found, '\n');
                value_index = (found - props->str) + needle_len + 1;
                g_string_erase (props, value_index, end ? (end - found - needle_len) : -1);
                g_string_insert (props, value_index, "\n");
                g_string_insert (props, value_index, value);
        } else {
                g_string_append_printf (props, "%s:\t%s\n", key, value);
        }
}

static void
xft_settings_set_xresources (GnomeXftSettings *settings)
{
        GString    *add_string;
        char        dpibuf[G_ASCII_DTOSTR_BUF_SIZE];
        Display    *dpy;

        gnome_settings_profile_start (NULL);

        /* get existing properties */
        dpy = XOpenDisplay (NULL);
        g_return_if_fail (dpy != NULL);
        add_string = g_string_new (XResourceManagerString (dpy));

        g_debug("xft_settings_set_xresources: orig res '%s'", add_string->str);

        update_property (add_string, "Xft.dpi",
                                g_ascii_dtostr (dpibuf, sizeof (dpibuf), (double) settings->dpi / 1024.0));
        update_property (add_string, "Xft.antialias",
                                settings->antialias ? "1" : "0");
        update_property (add_string, "Xft.hinting",
                                settings->hinting ? "1" : "0");
        update_property (add_string, "Xft.hintstyle",
                                settings->hintstyle);
        update_property (add_string, "Xft.rgba",
                                settings->rgba);
        update_property (add_string, "Xft.lcdfilter",
                         g_str_equal (settings->rgba, "rgb") ? "lcddefault" : "none");

        g_debug("xft_settings_set_xresources: new res '%s'", add_string->str);

        /* Set the new X property */
        XChangeProperty(dpy, RootWindow (dpy, 0),
                        XA_RESOURCE_MANAGER, XA_STRING, 8, PropModeReplace, (const unsigned char *) add_string->str, add_string->len);
        XCloseDisplay (dpy);

        g_string_free (add_string, TRUE);

        gnome_settings_profile_end (NULL);
}

/* We mirror the Xft properties both through XSETTINGS and through
 * X resources
 */
static void
update_xft_settings (GnomeXSettingsManager *manager)
{
        GnomeXftSettings settings;

        gnome_settings_profile_start (NULL);

        xft_settings_get (manager, &settings);
        xft_settings_set_xsettings (manager, &settings);
        xft_settings_set_xresources (&settings);

        gnome_settings_profile_end (NULL);
}

static void
xft_callback (GSettings             *settings,
              const gchar           *key,
              GnomeXSettingsManager *manager)
{
        int i;

        update_xft_settings (manager);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_notify (manager->priv->managers [i]);
        }
}

static void
plugin_callback (GSettings             *settings,
                 const char            *key,
                 GnomeXSettingsManager *manager)
{
        if (g_str_equal (key, GTK_MODULES_DISABLED_KEY) ||
            g_str_equal (key, GTK_MODULES_ENABLED_KEY)) {
                /* Do nothing, as GsdXsettingsGtk will handle it */
        } else {
                xft_callback (settings, key, manager);
        }
}

static void
gtk_modules_callback (GsdXSettingsGtk       *gtk,
                      GParamSpec            *spec,
                      GnomeXSettingsManager *manager)
{
        const char *modules = gsd_xsettings_gtk_get_modules (manager->priv->gtk);
        int i;

        if (modules == NULL) {
                for (i = 0; manager->priv->managers [i]; ++i) {
                        xsettings_manager_delete_setting (manager->priv->managers [i], "Gtk/Modules");
                }
        } else {
                g_debug ("Setting GTK modules '%s'", modules);
                for (i = 0; manager->priv->managers [i]; ++i) {
                        xsettings_manager_set_string (manager->priv->managers [i],
                                                      "Gtk/Modules",
                                                      modules);
                }
        }

        for (i = 0; manager->priv->managers [i]; ++i) {
                xsettings_manager_notify (manager->priv->managers [i]);
        }
}

static void
fontconfig_callback (fontconfig_monitor_handle_t *handle,
                     GnomeXSettingsManager       *manager)
{
        int i;
        int timestamp = time (NULL);

        gnome_settings_profile_start (NULL);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_int (manager->priv->managers [i], "Fontconfig/Timestamp", timestamp);
                xsettings_manager_notify (manager->priv->managers [i]);
        }
        gnome_settings_profile_end (NULL);
}

static gboolean
start_fontconfig_monitor_idle_cb (GnomeXSettingsManager *manager)
{
        gnome_settings_profile_start (NULL);

        manager->priv->fontconfig_handle = fontconfig_monitor_start ((GFunc) fontconfig_callback, manager);

        gnome_settings_profile_end (NULL);

        return FALSE;
}

static void
start_fontconfig_monitor (GnomeXSettingsManager  *manager)
{
        gnome_settings_profile_start (NULL);

        fontconfig_cache_init ();

        g_idle_add ((GSourceFunc) start_fontconfig_monitor_idle_cb, manager);

        gnome_settings_profile_end (NULL);
}

static void
stop_fontconfig_monitor (GnomeXSettingsManager  *manager)
{
        if (manager->priv->fontconfig_handle) {
                fontconfig_monitor_stop (manager->priv->fontconfig_handle);
                manager->priv->fontconfig_handle = NULL;
        }
}

static void
process_value (GnomeXSettingsManager *manager,
               TranslationEntry      *trans,
               GVariant              *value)
{
        (* trans->translate) (manager, trans, value);
}

static TranslationEntry *
find_translation_entry (GSettings *settings, const char *key)
{
        guint i;
        char *schema;

        g_object_get (settings, "schema", &schema, NULL);

        for (i = 0; i < G_N_ELEMENTS (translations); i++) {
                if (g_str_equal (schema, translations[i].gsettings_schema) &&
                    g_str_equal (key, translations[i].gsettings_key)) {
                            g_free (schema);
                        return &translations[i];
                }
        }

        g_free (schema);

        return NULL;
}

static void
xsettings_callback (GSettings             *settings,
                    const char            *key,
                    GnomeXSettingsManager *manager)
{
        TranslationEntry *trans;
        guint             i;
        GVariant         *value;

        trans = find_translation_entry (settings, key);
        if (trans == NULL) {
                return;
        }

        value = g_settings_get_value (settings, key);

        process_value (manager, trans, value);

        g_object_unref (value);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_string (manager->priv->managers [i],
                                              "Net/FallbackIconTheme",
                                              "gnome");
        }

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_notify (manager->priv->managers [i]);
        }
}

static void
terminate_cb (void *data)
{
        gboolean *terminated = data;

        if (*terminated) {
                return;
        }

        *terminated = TRUE;

        gtk_main_quit ();
}

static gboolean
setup_xsettings_managers (GnomeXSettingsManager *manager)
{
        GdkDisplay *display;
        int         i;
        int         n_screens;
        gboolean    res;
        gboolean    terminated;

        display = gdk_display_get_default ();
        n_screens = gdk_display_get_n_screens (display);

        res = xsettings_manager_check_running (gdk_x11_display_get_xdisplay (display),
                                               gdk_screen_get_number (gdk_screen_get_default ()));
        if (res) {
                g_warning ("You can only run one xsettings manager at a time; exiting");
                return FALSE;
        }

        manager->priv->managers = g_new0 (XSettingsManager *, n_screens + 1);

        terminated = FALSE;
        for (i = 0; i < n_screens; i++) {
                GdkScreen *screen;

                screen = gdk_display_get_screen (display, i);

                manager->priv->managers [i] = xsettings_manager_new (gdk_x11_display_get_xdisplay (display),
                                                                     gdk_screen_get_number (screen),
                                                                     terminate_cb,
                                                                     &terminated);
                if (! manager->priv->managers [i]) {
                        g_warning ("Could not create xsettings manager for screen %d!", i);
                        return FALSE;
                }
        }

        return TRUE;
}

gboolean
gnome_xsettings_manager_start (GnomeXSettingsManager *manager,
                               GError               **error)
{
        guint        i;
        GList       *list, *l;

        g_debug ("Starting xsettings manager");
        gnome_settings_profile_start (NULL);

        if (!setup_xsettings_managers (manager)) {
                g_set_error (error, GSD_XSETTINGS_ERROR,
                             GSD_XSETTINGS_ERROR_INIT,
                             "Could not initialize xsettings manager.");
                return FALSE;
        }

        manager->priv->settings = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                         NULL, (GDestroyNotify) g_object_unref);

        g_hash_table_insert (manager->priv->settings,
                             MOUSE_SETTINGS_SCHEMA, g_settings_new (MOUSE_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             INTERFACE_SETTINGS_SCHEMA, g_settings_new (INTERFACE_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             SOUND_SETTINGS_SCHEMA, g_settings_new (SOUND_SETTINGS_SCHEMA));

        for (i = 0; i < G_N_ELEMENTS (translations); i++) {
                GVariant *val;
                GSettings *settings;

                settings = g_hash_table_lookup (manager->priv->settings,
                                                translations[i].gsettings_schema);
                if (settings == NULL) {
                        g_warning ("Schemas '%s' has not been setup", translations[i].gsettings_schema);
                        continue;
                }

                val = g_settings_get_value (settings, translations[i].gsettings_key);

                process_value (manager, &translations[i], val);
        }

        list = g_hash_table_get_values (manager->priv->settings);
        for (l = list; l != NULL; l = l->next) {
                g_signal_connect (G_OBJECT (l->data), "changed",
                                  G_CALLBACK (xsettings_callback), manager);
        }
        g_list_free (list);

        /* Plugin settings (GTK modules and Xft) */
        manager->priv->plugin_settings = g_settings_new (XSETTINGS_PLUGIN_SCHEMA);
        g_signal_connect (manager->priv->plugin_settings, "changed",
                          G_CALLBACK (plugin_callback), manager);

        manager->priv->gtk = gsd_xsettings_gtk_new ();
        g_signal_connect (G_OBJECT (manager->priv->gtk), "notify::gtk-modules",
                          G_CALLBACK (gtk_modules_callback), manager);

        /* Xft settings */
        update_xft_settings (manager);

        start_fontconfig_monitor (manager);

        for (i = 0; manager->priv->managers [i]; i++)
                xsettings_manager_set_string (manager->priv->managers [i],
                                              "Net/FallbackIconTheme",
                                              "gnome");

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_notify (manager->priv->managers [i]);
        }


        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gnome_xsettings_manager_stop (GnomeXSettingsManager *manager)
{
        GnomeXSettingsManagerPrivate *p = manager->priv;
        int i;

        g_debug ("Stopping xsettings manager");

        if (p->managers != NULL) {
                for (i = 0; p->managers [i]; ++i)
                        xsettings_manager_destroy (p->managers [i]);

                g_free (p->managers);
                p->managers = NULL;
        }

        g_object_unref (manager->priv->plugin_settings);
        stop_fontconfig_monitor (manager);

        g_hash_table_destroy (p->settings);
        p->settings = NULL;

        g_object_unref (p->gtk);
        p->gtk = NULL;
}

static void
gnome_xsettings_manager_set_property (GObject        *object,
                                      guint           prop_id,
                                      const GValue   *value,
                                      GParamSpec     *pspec)
{
        GnomeXSettingsManager *self;

        self = GNOME_XSETTINGS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gnome_xsettings_manager_get_property (GObject        *object,
                                      guint           prop_id,
                                      GValue         *value,
                                      GParamSpec     *pspec)
{
        GnomeXSettingsManager *self;

        self = GNOME_XSETTINGS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gnome_xsettings_manager_constructor (GType                  type,
                                     guint                  n_construct_properties,
                                     GObjectConstructParam *construct_properties)
{
        GnomeXSettingsManager      *xsettings_manager;
        GnomeXSettingsManagerClass *klass;

        klass = GNOME_XSETTINGS_MANAGER_CLASS (g_type_class_peek (GNOME_TYPE_XSETTINGS_MANAGER));

        xsettings_manager = GNOME_XSETTINGS_MANAGER (G_OBJECT_CLASS (gnome_xsettings_manager_parent_class)->constructor (type,
                                                                                                                  n_construct_properties,
                                                                                                                  construct_properties));

        return G_OBJECT (xsettings_manager);
}

static void
gnome_xsettings_manager_dispose (GObject *object)
{
        GnomeXSettingsManager *xsettings_manager;

        xsettings_manager = GNOME_XSETTINGS_MANAGER (object);

        G_OBJECT_CLASS (gnome_xsettings_manager_parent_class)->dispose (object);
}

static void
gnome_xsettings_manager_class_init (GnomeXSettingsManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gnome_xsettings_manager_get_property;
        object_class->set_property = gnome_xsettings_manager_set_property;
        object_class->constructor = gnome_xsettings_manager_constructor;
        object_class->dispose = gnome_xsettings_manager_dispose;
        object_class->finalize = gnome_xsettings_manager_finalize;

        g_type_class_add_private (klass, sizeof (GnomeXSettingsManagerPrivate));
}

static void
gnome_xsettings_manager_init (GnomeXSettingsManager *manager)
{
        manager->priv = GNOME_XSETTINGS_MANAGER_GET_PRIVATE (manager);
}

static void
gnome_xsettings_manager_finalize (GObject *object)
{
        GnomeXSettingsManager *xsettings_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GNOME_IS_XSETTINGS_MANAGER (object));

        xsettings_manager = GNOME_XSETTINGS_MANAGER (object);

        g_return_if_fail (xsettings_manager->priv != NULL);

        G_OBJECT_CLASS (gnome_xsettings_manager_parent_class)->finalize (object);
}

GnomeXSettingsManager *
gnome_xsettings_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GNOME_TYPE_XSETTINGS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GNOME_XSETTINGS_MANAGER (manager_object);
}
