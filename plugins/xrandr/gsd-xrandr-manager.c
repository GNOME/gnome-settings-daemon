/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2007, 2008 Red Hat, Inc
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
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <libupower-glib/upower.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include <libgnome-desktop/gnome-rr-config.h>
#include <libgnome-desktop/gnome-rr.h>
#include <libgnome-desktop/gnome-pnp-ids.h>

#include "gsd-enums.h"
#include "gsd-input-helper.h"
#include "gnome-settings-plugin.h"
#include "gnome-settings-profile.h"
#include "gnome-settings-bus.h"
#include "gsd-device-mapper.h"
#include "gsd-xrandr-manager.h"

#define GSD_XRANDR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_XRANDR_MANAGER, GsdXrandrManagerPrivate))

#define CONF_SCHEMA "org.gnome.settings-daemon.plugins.xrandr"
#define CONF_KEY_DEFAULT_MONITORS_SETUP   "default-monitors-setup"

/* Number of seconds that the confirmation dialog will last before it resets the
 * RANDR configuration to its old state.
 */
#define CONFIRMATION_DIALOG_SECONDS 30

/* name of the icon files (gsd-xrandr.svg, etc.) */
#define GSD_XRANDR_ICON_NAME "gsd-xrandr"

#define GSD_XRANDR_DBUS_NAME GSD_DBUS_NAME ".XRANDR"
#define GSD_XRANDR_DBUS_PATH GSD_DBUS_PATH "/XRANDR"

static const gchar introspection_xml[] =
"<node name='/org/gnome/SettingsDaemon/XRANDR'>"
"  <interface name='org.gnome.SettingsDaemon.XRANDR_2'>"
"    <annotation name='org.freedesktop.DBus.GLib.CSymbol' value='gsd_xrandr_manager_2'/>"
"    <method name='VideoModeSwitch'>"
"       <!-- Timestamp for the RANDR call itself -->"
"       <arg name='timestamp' type='x' direction='in'/>"
"    </method>"
"    <method name='Rotate'>"
"       <!-- Timestamp for the RANDR call itself -->"
"       <arg name='timestamp' type='x' direction='in'/>"
"    </method>"
"    <method name='RotateTo'>"
"       <arg name='rotation' type='i' direction='in'/>"
"       <!-- Timestamp for the RANDR call itself -->"
"       <arg name='timestamp' type='x' direction='in'/>"
"    </method>"
"  </interface>"
"</node>";

struct GsdXrandrManagerPrivate {
        GnomeRRScreen *rw_screen;
        gboolean running;

        UpClient *upower_client;

        GSettings       *settings;
        GDBusNodeInfo   *introspection_data;
        guint            name_id;
        GDBusConnection *connection;
        GCancellable    *bus_cancellable;

        GsdDeviceMapper  *device_mapper;
        GsdDeviceManager *device_manager;
        guint             device_added_id;
        guint             device_removed_id;

        /* fn-F7 status */
        int             current_fn_f7_config;             /* -1 if no configs */
        GnomeRRConfig **fn_f7_configs;  /* NULL terminated, NULL if there are no configs */
};

static const GnomeRRRotation possible_rotations[] = {
        GNOME_RR_ROTATION_0,
        GNOME_RR_ROTATION_90,
        GNOME_RR_ROTATION_180,
        GNOME_RR_ROTATION_270
        /* We don't allow REFLECT_X or REFLECT_Y for now, as gnome-display-properties doesn't allow them, either */
};

static void     gsd_xrandr_manager_class_init  (GsdXrandrManagerClass *klass);
static void     gsd_xrandr_manager_init        (GsdXrandrManager      *xrandr_manager);
static void     gsd_xrandr_manager_finalize    (GObject             *object);

static void get_allowed_rotations_for_output (GnomeRRConfig *config,
                                              GnomeRRScreen *rr_screen,
                                              GnomeRROutputInfo *output,
                                              int *out_num_rotations,
                                              GnomeRRRotation *out_rotations);
static void handle_fn_f7 (GsdXrandrManager *mgr, gint64 timestamp);
static void handle_rotate_windows (GsdXrandrManager *mgr, GnomeRRRotation rotation, gint64 timestamp);
static void register_manager_dbus (GsdXrandrManager *manager);

G_DEFINE_TYPE (GsdXrandrManager, gsd_xrandr_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static FILE *log_file;

static void
log_open (void)
{
        char *toggle_filename;
        char *log_filename;
        struct stat st;

        if (log_file)
                return;

        toggle_filename = g_build_filename (g_get_home_dir (), "gsd-debug-randr", NULL);
        log_filename = g_build_filename (g_get_home_dir (), "gsd-debug-randr.log", NULL);

        if (stat (toggle_filename, &st) != 0)
                goto out;

        log_file = fopen (log_filename, "a");

        if (log_file && ftell (log_file) == 0)
                fprintf (log_file, "To keep this log from being created, please rm ~/gsd-debug-randr\n");

out:
        g_free (toggle_filename);
        g_free (log_filename);
}

static void
log_close (void)
{
        if (log_file) {
                fclose (log_file);
                log_file = NULL;
        }
}

static void
log_msg (const char *format, ...)
{
        if (log_file) {
                va_list args;

                va_start (args, format);
                vfprintf (log_file, format, args);
                va_end (args);
        }
}

static void
log_output (GnomeRROutputInfo *output)
{
        gchar *name = gnome_rr_output_info_get_name (output);
        gchar *display_name = gnome_rr_output_info_get_display_name (output);

        log_msg ("        %s: ", name ? name : "unknown");

        if (gnome_rr_output_info_is_connected (output)) {
                if (gnome_rr_output_info_is_active (output)) {
                        int x, y, width, height;
                        gnome_rr_output_info_get_geometry (output, &x, &y, &width, &height);
                        log_msg ("%dx%d@%d +%d+%d",
                                 width,
                                 height,
                                 gnome_rr_output_info_get_refresh_rate (output),
                                 x,
                                 y);
                } else
                        log_msg ("off");
        } else
                log_msg ("disconnected");

        if (display_name)
                log_msg (" (%s)", display_name);

        if (gnome_rr_output_info_get_primary (output))
                log_msg (" (primary output)");

        log_msg ("\n");
}

static void
log_configuration (GnomeRRConfig *config)
{
        int i;
        GnomeRROutputInfo **outputs = gnome_rr_config_get_outputs (config);

        log_msg ("        cloned: %s\n", gnome_rr_config_get_clone (config) ? "yes" : "no");

        for (i = 0; outputs[i] != NULL; i++)
                log_output (outputs[i]);

        if (i == 0)
                log_msg ("        no outputs!\n");
}

static void
log_screen (GnomeRRScreen *screen)
{
        GnomeRRConfig *config;
        int min_w, min_h, max_w, max_h;

        if (!log_file)
                return;

        config = gnome_rr_config_new_current (screen, NULL);

        gnome_rr_screen_get_ranges (screen, &min_w, &max_w, &min_h, &max_h);

        log_msg ("        Screen min(%d, %d), max(%d, %d)\n",
                 min_w, min_h,
                 max_w, max_h);

        log_configuration (config);
        g_object_unref (config);
}

static void
log_configurations (GnomeRRConfig **configs)
{
        int i;

        if (!configs) {
                log_msg ("    No configurations\n");
                return;
        }

        for (i = 0; configs[i]; i++) {
                log_msg ("    Configuration %d\n", i);
                log_configuration (configs[i]);
        }
}

static void
print_output (GnomeRROutputInfo *info)
{
        int x, y, width, height;

        g_debug ("  Output: %s attached to %s", gnome_rr_output_info_get_display_name (info), gnome_rr_output_info_get_name (info));
        g_debug ("     status: %s", gnome_rr_output_info_is_active (info) ? "on" : "off");

        gnome_rr_output_info_get_geometry (info, &x, &y, &width, &height);
        g_debug ("     width: %d", width);
        g_debug ("     height: %d", height);
        g_debug ("     rate: %d", gnome_rr_output_info_get_refresh_rate (info));
        g_debug ("     primary: %s", gnome_rr_output_info_get_primary (info) ? "true" : "false");
        g_debug ("     position: %d %d", x, y);
}

static void
print_configuration (GnomeRRConfig *config, const char *header)
{
        int i;
        GnomeRROutputInfo **outputs;

        g_debug ("=== %s Configuration ===", header);
        if (!config) {
                g_debug ("  none");
                return;
        }

        g_debug ("  Clone: %s", gnome_rr_config_get_clone (config) ? "true" : "false");

        outputs = gnome_rr_config_get_outputs (config);
        for (i = 0; outputs[i] != NULL; ++i)
                print_output (outputs[i]);
}

static gboolean
is_laptop (GnomeRRScreen *screen, GnomeRROutputInfo *output)
{
        GnomeRROutput *rr_output;

        rr_output = gnome_rr_screen_get_output_by_name (screen, gnome_rr_output_info_get_name (output));

        return gnome_rr_output_is_builtin_display (rr_output);
}

static GnomeRROutputInfo *
get_laptop_output_info (GnomeRRScreen *screen, GnomeRRConfig *config)
{
        int i;
        GnomeRROutputInfo **outputs = gnome_rr_config_get_outputs (config);

        for (i = 0; outputs[i] != NULL; i++) {
                if (is_laptop (screen, outputs[i]))
                        return outputs[i];
        }

        return NULL;
}

/* This function centralizes the use of gnome_rr_config_apply_with_time().
 *
 * Applies a configuration and displays an error message if an error happens.
 * We just return whether setting the configuration succeeded.
 */
static gboolean
apply_configuration (GsdXrandrManager *manager, GnomeRRConfig *config, gint64 timestamp)
{
        GsdXrandrManagerPrivate *priv = manager->priv;
        GError *error;
        gboolean success;

        gnome_rr_config_ensure_primary (config);

        print_configuration (config, "Applying Configuration");

        error = NULL;
        success = gnome_rr_config_apply (config, priv->rw_screen, &error);

        if (!success) {
                log_msg ("Could not switch to the following configuration (timestamp %" G_GINT64_FORMAT "): %s\n", timestamp, error->message);
                log_configuration (config);
                g_error_free (error);
        }

        return success;
}

/* DBus method for org.gnome.SettingsDaemon.XRANDR_2 VideoModeSwitch; see gsd-xrandr-manager.xml for the interface definition */
static gboolean
gsd_xrandr_manager_2_video_mode_switch (GsdXrandrManager *manager,
                                        gint64            timestamp,
                                        GError          **error)
{
        handle_fn_f7 (manager, timestamp);
        return TRUE;
}

/* DBus method for org.gnome.SettingsDaemon.XRANDR_2 Rotate; see gsd-xrandr-manager.xml for the interface definition */
static gboolean
gsd_xrandr_manager_2_rotate (GsdXrandrManager *manager,
                             gint64            timestamp,
                             GError          **error)
{
        handle_rotate_windows (manager, GNOME_RR_ROTATION_NEXT, timestamp);
        return TRUE;
}

/* DBus method for org.gnome.SettingsDaemon.XRANDR_2 RotateTo; see gsd-xrandr-manager.xml for the interface definition */
static gboolean
gsd_xrandr_manager_2_rotate_to (GsdXrandrManager *manager,
                                GnomeRRRotation   rotation,
                                gint64            timestamp,
                                GError          **error)
{
        guint i;
        gboolean found;

        found = FALSE;
        for (i = 0; i < G_N_ELEMENTS (possible_rotations); i++) {
                if (rotation == possible_rotations[i]) {
                        found = TRUE;
                        break;
                }
        }

        if (found == FALSE) {
                g_debug ("Not setting out of bounds rotation '%d'", rotation);
                return FALSE;
        }

        handle_rotate_windows (manager, rotation, timestamp);
        return TRUE;
}

static gboolean
get_clone_size (GnomeRRScreen *screen, int *width, int *height)
{
        GnomeRRMode **modes = gnome_rr_screen_list_clone_modes (screen);
        int best_w, best_h;
        int i;

        best_w = 0;
        best_h = 0;

        for (i = 0; modes[i] != NULL; ++i) {
                GnomeRRMode *mode = modes[i];
                int w, h;

                w = gnome_rr_mode_get_width (mode);
                h = gnome_rr_mode_get_height (mode);

                if (w * h > best_w * best_h) {
                        best_w = w;
                        best_h = h;
                }
        }

        if (best_w > 0 && best_h > 0) {
                if (width)
                        *width = best_w;
                if (height)
                        *height = best_h;

                return TRUE;
        }

        return FALSE;
}

static gboolean
config_is_all_off (GnomeRRConfig *config)
{
        int j;
        GnomeRROutputInfo **outputs;

        outputs = gnome_rr_config_get_outputs (config);

        for (j = 0; outputs[j] != NULL; ++j) {
                if (gnome_rr_output_info_is_active (outputs[j])) {
                        return FALSE;
                }
        }

        return TRUE;
}

static gboolean
laptop_lid_is_closed (GsdXrandrManager *manager)
{
        return up_client_get_lid_is_closed (manager->priv->upower_client);
}

static gboolean
is_laptop_with_closed_lid (GsdXrandrManager *manager, GnomeRRScreen *screen, GnomeRROutputInfo *info)
{
        return is_laptop (screen, info) && laptop_lid_is_closed (manager);
}

static GnomeRRConfig *
make_clone_setup (GsdXrandrManager *manager, GnomeRRScreen *screen)
{
        GnomeRRConfig *result;
        GnomeRROutputInfo **outputs;
        int width, height;
        int i;

        if (!get_clone_size (screen, &width, &height))
                return NULL;

        result = gnome_rr_config_new_current (screen, NULL);
        gnome_rr_config_set_clone (result, TRUE);

        outputs = gnome_rr_config_get_outputs (result);

        for (i = 0; outputs[i] != NULL; ++i) {
                GnomeRROutputInfo *info = outputs[i];

                gnome_rr_output_info_set_active (info, FALSE);
                if (!is_laptop_with_closed_lid (manager, screen, info) && gnome_rr_output_info_is_connected (info)) {
                        GnomeRROutput *output =
                                gnome_rr_screen_get_output_by_name (screen, gnome_rr_output_info_get_name (info));
                        GnomeRRMode **modes = gnome_rr_output_list_modes (output);
                        int j;
                        int best_rate = 0;

                        for (j = 0; modes[j] != NULL; ++j) {
                                GnomeRRMode *mode = modes[j];
                                int w, h;

                                w = gnome_rr_mode_get_width (mode);
                                h = gnome_rr_mode_get_height (mode);

                                if (w == width && h == height) {
                                        int r = gnome_rr_mode_get_freq (mode);
                                        if (r > best_rate)
                                                best_rate = r;
                                }
                        }

                        if (best_rate > 0) {
                                gnome_rr_output_info_set_active (info, TRUE);
                                gnome_rr_output_info_set_rotation (info, GNOME_RR_ROTATION_0);
                                gnome_rr_output_info_set_refresh_rate (info, best_rate);
                                gnome_rr_output_info_set_geometry (info, 0, 0, width, height);
                        }
                }
        }

        if (config_is_all_off (result)) {
                g_object_unref (G_OBJECT (result));
                result = NULL;
        }

        print_configuration (result, "clone setup");

        return result;
}

static GnomeRRMode *
find_best_mode (GnomeRROutput *output)
{
        GnomeRRMode *preferred;
        GnomeRRMode **modes;
        int best_size;
        int best_width, best_height, best_rate;
        int i;
        GnomeRRMode *best_mode;

        preferred = gnome_rr_output_get_preferred_mode (output);
        if (preferred)
                return preferred;

        modes = gnome_rr_output_list_modes (output);
        if (!modes)
                return NULL;

        best_size = best_width = best_height = best_rate = 0;
        best_mode = NULL;

        for (i = 0; modes[i] != NULL; i++) {
                int w, h, r;
                int size;

                w = gnome_rr_mode_get_width (modes[i]);
                h = gnome_rr_mode_get_height (modes[i]);
                r = gnome_rr_mode_get_freq  (modes[i]);

                size = w * h;

                if (size > best_size) {
                        best_size   = size;
                        best_width  = w;
                        best_height = h;
                        best_rate   = r;
                        best_mode   = modes[i];
                } else if (size == best_size) {
                        if (r > best_rate) {
                                best_rate = r;
                                best_mode = modes[i];
                        }
                }
        }

        return best_mode;
}

static gboolean
turn_on (GnomeRRScreen *screen,
         GnomeRROutputInfo *info,
         int x, int y)
{
        GnomeRROutput *output = gnome_rr_screen_get_output_by_name (screen, gnome_rr_output_info_get_name (info));
        GnomeRRMode *mode = find_best_mode (output);

        if (mode) {
                gnome_rr_output_info_set_active (info, TRUE);
                gnome_rr_output_info_set_geometry (info, x, y, gnome_rr_mode_get_width (mode), gnome_rr_mode_get_height (mode));
                gnome_rr_output_info_set_rotation (info, GNOME_RR_ROTATION_0);
                gnome_rr_output_info_set_refresh_rate (info, gnome_rr_mode_get_freq (mode));

                return TRUE;
        }

        return FALSE;
}

static GnomeRRConfig *
make_laptop_setup (GsdXrandrManager *manager, GnomeRRScreen *screen)
{
        /* Turn on the laptop, disable everything else */
        GnomeRRConfig *result = gnome_rr_config_new_current (screen, NULL);
        GnomeRROutputInfo **outputs = gnome_rr_config_get_outputs (result);
        int i;

        gnome_rr_config_set_clone (result, FALSE);

        for (i = 0; outputs[i] != NULL; ++i) {
                GnomeRROutputInfo *info = outputs[i];

                if (is_laptop (screen, info) && !laptop_lid_is_closed (manager)) {
                        if (!turn_on (screen, info, 0, 0)) {
                                g_object_unref (G_OBJECT (result));
                                result = NULL;
                                break;
                        }
                }
                else {
                        gnome_rr_output_info_set_active (info, FALSE);
                }
        }


        if (config_is_all_off (result)) {
                g_object_unref (G_OBJECT (result));
                result = NULL;
        }

        print_configuration (result, "Laptop setup");

        /* FIXME - Maybe we should return NULL if there is more than
         * one connected "laptop" screen?
         */
        return result;

}

static int
turn_on_and_get_rightmost_offset (GnomeRRScreen *screen, GnomeRROutputInfo *info, int x)
{
        if (turn_on (screen, info, x, 0)) {
                int width;
                gnome_rr_output_info_get_geometry (info, NULL, NULL, &width, NULL);
                x += width;
        }

        return x;
}

/* Used from g_ptr_array_sort(); compares outputs based on their X position */
static int
compare_output_positions (gconstpointer a, gconstpointer b)
{
        GnomeRROutputInfo **oa = (GnomeRROutputInfo **) a;
        GnomeRROutputInfo **ob = (GnomeRROutputInfo **) b;
        int xa, xb;

        gnome_rr_output_info_get_geometry (*oa, &xa, NULL, NULL, NULL);
        gnome_rr_output_info_get_geometry (*ob, &xb, NULL, NULL, NULL);

        return xb - xa;
}

/* A set of outputs with already-set sizes and positions may not fit in the
 * frame buffer that is available.  Turn off outputs right-to-left until we find
 * a size that fits.  Returns whether something applicable was found
 * (i.e. something that fits and that does not consist of only-off outputs).
 */
static gboolean
trim_rightmost_outputs_that_dont_fit_in_framebuffer (GnomeRRScreen *rr_screen, GnomeRRConfig *config)
{
        GnomeRROutputInfo **outputs;
        int i;
        gboolean applicable;
        GPtrArray *sorted_outputs;

        outputs = gnome_rr_config_get_outputs (config);
        g_return_val_if_fail (outputs != NULL, FALSE);

        /* How many are on? */

        sorted_outputs = g_ptr_array_new ();
        for (i = 0; outputs[i] != NULL; i++) {
                if (gnome_rr_output_info_is_active (outputs[i]))
                        g_ptr_array_add (sorted_outputs, outputs[i]);
        }

        /* Lay them out from left to right */

        g_ptr_array_sort (sorted_outputs, compare_output_positions);

        /* Trim! */

        applicable = FALSE;

        for (i = sorted_outputs->len - 1; i >= 0; i--) {
                GError *error = NULL;
                gboolean is_bounds_error;

                applicable = gnome_rr_config_applicable (config, rr_screen, &error);
                if (applicable)
                        break;

                is_bounds_error = g_error_matches (error, GNOME_RR_ERROR, GNOME_RR_ERROR_BOUNDS_ERROR);
                g_error_free (error);

                if (!is_bounds_error)
                        break;

                gnome_rr_output_info_set_active (sorted_outputs->pdata[i], FALSE);
        }

        if (config_is_all_off (config))
                applicable = FALSE;

        g_ptr_array_free (sorted_outputs, FALSE);

        return applicable;
}

static gboolean
follow_laptop_lid(GsdXrandrManager *manager)
{
        GsdXrandrBootBehaviour val;
        val = g_settings_get_enum (manager->priv->settings, CONF_KEY_DEFAULT_MONITORS_SETUP);
        return val == GSD_XRANDR_BOOT_BEHAVIOUR_FOLLOW_LID || val == GSD_XRANDR_BOOT_BEHAVIOUR_CLONE;
}

static GnomeRRConfig *
make_xinerama_setup (GsdXrandrManager *manager, GnomeRRScreen *screen)
{
        /* Turn on everything that has a preferred mode, and
         * position it from left to right
         */
        GnomeRRConfig *result = gnome_rr_config_new_current (screen, NULL);
        GnomeRROutputInfo **outputs = gnome_rr_config_get_outputs (result);
        int i;
        int x;

        gnome_rr_config_set_clone (result, FALSE);

        x = 0;
        for (i = 0; outputs[i] != NULL; ++i) {
                GnomeRROutputInfo *info = outputs[i];

                if (is_laptop (screen, info)) {
                        if (laptop_lid_is_closed (manager) && follow_laptop_lid (manager))
                                gnome_rr_output_info_set_active (info, FALSE);
                        else {
                                gnome_rr_output_info_set_primary (info, TRUE);
                                x = turn_on_and_get_rightmost_offset (screen, info, x);
                        }
                }
        }

        for (i = 0; outputs[i] != NULL; ++i) {
                GnomeRROutputInfo *info = outputs[i];

                if (gnome_rr_output_info_is_connected (info) && !is_laptop (screen, info)) {
                        gnome_rr_output_info_set_primary (info, FALSE);
                        x = turn_on_and_get_rightmost_offset (screen, info, x);
                }
        }

        if (!trim_rightmost_outputs_that_dont_fit_in_framebuffer (screen, result)) {
                g_object_unref (G_OBJECT (result));
                result = NULL;
        }

        print_configuration (result, "xinerama setup");

        return result;
}

static GnomeRRConfig *
make_other_setup (GnomeRRScreen *screen)
{
        /* Turn off all laptops, and make all external monitors clone
         * from (0, 0)
         */

        GnomeRRConfig *result = gnome_rr_config_new_current (screen, NULL);
        GnomeRROutputInfo **outputs = gnome_rr_config_get_outputs (result);
        int i;

        gnome_rr_config_set_clone (result, FALSE);

        for (i = 0; outputs[i] != NULL; ++i) {
                GnomeRROutputInfo *info = outputs[i];

                if (is_laptop (screen, info)) {
                        gnome_rr_output_info_set_active (info, FALSE);
                }
                else {
                        if (gnome_rr_output_info_is_connected (info))
                                turn_on (screen, info, 0, 0);
               }
        }

        if (!trim_rightmost_outputs_that_dont_fit_in_framebuffer (screen, result)) {
                g_object_unref (G_OBJECT (result));
                result = NULL;
        }

        print_configuration (result, "other setup");

        return result;
}

static GPtrArray *
sanitize (GsdXrandrManager *manager, GPtrArray *array)
{
        int i;
        GPtrArray *new;

        g_debug ("before sanitizing");

        for (i = 0; i < array->len; ++i) {
                if (array->pdata[i]) {
                        print_configuration (array->pdata[i], "before");
                }
        }


        /* Remove configurations that are duplicates of
         * configurations earlier in the cycle
         */
        for (i = 0; i < array->len; i++) {
                int j;

                for (j = i + 1; j < array->len; j++) {
                        GnomeRRConfig *this = array->pdata[j];
                        GnomeRRConfig *other = array->pdata[i];

                        if (this && other && gnome_rr_config_equal (this, other)) {
                                g_debug ("removing duplicate configuration");
                                g_object_unref (this);
                                array->pdata[j] = NULL;
                                break;
                        }
                }
        }

        for (i = 0; i < array->len; ++i) {
                GnomeRRConfig *config = array->pdata[i];

                if (config && config_is_all_off (config)) {
                        g_debug ("removing configuration as all outputs are off");
                        g_object_unref (array->pdata[i]);
                        array->pdata[i] = NULL;
                }
        }

        /* Do a final sanitization pass.  This will remove configurations that
         * don't fit in the framebuffer's Virtual size.
         */

        for (i = 0; i < array->len; i++) {
                GnomeRRConfig *config = array->pdata[i];

                if (config) {
                        GError *error;

                        error = NULL;
                        if (!gnome_rr_config_applicable (config, manager->priv->rw_screen, &error)) { /* NULL-GError */
                                g_debug ("removing configuration which is not applicable because %s", error->message);
                                g_error_free (error);

                                g_object_unref (config);
                                array->pdata[i] = NULL;
                        }
                }
        }

        /* Remove NULL configurations */
        new = g_ptr_array_new ();

        for (i = 0; i < array->len; ++i) {
                if (array->pdata[i]) {
                        g_ptr_array_add (new, array->pdata[i]);
                        print_configuration (array->pdata[i], "Final");
                }
        }

        if (new->len > 0) {
                g_ptr_array_add (new, NULL);
        } else {
                g_ptr_array_free (new, TRUE);
                new = NULL;
        }

        g_ptr_array_free (array, TRUE);

        return new;
}

static void
free_fn_f7_configs (GsdXrandrManager *mgr)
{
        if (mgr->priv->fn_f7_configs) {
                int i;

                for (i = 0; mgr->priv->fn_f7_configs[i] != NULL; ++i)
                        g_object_unref (mgr->priv->fn_f7_configs[i]);
                g_free (mgr->priv->fn_f7_configs);

                mgr->priv->fn_f7_configs = NULL;
                mgr->priv->current_fn_f7_config = -1;
        }
}

static void
generate_fn_f7_configs (GsdXrandrManager *mgr)
{
        GPtrArray *array = g_ptr_array_new ();
        GnomeRRScreen *screen = mgr->priv->rw_screen;

        g_debug ("Generating configurations");

        /* Free any existing list of configurations */
        free_fn_f7_configs (mgr);

        g_ptr_array_add (array, gnome_rr_config_new_current (screen, NULL));
        g_ptr_array_add (array, make_clone_setup (mgr, screen));
        g_ptr_array_add (array, make_xinerama_setup (mgr, screen));
        g_ptr_array_add (array, make_other_setup (screen));
        g_ptr_array_add (array, make_laptop_setup (mgr, screen));

        array = sanitize (mgr, array);

        if (array) {
                mgr->priv->fn_f7_configs = (GnomeRRConfig **)g_ptr_array_free (array, FALSE);
                mgr->priv->current_fn_f7_config = 0;
        }
}

static void
handle_fn_f7 (GsdXrandrManager *mgr, gint64 timestamp)
{
        GsdXrandrManagerPrivate *priv = mgr->priv;
        GnomeRRScreen *screen = priv->rw_screen;
        GnomeRRConfig *current;
        GError *error;

        /* Theory of fn-F7 operation
         *
         * We maintain a datastructure "fn_f7_status", that contains
         * a list of GnomeRRConfig's. Each of the GnomeRRConfigs has a
         * mode (or "off") for each connected output.
         *
         * When the user hits fn-F7, we cycle to the next GnomeRRConfig
         * in the data structure. If the data structure does not exist, it
         * is generated. If the configs in the data structure do not match
         * the current hardware reality, it is regenerated.
         *
         */
        g_debug ("Handling fn-f7");

        log_open ();
        log_msg ("Handling XF86Display hotkey - timestamp %" G_GINT64_FORMAT "\n", timestamp);

        error = NULL;
        if (!gnome_rr_screen_refresh (screen, &error) && error) {
                char *str;

                str = g_strdup_printf (_("Could not refresh the screen information: %s"), error->message);
                g_error_free (error);

                log_msg ("%s\n", str);
                g_free (str);
        }

        if (!priv->fn_f7_configs) {
                log_msg ("Generating stock configurations:\n");
                generate_fn_f7_configs (mgr);
                log_configurations (priv->fn_f7_configs);
        }

        current = gnome_rr_config_new_current (screen, NULL);

        if (priv->fn_f7_configs &&
            (!gnome_rr_config_match (current, priv->fn_f7_configs[0]) ||
             !gnome_rr_config_equal (current, priv->fn_f7_configs[mgr->priv->current_fn_f7_config]))) {
                    /* Our view of the world is incorrect, so regenerate the
                     * configurations
                     */
                    generate_fn_f7_configs (mgr);
                    log_msg ("Regenerated stock configurations:\n");
                    log_configurations (priv->fn_f7_configs);
            }

        g_object_unref (current);

        if (priv->fn_f7_configs) {
                gboolean success;

                mgr->priv->current_fn_f7_config++;

                if (priv->fn_f7_configs[mgr->priv->current_fn_f7_config] == NULL)
                        mgr->priv->current_fn_f7_config = 0;

                g_debug ("cycling to next configuration (%d)", mgr->priv->current_fn_f7_config);

                print_configuration (priv->fn_f7_configs[mgr->priv->current_fn_f7_config], "new config");

                g_debug ("applying");

                success = apply_configuration (mgr, priv->fn_f7_configs[mgr->priv->current_fn_f7_config], timestamp);

                if (success) {
                        log_msg ("Successfully switched to configuration (timestamp %" G_GINT64_FORMAT "):\n", timestamp);
                        log_configuration (priv->fn_f7_configs[mgr->priv->current_fn_f7_config]);
                }
        }
        else {
                g_debug ("no configurations generated");
        }

        log_close ();

        g_debug ("done handling fn-f7");
}

static GnomeRRRotation
get_next_rotation (GnomeRRRotation allowed_rotations, GnomeRRRotation current_rotation)
{
        int i;
        int current_index;

        /* First, find the index of the current rotation */

        current_index = -1;

        for (i = 0; i < G_N_ELEMENTS (possible_rotations); i++) {
                GnomeRRRotation r;

                r = possible_rotations[i];
                if (r == current_rotation) {
                        current_index = i;
                        break;
                }
        }

        if (current_index == -1) {
                /* Huh, the current_rotation was not one of the supported rotations.  Bail out. */
                return current_rotation;
        }

        /* Then, find the next rotation that is allowed */

        i = (current_index + 1) % G_N_ELEMENTS (possible_rotations);

        while (1) {
                GnomeRRRotation r;

                r = possible_rotations[i];
                if (r == current_rotation) {
                        /* We wrapped around and no other rotation is suported.  Bummer. */
                        return current_rotation;
                } else if (r & allowed_rotations)
                        return r;

                i = (i + 1) % G_N_ELEMENTS (possible_rotations);
        }
}

/* We use this when the XF86RotateWindows key is pressed, or the
 * orientation of a tablet changes. The key is present
 * on some tablet PCs; they use it so that the user can rotate the tablet
 * easily. Some other tablet PCs will have an accelerometer instead.
 */
static void
handle_rotate_windows (GsdXrandrManager *mgr,
                       GnomeRRRotation rotation,
                       gint64 timestamp)
{
        GsdXrandrManagerPrivate *priv = mgr->priv;
        GnomeRRScreen *screen = priv->rw_screen;
        GnomeRRConfig *current;
        GnomeRROutputInfo *rotatable_output_info;
        int num_allowed_rotations;
        GnomeRRRotation allowed_rotations;
        GnomeRRRotation next_rotation;

        g_debug ("Handling XF86RotateWindows with rotation %d", rotation);

        /* Which output? */

        current = gnome_rr_config_new_current (screen, NULL);

        rotatable_output_info = get_laptop_output_info (screen, current);
        if (rotatable_output_info == NULL) {
                g_debug ("No laptop outputs found to rotate; XF86RotateWindows key will do nothing");
                goto out;
        }

        if (rotation <= GNOME_RR_ROTATION_NEXT) {
                /* Which rotation? */

                get_allowed_rotations_for_output (current, priv->rw_screen, rotatable_output_info, &num_allowed_rotations, &allowed_rotations);
                next_rotation = get_next_rotation (allowed_rotations, gnome_rr_output_info_get_rotation (rotatable_output_info));

                if (next_rotation == gnome_rr_output_info_get_rotation (rotatable_output_info)) {
                        g_debug ("No rotations are supported other than the current one; XF86RotateWindows key will do nothing");
                        goto out;
                }
        } else {
                next_rotation = rotation;
        }

        /* Rotate */

        gnome_rr_output_info_set_rotation (rotatable_output_info, next_rotation);

        apply_configuration (mgr, current, timestamp);

out:
        g_object_unref (current);
}

static void
get_allowed_rotations_for_output (GnomeRRConfig *config,
                                  GnomeRRScreen *rr_screen,
                                  GnomeRROutputInfo *output,
                                  int *out_num_rotations,
                                  GnomeRRRotation *out_rotations)
{
        GnomeRRRotation current_rotation;
        int i;

        *out_num_rotations = 0;
        *out_rotations = 0;

        current_rotation = gnome_rr_output_info_get_rotation (output);

        /* Yay for brute force */

        for (i = 0; i < G_N_ELEMENTS (possible_rotations); i++) {
                GnomeRRRotation rotation_to_test;

                rotation_to_test = possible_rotations[i];

                gnome_rr_output_info_set_rotation (output, rotation_to_test);

                if (gnome_rr_config_applicable (config, rr_screen, NULL)) { /* NULL-GError */
                        (*out_num_rotations)++;
                        (*out_rotations) |= rotation_to_test;
                }
        }

        gnome_rr_output_info_set_rotation (output, current_rotation);

        if (*out_num_rotations == 0 || *out_rotations == 0) {
                g_warning ("Huh, output %p says it doesn't support any rotations, and yet it has a current rotation?", output);
                *out_num_rotations = 1;
                *out_rotations = gnome_rr_output_info_get_rotation (output);
        }
}

static void
manager_device_added (GsdXrandrManager *manager,
                      GsdDevice        *device)
{
        GsdDeviceType type;

        type = gsd_device_get_device_type (device);

        if ((type & GSD_DEVICE_TYPE_TOUCHSCREEN) == 0)
                return;

        gsd_device_mapper_add_input (manager->priv->device_mapper, device);
}

static void
manager_device_removed (GsdXrandrManager *manager,
                        GsdDevice        *device)
{
        GsdDeviceType type;

        type = gsd_device_get_device_type (device);

        if ((type & GSD_DEVICE_TYPE_TOUCHSCREEN) == 0)
                return;

        gsd_device_mapper_remove_input (manager->priv->device_mapper, device);
}

static void
manager_init_devices (GsdXrandrManager *manager)
{
        GList *devices, *d;

        manager->priv->device_mapper = gsd_device_mapper_get ();
        manager->priv->device_manager = gsd_device_manager_get ();
        manager->priv->device_added_id =
                g_signal_connect_swapped (manager->priv->device_manager, "device-added",
                                          G_CALLBACK (manager_device_added), manager);
        manager->priv->device_removed_id =
                g_signal_connect_swapped (manager->priv->device_manager, "device-removed",
                                  G_CALLBACK (manager_device_removed), manager);

        devices = gsd_device_manager_list_devices (manager->priv->device_manager,
                                                   GSD_DEVICE_TYPE_TOUCHSCREEN);

        for (d = devices; d; d = d->next)
                manager_device_added (manager, d->data);

        g_list_free (devices);
}

static void
on_rr_screen_acquired (GObject      *object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
        GsdXrandrManager *manager = user_data;
        GError *error = NULL;

        manager->priv->rw_screen = gnome_rr_screen_new_finish (result, &error);

        if (manager->priv->rw_screen == NULL) {
                log_msg ("Could not initialize the RANDR plugin: %s\n",
                         error->message);
                g_error_free (error);
                log_close ();
                return;
        }

        manager->priv->upower_client = up_client_new ();

        log_msg ("State of screen at startup:\n");
        log_screen (manager->priv->rw_screen);

        manager->priv->running = TRUE;
        manager->priv->settings = g_settings_new (CONF_SCHEMA);

        manager_init_devices (manager);
        register_manager_dbus (manager);

        log_close ();
}

gboolean
gsd_xrandr_manager_start (GsdXrandrManager *manager,
                          GError          **error)
{
        g_debug ("Starting xrandr manager");
        gnome_settings_profile_start (NULL);

        log_open ();
        log_msg ("------------------------------------------------------------\nSTARTING XRANDR PLUGIN\n");

        gnome_rr_screen_new_async (gdk_screen_get_default (),
                                   on_rr_screen_acquired, manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_xrandr_manager_stop (GsdXrandrManager *manager)
{
        g_debug ("Stopping xrandr manager");

        manager->priv->running = FALSE;

        if (manager->priv->bus_cancellable != NULL) {
                g_cancellable_cancel (manager->priv->bus_cancellable);
                g_object_unref (manager->priv->bus_cancellable);
                manager->priv->bus_cancellable = NULL;
        }

        if (manager->priv->settings != NULL) {
                g_object_unref (manager->priv->settings);
                manager->priv->settings = NULL;
        }

        if (manager->priv->rw_screen != NULL) {
                g_object_unref (manager->priv->rw_screen);
                manager->priv->rw_screen = NULL;
        }

        if (manager->priv->upower_client != NULL) {
                g_signal_handlers_disconnect_by_data (manager->priv->upower_client, manager);
                g_object_unref (manager->priv->upower_client);
                manager->priv->upower_client = NULL;
        }

        if (manager->priv->name_id != 0)
                g_bus_unown_name (manager->priv->name_id);

        if (manager->priv->introspection_data) {
                g_dbus_node_info_unref (manager->priv->introspection_data);
                manager->priv->introspection_data = NULL;
        }

        if (manager->priv->connection != NULL) {
                g_object_unref (manager->priv->connection);
                manager->priv->connection = NULL;
        }

        if (manager->priv->device_manager != NULL) {
                g_signal_handler_disconnect (manager->priv->device_manager,
                                             manager->priv->device_added_id);
                g_signal_handler_disconnect (manager->priv->device_manager,
                                             manager->priv->device_removed_id);
                manager->priv->device_manager = NULL;
        }

        free_fn_f7_configs (manager);

        log_open ();
        log_msg ("STOPPING XRANDR PLUGIN\n------------------------------------------------------------\n");
        log_close ();
}

static void
gsd_xrandr_manager_class_init (GsdXrandrManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_xrandr_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdXrandrManagerPrivate));
}

static void
gsd_xrandr_manager_init (GsdXrandrManager *manager)
{
        manager->priv = GSD_XRANDR_MANAGER_GET_PRIVATE (manager);

        manager->priv->current_fn_f7_config = -1;
        manager->priv->fn_f7_configs = NULL;
}

static void
gsd_xrandr_manager_finalize (GObject *object)
{
        GsdXrandrManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_XRANDR_MANAGER (object));

        manager = GSD_XRANDR_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        gsd_xrandr_manager_stop (manager);

        G_OBJECT_CLASS (gsd_xrandr_manager_parent_class)->finalize (object);
}

static void
handle_method_call_xrandr_2 (GsdXrandrManager *manager,
                             const gchar *method_name,
                             GVariant *parameters,
                             GDBusMethodInvocation *invocation)
{
        gint64 timestamp;

        g_debug ("Calling method '%s' for org.gnome.SettingsDaemon.XRANDR_2", method_name);

        if (g_strcmp0 (method_name, "VideoModeSwitch") == 0) {
                g_variant_get (parameters, "(x)", &timestamp);
                gsd_xrandr_manager_2_video_mode_switch (manager, timestamp, NULL);
                g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "Rotate") == 0) {
                g_variant_get (parameters, "(x)", &timestamp);
                gsd_xrandr_manager_2_rotate (manager, timestamp, NULL);
                g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "RotateTo") == 0) {
                GnomeRRRotation rotation;
                g_variant_get (parameters, "(ix)", &rotation, &timestamp);
                gsd_xrandr_manager_2_rotate_to (manager, rotation, timestamp, NULL);
                g_dbus_method_invocation_return_value (invocation, NULL);
        }
}

static void
handle_method_call (GDBusConnection       *connection,
                    const gchar           *sender,
                    const gchar           *object_path,
                    const gchar           *interface_name,
                    const gchar           *method_name,
                    GVariant              *parameters,
                    GDBusMethodInvocation *invocation,
                    gpointer               user_data)
{
        GsdXrandrManager *manager = (GsdXrandrManager *) user_data;

        g_debug ("Handling method call %s.%s", interface_name, method_name);

        if (g_strcmp0 (interface_name, "org.gnome.SettingsDaemon.XRANDR_2") == 0)
                handle_method_call_xrandr_2 (manager, method_name, parameters, invocation);
        else
                g_warning ("unknown interface: %s", interface_name);
}


static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL, /* Get Property */
        NULL, /* Set Property */
};

static void
on_bus_gotten (GObject             *source_object,
               GAsyncResult        *res,
               GsdXrandrManager    *manager)
{
        GDBusConnection *connection;
        GError *error = NULL;
        GDBusInterfaceInfo **infos;
        int i;

        connection = g_bus_get_finish (res, &error);
        if (connection == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("Could not get session bus: %s", error->message);
                g_error_free (error);
                return;
        }
        manager->priv->connection = connection;

        infos = manager->priv->introspection_data->interfaces;
        for (i = 0; infos[i] != NULL; i++) {
                g_dbus_connection_register_object (connection,
                                                   GSD_XRANDR_DBUS_PATH,
                                                   infos[i],
                                                   &interface_vtable,
                                                   manager,
                                                   NULL,
                                                   NULL);
        }

        manager->priv->name_id = g_bus_own_name_on_connection (connection,
                                                               GSD_XRANDR_DBUS_NAME,
                                                               G_BUS_NAME_OWNER_FLAGS_NONE,
                                                               NULL,
                                                               NULL,
                                                               NULL,
                                                               NULL);
}

static void
register_manager_dbus (GsdXrandrManager *manager)
{
        manager->priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        manager->priv->bus_cancellable = g_cancellable_new ();
        g_assert (manager->priv->introspection_data != NULL);

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->priv->bus_cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);
}

GsdXrandrManager *
gsd_xrandr_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_XRANDR_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_XRANDR_MANAGER (manager_object);
}
