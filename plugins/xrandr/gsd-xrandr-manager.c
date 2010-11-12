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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include <libgnome-desktop/gnome-rr-config.h>
#include <libgnome-desktop/gnome-rr.h>
#include <libgnome-desktop/gnome-rr-labeler.h>

#ifdef HAVE_LIBNOTIFY
#include <libnotify/notify.h>
#endif

#include "gsd-enums.h"
#include "gnome-settings-profile.h"
#include "gsd-xrandr-manager.h"

#define GSD_XRANDR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_XRANDR_MANAGER, GsdXrandrManagerPrivate))

#define CONF_DIR "org.gnome.settings-daemon.plugins.xrandr"
#define CONF_KEY_DEFAULT_MONITORS_SETUP   "default-monitors-setup"
#define CONF_KEY_DEFAULT_CONFIGURATION_FILE   "default-configuration-file"

#define VIDEO_KEYSYM    "XF86Display"
#define ROTATE_KEYSYM   "XF86RotateWindows"

/* Number of seconds that the confirmation dialog will last before it resets the
 * RANDR configuration to its old state.
 */
#define CONFIRMATION_DIALOG_SECONDS 30

/* name of the icon files (gsd-xrandr.svg, etc.) */
#define GSD_XRANDR_ICON_NAME "gsd-xrandr"

#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_XRANDR_DBUS_PATH GSD_DBUS_PATH "/XRANDR"
#define GSD_XRANDR_DBUS_NAME GSD_DBUS_NAME ".XRANDR"

struct GsdXrandrManagerPrivate
{
        DBusGConnection *dbus_connection;

        GnomeRRScreen *rw_screen;
        gboolean running;

        GSettings *settings;

        /* fn-F7 status */
        int             current_fn_f7_config;             /* -1 if no configs */
        GnomeRRConfig **fn_f7_configs;  /* NULL terminated, NULL if there are no configs */

        /* Last time at which we got a "screen got reconfigured" event; see on_randr_event() */
        guint32 last_config_timestamp;
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

static void error_message (GsdXrandrManager *mgr, const char *primary_text, GError *error_to_display, const char *secondary_text);

static void get_allowed_rotations_for_output (GnomeRRConfig *config,
                                              GnomeRRScreen *rr_screen,
                                              GnomeOutputInfo *output,
                                              int *out_num_rotations,
                                              GnomeRRRotation *out_rotations);
static void handle_fn_f7 (GsdXrandrManager *mgr, guint32 timestamp);
static void handle_rotate_windows (GsdXrandrManager *mgr, guint32 timestamp);

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
log_output (GnomeOutputInfo *output)
{
        log_msg ("        %s: ", output->name ? output->name : "unknown");

        if (output->connected) {
                if (output->on) {
                        log_msg ("%dx%d@%d +%d+%d",
                                 output->width,
                                 output->height,
                                 output->rate,
                                 output->x,
                                 output->y);
                } else
                        log_msg ("off");
        } else
                log_msg ("disconnected");

        if (output->display_name)
                log_msg (" (%s)", output->display_name);

        if (output->primary)
                log_msg (" (primary output)");

        log_msg ("\n");
}

static void
log_configuration (GnomeRRConfig *config)
{
        int i;

        log_msg ("        cloned: %s\n", config->clone ? "yes" : "no");

        for (i = 0; config->outputs[i] != NULL; i++)
                log_output (config->outputs[i]);

        if (i == 0)
                log_msg ("        no outputs!\n");
}

static char
timestamp_relationship (guint32 a, guint32 b)
{
        if (a < b)
                return '<';
        else if (a > b)
                return '>';
        else
                return '=';
}

static void
log_screen (GnomeRRScreen *screen)
{
        GnomeRRConfig *config;
        int min_w, min_h, max_w, max_h;
        guint32 change_timestamp, config_timestamp;

        if (!log_file)
                return;

        config = gnome_rr_config_new_current (screen);

        gnome_rr_screen_get_ranges (screen, &min_w, &max_w, &min_h, &max_h);
        gnome_rr_screen_get_timestamps (screen, &change_timestamp, &config_timestamp);

        log_msg ("        Screen min(%d, %d), max(%d, %d), change=%u %c config=%u\n",
                 min_w, min_h,
                 max_w, max_h,
                 change_timestamp,
                 timestamp_relationship (change_timestamp, config_timestamp),
                 config_timestamp);

        log_configuration (config);
        gnome_rr_config_free (config);
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
show_timestamps_dialog (GsdXrandrManager *manager, const char *msg)
{
#if 1
        return;
#else
        struct GsdXrandrManagerPrivate *priv = manager->priv;
        GtkWidget *dialog;
        guint32 change_timestamp, config_timestamp;
        static int serial;

        gnome_rr_screen_get_timestamps (priv->rw_screen, &change_timestamp, &config_timestamp);

        dialog = gtk_message_dialog_new (NULL,
                                         0,
                                         GTK_MESSAGE_INFO,
                                         GTK_BUTTONS_CLOSE,
                                         "RANDR timestamps (%d):\n%s\nchange: %u\nconfig: %u",
                                         serial++,
                                         msg,
                                         change_timestamp,
                                         config_timestamp);
        g_signal_connect (dialog, "response",
                          G_CALLBACK (gtk_widget_destroy), NULL);
        gtk_widget_show (dialog);
#endif
}

/* This function centralizes the use of gnome_rr_config_apply_from_filename_with_time().
 *
 * Optionally filters out GNOME_RR_ERROR_NO_MATCHING_CONFIG from
 * gnome_rr_config_apply_from_filename_with_time(), since that is not usually an error.
 */
static gboolean
apply_configuration_from_filename (GsdXrandrManager *manager,
                                   const char       *filename,
                                   gboolean          no_matching_config_is_an_error,
                                   guint32           timestamp,
                                   GError          **error)
{
        struct GsdXrandrManagerPrivate *priv = manager->priv;
        GError *my_error;
        gboolean success;
        char *str;

        str = g_strdup_printf ("Applying %s with timestamp %d", filename, timestamp);
        show_timestamps_dialog (manager, str);
        g_free (str);

        my_error = NULL;
        success = gnome_rr_config_apply_from_filename_with_time (priv->rw_screen, filename, timestamp, &my_error);
        if (success)
                return TRUE;

        if (g_error_matches (my_error, GNOME_RR_ERROR, GNOME_RR_ERROR_NO_MATCHING_CONFIG)) {
                if (no_matching_config_is_an_error)
                        goto fail;

                /* This is not an error; the user probably changed his monitors
                 * and so they don't match any of the stored configurations.
                 */
                g_error_free (my_error);
                return TRUE;
        }

fail:
        g_propagate_error (error, my_error);
        return FALSE;
}

/* This function centralizes the use of gnome_rr_config_apply_with_time().
 *
 * Applies a configuration and displays an error message if an error happens.
 * We just return whether setting the configuration succeeded.
 */
static gboolean
apply_configuration_and_display_error (GsdXrandrManager *manager, GnomeRRConfig *config, guint32 timestamp)
{
        GsdXrandrManagerPrivate *priv = manager->priv;
        GError *error;
        gboolean success;

        error = NULL;
        success = gnome_rr_config_apply_with_time (config, priv->rw_screen, timestamp, &error);
        if (!success) {
                log_msg ("Could not switch to the following configuration (timestamp %u): %s\n", timestamp, error->message);
                log_configuration (config);
                error_message (manager, _("Could not switch the monitor configuration"), error, NULL);
                g_error_free (error);
        }

        return success;
}

static void
restore_backup_configuration_without_messages (const char *backup_filename, const char *intended_filename)
{
        backup_filename = gnome_rr_config_get_backup_filename ();
        rename (backup_filename, intended_filename);
}

static void
restore_backup_configuration (GsdXrandrManager *manager, const char *backup_filename, const char *intended_filename, guint32 timestamp)
{
        int saved_errno;

        if (rename (backup_filename, intended_filename) == 0) {
                GError *error;

                error = NULL;
                if (!apply_configuration_from_filename (manager, intended_filename, FALSE, timestamp, &error)) {
                        error_message (manager, _("Could not restore the display's configuration"), error, NULL);

                        if (error)
                                g_error_free (error);
                }

                return;
        }

        saved_errno = errno;

        /* ENOENT means the original file didn't exist.  That is *not* an error;
         * the backup was not created because there wasn't even an original
         * monitors.xml (such as on a first-time login).  Note that *here* there
         * is a "didn't work" monitors.xml, so we must delete that one.
         */
        if (saved_errno == ENOENT)
                unlink (intended_filename);
        else {
                char *msg;

                msg = g_strdup_printf ("Could not rename %s to %s: %s",
                                       backup_filename, intended_filename,
                                       g_strerror (saved_errno));
                error_message (manager,
                               _("Could not restore the display's configuration from a backup"),
                               NULL,
                               msg);
                g_free (msg);
        }

        unlink (backup_filename);
}

typedef struct {
        GsdXrandrManager *manager;
        GtkWidget *dialog;

        int countdown;
        int response_id;
} TimeoutDialog;

static void
print_countdown_text (TimeoutDialog *timeout)
{
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (timeout->dialog),
                                                  ngettext ("The display will be reset to its previous configuration in %d second",
                                                            "The display will be reset to its previous configuration in %d seconds",
                                                            timeout->countdown),
                                                  timeout->countdown);
}

static gboolean
timeout_cb (gpointer data)
{
        TimeoutDialog *timeout = data;

        timeout->countdown--;

        if (timeout->countdown == 0) {
                timeout->response_id = GTK_RESPONSE_CANCEL;
                gtk_main_quit ();
        } else {
                print_countdown_text (timeout);
        }

        return TRUE;
}

static void
timeout_response_cb (GtkDialog *dialog, int response_id, gpointer data)
{
        TimeoutDialog *timeout = data;

        if (response_id == GTK_RESPONSE_DELETE_EVENT) {
                /* The user closed the dialog or pressed ESC, revert */
                timeout->response_id = GTK_RESPONSE_CANCEL;
        } else
                timeout->response_id = response_id;

        gtk_main_quit ();
}

static gboolean
user_says_things_are_ok (GsdXrandrManager *manager, GdkWindow *parent_window)
{
        TimeoutDialog timeout;
        guint timeout_id;

        timeout.manager = manager;

        timeout.dialog = gtk_message_dialog_new (NULL,
                                                 GTK_DIALOG_MODAL,
                                                 GTK_MESSAGE_QUESTION,
                                                 GTK_BUTTONS_NONE,
                                                 _("Does the display look OK?"));

        timeout.countdown = CONFIRMATION_DIALOG_SECONDS;

        print_countdown_text (&timeout);

        gtk_window_set_icon_name (GTK_WINDOW (timeout.dialog), "preferences-desktop-display");
        gtk_dialog_add_button (GTK_DIALOG (timeout.dialog), _("_Restore Previous Configuration"), GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button (GTK_DIALOG (timeout.dialog), _("_Keep This Configuration"), GTK_RESPONSE_ACCEPT);
        gtk_dialog_set_default_response (GTK_DIALOG (timeout.dialog), GTK_RESPONSE_ACCEPT); /* ah, the optimism */

        g_signal_connect (timeout.dialog, "response",
                          G_CALLBACK (timeout_response_cb),
                          &timeout);

        gtk_widget_realize (timeout.dialog);

        if (parent_window)
                gdk_window_set_transient_for (gtk_widget_get_window (timeout.dialog), parent_window);

        gtk_widget_show_all (timeout.dialog);
        /* We don't use g_timeout_add_seconds() since we actually care that the user sees "real" second ticks in the dialog */
        timeout_id = g_timeout_add (1000,
                                    timeout_cb,
                                    &timeout);
        gtk_main ();

        gtk_widget_destroy (timeout.dialog);
        g_source_remove (timeout_id);

        if (timeout.response_id == GTK_RESPONSE_ACCEPT)
                return TRUE;
        else
                return FALSE;
}

struct confirmation {
        GsdXrandrManager *manager;
        GdkWindow *parent_window;
        guint32 timestamp;
};

static gboolean
confirm_with_user_idle_cb (gpointer data)
{
        struct confirmation *confirmation = data;
        char *backup_filename;
        char *intended_filename;

        backup_filename = gnome_rr_config_get_backup_filename ();
        intended_filename = gnome_rr_config_get_intended_filename ();

        if (user_says_things_are_ok (confirmation->manager, confirmation->parent_window))
                unlink (backup_filename);
        else
                restore_backup_configuration (confirmation->manager, backup_filename, intended_filename, confirmation->timestamp);

        g_free (confirmation);

        return FALSE;
}

static void
queue_confirmation_by_user (GsdXrandrManager *manager, GdkWindow *parent_window, guint32 timestamp)
{
        struct confirmation *confirmation;

        confirmation = g_new (struct confirmation, 1);
        confirmation->manager = manager;
        confirmation->parent_window = parent_window;
        confirmation->timestamp = timestamp;

        g_idle_add (confirm_with_user_idle_cb, confirmation);
}

static gboolean
try_to_apply_intended_configuration (GsdXrandrManager *manager, GdkWindow *parent_window, guint32 timestamp, GError **error)
{
        char *backup_filename;
        char *intended_filename;
        gboolean result;

        /* Try to apply the intended configuration */

        backup_filename = gnome_rr_config_get_backup_filename ();
        intended_filename = gnome_rr_config_get_intended_filename ();

        result = apply_configuration_from_filename (manager, intended_filename, FALSE, timestamp, error);
        if (!result) {
                error_message (manager, _("The selected configuration for displays could not be applied"), error ? *error : NULL, NULL);
                restore_backup_configuration_without_messages (backup_filename, intended_filename);
                goto out;
        } else {
                /* We need to return as quickly as possible, so instead of
                 * confirming with the user right here, we do it in an idle
                 * handler.  The caller only expects a status for "could you
                 * change the RANDR configuration?", not "is the user OK with it
                 * as well?".
                 */
                queue_confirmation_by_user (manager, parent_window, timestamp);
        }

out:
        g_free (backup_filename);
        g_free (intended_filename);

        return result;
}

/* DBus method for org.gnome.SettingsDaemon.XRANDR ApplyConfiguration; see gsd-xrandr-manager.xml for the interface definition */
static gboolean
gsd_xrandr_manager_apply_configuration (GsdXrandrManager *manager,
                                        GError          **error)
{
        return try_to_apply_intended_configuration (manager, NULL, GDK_CURRENT_TIME, error);
}

/* DBus method for org.gnome.SettingsDaemon.XRANDR_2 ApplyConfiguration; see gsd-xrandr-manager.xml for the interface definition */
static gboolean
gsd_xrandr_manager_2_apply_configuration (GsdXrandrManager *manager,
                                          gint64            parent_window_id,
                                          gint64            timestamp,
                                          GError          **error)
{
        GdkWindow *parent_window;
        gboolean result;

        if (parent_window_id != 0)
                parent_window = gdk_window_foreign_new_for_display (gdk_display_get_default (), (GdkNativeWindow) parent_window_id);
        else
                parent_window = NULL;

        result = try_to_apply_intended_configuration (manager, parent_window, (guint32) timestamp, error);

        if (parent_window)
                g_object_unref (parent_window);

        return result;
}

/* DBus method for org.gnome.SettingsDaemon.XRANDR_2 VideoModeSwitch; see gsd-xrandr-manager.xml for the interface definition */
static gboolean
gsd_xrandr_manager_2_video_mode_switch (GsdXrandrManager *manager,
					guint32           timestamp,
					GError          **error)
{
	handle_fn_f7 (manager, timestamp);
	return TRUE;
}

/* DBus method for org.gnome.SettingsDaemon.XRANDR_2 Rotate; see gsd-xrandr-manager.xml for the interface definition */
static gboolean
gsd_xrandr_manager_2_rotate (GsdXrandrManager *manager,
			     guint32           timestamp,
			     GError          **error)
{
	handle_rotate_windows (manager, timestamp);
	return TRUE;
}

/* We include this after the definition of gsd_xrandr_manager_* D-Bus methods so the prototype will already exist */
#include "gsd-xrandr-manager-glue.h"

static gboolean
is_laptop (GnomeRRScreen *screen, GnomeOutputInfo *output)
{
        GnomeRROutput *rr_output;

        rr_output = gnome_rr_screen_get_output_by_name (screen, output->name);
        return gnome_rr_output_is_laptop (rr_output);
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

static void
print_output (GnomeOutputInfo *info)
{
        g_print ("  Output: %s attached to %s\n", info->display_name, info->name);
        g_print ("     status: %s\n", info->on ? "on" : "off");
        g_print ("     width: %d\n", info->width);
        g_print ("     height: %d\n", info->height);
        g_print ("     rate: %d\n", info->rate);
        g_print ("     position: %d %d\n", info->x, info->y);
}

static void
print_configuration (GnomeRRConfig *config, const char *header)
{
        int i;

        g_print ("=== %s Configuration ===\n", header);
        if (!config) {
                g_print ("  none\n");
                return;
        }

        for (i = 0; config->outputs[i] != NULL; ++i)
                print_output (config->outputs[i]);
}

static gboolean
config_is_all_off (GnomeRRConfig *config)
{
        int j;

        for (j = 0; config->outputs[j] != NULL; ++j) {
                if (config->outputs[j]->on) {
                        return FALSE;
                }
        }

        return TRUE;
}

static GnomeRRConfig *
make_clone_setup (GnomeRRScreen *screen)
{
        GnomeRRConfig *result;
        int width, height;
        int i;

        if (!get_clone_size (screen, &width, &height))
                return NULL;

        result = gnome_rr_config_new_current (screen);

        for (i = 0; result->outputs[i] != NULL; ++i) {
                GnomeOutputInfo *info = result->outputs[i];

                info->on = FALSE;
                if (info->connected) {
                        GnomeRROutput *output =
                                gnome_rr_screen_get_output_by_name (screen, info->name);
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
                                info->on = TRUE;
                                info->width = width;
                                info->height = height;
                                info->rate = best_rate;
                                info->rotation = GNOME_RR_ROTATION_0;
                                info->x = 0;
                                info->y = 0;
                        }
                }
        }

        if (config_is_all_off (result)) {
                gnome_rr_config_free (result);
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
         GnomeOutputInfo *info,
         int x, int y)
{
        GnomeRROutput *output = gnome_rr_screen_get_output_by_name (screen, info->name);
        GnomeRRMode *mode = find_best_mode (output);

        if (mode) {
                info->on = TRUE;
                info->x = x;
                info->y = y;
                info->width = gnome_rr_mode_get_width (mode);
                info->height = gnome_rr_mode_get_height (mode);
                info->rotation = GNOME_RR_ROTATION_0;
                info->rate = gnome_rr_mode_get_freq (mode);

                return TRUE;
        }

        return FALSE;
}

static GnomeRRConfig *
make_laptop_setup (GnomeRRScreen *screen)
{
        /* Turn on the laptop, disable everything else */
        GnomeRRConfig *result = gnome_rr_config_new_current (screen);
        int i;

        for (i = 0; result->outputs[i] != NULL; ++i) {
                GnomeOutputInfo *info = result->outputs[i];

                if (is_laptop (screen, info)) {
                        if (!turn_on (screen, info, 0, 0)) {
                                gnome_rr_config_free (result);
                                result = NULL;
                                break;
                        }
                }
                else {
                        info->on = FALSE;
                }
        }

        if (config_is_all_off (result)) {
                gnome_rr_config_free (result);
                result = NULL;
        }

        print_configuration (result, "Laptop setup");

        /* FIXME - Maybe we should return NULL if there is more than
         * one connected "laptop" screen?
         */
        return result;

}

static int
turn_on_and_get_rightmost_offset (GnomeRRScreen *screen, GnomeOutputInfo *info, int x)
{
        if (turn_on (screen, info, x, 0))
                x += info->width;

        return x;
}

static GnomeRRConfig *
make_xinerama_setup (GnomeRRScreen *screen)
{
        /* Turn on everything that has a preferred mode, and
         * position it from left to right
         */
        GnomeRRConfig *result = gnome_rr_config_new_current (screen);
        int i;
        int x;

        x = 0;
        for (i = 0; result->outputs[i] != NULL; ++i) {
                GnomeOutputInfo *info = result->outputs[i];

                if (is_laptop (screen, info))
                        x = turn_on_and_get_rightmost_offset (screen, info, x);
        }

        for (i = 0; result->outputs[i] != NULL; ++i) {
                GnomeOutputInfo *info = result->outputs[i];

                if (info->connected && !is_laptop (screen, info))
                        x = turn_on_and_get_rightmost_offset (screen, info, x);
        }

        if (config_is_all_off (result)) {
                gnome_rr_config_free (result);
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

        GnomeRRConfig *result = gnome_rr_config_new_current (screen);
        int i;

        for (i = 0; result->outputs[i] != NULL; ++i) {
                GnomeOutputInfo *info = result->outputs[i];

                if (is_laptop (screen, info)) {
                        info->on = FALSE;
                }
                else {
                        if (info->connected)
                                turn_on (screen, info, 0, 0);
               }
        }

        if (config_is_all_off (result)) {
                gnome_rr_config_free (result);
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
                                gnome_rr_config_free (this);
                                array->pdata[j] = NULL;
                                break;
                        }
                }
        }

        for (i = 0; i < array->len; ++i) {
                GnomeRRConfig *config = array->pdata[i];

                if (config && config_is_all_off (config)) {
                        g_debug ("removing configuration as all outputs are off");
                        gnome_rr_config_free (array->pdata[i]);
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

                                gnome_rr_config_free (config);
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
generate_fn_f7_configs (GsdXrandrManager *mgr)
{
        GPtrArray *array = g_ptr_array_new ();
        GnomeRRScreen *screen = mgr->priv->rw_screen;

        g_debug ("Generating configurations");

        /* Free any existing list of configurations */
        if (mgr->priv->fn_f7_configs) {
                int i;

                for (i = 0; mgr->priv->fn_f7_configs[i] != NULL; ++i)
                        gnome_rr_config_free (mgr->priv->fn_f7_configs[i]);
                g_free (mgr->priv->fn_f7_configs);

                mgr->priv->fn_f7_configs = NULL;
                mgr->priv->current_fn_f7_config = -1;
        }

        g_ptr_array_add (array, gnome_rr_config_new_current (screen));
        g_ptr_array_add (array, make_clone_setup (screen));
        g_ptr_array_add (array, make_xinerama_setup (screen));
        g_ptr_array_add (array, make_laptop_setup (screen));
        g_ptr_array_add (array, make_other_setup (screen));

        array = sanitize (mgr, array);

        if (array) {
                mgr->priv->fn_f7_configs = (GnomeRRConfig **)g_ptr_array_free (array, FALSE);
                mgr->priv->current_fn_f7_config = 0;
        }
}

static void
error_message (GsdXrandrManager *mgr, const char *primary_text, GError *error_to_display, const char *secondary_text)
{
#ifdef HAVE_LIBNOTIFY
        NotifyNotification *notification;

        g_assert (error_to_display == NULL || secondary_text == NULL);

        notification = notify_notification_new (primary_text,
                                                error_to_display ? error_to_display->message : secondary_text,
                                                GSD_XRANDR_ICON_NAME);

        notify_notification_show (notification, NULL); /* NULL-GError */
#else
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                         "%s", primary_text);
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
                                                  error_to_display ? error_to_display->message : secondary_text);

        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
#endif /* HAVE_LIBNOTIFY */
}

static void
handle_fn_f7 (GsdXrandrManager *mgr, guint32 timestamp)
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
        log_msg ("Handling XF86Display hotkey - timestamp %u\n", timestamp);

        error = NULL;
        if (!gnome_rr_screen_refresh (screen, &error) && error) {
                char *str;

                str = g_strdup_printf (_("Could not refresh the screen information: %s"), error->message);
                g_error_free (error);

                log_msg ("%s\n", str);
                error_message (mgr, str, NULL, _("Trying to switch the monitor configuration anyway."));
                g_free (str);
        }

        if (!priv->fn_f7_configs) {
                log_msg ("Generating stock configurations:\n");
                generate_fn_f7_configs (mgr);
                log_configurations (priv->fn_f7_configs);
        }

        current = gnome_rr_config_new_current (screen);

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

        gnome_rr_config_free (current);

        if (priv->fn_f7_configs) {
                guint32 server_timestamp;
                gboolean success;

                mgr->priv->current_fn_f7_config++;

                if (priv->fn_f7_configs[mgr->priv->current_fn_f7_config] == NULL)
                        mgr->priv->current_fn_f7_config = 0;

                g_debug ("cycling to next configuration (%d)", mgr->priv->current_fn_f7_config);

                print_configuration (priv->fn_f7_configs[mgr->priv->current_fn_f7_config], "new config");

                g_debug ("applying");

                /* See https://bugzilla.gnome.org/show_bug.cgi?id=610482
                 *
                 * Sometimes we'll get two rapid XF86Display keypress events,
                 * but their timestamps will be out of order with respect to the
                 * RANDR timestamps.  This *may* be due to stupid BIOSes sending
                 * out display-switch keystrokes "to make Windows work".
                 *
                 * The X server will error out if the timestamp provided is
                 * older than a previous change configuration timestamp. We
                 * assume here that we do want this event to go through still,
                 * since kernel timestamps may be skewed wrt the X server.
                 */
                gnome_rr_screen_get_timestamps (screen, NULL, &server_timestamp);
                if (timestamp < server_timestamp)
                        timestamp = server_timestamp;

                success = apply_configuration_and_display_error (mgr, priv->fn_f7_configs[mgr->priv->current_fn_f7_config], timestamp);

                if (success) {
                        log_msg ("Successfully switched to configuration (timestamp %u):\n", timestamp);
                        log_configuration (priv->fn_f7_configs[mgr->priv->current_fn_f7_config]);
                }
        }
        else {
                g_debug ("no configurations generated");
        }

        log_close ();

        g_debug ("done handling fn-f7");
}

static GnomeOutputInfo *
get_laptop_output_info (GnomeRRScreen *screen, GnomeRRConfig *config)
{
        int i;

        for (i = 0; config->outputs[i] != NULL; i++) {
                GnomeOutputInfo *info;

                info = config->outputs[i];
                if (is_laptop (screen, info))
                        return info;
        }

        return NULL;
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

/* We use this when the XF86RotateWindows key is pressed.  That key is present
 * on some tablet PCs; they use it so that the user can rotate the tablet
 * easily.
 */
static void
handle_rotate_windows (GsdXrandrManager *mgr, guint32 timestamp)
{
        GsdXrandrManagerPrivate *priv = mgr->priv;
        GnomeRRScreen *screen = priv->rw_screen;
        GnomeRRConfig *current;
        GnomeOutputInfo *rotatable_output_info;
        int num_allowed_rotations;
        GnomeRRRotation allowed_rotations;
        GnomeRRRotation next_rotation;

        g_debug ("Handling XF86RotateWindows");

        /* Which output? */

        current = gnome_rr_config_new_current (screen);

        rotatable_output_info = get_laptop_output_info (screen, current);
        if (rotatable_output_info == NULL) {
                g_debug ("No laptop outputs found to rotate; XF86RotateWindows key will do nothing");
                goto out;
        }

        /* Which rotation? */

        get_allowed_rotations_for_output (current, priv->rw_screen, rotatable_output_info, &num_allowed_rotations, &allowed_rotations);
        next_rotation = get_next_rotation (allowed_rotations, rotatable_output_info->rotation);

        if (next_rotation == rotatable_output_info->rotation) {
                g_debug ("No rotations are supported other than the current one; XF86RotateWindows key will do nothing");
                goto out;
        }

        /* Rotate */

        rotatable_output_info->rotation = next_rotation;

        apply_configuration_and_display_error (mgr, current, timestamp);

out:
        gnome_rr_config_free (current);
}

static void
auto_configure_outputs (GsdXrandrManager *manager, guint32 timestamp)
{
        GsdXrandrManagerPrivate *priv = manager->priv;
        GnomeRRConfig *config;
        int i;
        GList *just_turned_on;
        GList *l;
        int x;
        GError *error;
        gboolean applicable;

        config = gnome_rr_config_new_current (priv->rw_screen);

        /* For outputs that are connected and on (i.e. they have a CRTC assigned
         * to them, so they are getting a signal), we leave them as they are
         * with their current modes.
         *
         * For other outputs, we will turn on connected-but-off outputs and turn
         * off disconnected-but-on outputs.
         *
         * FIXME: If an output remained connected+on, it would be nice to ensure
         * that the output's CRTCs still has a reasonable mode (think of
         * changing one monitor for another with different capabilities).
         */

        just_turned_on = NULL;

        for (i = 0; config->outputs[i] != NULL; i++) {
                GnomeOutputInfo *output = config->outputs[i];

                if (output->connected && !output->on) {
                        output->on = TRUE;
                        output->rotation = GNOME_RR_ROTATION_0;
                        just_turned_on = g_list_prepend (just_turned_on, GINT_TO_POINTER (i));
                } else if (!output->connected && output->on)
                        output->on = FALSE;
        }

        /* Now, lay out the outputs from left to right.  Put first the outputs
         * which remained on; put last the outputs that were newly turned on.
         */

        x = 0;

        /* First, outputs that remained on */

        for (i = 0; config->outputs[i] != NULL; i++) {
                GnomeOutputInfo *output = config->outputs[i];

                if (g_list_find (just_turned_on, GINT_TO_POINTER (i)))
                        continue;

                if (output->on) {
                        g_assert (output->connected);

                        output->x = x;
                        output->y = 0;

                        x += output->width;
                }
        }

        /* Second, outputs that were newly-turned on */

        for (l = just_turned_on; l; l = l->next) {
                GnomeOutputInfo *output;

                i = GPOINTER_TO_INT (l->data);
                output = config->outputs[i];

                g_assert (output->on && output->connected);

                output->x = x;
                output->y = 0;

                /* since the output was off, use its preferred width/height (it doesn't have a real width/height yet) */
                output->width = output->pref_width;
                output->height = output->pref_height;

                x += output->width;
        }

        /* Check if we have a large enough framebuffer size.  If not, turn off
         * outputs from right to left until we reach a usable size.
         */

        just_turned_on = g_list_reverse (just_turned_on); /* now the outputs here are from right to left */

        l = just_turned_on;
        while (1) {
                GnomeOutputInfo *output;
                gboolean is_bounds_error;

                error = NULL;
                applicable = gnome_rr_config_applicable (config, priv->rw_screen, &error);

                if (applicable)
                        break;

                is_bounds_error = g_error_matches (error, GNOME_RR_ERROR, GNOME_RR_ERROR_BOUNDS_ERROR);
                g_error_free (error);

                if (!is_bounds_error)
                        break;

                if (l) {
                        i = GPOINTER_TO_INT (l->data);
                        l = l->next;

                        output = config->outputs[i];
                        output->on = FALSE;
                } else
                        break;
        }

        /* Apply the configuration! */

        if (applicable)
                apply_configuration_and_display_error (manager, config, timestamp);

        g_list_free (just_turned_on);
        gnome_rr_config_free (config);
}

static void
apply_color_profiles (void)
{
        gboolean ret;
        GError *error = NULL;

        /* run the gnome-color-manager apply program */
        ret = g_spawn_command_line_async (BINDIR "/gcm-apply", &error);
        if (!ret) {
                /* only print the warning if the binary is installed */
                if (error->code != G_SPAWN_ERROR_NOENT) {
                        g_warning ("failed to apply color profiles: %s", error->message);
                }
                g_error_free (error);
        }
}

static void
on_randr_event (GnomeRRScreen *screen, gpointer data)
{
        GsdXrandrManager *manager = GSD_XRANDR_MANAGER (data);
        GsdXrandrManagerPrivate *priv = manager->priv;
        guint32 change_timestamp, config_timestamp;

        if (!priv->running)
                return;

        gnome_rr_screen_get_timestamps (screen, &change_timestamp, &config_timestamp);

        log_open ();
        log_msg ("Got RANDR event with timestamps change=%u %c config=%u\n",
                 change_timestamp,
                 timestamp_relationship (change_timestamp, config_timestamp),
                 config_timestamp);

        if (change_timestamp >= config_timestamp) {
                /* The event is due to an explicit configuration change.
                 *
                 * If the change was performed by us, then we need to do nothing.
                 *
                 * If the change was done by some other X client, we don't need
                 * to do anything, either; the screen is already configured.
                 */
                show_timestamps_dialog (manager, "ignoring since change > config");
                log_msg ("  Ignoring event since change >= config\n");
        } else {
                /* Here, config_timestamp > change_timestamp.  This means that
                 * the screen got reconfigured because of hotplug/unplug; the X
                 * server is just notifying us, and we need to configure the
                 * outputs in a sane way.
                 */

                char *intended_filename;
                GError *error;
                gboolean success;

                show_timestamps_dialog (manager, "need to deal with reconfiguration, as config > change");

                intended_filename = gnome_rr_config_get_intended_filename ();

                error = NULL;
                success = apply_configuration_from_filename (manager, intended_filename, TRUE, config_timestamp, &error);
                g_free (intended_filename);

                if (!success) {
                        /* We don't bother checking the error type.
                         *
                         * Both G_FILE_ERROR_NOENT and
                         * GNOME_RR_ERROR_NO_MATCHING_CONFIG would mean, "there
                         * was no configuration to apply, or none that matched
                         * the current outputs", and in that case we need to run
                         * our fallback.
                         *
                         * Any other error means "we couldn't do the smart thing
                         * of using a previously- saved configuration, anyway,
                         * for some other reason.  In that case, we also need to
                         * run our fallback to avoid leaving the user with a
                         * bogus configuration.
                         */

                        if (error)
                                g_error_free (error);

                        if (config_timestamp != priv->last_config_timestamp) {
                                priv->last_config_timestamp = config_timestamp;
                                auto_configure_outputs (manager, config_timestamp);
                                log_msg ("  Automatically configured outputs to deal with event\n");
                        } else
                                log_msg ("  Ignored event as old and new config timestamps are the same\n");
                } else
                        log_msg ("Applied stored configuration to deal with event\n");
        }

        /* poke gnome-color-manager */
        apply_color_profiles ();

        log_close ();
}

static void
get_allowed_rotations_for_output (GnomeRRConfig *config,
                                  GnomeRRScreen *rr_screen,
                                  GnomeOutputInfo *output,
                                  int *out_num_rotations,
                                  GnomeRRRotation *out_rotations)
{
        GnomeRRRotation current_rotation;
        int i;

        *out_num_rotations = 0;
        *out_rotations = 0;

        current_rotation = output->rotation;

        /* Yay for brute force */

        for (i = 0; i < G_N_ELEMENTS (possible_rotations); i++) {
                GnomeRRRotation rotation_to_test;

                rotation_to_test = possible_rotations[i];

                output->rotation = rotation_to_test;

                if (gnome_rr_config_applicable (config, rr_screen, NULL)) { /* NULL-GError */
                        (*out_num_rotations)++;
                        (*out_rotations) |= rotation_to_test;
                }
        }

        output->rotation = current_rotation;

        if (*out_num_rotations == 0 || *out_rotations == 0) {
                g_warning ("Huh, output %p says it doesn't support any rotations, and yet it has a current rotation?", output);
                *out_num_rotations = 1;
                *out_rotations = output->rotation;
        }
}

static gboolean
apply_intended_configuration (GsdXrandrManager *manager, const char *intended_filename, guint32 timestamp)
{
        GError *my_error;
        gboolean result;

        my_error = NULL;
        result = apply_configuration_from_filename (manager, intended_filename, TRUE, timestamp, &my_error);
        if (!result) {
                if (my_error) {
                        if (!g_error_matches (my_error, G_FILE_ERROR, G_FILE_ERROR_NOENT) &&
                            !g_error_matches (my_error, GNOME_RR_ERROR, GNOME_RR_ERROR_NO_MATCHING_CONFIG))
                                error_message (manager, _("Could not apply the stored configuration for monitors"), my_error, NULL);

                        g_error_free (my_error);
                }
        }

        return result;
}

static void
apply_default_boot_configuration (GsdXrandrManager *mgr, guint32 timestamp)
{
        GsdXrandrManagerPrivate *priv = mgr->priv;
        GnomeRRScreen *screen = priv->rw_screen;
        GnomeRRConfig *config;
        GsdXrandrBootBehaviour boot;

        boot = g_settings_get_enum (priv->settings, CONF_KEY_DEFAULT_MONITORS_SETUP);

        switch (boot) {
	case GSD_XRANDR_BOOT_BEHAVIOUR_DO_NOTHING:
		return;
	case GSD_XRANDR_BOOT_BEHAVIOUR_CLONE:
		config = make_clone_setup (screen);
		break;
	case GSD_XRANDR_BOOT_BEHAVIOUR_DOCK:
		config = make_other_setup (screen);
		break;
	default:
		g_assert_not_reached ();
	}

	if (config) {
		apply_configuration_and_display_error (mgr, config, timestamp);
		gnome_rr_config_free (config);
	}
}

static gboolean
apply_stored_configuration_at_startup (GsdXrandrManager *manager, guint32 timestamp)
{
        GError *my_error;
        gboolean success;
        char *backup_filename;
        char *intended_filename;

        backup_filename = gnome_rr_config_get_backup_filename ();
        intended_filename = gnome_rr_config_get_intended_filename ();

        /* 1. See if there was a "saved" configuration.  If there is one, it means
         * that the user had selected to change the display configuration, but the
         * machine crashed.  In that case, we'll apply *that* configuration and save it on top of the
         * "intended" one.
         */

        my_error = NULL;

        success = apply_configuration_from_filename (manager, backup_filename, FALSE, timestamp, &my_error);
        if (success) {
                /* The backup configuration existed, and could be applied
                 * successfully, so we must restore it on top of the
                 * failed/intended one.
                 */
                restore_backup_configuration (manager, backup_filename, intended_filename, timestamp);
                goto out;
        }

        if (!g_error_matches (my_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
                /* Epic fail:  there (probably) was a backup configuration, but
                 * we could not apply it.  The only thing we can do is delete
                 * the backup configuration.  Let's hope that the user doesn't
                 * get left with an unusable display...
                 */

                unlink (backup_filename);
                goto out;
        }

        /* 2. There was no backup configuration!  This means we are
         * good.  Apply the intended configuration instead.
         */

        success = apply_intended_configuration (manager, intended_filename, timestamp);

out:

        if (my_error)
                g_error_free (my_error);

        g_free (backup_filename);
        g_free (intended_filename);

        return success;
}

static gboolean
apply_default_configuration_from_file (GsdXrandrManager *manager, guint32 timestamp)
{
        GsdXrandrManagerPrivate *priv = manager->priv;
        char *default_config_filename;
        gboolean result;

        default_config_filename = g_settings_get_string (priv->settings, CONF_KEY_DEFAULT_CONFIGURATION_FILE);
        if (!default_config_filename)
                return FALSE;

        result = apply_configuration_from_filename (manager, default_config_filename, TRUE, timestamp, NULL);

        g_free (default_config_filename);
        return result;
}

gboolean
gsd_xrandr_manager_start (GsdXrandrManager *manager,
                          GError          **error)
{
        g_debug ("Starting xrandr manager");
        gnome_settings_profile_start (NULL);

        log_open ();
        log_msg ("------------------------------------------------------------\nSTARTING XRANDR PLUGIN\n");

        manager->priv->rw_screen = gnome_rr_screen_new (
                gdk_screen_get_default (), on_randr_event, manager, error);

        if (manager->priv->rw_screen == NULL) {
                log_msg ("Could not initialize the RANDR plugin%s%s\n",
                         (error && *error) ? ": " : "",
                         (error && *error) ? (*error)->message : "");
                log_close ();
                return FALSE;
        }

        log_msg ("State of screen at startup:\n");
        log_screen (manager->priv->rw_screen);

        manager->priv->running = TRUE;
        manager->priv->settings = g_settings_new (CONF_DIR);

        show_timestamps_dialog (manager, "Startup");
        if (!apply_stored_configuration_at_startup (manager, GDK_CURRENT_TIME)) /* we don't have a real timestamp at startup anyway */
                if (!apply_default_configuration_from_file (manager, GDK_CURRENT_TIME))
			apply_default_boot_configuration (manager, GDK_CURRENT_TIME);

        log_msg ("State of screen after initial configuration:\n");
        log_screen (manager->priv->rw_screen);

        log_close ();

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_xrandr_manager_stop (GsdXrandrManager *manager)
{
        g_debug ("Stopping xrandr manager");

        manager->priv->running = FALSE;

        if (manager->priv->settings != NULL) {
                g_object_unref (manager->priv->settings);
                manager->priv->settings = NULL;
        }

        if (manager->priv->rw_screen != NULL) {
                gnome_rr_screen_destroy (manager->priv->rw_screen);
                manager->priv->rw_screen = NULL;
        }

        if (manager->priv->dbus_connection != NULL) {
                dbus_g_connection_unref (manager->priv->dbus_connection);
                manager->priv->dbus_connection = NULL;
        }

        log_open ();
        log_msg ("STOPPING XRANDR PLUGIN\n------------------------------------------------------------\n");
        log_close ();
}

static void
gsd_xrandr_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdXrandrManager *self;

        self = GSD_XRANDR_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_xrandr_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdXrandrManager *self;

        self = GSD_XRANDR_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_xrandr_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdXrandrManager      *xrandr_manager;
        GsdXrandrManagerClass *klass;

        klass = GSD_XRANDR_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_XRANDR_MANAGER));

        xrandr_manager = GSD_XRANDR_MANAGER (G_OBJECT_CLASS (gsd_xrandr_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (xrandr_manager);
}

static void
gsd_xrandr_manager_dispose (GObject *object)
{
        GsdXrandrManager *xrandr_manager;

        xrandr_manager = GSD_XRANDR_MANAGER (object);

        G_OBJECT_CLASS (gsd_xrandr_manager_parent_class)->dispose (object);
}

static void
gsd_xrandr_manager_class_init (GsdXrandrManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_xrandr_manager_get_property;
        object_class->set_property = gsd_xrandr_manager_set_property;
        object_class->constructor = gsd_xrandr_manager_constructor;
        object_class->dispose = gsd_xrandr_manager_dispose;
        object_class->finalize = gsd_xrandr_manager_finalize;

        dbus_g_object_type_install_info (GSD_TYPE_XRANDR_MANAGER, &dbus_glib_gsd_xrandr_manager_object_info);

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
        GsdXrandrManager *xrandr_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_XRANDR_MANAGER (object));

        xrandr_manager = GSD_XRANDR_MANAGER (object);

        g_return_if_fail (xrandr_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_xrandr_manager_parent_class)->finalize (object);
}

static gboolean
register_manager_dbus (GsdXrandrManager *manager)
{
        GError *error = NULL;

        manager->priv->dbus_connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (manager->priv->dbus_connection == NULL) {
                if (error != NULL) {
                        g_warning ("Error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        /* Hmm, should we do this in gsd_xrandr_manager_start()? */
        dbus_g_connection_register_g_object (manager->priv->dbus_connection, GSD_XRANDR_DBUS_PATH, G_OBJECT (manager));

        return TRUE;
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

                if (!register_manager_dbus (manager_object)) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return GSD_XRANDR_MANAGER (manager_object);
}
