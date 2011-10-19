/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2001-2003 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
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
#include <math.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>

#include "gnome-settings-profile.h"
#include "gsd-marshal.h"
#include "gsd-media-keys-manager.h"

#include "eggaccelerators.h"
#include "acme.h"
#include "gsd-media-keys-window.h"
#include "gsd-input-helper.h"
#include "gsd-enums.h"

#include <canberra-gtk.h>
#include <pulse/pulseaudio.h>
#include "gvc-mixer-control.h"

#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_MEDIA_KEYS_DBUS_PATH GSD_DBUS_PATH "/MediaKeys"
#define GSD_MEDIA_KEYS_DBUS_NAME GSD_DBUS_NAME ".MediaKeys"

#define GNOME_SESSION_DBUS_NAME "org.gnome.SessionManager"
#define GNOME_SESSION_DBUS_PATH "/org/gnome/SessionManager"
#define GNOME_SESSION_DBUS_INTERFACE "org.gnome.SessionManager"

static const gchar introspection_xml[] =
"<node>"
"  <interface name='org.gnome.SettingsDaemon.MediaKeys'>"
"    <annotation name='org.freedesktop.DBus.GLib.CSymbol' value='gsd_media_keys_manager'/>"
"    <method name='GrabMediaPlayerKeys'>"
"      <arg name='application' direction='in' type='s'/>"
"      <arg name='time' direction='in' type='u'/>"
"    </method>"
"    <method name='ReleaseMediaPlayerKeys'>"
"      <arg name='application' direction='in' type='s'/>"
"    </method>"
"    <signal name='MediaPlayerKeyPressed'>"
"      <arg name='application' type='s'/>"
"      <arg name='key' type='s'/>"
"    </signal>"
"  </interface>"
"</node>";

#define SETTINGS_INTERFACE_DIR "org.gnome.desktop.interface"
#define SETTINGS_POWER_DIR "org.gnome.settings-daemon.plugins.power"
#define SETTINGS_XSETTINGS_DIR "org.gnome.settings-daemon.plugins.xsettings"
#define SETTINGS_TOUCHPAD_DIR "org.gnome.settings-daemon.peripherals.touchpad"
#define TOUCHPAD_ENABLED_KEY "touchpad-enabled"
#define HIGH_CONTRAST "HighContrast"

#define VOLUME_STEP 6           /* percents for one volume button press */
#define MAX_VOLUME 65536.0

#define GSD_MEDIA_KEYS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_MEDIA_KEYS_MANAGER, GsdMediaKeysManagerPrivate))

typedef struct {
        char   *application;
        char   *name;
        guint32 time;
        guint   watch_id;
} MediaPlayer;

struct GsdMediaKeysManagerPrivate
{
        /* Volume bits */
        GvcMixerControl *volume;
        GvcMixerStream  *stream;

        GtkWidget       *dialog;
        GSettings       *settings;

        /* HighContrast theme settings */
        GSettings       *interface_settings;
        char            *icon_theme;
        char            *gtk_theme;

        /* Power stuff */
        GSettings       *power_settings;
        GDBusProxy      *upower_proxy;
        GDBusProxy      *power_screen_proxy;
        GDBusProxy      *power_keyboard_proxy;

        /* Multihead stuff */
        GdkScreen       *current_screen;
        GSList          *screens;

        GList           *media_players;

        GDBusNodeInfo   *introspection_data;
        GDBusConnection *connection;
        GCancellable    *bus_cancellable;
        GDBusProxy      *xrandr_proxy;
        GCancellable    *cancellable;

        guint            start_idle_id;
};

static void     gsd_media_keys_manager_class_init  (GsdMediaKeysManagerClass *klass);
static void     gsd_media_keys_manager_init        (GsdMediaKeysManager      *media_keys_manager);
static void     gsd_media_keys_manager_finalize    (GObject                  *object);
static void     register_manager                   (GsdMediaKeysManager      *manager);
G_DEFINE_TYPE (GsdMediaKeysManager, gsd_media_keys_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;


static void
init_screens (GsdMediaKeysManager *manager)
{
        GdkDisplay *display;
        int i;

        display = gdk_display_get_default ();
        for (i = 0; i < gdk_display_get_n_screens (display); i++) {
                GdkScreen *screen;

                screen = gdk_display_get_screen (display, i);
                if (screen == NULL) {
                        continue;
                }
                manager->priv->screens = g_slist_append (manager->priv->screens, screen);
        }

        manager->priv->current_screen = manager->priv->screens->data;
}


static void
acme_error (char * msg)
{
        GtkWidget *error_dialog;

        error_dialog = gtk_message_dialog_new (NULL,
                                               GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_OK,
                                               msg, NULL);
        gtk_dialog_set_default_response (GTK_DIALOG (error_dialog),
                                         GTK_RESPONSE_OK);
        gtk_widget_show (error_dialog);
        g_signal_connect (error_dialog,
                          "response",
                          G_CALLBACK (gtk_widget_destroy),
                          NULL);
}

static char *
get_term_command (GsdMediaKeysManager *manager)
{
        char *cmd_term, *cmd_args;;
        char *cmd = NULL;
        GSettings *settings;

        settings = g_settings_new ("org.gnome.desktop.default-applications.terminal");
        cmd_term = g_settings_get_string (settings, "exec");
        if (cmd_term[0] == '\0')
                cmd_term = g_strdup ("gnome-terminal");

        cmd_args = g_settings_get_string (settings, "exec-arg");
        if (strcmp (cmd_term, "") != 0) {
                cmd = g_strdup_printf ("%s %s -e", cmd_term, cmd_args);
        } else {
                cmd = g_strdup_printf ("%s -e", cmd_term);
        }

        g_free (cmd_args);
        g_free (cmd_term);
        g_object_unref (settings);

        return cmd;
}

static void
execute (GsdMediaKeysManager *manager,
         char                *cmd,
         gboolean             sync,
         gboolean             need_term)
{
        gboolean retval;
        char   **argv;
        int      argc;
        char    *exec;
        char    *term = NULL;

        retval = FALSE;

        if (need_term) {
                term = get_term_command (manager);
                if (term == NULL) {
                        acme_error (_("Could not get default terminal. Verify that your default "
                                      "terminal command is set and points to a valid application."));
                        return;
                }
        }

        if (term) {
                exec = g_strdup_printf ("%s %s", term, cmd);
                g_free (term);
        } else {
                exec = g_strdup (cmd);
        }

        if (g_shell_parse_argv (exec, &argc, &argv, NULL)) {
                if (sync != FALSE) {
                        retval = g_spawn_sync (g_get_home_dir (),
                                               argv,
                                               NULL,
                                               G_SPAWN_SEARCH_PATH,
                                               NULL,
                                               NULL,
                                               NULL,
                                               NULL,
                                               NULL,
                                               NULL);
                } else {
                        retval = g_spawn_async (g_get_home_dir (),
                                                argv,
                                                NULL,
                                                G_SPAWN_SEARCH_PATH,
                                                NULL,
                                                NULL,
                                                NULL,
                                                NULL);
                }
                g_strfreev (argv);
        }

        if (retval == FALSE) {
                char *msg;
                msg = g_strdup_printf (_("Couldn't execute command: %s\n"
                                         "Verify that this is a valid command."),
                                       exec);

                acme_error (msg);
                g_free (msg);
        }
        g_free (exec);
}

static void
dialog_init (GsdMediaKeysManager *manager)
{
        if (manager->priv->dialog != NULL
            && !gsd_osd_window_is_valid (GSD_OSD_WINDOW (manager->priv->dialog))) {
                gtk_widget_destroy (manager->priv->dialog);
                manager->priv->dialog = NULL;
        }

        if (manager->priv->dialog == NULL) {
                manager->priv->dialog = gsd_media_keys_window_new ();
        }
}

static gboolean
is_valid_shortcut (const char *string)
{
        if (string == NULL || string[0] == '\0') {
                return FALSE;
        }
        if (strcmp (string, "disabled") == 0) {
                return FALSE;
        }

        return TRUE;
}

static void
update_kbd_cb (GSettings           *settings,
               const gchar         *key,
               GsdMediaKeysManager *manager)
{
        int      i;
        gboolean need_flush = TRUE;

        g_return_if_fail (key != NULL);

        gdk_error_trap_push ();

        /* Find the key that was modified */
        for (i = 0; i < HANDLED_KEYS; i++) {
                /* Skip over hard-coded keys */
                if (keys[i].settings_key == NULL)
                        continue;
                if (strcmp (key, keys[i].settings_key) == 0) {
                        char *tmp;
                        Key  *key;

                        if (keys[i].key != NULL) {
                                need_flush = TRUE;
                                grab_key_unsafe (keys[i].key, FALSE, manager->priv->screens);
                        }

                        g_free (keys[i].key);
                        keys[i].key = NULL;

                        /* We can't have a change in a hard-coded key */
                        g_assert (keys[i].settings_key != NULL);

                        tmp = g_settings_get_string (manager->priv->settings, keys[i].settings_key);
                        if (is_valid_shortcut (tmp) == FALSE) {
                                g_free (tmp);
                                break;
                        }

                        key = g_new0 (Key, 1);
                        if (egg_accelerator_parse_virtual (tmp, &key->keysym, &key->keycodes, &key->state) != EGG_PARSE_ERROR_NONE) {
                                g_free (tmp);
                                g_free (key);
                                break;
                        }

                        need_flush = TRUE;
                        grab_key_unsafe (key, TRUE, manager->priv->screens);
                        keys[i].key = key;

                        g_free (tmp);

                        break;
                }
        }

        if (need_flush)
                gdk_flush ();
        if (gdk_error_trap_pop ())
                g_warning ("Grab failed for some keys, another application may already have access the them.");
}

static void
init_kbd (GsdMediaKeysManager *manager)
{
        int i;
        gboolean need_flush = FALSE;

        gnome_settings_profile_start (NULL);

        gdk_error_trap_push ();

        for (i = 0; i < HANDLED_KEYS; i++) {
                char *tmp;
                Key  *key;
                EggParseError ret;

                if (keys[i].settings_key != NULL) {
                        tmp = g_settings_get_string (manager->priv->settings, keys[i].settings_key);
                } else {
                        tmp = g_strdup (keys[i].hard_coded);
                }

                if (!is_valid_shortcut (tmp)) {
                        if (*tmp != '\0')
                                g_debug ("Not a valid shortcut: '%s'", tmp);
                        g_free (tmp);
                        continue;
                }

                key = g_new0 (Key, 1);
                ret = egg_accelerator_parse_virtual (tmp, &key->keysym, &key->keycodes, &key->state);
                if (ret != EGG_PARSE_ERROR_NONE) {
                        if (keys[i].settings_key != NULL)
                                g_debug ("Unable to parse key '%s' for GSettings entry '%s' (%d)", tmp, keys[i].settings_key, ret);
                        else
                                g_debug ("Unable to parse hard-coded key '%s' (%d)", keys[i].hard_coded, ret);
                        g_free (tmp);
                        g_free (key);
                        continue;
                }

                g_free (tmp);

                keys[i].key = key;

                need_flush = TRUE;
                grab_key_unsafe (key, TRUE, manager->priv->screens);
        }

        if (need_flush)
                gdk_flush ();
        if (gdk_error_trap_pop ())
                g_warning ("Grab failed for some keys, another application may already have access the them.");

        gnome_settings_profile_end (NULL);
}

static void
dialog_show (GsdMediaKeysManager *manager)
{
        int            orig_w;
        int            orig_h;
        int            screen_w;
        int            screen_h;
        int            x;
        int            y;
        GtkRequisition win_req;
        GdkRectangle   geometry;
        int            monitor;

        gtk_window_set_screen (GTK_WINDOW (manager->priv->dialog),
                               manager->priv->current_screen);

        /*
         * get the window size
         * if the window hasn't been mapped, it doesn't necessarily
         * know its true size, yet, so we need to jump through hoops
         */
        gtk_window_get_default_size (GTK_WINDOW (manager->priv->dialog), &orig_w, &orig_h);
        gtk_widget_size_request (manager->priv->dialog, &win_req);

        if (win_req.width > orig_w)
                orig_w = win_req.width;
        if (win_req.height > orig_h)
                orig_h = win_req.height;

        monitor = gdk_screen_get_primary_monitor (manager->priv->current_screen);

        gdk_screen_get_monitor_geometry (manager->priv->current_screen,
                                         monitor,
                                         &geometry);

        screen_w = geometry.width;
        screen_h = geometry.height;

        x = ((screen_w - orig_w) / 2) + geometry.x;
        y = geometry.y + (screen_h / 2) + (screen_h / 2 - orig_h) / 2;

        gtk_window_move (GTK_WINDOW (manager->priv->dialog), x, y);

        gtk_widget_show (manager->priv->dialog);

        gdk_display_sync (gdk_screen_get_display (manager->priv->current_screen));
}

static void
launch_app (GAppInfo *app_info,
	    gint64    timestamp)
{
	GError *error = NULL;
        GdkAppLaunchContext *launch_context;

        /* setup the launch context so the startup notification is correct */
        launch_context = gdk_display_get_app_launch_context (gdk_display_get_default ());
        gdk_app_launch_context_set_timestamp (launch_context, timestamp);

	if (!g_app_info_launch (app_info, NULL, G_APP_LAUNCH_CONTEXT (launch_context), &error)) {
		g_warning ("Could not launch '%s': %s",
			   g_app_info_get_commandline (app_info),
			   error->message);
		g_error_free (error);
	}
        g_object_unref (launch_context);
}

static void
do_url_action (GsdMediaKeysManager *manager,
               const char          *scheme,
               gint64               timestamp)
{
        GAppInfo *app_info;

        app_info = g_app_info_get_default_for_uri_scheme (scheme);
        if (app_info != NULL) {
                launch_app (app_info, timestamp);
                g_object_unref (app_info);
        } else {
                g_warning ("Could not find default application for '%s' scheme", scheme);
	}
}

static void
do_media_action (GsdMediaKeysManager *manager,
		 gint64               timestamp)
{
        GAppInfo *app_info;

        app_info = g_app_info_get_default_for_type ("audio/x-vorbis+ogg", FALSE);
        if (app_info != NULL) {
                launch_app (app_info, timestamp);
                g_object_unref (app_info);
        } else {
                g_warning ("Could not find default application for '%s' mime-type", "audio/x-vorbis+ogg");
        }
}

static void
do_logout_action (GsdMediaKeysManager *manager)
{
        execute (manager, "gnome-session-quit --logout", FALSE, FALSE);
}

static void
do_eject_action_cb (GDrive              *drive,
                    GAsyncResult        *res,
                    GsdMediaKeysManager *manager)
{
        g_drive_eject_with_operation_finish (drive, res, NULL);
}

#define NO_SCORE 0
#define SCORE_CAN_EJECT 50
#define SCORE_HAS_MEDIA 100
static void
do_eject_action (GsdMediaKeysManager *manager)
{
        GList *drives, *l;
        GDrive *fav_drive;
        guint score;
        GVolumeMonitor *volume_monitor;

        volume_monitor = g_volume_monitor_get ();


        /* Find the best drive to eject */
        fav_drive = NULL;
        score = NO_SCORE;
        drives = g_volume_monitor_get_connected_drives (volume_monitor);
        for (l = drives; l != NULL; l = l->next) {
                GDrive *drive = l->data;

                if (g_drive_can_eject (drive) == FALSE)
                        continue;
                if (g_drive_is_media_removable (drive) == FALSE)
                        continue;
                if (score < SCORE_CAN_EJECT) {
                        fav_drive = drive;
                        score = SCORE_CAN_EJECT;
                }
                if (g_drive_has_media (drive) == FALSE)
                        continue;
                if (score < SCORE_HAS_MEDIA) {
                        fav_drive = drive;
                        score = SCORE_HAS_MEDIA;
                        break;
                }
        }

        /* Show the dialogue */
        dialog_init (manager);
        gsd_media_keys_window_set_action_custom (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                 "media-eject-symbolic",
                                                 FALSE);
        dialog_show (manager);

        /* Clean up the drive selection and exit if no suitable
         * drives are found */
        if (fav_drive != NULL)
                fav_drive = g_object_ref (fav_drive);

        g_list_foreach (drives, (GFunc) g_object_unref, NULL);
        if (fav_drive == NULL)
                return;

        /* Eject! */
        g_drive_eject_with_operation (fav_drive, G_MOUNT_UNMOUNT_FORCE,
                                      NULL, NULL,
                                      (GAsyncReadyCallback) do_eject_action_cb,
                                      manager);
        g_object_unref (fav_drive);
        g_object_unref (volume_monitor);
}

static void
do_home_key_action (GsdMediaKeysManager *manager,
		    gint64               timestamp)
{
	GFile *file;
	GError *error = NULL;
	char *uri;

	file = g_file_new_for_path (g_get_home_dir ());
	uri = g_file_get_uri (file);
	g_object_unref (file);

	if (gtk_show_uri (NULL, uri, timestamp, &error) == FALSE) {
		g_warning ("Failed to launch '%s': %s", uri, error->message);
		g_error_free (error);
	}
	g_free (uri);
}

static void
do_execute_desktop (GsdMediaKeysManager *manager,
		    const char          *desktop,
		    gint64               timestamp)
{
        GDesktopAppInfo *app_info;

        app_info = g_desktop_app_info_new (desktop);
        if (app_info != NULL) {
                launch_app (G_APP_INFO (app_info), timestamp);
                g_object_unref (app_info);
        } else {
                g_warning ("Could not find application '%s'", desktop);
	}
}

static void
do_touchpad_osd_action (GsdMediaKeysManager *manager, gboolean state)
{
        dialog_init (manager);
        gsd_media_keys_window_set_action_custom (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                 state ? "input-touchpad-symbolic" : "touchpad-disabled-symbolic",
                                                 FALSE);
        dialog_show (manager);
}

static void
do_touchpad_action (GsdMediaKeysManager *manager)
{
        GSettings *settings;
        gboolean state;

        if (touchpad_is_present () == FALSE) {
                do_touchpad_osd_action (manager, FALSE);
                return;
        }

        settings = g_settings_new (SETTINGS_TOUCHPAD_DIR);
        state = g_settings_get_boolean (settings, TOUCHPAD_ENABLED_KEY);

        do_touchpad_osd_action (manager, !state);

        g_settings_set_boolean (settings, TOUCHPAD_ENABLED_KEY, !state);
        g_object_unref (settings);
}

static void
update_dialog (GsdMediaKeysManager *manager,
               guint vol,
               gboolean muted,
               gboolean sound_changed,
               gboolean quiet)
{
        if (!muted) {
                vol = (int) (100 * (double) vol / PA_VOLUME_NORM);
                vol = CLAMP (vol, 0, 100);
        } else {
                vol = 0.0;
        }

        dialog_init (manager);
        gsd_media_keys_window_set_volume_muted (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                muted);
        gsd_media_keys_window_set_volume_level (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog), vol);
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_VOLUME);
        dialog_show (manager);

        if (quiet == FALSE && sound_changed != FALSE && muted == FALSE)
                ca_gtk_play_for_widget (manager->priv->dialog, 0,
                                        CA_PROP_EVENT_ID, "audio-volume-change",
                                        CA_PROP_EVENT_DESCRIPTION, "volume changed through key press",
                                        CA_PROP_APPLICATION_ID, "org.gnome.VolumeControl",
                                        NULL);
}

static void
do_sound_action (GsdMediaKeysManager *manager,
                 int                  type,
                 gboolean             quiet)
{
        gboolean old_muted, new_muted;
        guint old_vol, new_vol, norm_vol_step;
        gboolean sound_changed;

        if (manager->priv->stream == NULL)
                return;

        norm_vol_step = PA_VOLUME_NORM * VOLUME_STEP / 100;

        /* FIXME: this is racy */
        new_vol = old_vol = gvc_mixer_stream_get_volume (manager->priv->stream);
        new_muted = old_muted = gvc_mixer_stream_get_is_muted (manager->priv->stream);
        sound_changed = FALSE;

        switch (type) {
        case MUTE_KEY:
                new_muted = !old_muted;
                break;
        case VOLUME_DOWN_KEY:
                if (old_vol <= norm_vol_step) {
                        new_vol = 0;
                        new_muted = TRUE;
                } else {
                        new_vol = old_vol - norm_vol_step;
                }
                break;
        case VOLUME_UP_KEY:
                new_muted = FALSE;
                /* When coming out of mute only increase the volume if it was 0 */
                if (!old_muted || old_vol == 0)
                        new_vol = MIN (old_vol + norm_vol_step, MAX_VOLUME);
                break;
        }

        if (old_muted != new_muted) {
                gvc_mixer_stream_change_is_muted (manager->priv->stream, new_muted);
                sound_changed = TRUE;
        }

        if (old_vol != new_vol) {
                if (gvc_mixer_stream_set_volume (manager->priv->stream, new_vol) != FALSE) {
                        gvc_mixer_stream_push_volume (manager->priv->stream);
                        sound_changed = TRUE;
                }
        }

        update_dialog (manager, new_vol, new_muted, sound_changed, quiet);
}

static void
update_default_sink (GsdMediaKeysManager *manager)
{
        GvcMixerStream *stream;

        stream = gvc_mixer_control_get_default_sink (manager->priv->volume);
        if (stream == manager->priv->stream)
                return;

        if (manager->priv->stream != NULL) {
                g_object_unref (manager->priv->stream);
                manager->priv->stream = NULL;
        }

        if (stream != NULL) {
                manager->priv->stream = g_object_ref (stream);
        } else {
                g_warning ("Unable to get default sink");
        }
}

static void
on_control_ready (GvcMixerControl     *control,
                  GsdMediaKeysManager *manager)
{
        update_default_sink (manager);
}

static void
on_control_default_sink_changed (GvcMixerControl     *control,
                                 guint                id,
                                 GsdMediaKeysManager *manager)
{
        update_default_sink (manager);
}

static void
on_control_stream_removed (GvcMixerControl     *control,
                           guint                id,
                           GsdMediaKeysManager *manager)
{
        if (manager->priv->stream != NULL) {
		if (gvc_mixer_stream_get_id (manager->priv->stream) == id) {
	                g_object_unref (manager->priv->stream);
			manager->priv->stream = NULL;
		}
        }
}

static void
free_media_player (MediaPlayer *player)
{
        if (player->watch_id > 0) {
                g_bus_unwatch_name (player->watch_id);
                player->watch_id = 0;
        }
        g_free (player->application);
        g_free (player->name);
        g_free (player);
}

static gint
find_by_application (gconstpointer a,
                     gconstpointer b)
{
        return strcmp (((MediaPlayer *)a)->application, b);
}

static gint
find_by_name (gconstpointer a,
              gconstpointer b)
{
        return strcmp (((MediaPlayer *)a)->name, b);
}

static gint
find_by_time (gconstpointer a,
              gconstpointer b)
{
        return ((MediaPlayer *)a)->time < ((MediaPlayer *)b)->time;
}

static void
name_vanished_handler (GDBusConnection     *connection,
                       const gchar         *name,
                       GsdMediaKeysManager *manager)
{
        GList *iter;

        iter = g_list_find_custom (manager->priv->media_players,
                                   name,
                                   find_by_name);

        if (iter != NULL) {
                MediaPlayer *player;

                player = iter->data;
                g_debug ("Deregistering vanished %s (name: %s)", player->application, player->name);
                free_media_player (player);
                manager->priv->media_players = g_list_delete_link (manager->priv->media_players, iter);
        }
}

/*
 * Register a new media player. Most applications will want to call
 * this with time = GDK_CURRENT_TIME. This way, the last registered
 * player will receive media events. In some cases, applications
 * may want to register with a lower priority (usually 1), to grab
 * events only nobody is interested.
 */
static void
gsd_media_keys_manager_grab_media_player_keys (GsdMediaKeysManager *manager,
                                               const char          *application,
                                               const char          *name,
                                               guint32              time)
{
        GList       *iter;
        MediaPlayer *media_player;
        guint        watch_id;

        if (time == GDK_CURRENT_TIME) {
                GTimeVal tv;

                g_get_current_time (&tv);
                time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
        }

        iter = g_list_find_custom (manager->priv->media_players,
                                   application,
                                   find_by_application);

        if (iter != NULL) {
                if (((MediaPlayer *)iter->data)->time < time) {
                        MediaPlayer *player = iter->data;
                        free_media_player (player);
                        manager->priv->media_players = g_list_delete_link (manager->priv->media_players, iter);
                } else {
                        return;
                }
        }

        watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                     name,
                                     G_BUS_NAME_WATCHER_FLAGS_NONE,
                                     NULL,
                                     (GBusNameVanishedCallback) name_vanished_handler,
                                     manager,
                                     NULL);

        g_debug ("Registering %s at %u", application, time);
        media_player = g_new0 (MediaPlayer, 1);
        media_player->application = g_strdup (application);
        media_player->name = g_strdup (name);
        media_player->time = time;
        media_player->watch_id = watch_id;

        manager->priv->media_players = g_list_insert_sorted (manager->priv->media_players,
                                                             media_player,
                                                             find_by_time);
}

static void
gsd_media_keys_manager_release_media_player_keys (GsdMediaKeysManager *manager,
                                                  const char          *application,
                                                  const char          *name)
{
        GList *iter = NULL;

        g_return_if_fail (application != NULL || name != NULL);

        if (application != NULL) {
                iter = g_list_find_custom (manager->priv->media_players,
                                           application,
                                           find_by_application);
        }

        if (iter == NULL && name != NULL) {
                iter = g_list_find_custom (manager->priv->media_players,
                                           name,
                                           find_by_name);
        }

        if (iter != NULL) {
                MediaPlayer *player;

                player = iter->data;
                g_debug ("Deregistering %s (name: %s)", application, player->name);
                free_media_player (player);
                manager->priv->media_players = g_list_delete_link (manager->priv->media_players, iter);
        }
}

static gboolean
gsd_media_player_key_pressed (GsdMediaKeysManager *manager,
                              const char          *key)
{
        const char  *application;
        gboolean     have_listeners;
        GError      *error = NULL;
        MediaPlayer *player;

        g_return_val_if_fail (key != NULL, FALSE);

        g_debug ("Media key '%s' pressed", key);

        have_listeners = (manager->priv->media_players != NULL);

        if (!have_listeners) {
                /* Popup a dialog with an (/) icon */
                dialog_init (manager);
                gsd_media_keys_window_set_action_custom (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                         "action-unavailable-symbolic",
                                                         FALSE);
                dialog_show (manager);
                return TRUE;
        }

        player = manager->priv->media_players->data;
        application = player->application;

        if (g_dbus_connection_emit_signal (manager->priv->connection,
                                           player->name,
                                           GSD_MEDIA_KEYS_DBUS_PATH,
                                           GSD_MEDIA_KEYS_DBUS_NAME,
                                           "MediaPlayerKeyPressed",
                                           g_variant_new ("(ss)", application ? application : "", key),
                                           &error) == FALSE) {
                g_debug ("Error emitting signal: %s", error->message);
                g_error_free (error);
        }

        return !have_listeners;
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
        GsdMediaKeysManager *manager = (GsdMediaKeysManager *) user_data;

        g_debug ("Calling method '%s' for media-keys", method_name);

        if (g_strcmp0 (method_name, "ReleaseMediaPlayerKeys") == 0) {
                const char *app_name;

                g_variant_get (parameters, "(&s)", &app_name);
                gsd_media_keys_manager_release_media_player_keys (manager, app_name, sender);
                g_dbus_method_invocation_return_value (invocation, NULL);
        } else if (g_strcmp0 (method_name, "GrabMediaPlayerKeys") == 0) {
                const char *app_name;
                guint32 time;

                g_variant_get (parameters, "(&su)", &app_name, &time);
                gsd_media_keys_manager_grab_media_player_keys (manager, app_name, sender, time);
                g_dbus_method_invocation_return_value (invocation, NULL);
        }
}

static const GDBusInterfaceVTable interface_vtable =
{
        handle_method_call,
        NULL, /* Get Property */
        NULL, /* Set Property */
};

static gboolean
do_multimedia_player_action (GsdMediaKeysManager *manager,
                             const char          *key)
{
        return gsd_media_player_key_pressed (manager, key);
}

static void
on_xrandr_action_call_finished (GObject             *source_object,
                                GAsyncResult        *res,
                                GsdMediaKeysManager *manager)
{
        GError *error = NULL;
        GVariant *variant;
        char *action;

        action = g_object_get_data (G_OBJECT (source_object),
                                    "gsd-media-keys-manager-xrandr-action");

        variant = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &error);

        g_object_unref (manager->priv->cancellable);
        manager->priv->cancellable = NULL;

        if (error != NULL) {
                g_warning ("Unable to call '%s': %s", action, error->message);
                g_error_free (error);
        } else {
                g_variant_unref (variant);
        }

        g_free (action);
}

static void
do_xrandr_action (GsdMediaKeysManager *manager,
                  const char          *action,
                  gint64               timestamp)
{
        GsdMediaKeysManagerPrivate *priv = manager->priv;

        if (priv->connection == NULL || priv->xrandr_proxy == NULL) {
                g_warning ("No existing D-Bus connection trying to handle XRANDR keys");
                return;
        }

        if (priv->cancellable != NULL) {
                g_debug ("xrandr action already in flight");
                return;
        }

        priv->cancellable = g_cancellable_new ();

        g_object_set_data (G_OBJECT (priv->xrandr_proxy),
                           "gsd-media-keys-manager-xrandr-action",
                           g_strdup (action));

        g_dbus_proxy_call (priv->xrandr_proxy,
                           action,
                           g_variant_new ("(x)", timestamp),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           priv->cancellable,
                           (GAsyncReadyCallback) on_xrandr_action_call_finished,
                           manager);
}

static gboolean
do_video_out_action (GsdMediaKeysManager *manager,
                     gint64               timestamp)
{
        do_xrandr_action (manager, "VideoModeSwitch", timestamp);
        return FALSE;
}

static gboolean
do_video_rotate_action (GsdMediaKeysManager *manager,
                        gint64               timestamp)
{
        do_xrandr_action (manager, "Rotate", timestamp);
        return FALSE;
}

static void
do_toggle_accessibility_key (const char *key)
{
        GSettings *settings;
        gboolean state;

        settings = g_settings_new ("org.gnome.desktop.a11y.applications");
        state = g_settings_get_boolean (settings, key);
        g_settings_set_boolean (settings, key, !state);
        g_object_unref (settings);
}

static void
do_magnifier_action (GsdMediaKeysManager *manager)
{
        do_toggle_accessibility_key ("screen-magnifier-enabled");
}

static void
do_screenreader_action (GsdMediaKeysManager *manager)
{
        do_toggle_accessibility_key ("screen-reader-enabled");
}

static void
do_on_screen_keyboard_action (GsdMediaKeysManager *manager)
{
        do_toggle_accessibility_key ("screen-keyboard-enabled");
}

static void
do_text_size_action (GsdMediaKeysManager *manager,
		     MediaKeyType         type)
{
	gdouble factor, best, distance;
	guint i;

	/* Same values used in the Seeing tab of the Universal Access panel */
	static gdouble factors[] = {
		0.75,
		1.0,
		1.25,
		1.5
	};

	/* Figure out the current DPI scaling factor */
	factor = g_settings_get_double (manager->priv->interface_settings, "text-scaling-factor");
	factor += (type == INCREASE_TEXT_KEY ? 0.25 : -0.25);

	/* Try to find a matching value */
	distance = 1e6;
	best = 1.0;
	for (i = 0; i < G_N_ELEMENTS(factors); i++) {
		gdouble d;
		d = fabs (factor - factors[i]);
		if (d < distance) {
			best = factors[i];
			distance = d;
		}
	}

	if (best == 1.0)
		g_settings_reset (manager->priv->interface_settings, "text-scaling-factor");
	else
		g_settings_set_double (manager->priv->interface_settings, "text-scaling-factor", best);
}

static void
do_magnifier_zoom_action (GsdMediaKeysManager *manager,
			  MediaKeyType         type)
{
	GSettings *settings;
	gdouble offset, value;

	if (type == MAGNIFIER_ZOOM_IN_KEY)
		offset = 1.0;
	else
		offset = -1.0;

	settings = g_settings_new ("org.gnome.desktop.a11y.magnifier");
	value = g_settings_get_double (settings, "mag-factor");
	value += offset;
	value = roundl (value);
	g_settings_set_double (settings, "mag-factor", value);
	g_object_unref (settings);
}

static void
do_toggle_contrast_action (GsdMediaKeysManager *manager)
{
	gboolean high_contrast;
	char *theme;

	/* Are we using HighContrast now? */
	theme = g_settings_get_string (manager->priv->interface_settings, "gtk-theme");
	high_contrast = g_str_equal (theme, HIGH_CONTRAST);
	g_free (theme);

	if (high_contrast != FALSE) {
		if (manager->priv->gtk_theme == NULL)
			g_settings_reset (manager->priv->interface_settings, "gtk-theme");
		else
			g_settings_set (manager->priv->interface_settings, "gtk-theme", manager->priv->gtk_theme);
		g_settings_set (manager->priv->interface_settings, "icon-theme", manager->priv->icon_theme);
	} else {
		g_settings_set (manager->priv->interface_settings, "gtk-theme", HIGH_CONTRAST);
		g_settings_set (manager->priv->interface_settings, "icon-theme", HIGH_CONTRAST);
	}
}

static void
gnome_session_shutdown_cb (GObject *source_object,
                           GAsyncResult *res,
                           gpointer user_data)
{
        GVariant *result;
        GError *error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL) {
                g_warning ("couldn't shutdown using gnome-session: %s",
                           error->message);
                g_error_free (error);
        } else {
                g_variant_unref (result);
        }
}

static void
gnome_session_shutdown (void)
{
        GError *error = NULL;
        GDBusProxy *proxy;

        /* ask gnome-session to show the shutdown dialog with a timeout */
        proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                               G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                               NULL,
                                               GNOME_SESSION_DBUS_NAME,
                                               GNOME_SESSION_DBUS_PATH,
                                               GNOME_SESSION_DBUS_INTERFACE,
                                               NULL, &error);
        if (proxy == NULL) {
                g_warning ("cannot connect to gnome-session: %s",
                           error->message);
                g_error_free (error);
                return;
        }
        g_dbus_proxy_call (proxy,
                           "Shutdown",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1, NULL,
                           gnome_session_shutdown_cb, NULL);
        g_object_unref (proxy);
}

static void
upower_sleep_cb (GObject *source_object,
                 GAsyncResult *res,
                 gpointer user_data)
{
        GVariant *result;
        GError *error = NULL;

        result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                           res,
                                           &error);
        if (result == NULL) {
                g_warning ("couldn't sleep using UPower: %s",
                           error->message);
                g_error_free (error);
        } else {
                g_variant_unref (result);
        }
}

static void
do_config_power_action (GsdMediaKeysManager *manager,
                        const gchar *config_key)
{
        GsdPowerActionType action_type;

        action_type = g_settings_get_enum (manager->priv->power_settings,
                                           config_key);
        switch (action_type) {
        case GSD_POWER_ACTION_SUSPEND:
                g_dbus_proxy_call (manager->priv->upower_proxy,
                                   "Suspend",
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1, NULL,
                                   upower_sleep_cb, NULL);
                break;
        case GSD_POWER_ACTION_INTERACTIVE:
        case GSD_POWER_ACTION_SHUTDOWN:
                gnome_session_shutdown ();
                break;
        case GSD_POWER_ACTION_HIBERNATE:
                g_dbus_proxy_call (manager->priv->upower_proxy,
                                   "Hibernate",
                                   NULL,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1, NULL,
                                   upower_sleep_cb, NULL);
                break;
        case GSD_POWER_ACTION_BLANK:
        case GSD_POWER_ACTION_NOTHING:
                /* these actions cannot be handled by media-keys and
                 * are not used in this context */
                break;
        }
}

static void
update_screen_cb (GObject             *source_object,
                  GAsyncResult        *res,
                  gpointer             user_data)
{
        GError *error = NULL;
        guint percentage;
        GVariant *new_percentage;
        GsdMediaKeysManager *manager = GSD_MEDIA_KEYS_MANAGER (user_data);

        new_percentage = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                                   res, &error);
        if (new_percentage == NULL) {
                g_warning ("Failed to set new screen percentage: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        /* update the dialog with the new value */
        g_variant_get (new_percentage, "(u)", &percentage);
        dialog_init (manager);
        gsd_media_keys_window_set_action_custom (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                 "display-brightness-symbolic",
                                                 TRUE);
        gsd_media_keys_window_set_volume_level (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                percentage);
        dialog_show (manager);
        g_variant_unref (new_percentage);
}

static void
do_screen_brightness_action (GsdMediaKeysManager *manager,
                             MediaKeyType type)
{
        if (manager->priv->connection == NULL ||
            manager->priv->power_screen_proxy == NULL) {
                g_warning ("No existing D-Bus connection trying to handle power keys");
                return;
        }

        /* call into the power plugin */
        g_dbus_proxy_call (manager->priv->power_screen_proxy,
                           type == SCREEN_BRIGHTNESS_UP_KEY ? "StepUp" : "StepDown",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL,
                           update_screen_cb,
                           manager);
}

static void
update_keyboard_cb (GObject             *source_object,
                    GAsyncResult        *res,
                    gpointer             user_data)
{
        GError *error = NULL;
        guint percentage;
        GVariant *new_percentage;
        GsdMediaKeysManager *manager = GSD_MEDIA_KEYS_MANAGER (user_data);

        new_percentage = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object),
                                                   res, &error);
        if (new_percentage == NULL) {
                g_warning ("Failed to set new keyboard percentage: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        /* update the dialog with the new value */
        g_variant_get (new_percentage, "(u)", &percentage);
        dialog_init (manager);
        gsd_media_keys_window_set_action_custom (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                 "keyboard-brightness-symbolic",
                                                 TRUE);
        gsd_media_keys_window_set_volume_level (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                percentage);
        dialog_show (manager);
        g_variant_unref (new_percentage);
}

static void
do_keyboard_brightness_action (GsdMediaKeysManager *manager,
                               MediaKeyType type)
{
        if (manager->priv->connection == NULL ||
            manager->priv->power_keyboard_proxy == NULL) {
                g_warning ("No existing D-Bus connection trying to handle power keys");
                return;
        }

        /* call into the power plugin */
        g_dbus_proxy_call (manager->priv->power_keyboard_proxy,
                           type == KEYBOARD_BRIGHTNESS_UP_KEY ? "StepUp" : "StepDown",
                           NULL,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL,
                           update_keyboard_cb,
                           manager);
}

static gboolean
do_action (GsdMediaKeysManager *manager,
           MediaKeyType         type,
           gint64               timestamp)
{
        char *cmd;

        g_debug ("Launching action for key type '%d'", type);

        switch (type) {
        case TOUCHPAD_KEY:
                do_touchpad_action (manager);
                break;
        case TOUCHPAD_ON_KEY:
                do_touchpad_osd_action (manager, TRUE);
                break;
        case TOUCHPAD_OFF_KEY:
                do_touchpad_osd_action (manager, FALSE);
                break;
        case MUTE_KEY:
        case VOLUME_DOWN_KEY:
        case VOLUME_UP_KEY:
                do_sound_action (manager, type, FALSE);
                break;
        case MUTE_QUIET_KEY:
                do_sound_action (manager, MUTE_KEY, TRUE);
                break;
        case VOLUME_DOWN_QUIET_KEY:
                do_sound_action (manager, VOLUME_DOWN_KEY, TRUE);
                break;
        case VOLUME_UP_QUIET_KEY:
                do_sound_action (manager, VOLUME_UP_KEY, TRUE);
                break;
        case LOGOUT_KEY:
                do_logout_action (manager);
                break;
        case EJECT_KEY:
                do_eject_action (manager);
                break;
        case HOME_KEY:
                do_home_key_action (manager, timestamp);
                break;
        case SEARCH_KEY:
                cmd = NULL;
                if ((cmd = g_find_program_in_path ("tracker-search-tool")))
                        do_execute_desktop (manager, "tracker-needle.desktop", timestamp);
                else
                        do_execute_desktop (manager, "gnome-search-tool.desktop", timestamp);
                g_free (cmd);
                break;
        case EMAIL_KEY:
                do_url_action (manager, "mailto", timestamp);
                break;
        case SCREENSAVER_KEY:
        case SCREENSAVER2_KEY:
                execute (manager, "gnome-screensaver-command --lock", FALSE, FALSE);
                break;
        case HELP_KEY:
                do_url_action (manager, "ghelp", timestamp);
                break;
        case WWW_KEY:
                do_url_action (manager, "http", timestamp);
                break;
        case MEDIA_KEY:
                do_media_action (manager, timestamp);
                break;
        case CALCULATOR_KEY:
                do_execute_desktop (manager, "gcalctool.desktop", timestamp);
                break;
        case PLAY_KEY:
                return do_multimedia_player_action (manager, "Play");
        case PAUSE_KEY:
                return do_multimedia_player_action (manager, "Pause");
        case STOP_KEY:
                return do_multimedia_player_action (manager, "Stop");
        case PREVIOUS_KEY:
                return do_multimedia_player_action (manager, "Previous");
        case NEXT_KEY:
                return do_multimedia_player_action (manager, "Next");
        case REWIND_KEY:
                return do_multimedia_player_action (manager, "Rewind");
        case FORWARD_KEY:
                return do_multimedia_player_action (manager, "FastForward");
        case REPEAT_KEY:
                return do_multimedia_player_action (manager, "Repeat");
        case RANDOM_KEY:
                return do_multimedia_player_action (manager, "Shuffle");
        case VIDEO_OUT_KEY:
        case VIDEO_OUT2_KEY:
                do_video_out_action (manager, timestamp);
                break;
        case ROTATE_VIDEO_KEY:
                do_video_rotate_action (manager, timestamp);
                break;
        case MAGNIFIER_KEY:
                do_magnifier_action (manager);
                break;
        case SCREENREADER_KEY:
                do_screenreader_action (manager);
                break;
        case ON_SCREEN_KEYBOARD_KEY:
                do_on_screen_keyboard_action (manager);
                break;
	case INCREASE_TEXT_KEY:
	case DECREASE_TEXT_KEY:
		do_text_size_action (manager, type);
		break;
	case MAGNIFIER_ZOOM_IN_KEY:
	case MAGNIFIER_ZOOM_OUT_KEY:
		do_magnifier_zoom_action (manager, type);
		break;
	case TOGGLE_CONTRAST_KEY:
		do_toggle_contrast_action (manager);
		break;
        case POWER_KEY:
                do_config_power_action (manager, "button-power");
                break;
        case SLEEP_KEY:
                do_config_power_action (manager, "button-sleep");
                break;
        case SUSPEND_KEY:
                do_config_power_action (manager, "button-suspend");
                break;
        case HIBERNATE_KEY:
                do_config_power_action (manager, "button-hibernate");
                break;
        case SCREEN_BRIGHTNESS_UP_KEY:
        case SCREEN_BRIGHTNESS_DOWN_KEY:
                do_screen_brightness_action (manager, type);
                break;
        case KEYBOARD_BRIGHTNESS_UP_KEY:
        case KEYBOARD_BRIGHTNESS_DOWN_KEY:
        case KEYBOARD_BRIGHTNESS_TOGGLE_KEY:
                do_keyboard_brightness_action (manager, type);
                break;
        case BATTERY_KEY:
                do_execute_desktop (manager, "gnome-power-statistics.desktop", timestamp);
                break;
        case HANDLED_KEYS:
                g_assert_not_reached ();
        /* Note, no default so compiler catches missing keys */
        }

        return FALSE;
}

static GdkScreen *
acme_get_screen_from_event (GsdMediaKeysManager *manager,
                            XAnyEvent           *xanyev)
{
        GdkWindow *window;
        GdkScreen *screen;
        GSList    *l;

        /* Look for which screen we're receiving events */
        for (l = manager->priv->screens; l != NULL; l = l->next) {
                screen = (GdkScreen *) l->data;
                window = gdk_screen_get_root_window (screen);

                if (GDK_WINDOW_XID (window) == xanyev->window) {
                        return screen;
                }
        }

        return NULL;
}

static GdkFilterReturn
acme_filter_events (GdkXEvent           *xevent,
                    GdkEvent            *event,
                    GsdMediaKeysManager *manager)
{
        XEvent    *xev = (XEvent *) xevent;
        XAnyEvent *xany = (XAnyEvent *) xevent;
        int        i;

        /* verify we have a key event */
        if (xev->type != KeyPress && xev->type != KeyRelease) {
                return GDK_FILTER_CONTINUE;
        }

        for (i = 0; i < HANDLED_KEYS; i++) {
                if (match_key (keys[i].key, xev)) {
                        switch (keys[i].key_type) {
                        case VOLUME_DOWN_KEY:
                        case VOLUME_UP_KEY:
                        case VOLUME_DOWN_QUIET_KEY:
                        case VOLUME_UP_QUIET_KEY:
                                /* auto-repeatable keys */
                                if (xev->type != KeyPress) {
                                        return GDK_FILTER_CONTINUE;
                                }
                                break;
                        default:
                                if (xev->type != KeyRelease) {
                                        return GDK_FILTER_CONTINUE;
                                }
                        }

                        manager->priv->current_screen = acme_get_screen_from_event (manager, xany);

                        if (do_action (manager, keys[i].key_type, xev->xkey.time) == FALSE) {
                                return GDK_FILTER_REMOVE;
                        } else {
                                return GDK_FILTER_CONTINUE;
                        }
                }
        }

        return GDK_FILTER_CONTINUE;
}

static void
update_theme_settings (GSettings           *settings,
		       const char          *key,
		       GsdMediaKeysManager *manager)
{
	char *theme;

	theme = g_settings_get_string (manager->priv->interface_settings, key);
	if (g_str_equal (theme, HIGH_CONTRAST)) {
		g_free (theme);
	} else {
		if (g_str_equal (key, "gtk-theme")) {
			g_free (manager->priv->gtk_theme);
			manager->priv->gtk_theme = theme;
		} else {
			g_free (manager->priv->icon_theme);
			manager->priv->icon_theme = theme;
		}
	}
}

static gboolean
start_media_keys_idle_cb (GsdMediaKeysManager *manager)
{
        GSList *l;

        g_debug ("Starting media_keys manager");
        gnome_settings_profile_start (NULL);
        manager->priv->settings = g_settings_new (SETTINGS_BINDING_DIR);
        g_signal_connect (G_OBJECT (manager->priv->settings), "changed",
                          G_CALLBACK (update_kbd_cb), manager);

        /* for the power plugin interface code */
        manager->priv->power_settings = g_settings_new (SETTINGS_POWER_DIR);

        /* Logic from http://git.gnome.org/browse/gnome-shell/tree/js/ui/status/accessibility.js#n163 */
        manager->priv->interface_settings = g_settings_new (SETTINGS_INTERFACE_DIR);
        g_signal_connect (G_OBJECT (manager->priv->interface_settings), "changed::gtk-theme",
			  G_CALLBACK (update_theme_settings), manager);
        g_signal_connect (G_OBJECT (manager->priv->interface_settings), "changed::icon-theme",
			  G_CALLBACK (update_theme_settings), manager);
	manager->priv->gtk_theme = g_settings_get_string (manager->priv->interface_settings, "gtk-theme");
	if (g_str_equal (manager->priv->gtk_theme, HIGH_CONTRAST)) {
		g_free (manager->priv->gtk_theme);
		manager->priv->gtk_theme = NULL;
	}
	manager->priv->icon_theme = g_settings_get_string (manager->priv->interface_settings, "icon-theme");

        init_screens (manager);
        init_kbd (manager);

        /* Start filtering the events */
        for (l = manager->priv->screens; l != NULL; l = l->next) {
                gnome_settings_profile_start ("gdk_window_add_filter");

                g_debug ("adding key filter for screen: %d",
                         gdk_screen_get_number (l->data));

                gdk_window_add_filter (gdk_screen_get_root_window (l->data),
                                       (GdkFilterFunc)acme_filter_events,
                                       manager);
                gnome_settings_profile_end ("gdk_window_add_filter");
        }

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_media_keys_manager_start (GsdMediaKeysManager *manager,
                              GError             **error)
{
        gnome_settings_profile_start (NULL);

        /* initialise Volume handler
         *
         * We do this one here to force checking gstreamer cache, etc.
         * The rest (grabbing and setting the keys) can happen in an
         * idle.
         */
        gnome_settings_profile_start ("gvc_mixer_control_new");

        manager->priv->volume = gvc_mixer_control_new ("GNOME Volume Control Media Keys");

        g_signal_connect (manager->priv->volume,
                          "ready",
                          G_CALLBACK (on_control_ready),
                          manager);
        g_signal_connect (manager->priv->volume,
                          "default-sink-changed",
                          G_CALLBACK (on_control_default_sink_changed),
                          manager);
        g_signal_connect (manager->priv->volume,
                          "stream-removed",
                          G_CALLBACK (on_control_stream_removed),
                          manager);

        gvc_mixer_control_open (manager->priv->volume);

        gnome_settings_profile_end ("gvc_mixer_control_new");

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) start_media_keys_idle_cb, manager);

        register_manager (manager_object);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_media_keys_manager_stop (GsdMediaKeysManager *manager)
{
        GsdMediaKeysManagerPrivate *priv = manager->priv;
        GSList *ls;
        GList *l;
        int i;
        gboolean need_flush;

        g_debug ("Stopping media_keys manager");

        if (priv->bus_cancellable != NULL) {
                g_cancellable_cancel (priv->bus_cancellable);
                g_object_unref (priv->bus_cancellable);
                priv->bus_cancellable = NULL;
        }

        for (ls = priv->screens; ls != NULL; ls = ls->next) {
                gdk_window_remove_filter (gdk_screen_get_root_window (ls->data),
                                          (GdkFilterFunc) acme_filter_events,
                                          manager);
        }

        if (priv->settings) {
                g_object_unref (priv->settings);
                priv->settings = NULL;
        }

        if (priv->power_settings) {
                g_object_unref (priv->power_settings);
                priv->power_settings = NULL;
        }

        if (priv->power_screen_proxy) {
                g_object_unref (priv->power_screen_proxy);
                priv->power_screen_proxy = NULL;
        }

        if (priv->power_keyboard_proxy) {
                g_object_unref (priv->power_keyboard_proxy);
                priv->power_keyboard_proxy = NULL;
        }

        if (priv->upower_proxy) {
                g_object_unref (priv->upower_proxy);
                priv->upower_proxy = NULL;
        }

        if (priv->cancellable != NULL) {
                g_cancellable_cancel (priv->cancellable);
                g_object_unref (priv->cancellable);
                priv->cancellable = NULL;
        }

        if (priv->introspection_data) {
                g_dbus_node_info_unref (priv->introspection_data);
                priv->introspection_data = NULL;
        }

        if (priv->connection != NULL) {
                g_object_unref (priv->connection);
                priv->connection = NULL;
        }

        need_flush = FALSE;
        gdk_error_trap_push ();

        for (i = 0; i < HANDLED_KEYS; ++i) {
                if (keys[i].key) {
                        need_flush = TRUE;
                        grab_key_unsafe (keys[i].key, FALSE, priv->screens);

                        g_free (keys[i].key->keycodes);
                        g_free (keys[i].key);
                        keys[i].key = NULL;
                }
        }

        if (need_flush)
                gdk_flush ();
        gdk_error_trap_pop_ignored ();

        if (priv->screens != NULL) {
                g_slist_free (priv->screens);
                priv->screens = NULL;
        }

        if (priv->stream) {
                g_object_unref (priv->stream);
                priv->stream = NULL;
        }

        if (priv->volume) {
                g_object_unref (priv->volume);
                priv->volume = NULL;
        }

        if (priv->dialog != NULL) {
                gtk_widget_destroy (priv->dialog);
                priv->dialog = NULL;
        }

        if (priv->media_players != NULL) {
                for (l = priv->media_players; l; l = l->next) {
                        MediaPlayer *mp = l->data;
                        g_free (mp->application);
                        g_free (mp);
                }
                g_list_free (priv->media_players);
                priv->media_players = NULL;
        }
}

static void
gsd_media_keys_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_media_keys_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_media_keys_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdMediaKeysManager      *media_keys_manager;

        media_keys_manager = GSD_MEDIA_KEYS_MANAGER (G_OBJECT_CLASS (gsd_media_keys_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (media_keys_manager);
}

static void
gsd_media_keys_manager_class_init (GsdMediaKeysManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_media_keys_manager_get_property;
        object_class->set_property = gsd_media_keys_manager_set_property;
        object_class->constructor = gsd_media_keys_manager_constructor;
        object_class->finalize = gsd_media_keys_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdMediaKeysManagerPrivate));
}

static void
gsd_media_keys_manager_init (GsdMediaKeysManager *manager)
{
        manager->priv = GSD_MEDIA_KEYS_MANAGER_GET_PRIVATE (manager);
}

static void
gsd_media_keys_manager_finalize (GObject *object)
{
        GsdMediaKeysManager *media_keys_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_MEDIA_KEYS_MANAGER (object));

        media_keys_manager = GSD_MEDIA_KEYS_MANAGER (object);

        g_return_if_fail (media_keys_manager->priv != NULL);

        if (media_keys_manager->priv->start_idle_id != 0)
                g_source_remove (media_keys_manager->priv->start_idle_id);

        G_OBJECT_CLASS (gsd_media_keys_manager_parent_class)->finalize (object);
}

static void
xrandr_ready_cb (GObject             *source_object,
                 GAsyncResult        *res,
                 GsdMediaKeysManager *manager)
{
        GError *error = NULL;

        manager->priv->xrandr_proxy = g_dbus_proxy_new_finish (res, &error);
        if (manager->priv->xrandr_proxy == NULL) {
                g_warning ("Failed to get proxy for XRandR operations: %s", error->message);
                g_error_free (error);
        }
}

static void
upower_ready_cb (GObject             *source_object,
                 GAsyncResult        *res,
                 GsdMediaKeysManager *manager)
{
        GError *error = NULL;

        manager->priv->upower_proxy = g_dbus_proxy_new_finish (res, &error);
        if (manager->priv->upower_proxy == NULL) {
                g_warning ("Failed to get proxy for upower: %s",
                           error->message);
                g_error_free (error);
        }
}

static void
power_screen_ready_cb (GObject             *source_object,
                       GAsyncResult        *res,
                       GsdMediaKeysManager *manager)
{
        GError *error = NULL;

        manager->priv->power_screen_proxy = g_dbus_proxy_new_finish (res, &error);
        if (manager->priv->power_screen_proxy == NULL) {
                g_warning ("Failed to get proxy for power (screen): %s",
                           error->message);
                g_error_free (error);
        }
}

static void
power_keyboard_ready_cb (GObject             *source_object,
                         GAsyncResult        *res,
                         GsdMediaKeysManager *manager)
{
        GError *error = NULL;

        manager->priv->power_keyboard_proxy = g_dbus_proxy_new_finish (res, &error);
        if (manager->priv->power_keyboard_proxy == NULL) {
                g_warning ("Failed to get proxy for power (keyboard): %s",
                           error->message);
                g_error_free (error);
        }
}

static void
on_bus_gotten (GObject             *source_object,
               GAsyncResult        *res,
               GsdMediaKeysManager *manager)
{
        GDBusConnection *connection;
        GError *error = NULL;

        if (manager->priv->bus_cancellable == NULL ||
            g_cancellable_is_cancelled (manager->priv->bus_cancellable)) {
                g_warning ("Operation has been cancelled, so not retrieving session bus");
                return;
        }

        connection = g_bus_get_finish (res, &error);
        if (connection == NULL) {
                g_warning ("Could not get session bus: %s", error->message);
                g_error_free (error);
                return;
        }
        manager->priv->connection = connection;

        g_dbus_connection_register_object (connection,
                                           GSD_MEDIA_KEYS_DBUS_PATH,
                                           manager->priv->introspection_data->interfaces[0],
                                           &interface_vtable,
                                           manager,
                                           NULL,
                                           NULL);

        g_dbus_proxy_new (manager->priv->connection,
                          G_DBUS_PROXY_FLAGS_NONE,
                          NULL,
                          "org.gnome.SettingsDaemon",
                          "/org/gnome/SettingsDaemon/XRANDR",
                          "org.gnome.SettingsDaemon.XRANDR_2",
                          NULL,
                          (GAsyncReadyCallback) xrandr_ready_cb,
                          manager);

        g_dbus_proxy_new (manager->priv->connection,
                          G_DBUS_PROXY_FLAGS_NONE,
                          NULL,
                          "org.gnome.SettingsDaemon",
                          "/org/gnome/SettingsDaemon/Power",
                          "org.gnome.SettingsDaemon.Power.Screen",
                          NULL,
                          (GAsyncReadyCallback) power_screen_ready_cb,
                          manager);

        g_dbus_proxy_new (manager->priv->connection,
                          G_DBUS_PROXY_FLAGS_NONE,
                          NULL,
                          "org.gnome.SettingsDaemon",
                          "/org/gnome/SettingsDaemon/Power",
                          "org.gnome.SettingsDaemon.Power.Keyboard",
                          NULL,
                          (GAsyncReadyCallback) power_keyboard_ready_cb,
                          manager);
}

static void
register_manager (GsdMediaKeysManager *manager)
{
        manager->priv->introspection_data = g_dbus_node_info_new_for_xml (introspection_xml, NULL);
        manager->priv->bus_cancellable = g_cancellable_new ();
        g_assert (manager->priv->introspection_data != NULL);

        g_bus_get (G_BUS_TYPE_SESSION,
                   manager->priv->bus_cancellable,
                   (GAsyncReadyCallback) on_bus_gotten,
                   manager);

        g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,
                                  "org.freedesktop.UPower",
                                  "/org/freedesktop/UPower",
                                  "org.freedesktop.UPower",
                                  NULL,
                                  (GAsyncReadyCallback) upower_ready_cb,
                                  manager);
}

GsdMediaKeysManager *
gsd_media_keys_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_MEDIA_KEYS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_MEDIA_KEYS_MANAGER (manager_object);
}
