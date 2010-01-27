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

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "libhal-glib/hal-manager.h"
#include "libhal-glib/hal-device.h"

#include <X11/XKBlib.h>
#include <X11/extensions/Xrandr.h>

#include <nm-client.h>
#include <nm-device-wifi.h>

#include "gnome-settings-profile.h"
#include "gsd-marshal.h"
#include "gsd-media-keys-manager.h"
#include "gsd-media-keys-manager-glue.h"

#include "eggaccelerators.h"
#include "acme.h"
#include "gsd-media-keys-window.h"

#ifdef HAVE_PULSE
#include <canberra-gtk.h>
#include "gvc-mixer-control.h"
#endif /* HAVE_PULSE */

#define GSD_DBUS_PATH "/org/gnome/SettingsDaemon"
#define GSD_DBUS_NAME "org.gnome.SettingsDaemon"
#define GSD_MEDIA_KEYS_DBUS_PATH GSD_DBUS_PATH "/MediaKeys"
#define GSD_MEDIA_KEYS_DBUS_NAME GSD_DBUS_NAME ".MediaKeys"

#define CPU0_FREQ_GOVERNOR "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"

#define VOLUME_STEP 6           /* percents for one volume button press */
#define MAX_VOLUME 65536.0

#if defined(__OpenBSD__)
# define EJECT_COMMAND "eject -t /dev/cd0"
#else
# define EJECT_COMMAND "eject -T"
#endif

#if defined(__OpenBSD__) || defined(__FreeBSD__)
# define SLEEP_COMMAND "zzz"
#else
# define SLEEP_COMMAND "apm"
#endif

#define GSD_MEDIA_KEYS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_MEDIA_KEYS_MANAGER, GsdMediaKeysManagerPrivate))

typedef struct {
        char   *application;
        guint32 time;
} MediaPlayer;

struct GsdMediaKeysManagerPrivate
{
#ifdef HAVE_PULSE
        /* Volume bits */
        GvcMixerControl *volume;
        GvcMixerStream  *stream;
#endif /* HAVE_PULSE */
        GtkWidget       *dialog;
        GConfClient     *conf_client;
	HalManager    	*manager;
	GList         	*devices;

        /* Multihead stuff */
        GdkScreen       *current_screen;
        GSList          *screens;

        GList           *media_players;

        DBusGConnection *connection;
        DBusGConnection *sys_connection;
        guint            notify[HANDLED_KEYS];

	/* variables for key post action  */
	int 		 type_for_hide;
	gboolean	 osd_window_showing;
	gboolean	 esc_post_action;
	Key 		*esc_key;

	/* OSD window parameter */
        gboolean 	 draw_osd_box;
	int 		 osd_window_size;
	gboolean 	 volume_step_icons;

        /* customer OSD theme */
        GtkIconTheme    *cust_theme;

	/* hotkey properties */
	GsdMediaKeysXrandr	xrandr;
	GsdMediaKeysXrandr	last_xrandr;
	gboolean		backlight_xrandr;
	gboolean		cpu_governor_powersave;

	gboolean		wifi_nm_sw_kill;
	gboolean		wifi_hw_rfkill;
	char		       *wifi_hw_rfkill_udi;

	gint			p_num_levels;
	gint			p_level;

	gboolean		toggle_touchpad;
	gboolean		touchpad_enabled;
};

enum {
        MEDIA_PLAYER_KEY_PRESSED,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void     gsd_media_keys_manager_class_init  (GsdMediaKeysManagerClass *klass);
static void     gsd_media_keys_manager_init        (GsdMediaKeysManager      *media_keys_manager);
static void     gsd_media_keys_manager_finalize    (GObject                  *object);

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
        char *cmd_term;
        char *cmd = NULL;

        cmd_term = gconf_client_get_string (manager->priv->conf_client,
                                            "/desktop/gnome/applications/terminal/exec", NULL);
        if ((cmd_term != NULL) && (strcmp (cmd_term, "") != 0)) {
                char *cmd_args;
                cmd_args = gconf_client_get_string (manager->priv->conf_client,
                                                    "/desktop/gnome/applications/terminal/exec_arg", NULL);
                if ((cmd_args != NULL) && (strcmp (cmd_term, "") != 0)) {
                        cmd = g_strdup_printf ("%s %s -e", cmd_term, cmd_args);
                } else {
                        cmd = g_strdup_printf ("%s -e", cmd_term);
                }

                g_free (cmd_args);
        }

        g_free (cmd_term);

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

static gboolean
is_lvds (XRROutputInfo *output_info)
{
        const char *output_name = output_info->name;

        if (output_info->connection == RR_Connected &&
	    output_name &&
            (strstr (output_name, "lvds")       ||
             strstr (output_name, "LVDS")       ||
             strstr (output_name, "Lvds")))
        {
                return TRUE;
        }

        return FALSE;
}

static gboolean
is_crt_connected (gchar **lcd, gchar **crt)
{
    	Display  *dpy;
	int	  i, screen;
    	Window    root;
        int	  minWidth, maxWidth, minHeight, maxHeight;
    	XRRScreenResources  *res;
	gboolean 	     crt_connected = FALSE;
//	gboolean lcd_connected = FALSE;	TODO: lcd mode off?

	dpy = XOpenDisplay (NULL);
        screen = DefaultScreen (dpy);
	root = RootWindow (dpy, screen);

	XRRGetScreenSizeRange (dpy, root, &minWidth, &minHeight,
                               &maxWidth, &maxHeight);
    	res = XRRGetScreenResources (dpy, root);
    	if (!res) {
		g_warning ("could not get screen resources");
		return FALSE;
    	}

	/* check it has right status to CRT mode */
	for (i = 0; i < res->noutput; i++)
    	{
        	XRROutputInfo   *output_info = XRRGetOutputInfo (dpy, res, res->outputs[i]);
        	if (!output_info) {
                	g_debug ("could not get output 0x%x information\n", (int) res->outputs[i]);
                	continue;
        	}

		/* check if is LVDS (LCD, internal monitor) */
		if (is_lvds(output_info)) {
			*lcd = g_strdup (output_info->name);
			//TODO: lcd mode is on/off?
		} else {
			*crt = g_strdup (output_info->name);
			crt_connected = (output_info->connection == RR_Connected);
		}
	}

	return crt_connected;
}

static void
do_xrandr_post_action (GsdMediaKeysManager *manager)
{
	gchar *lcd = NULL, *crt = NULL;

	if (is_crt_connected (&lcd, &crt)) {
		gchar	command1[255], command2[255];

		/* change to next status when hotkey click*/
		switch (manager->priv->xrandr) {
		case GSD_MEDIA_KEYS_XRANDR_LCD_ONLY:
			break;
		case GSD_MEDIA_KEYS_XRANDR_LCD:
			g_snprintf (command1, 255, "/usr/bin/xrandr --output %s --auto", lcd);
			execute (manager, command1, FALSE, FALSE);
			g_snprintf (command2, 255, "/usr/bin/xrandr --output %s --off", crt);
			execute (manager, command2, FALSE, FALSE);
			break;
		case GSD_MEDIA_KEYS_XRANDR_CRT:
 			g_snprintf (command1, 255, "/usr/bin/xrandr --output %s --mode 1024x768", crt);
			execute (manager, command1, FALSE, FALSE);
			g_snprintf (command2, 255, "/usr/bin/xrandr --output %s --off", lcd);
			execute (manager, command2, FALSE, FALSE);
			break;
		case GSD_MEDIA_KEYS_XRANDR_CLONE:
			if (manager->priv->last_xrandr == GSD_MEDIA_KEYS_XRANDR_DUAL) {
				g_snprintf (command1, 255, "/usr/bin/xrandr --output %s --off", crt);
				execute (manager, command1, FALSE, FALSE);
				sleep (1);
			}
			g_snprintf (command1, 255, "/usr/bin/xrandr --output %s --mode 1024x768", crt);
			execute (manager, command1, FALSE, FALSE);
			g_snprintf (command2, 255, "/usr/bin/xrandr --output %s --auto", lcd);
			execute (manager, command2, FALSE, FALSE);
			break;
		case GSD_MEDIA_KEYS_XRANDR_DUAL:
			/* TODO: virtual desktop can't large than 2048x2048? */
			//TODO: capture the virtual desktop size and choice the best resolution
			g_snprintf (command1, 255, "/usr/bin/xrandr --output %s --auto", lcd);
			execute (manager, command1, FALSE, FALSE);
			g_snprintf (command2, 255, "/usr/bin/xrandr --output %s --mode 800x600 --right-of %s", crt, lcd);
			execute (manager, command2, FALSE, FALSE);
			break;
		}
	} else {
		if (manager->priv->xrandr != manager->priv->last_xrandr) {
			gchar	command1[255], command2[255];

			g_snprintf (command1, 255, "/usr/bin/xrandr --output %s --auto", lcd);
			execute (manager, command1, FALSE, FALSE);
			g_snprintf (command2, 255, "/usr/bin/xrandr --output %s --off", crt);
			execute (manager, command2, FALSE, FALSE);
		}
		manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_LCD_ONLY;
	}
	manager->priv->last_xrandr = manager->priv->xrandr;

	g_free (lcd);
	g_free (crt);
}

static void
do_xrandr_esc_post_action (GsdMediaKeysManager *manager)
{
	/* change back to last status when hotkey click*/
	manager->priv->xrandr = manager->priv->last_xrandr;
/*
	switch (manager->priv->xrandr) {
	case GSD_MEDIA_KEYS_XRANDR_LCD_ONLY:
		manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_LCD;
		break;
	case GSD_MEDIA_KEYS_XRANDR_LCD:
		manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_CRT;
		break;
	case GSD_MEDIA_KEYS_XRANDR_CRT:
		manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_CLONE;
		break;
	case GSD_MEDIA_KEYS_XRANDR_CLONE:
		manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_DUAL;
		break;
	case GSD_MEDIA_KEYS_XRANDR_DUAL:
		manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_LCD;
		break;
	}
*/
}

static void
do_performance_post_action (GsdMediaKeysManager *manager)
{
	if (manager->priv->p_level >= 0) {

		DBusGProxy *proxy;
		GError *error = NULL;

		//set new performance level
		proxy = dbus_g_proxy_new_for_name (manager->priv->sys_connection,
						  "org.freedesktop.Hal",
						   "/org/freedesktop/Hal/devices/performance",
						   "org.freedesktop.Hal.Device.Performance");
		if (!dbus_g_proxy_call (proxy, "SetPerformance", &error,
						G_TYPE_INT, manager->priv->p_level ,
						G_TYPE_INVALID, G_TYPE_INVALID)) {
			g_print ("DBus method call failed\n");
		}

		if (proxy)
			g_object_unref (proxy);
	}
}

static void
_dialog_hide_cb (GtkWidget   	     *dialog,
                 GsdMediaKeysManager *manager)
{
	g_debug ("got dialog hide cb");

	if (!manager->priv->esc_post_action) {
		switch (manager->priv->type_for_hide) {
		case XRANDR_KEY:
			do_xrandr_post_action (manager);
			break;
        	case PERFORMANCE_KEY:
                	do_performance_post_action (manager);
                	break;
		default:
			break;
		}
	} else {
                switch (manager->priv->type_for_hide) {
                case XRANDR_KEY:
                        do_xrandr_esc_post_action (manager);
                        break;
                default:
                        break;
                }
	}

	/* reset key type and variables for post action */
	manager->priv->type_for_hide = -1;
	manager->priv->esc_post_action = FALSE;
	manager->priv->osd_window_showing = FALSE;

	/* un-grab the esc key */
	if (manager->priv->esc_key != NULL)
		grab_key_unsafe (manager->priv->esc_key, FALSE, manager->priv->screens);
}

static void
dialog_init (GsdMediaKeysManager *manager)
{
        if (manager->priv->dialog != NULL
            && !gsd_media_keys_window_is_valid (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog))) {
                gtk_widget_destroy (manager->priv->dialog);
                manager->priv->dialog = NULL;
        }

        if (manager->priv->dialog == NULL) {
                manager->priv->dialog = gsd_media_keys_window_new ();
        	gsd_media_keys_window_set_draw_osd_box (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
							manager->priv->draw_osd_box);
                gsd_media_keys_window_set_osd_window_size (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                           manager->priv->osd_window_size);
        	gsd_media_keys_window_set_volume_step_icons (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
							     manager->priv->volume_step_icons);
		gsd_media_keys_window_set_cust_theme (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                      manager->priv->cust_theme);

		g_signal_connect (manager->priv->dialog,
      				  "hide",
                        	  (GCallback)_dialog_hide_cb,
                        	  manager);
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
update_kbd_cb (GConfClient         *client,
               guint                id,
               GConfEntry          *entry,
               GsdMediaKeysManager *manager)
{
        int      i;
        gboolean need_flush = TRUE;

        g_return_if_fail (entry->key != NULL);

	manager->priv->type_for_hide = -1;
	manager->priv->esc_key = NULL;

        gdk_error_trap_push ();

        /* Find the key that was modified */
        for (i = 0; i < HANDLED_KEYS; i++) {
                if (strcmp (entry->key, keys[i].gconf_key) == 0) {
                        char *tmp;
                        Key  *key;
                	char *prefix;

                        if (keys[i].key != NULL) {
                                need_flush = TRUE;
                                grab_key_unsafe (keys[i].key, FALSE, manager->priv->screens);
                        }

                        g_free (keys[i].key);
                        keys[i].key = NULL;
			g_free (keys[i].hk_cond_detail);
			keys[i].hk_cond_detail = NULL;

                        tmp = gconf_client_get_string (manager->priv->conf_client,
                                                       keys[i].gconf_key, NULL);

                        if (is_valid_shortcut (tmp) == FALSE) {
                                g_free (tmp);
                                break;
                        }

                	/* parse for HAL key event */
                	prefix = strstr (tmp,"HK");
                	if (tmp == prefix) {
                                keys[i].hk_cond_detail = g_strdup (tmp + 2);
                                if (keys[i].hk_cond_detail == NULL) {
                                        continue;
                                }

                                g_debug ("HAL key hk_cond_detail: '%s'", keys[i].hk_cond_detail);
                	}

                        key = g_new0 (Key, 1);
                        if (!egg_accelerator_parse_virtual (tmp, &key->keysym, &key->keycodes, &key->state)) {
                                g_free (tmp);
                                g_free (key);
                                break;
                        }

			/* ESC key is only grab when OSD window showing  */
			if (keys[i].key_type == ESC_KEY) {
				manager->priv->esc_key = key;
				continue;
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

	manager->priv->type_for_hide = -1;
	manager->priv->esc_key = NULL;

        gnome_settings_profile_start (NULL);

        gdk_error_trap_push ();

        for (i = 0; i < HANDLED_KEYS; i++) {
                char *tmp;
                Key  *key;
		char *prefix;

                manager->priv->notify[i] =
                        gconf_client_notify_add (manager->priv->conf_client,
                                                 keys[i].gconf_key,
                                                 (GConfClientNotifyFunc) update_kbd_cb,
                                                 manager,
                                                 NULL,
                                                 NULL);

                tmp = gconf_client_get_string (manager->priv->conf_client,
                                               keys[i].gconf_key,
                                               NULL);

                if (!is_valid_shortcut (tmp)) {
                        g_debug ("Not a valid shortcut: '%s'", tmp);
                        g_free (tmp);
                        continue;
                }

		/* parse for HAL key event */
		prefix = strstr (tmp,"HK");
		if (tmp == prefix) {
                        keys[i].hk_cond_detail = g_strdup (tmp + 2);
                        if (keys[i].hk_cond_detail == NULL) {
                                continue;
                        }

                        g_debug ("HAL key hk_cond_detail: '%s'", keys[i].hk_cond_detail);
		}

		/* parse for keysym  */
                key = g_new0 (Key, 1);
                if (!egg_accelerator_parse_virtual (tmp, &key->keysym, &key->keycodes, &key->state)) {
                        g_debug ("Unable to parse: '%s'", tmp);
                        g_free (tmp);
                        g_free (key);
                        continue;
                }

                g_free (tmp);

                keys[i].key = key;

		/* ESC key is only grab when OSD window showing  */
		if (keys[i].key_type == ESC_KEY) {
			manager->priv->esc_key = key;
			continue;
		}

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
        int            pointer_x;
        int            pointer_y;
        GtkRequisition win_req;
        GdkScreen     *pointer_screen;
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

        if (win_req.width > orig_w) {
                orig_w = win_req.width;
        }
        if (win_req.height > orig_h) {
                orig_h = win_req.height;
        }

        pointer_screen = NULL;
        gdk_display_get_pointer (gdk_screen_get_display (manager->priv->current_screen),
                                 &pointer_screen,
                                 &pointer_x,
                                 &pointer_y,
                                 NULL);
        if (pointer_screen != manager->priv->current_screen) {
                /* The pointer isn't on the current screen, so just
                 * assume the default monitor
                 */
                monitor = 0;
        } else {
                monitor = gdk_screen_get_monitor_at_point (manager->priv->current_screen,
                                                           pointer_x,
                                                           pointer_y);
        }

        gdk_screen_get_monitor_geometry (manager->priv->current_screen,
                                         monitor,
                                         &geometry);

        screen_w = geometry.width;
        screen_h = geometry.height;

        x = ((screen_w - orig_w) / 2) + geometry.x;
        y = geometry.y + (screen_h / 2) + (screen_h / 2 - orig_h) / 2;

        gtk_window_move (GTK_WINDOW (manager->priv->dialog), x, y);

        gtk_widget_show (manager->priv->dialog);
	manager->priv->osd_window_showing = TRUE;

	/* grab the esc key */
	if (manager->priv->esc_key != NULL)
		grab_key_unsafe (manager->priv->esc_key, TRUE, manager->priv->screens);

        gdk_display_sync (gdk_screen_get_display (manager->priv->current_screen));
}

static void
do_unknown_action (GsdMediaKeysManager *manager,
                   const char          *url)
{
        char *string;

        g_return_if_fail (url != NULL);

        string = gconf_client_get_string (manager->priv->conf_client,
                                          "/desktop/gnome/url-handlers/unknown/command",
                                          NULL);

        if ((string != NULL) && (strcmp (string, "") != 0)) {
                char *cmd;
                cmd = g_strdup_printf (string, url);
                execute (manager, cmd, FALSE, FALSE);
                g_free (cmd);
        }
        g_free (string);
}

static void
do_help_action (GsdMediaKeysManager *manager)
{
        char *string;

        string = gconf_client_get_string (manager->priv->conf_client,
                                          "/desktop/gnome/url-handlers/ghelp/command",
                                          NULL);

        if ((string != NULL) && (strcmp (string, "") != 0)) {
                char *cmd;
                cmd = g_strdup_printf (string, "");
                execute (manager, cmd, FALSE, FALSE);
                g_free (cmd);
        } else {
                do_unknown_action (manager, "ghelp:");
        }

        g_free (string);
}

static void
do_mail_action (GsdMediaKeysManager *manager)
{
        char *string;

        string = gconf_client_get_string (manager->priv->conf_client,
                                          "/desktop/gnome/url-handlers/mailto/command",
                                          NULL);

        if ((string != NULL) && (strcmp (string, "") != 0)) {
                char *cmd;
                cmd = g_strdup_printf (string, "");
                execute (manager,
                         cmd,
                         FALSE,
                         gconf_client_get_bool (manager->priv->conf_client,
                                                "/desktop/gnome/url-handlers/mailto/needs_terminal", NULL));
                g_free (cmd);
        }
        g_free (string);
}

static void
do_media_action (GsdMediaKeysManager *manager)
{
        char *command;

        command = gconf_client_get_string (manager->priv->conf_client,
                                           "/desktop/gnome/applications/media/exec", NULL);
        if ((command != NULL) && (strcmp (command, "") != 0)) {
                execute (manager,
                         command,
                         FALSE,
                         gconf_client_get_bool (manager->priv->conf_client,
                                                "/desktop/gnome/applications/media/needs_term", NULL));
        }
        g_free (command);
}

static void
do_www_action (GsdMediaKeysManager *manager,
               const char          *url)
{
        char *string;

        string = gconf_client_get_string (manager->priv->conf_client,
                                          "/desktop/gnome/url-handlers/http/command",
                                          NULL);

        if ((string != NULL) && (strcmp (string, "") != 0)) {
                gchar *cmd;

                if (url == NULL) {
                        cmd = g_strdup_printf (string, "");
                } else {
                        cmd = g_strdup_printf (string, url);
                }

                execute (manager,
                         cmd,
                         FALSE,
                         gconf_client_get_bool (manager->priv->conf_client,
                                                "/desktop/gnome/url-handlers/http/needs_terminal", NULL));
                g_free (cmd);
        } else {
                do_unknown_action (manager, url ? url : "");
        }
        g_free (string);
}

static void
do_exit_action (GsdMediaKeysManager *manager)
{
        execute (manager, "gnome-session-save --shutdown-dialog", FALSE, FALSE);
}

static void
do_eject_action (GsdMediaKeysManager *manager)
{
        char *command;

        dialog_init (manager);
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_EJECT);
        dialog_show (manager);

        command = gconf_client_get_string (manager->priv->conf_client,
                                           GCONF_MISC_DIR "/eject_command",
                                           NULL);
        if ((command != NULL) && (strcmp (command, "") != 0)) {
                execute (manager, command, FALSE, FALSE);
        } else {
                execute (manager, EJECT_COMMAND, FALSE, FALSE);
        }

        g_free (command);
}

#ifdef HAVE_PULSE
static void
update_dialog (GsdMediaKeysManager *manager,
               guint vol,
               gboolean muted)
{
        dialog_init (manager);
        gsd_media_keys_window_set_volume_muted (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                muted);
        gsd_media_keys_window_set_volume_level (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                (int) (100 * (double)vol / PA_VOLUME_NORM));
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_VOLUME);
        dialog_show (manager);

        ca_gtk_play_for_widget (manager->priv->dialog, 0,
                                CA_PROP_EVENT_ID, "audio-volume-change",
                                CA_PROP_EVENT_DESCRIPTION, "volume changed through key press",
                                CA_PROP_APPLICATION_ID, "org.gnome.VolumeControl",
                                NULL);
}

static void
do_sound_action (GsdMediaKeysManager *manager,
                 int                  type)
{
        gboolean muted;
        guint vol, norm_vol_step;
        int vol_step;

        if (manager->priv->stream == NULL)
                return;

        vol_step = gconf_client_get_int (manager->priv->conf_client,
                                         GCONF_MISC_DIR "/volume_step",
                                         NULL);

        if (vol_step <= 0 || vol_step > 100)
                vol_step = VOLUME_STEP;

        norm_vol_step = PA_VOLUME_NORM * vol_step / 100;

        /* FIXME: this is racy */
        vol = gvc_mixer_stream_get_volume (manager->priv->stream);
        muted = gvc_mixer_stream_get_is_muted (manager->priv->stream);

        switch (type) {
        case MUTE_KEY:
                muted = !muted;
                gvc_mixer_stream_change_is_muted (manager->priv->stream, muted);
                break;
        case VOLUME_DOWN_KEY:
                if (!muted && (vol <= norm_vol_step)) {
                        muted = !muted;
                        vol = 0;
                        gvc_mixer_stream_change_is_muted (manager->priv->stream, muted);
                        if (gvc_mixer_stream_set_volume (manager->priv->stream, vol) != FALSE)
                                gvc_mixer_stream_push_volume (manager->priv->stream);
                } else if (!muted) {
                        vol = vol - norm_vol_step;
                        if (gvc_mixer_stream_set_volume (manager->priv->stream, vol) != FALSE)
                                gvc_mixer_stream_push_volume (manager->priv->stream);
                }
                break;
        case VOLUME_UP_KEY:
                if (muted) {
                        muted = !muted;
                        if (vol == 0) {
                               vol = vol + norm_vol_step;
                               gvc_mixer_stream_change_is_muted (manager->priv->stream, muted);
                               if (gvc_mixer_stream_set_volume (manager->priv->stream, vol) != FALSE)
                                        gvc_mixer_stream_push_volume (manager->priv->stream);
                        } else {
                                gvc_mixer_stream_change_is_muted (manager->priv->stream, muted);
                        }
                } else {
                        if (vol < MAX_VOLUME) {
                                gboolean set;
                                if (vol + norm_vol_step >= MAX_VOLUME) {
                                        vol = MAX_VOLUME;
                                } else {
                                        vol = vol + norm_vol_step;
                                }
                                if (gvc_mixer_stream_set_volume (manager->priv->stream, vol) != FALSE)
                                        gvc_mixer_stream_push_volume (manager->priv->stream);
                        }
                }
                break;
        }

        gsd_media_keys_window_set_volume_step (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                vol_step);
        gsd_media_keys_window_set_is_mute_key (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                               type == MUTE_KEY);
        update_dialog (manager, vol, muted);
}

static void
do_sleep_action (GsdMediaKeysManager *manager,
                 char *cmd1,
                 char *cmd2)
{
	if (g_spawn_command_line_async (cmd1, NULL) == FALSE) {
		if (g_spawn_command_line_async (cmd2, NULL) == FALSE) {
			acme_error (_("Couldn't put the machine to sleep.\n"
					"Verify that the machine is correctly configured."));
		}
	}
}

static void
do_bluetooth_action (GsdMediaKeysManager *manager)
{
	gboolean bluetooth_enabled;
	gchar **names;
	GError *error = NULL;
	HalDevice *device;

	/* check have available video4linux.video_capture */
	g_usleep (1000000);      // 1 second to wait USB H/W on/off
	hal_manager_find_device_string_match (manager->priv->manager,
						"killswitch.type",
						"bluetooth",
						&names,
						&error);
	bluetooth_enabled = (names[0] != NULL)? TRUE:FALSE;
	if (names[0] != NULL)
		g_debug ("bluetooth: %s", names[0]);

        dialog_init (manager);
        gsd_media_keys_window_set_bluetooth_enabled (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                  bluetooth_enabled);
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_BLUETOOTH);
        dialog_show (manager);
}

static void
do_num_lock_action (GsdMediaKeysManager *manager)
{
	Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        unsigned int state = 0;

        dialog_init (manager);
        XkbGetIndicatorState(display, XkbUseCoreKbd, &state);
        gsd_media_keys_window_set_num_locked (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                              state & (1 << 1));
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_NUM_LOCK);
        dialog_show (manager);
}

static void
do_scroll_lock_action (GsdMediaKeysManager *manager)
{
        Display *display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
        unsigned int state = 0;

        dialog_init (manager);
        XkbGetIndicatorState(display, XkbUseCoreKbd, &state);
	g_debug ("scroll_lock: %d", state & (1 << 2));
        gsd_media_keys_window_set_scroll_locked (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                 state & (1 << 2));
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_SCROLL_LOCK);
        dialog_show (manager);
}

static void
do_wifi_action (GsdMediaKeysManager *manager)
{
        gboolean  wifi_enabled;

	/* call HAL to run hardware rfkill */
	if (manager->priv->wifi_hw_rfkill) {

		DBusGProxy *proxy;
		GError *error = NULL;
		guint state = -1;

		//get wireless rf state
		proxy = dbus_g_proxy_new_for_name (manager->priv->sys_connection,
						  "org.freedesktop.Hal",
						   manager->priv->wifi_hw_rfkill_udi,
						   "org.freedesktop.Hal.Device.KillSwitch");

		if (!dbus_g_proxy_call (proxy, "GetPower", &error,
					G_TYPE_INVALID,
					G_TYPE_INT, &state, G_TYPE_INVALID)) {
			g_print ("DBus method call failed\n");
		}

		if (state >= 0) {
			//set new state
			wifi_enabled = (state == 1)? TRUE:FALSE;
			if (!dbus_g_proxy_call (proxy, "SetPower", &error,
						G_TYPE_BOOLEAN, !wifi_enabled ,
						G_TYPE_INVALID, G_TYPE_INVALID)) {
				g_print ("DBus method call failed\n");
			}
		}

		if (proxy)
			g_object_unref (proxy);
	}

	/* run networkmanager software disable*/
	if (manager->priv->wifi_nm_sw_kill) {

		NMClient *client;

		g_usleep (500000);	//0.5 second to wait H/W rfkill

		client = nm_client_new ();
		if (!client) {
			g_debug ("do_wifi_action new nm_client fail.");
			return;
		}

		//get wifi status
		wifi_enabled = nm_client_wireless_get_enabled (client);
		nm_client_wireless_set_enabled (client, !wifi_enabled);

		g_object_unref (client);
	}

        dialog_init (manager);
        gsd_media_keys_window_set_wifi_enabled (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                !wifi_enabled);
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_WIFI);
        dialog_show (manager);
}

static void
do_touchpad_action (GsdMediaKeysManager *manager,
		    int 		 type)
{
	if (manager->priv->toggle_touchpad) {
		type = (manager->priv->touchpad_enabled)? TOUCHPAD_OFF_KEY:TOUCHPAD_ON_KEY;
		manager->priv->touchpad_enabled	= (manager->priv->touchpad_enabled)? FALSE:TRUE;
	}

        switch (type) {
        case TOUCHPAD_ON_KEY:
        	dialog_init (manager);
        	gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          	  GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_ON);
        	dialog_show (manager);
                break;
        case TOUCHPAD_OFF_KEY:
                dialog_init (manager);
                gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                  GSD_MEDIA_KEYS_WINDOW_ACTION_TOUCHPAD_OFF);
                dialog_show (manager);
		break;
        }
}

static void
do_webcam_action (GsdMediaKeysManager *manager)
{
	gboolean webcam_enabled;
	gchar **names;
	GError *error = NULL;
	HalDevice *device;

	/* check have available video4linux.video_capture */
	g_usleep (1000000);      // 1 second to wait USB H/W on/off
	hal_manager_find_capability (manager->priv->manager,
					"video4linux.video_capture",
					&names,
					&error);
	webcam_enabled = (names[0] != NULL)? TRUE:FALSE;
	if (names[0] != NULL)
		g_debug ("webcam: %s", names[0]);

        dialog_init (manager);
        gsd_media_keys_window_set_webcam_enabled (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                                  webcam_enabled);
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_WEBCAM);
        dialog_show (manager);
}

static void
do_xrandr_action (GsdMediaKeysManager *manager)
{
	gchar	*lcd = NULL, *crt = NULL;

	if (is_crt_connected (&lcd, &crt)) {
		if (manager->priv->osd_window_showing) {
			/* change to next status when hotkey click*/
			switch (manager->priv->xrandr) {
			case GSD_MEDIA_KEYS_XRANDR_LCD_ONLY:
				manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_LCD;
				break;
			case GSD_MEDIA_KEYS_XRANDR_LCD:
				manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_CRT;
				break;
			case GSD_MEDIA_KEYS_XRANDR_CRT:
				manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_CLONE;
				break;
			case GSD_MEDIA_KEYS_XRANDR_CLONE:
				manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_DUAL;
				break;
			case GSD_MEDIA_KEYS_XRANDR_DUAL:
				manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_LCD;
				break;
			}
		} else if (manager->priv->xrandr == GSD_MEDIA_KEYS_XRANDR_LCD_ONLY) {
			manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_LCD;
		}
	} else {
		manager->priv->xrandr = GSD_MEDIA_KEYS_XRANDR_LCD_ONLY;
	}
	g_free(lcd);
	g_free(crt);

        dialog_init (manager);
        gsd_media_keys_window_set_xrandr (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
         				  manager->priv->xrandr);
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_XRANDR);
        dialog_show (manager);
}

static void
do_cpu_governor_action (GsdMediaKeysManager *manager)
{
	//logic to get cpus governor
        DBusGProxy *proxy;
	gchar* cpu_governor = CPU_GOVERNOR_ONDEMAND;
	gchar *current_governor;
	gsize bytes;
	GError *error = NULL;

	if (!g_file_test (CPU0_FREQ_GOVERNOR, G_FILE_TEST_EXISTS)) {
		g_warning ("Warning: /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor does not exist!");
		return;
	}

	g_file_get_contents (CPU0_FREQ_GOVERNOR, &current_governor, &bytes, &error);

	if (!g_ascii_strncasecmp (current_governor, CPU_GOVERNOR_PERFORMANCE, 11)) {
		if (manager->priv->cpu_governor_powersave)
			cpu_governor = CPU_GOVERNOR_POWERSAVE;
	} else if (!g_ascii_strncasecmp (current_governor, CPU_GOVERNOR_POWERSAVE, 9)) {
		cpu_governor = CPU_GOVERNOR_ONDEMAND;
	} else if (!g_ascii_strncasecmp (current_governor, CPU_GOVERNOR_ONDEMAND, 8)) {
		cpu_governor = CPU_GOVERNOR_PERFORMANCE;
	}
	g_free (current_governor);

	g_debug ("cpu_governor: %s", cpu_governor);

	//set new governor
        proxy = dbus_g_proxy_new_for_name (manager->priv->sys_connection,
                                          "org.freedesktop.Hal",
                                           "/org/freedesktop/Hal/devices/computer",
                                           "org.freedesktop.Hal.Device.CPUFreq");

        if (!dbus_g_proxy_call (proxy, "SetCPUFreqGovernor", &error,
                                G_TYPE_STRING, cpu_governor, G_TYPE_INVALID,
                                G_TYPE_INVALID)) {
                g_print ("DBus method call failed\n");
        }

	if (proxy)
		g_object_unref (proxy);

	dialog_init (manager);
        gsd_media_keys_window_set_cpu_governor (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
         					cpu_governor);
        gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
                                          GSD_MEDIA_KEYS_WINDOW_ACTION_CPU_GOVERNOR);
        dialog_show (manager);
}

static void
do_backlight_action (GsdMediaKeysManager *manager)
{
        DBusGProxy *proxy;
	GError *error = NULL;
	guint level = -1;

	//get current backlight power state
       	proxy = dbus_g_proxy_new_for_name (manager->priv->sys_connection,
                                          "org.freedesktop.Hal",
                                           "/org/freedesktop/Hal/devices/computer_backlight",
                                           "org.freedesktop.Hal.Device.LaptopPanel");

        if (!dbus_g_proxy_call (proxy, "GetPower", &error,
                                G_TYPE_INVALID,
				G_TYPE_INT, &level, G_TYPE_INVALID)) {
                g_print ("DBus method call failed\n");
        }

	g_debug ("bl_power: %d", level);

	if (level >= 0) {
		/* turn on/off backlight */
		if (!dbus_g_proxy_call (proxy, "SetPower", &error,
					G_TYPE_INT, (level == 0)? 1:0 ,
					G_TYPE_INVALID, G_TYPE_INVALID)) {
			g_print ("DBus method call failed\n");
		}

		//show backlight on/off OSD
		dialog_init (manager);
		gsd_media_keys_window_set_backlight (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
						     (level == 0)? 1:0 );
		gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
						  GSD_MEDIA_KEYS_WINDOW_ACTION_BACKLIGHT);
		dialog_show (manager);
	}

	if (proxy)
		g_object_unref (proxy);
}

static void
do_performance_action (GsdMediaKeysManager *manager)
{
	if (manager->priv->p_num_levels <= 0)
		return;

	if (manager->priv->osd_window_showing) {

		manager->priv->p_level++;
		if (manager->priv->p_level >= manager->priv->p_num_levels)
			manager->priv->p_level = 0;		//back to 0 level
	} else {
		DBusGProxy *proxy;
		GError *error = NULL;
		guint level = -1;

		//get current performance level
		proxy = dbus_g_proxy_new_for_name (manager->priv->sys_connection,
						  "org.freedesktop.Hal",
						   "/org/freedesktop/Hal/devices/performance",
						   "org.freedesktop.Hal.Device.Performance");

		if (!dbus_g_proxy_call (proxy, "GetPerformance", &error,
					G_TYPE_INVALID,
					G_TYPE_INT, &level, G_TYPE_INVALID)) {
			g_print ("DBus method call failed\n");
		}

		manager->priv->p_level = level;

		dialog_show (manager);

		if (proxy)
			g_object_unref (proxy);
	}

	//show OSD
	dialog_init (manager);
	gsd_media_keys_window_set_performance (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
					       manager->priv->p_level );
	gsd_media_keys_window_set_action (GSD_MEDIA_KEYS_WINDOW (manager->priv->dialog),
					  GSD_MEDIA_KEYS_WINDOW_ACTION_PERFORMANCE);
	dialog_show (manager);
}

static void
do_esc_action (GsdMediaKeysManager *manager)
{
	/* interrupt osc window and post action */
	if (manager->priv->osd_window_showing) {

		manager->priv->esc_post_action = TRUE;

		//TODO: interrupt OSD
		//remove OSD icon
	}
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

#endif /* HAVE_PULSE */

static gint
find_by_application (gconstpointer a,
                     gconstpointer b)
{
        return strcmp (((MediaPlayer *)a)->application, b);
}

static gint
find_by_time (gconstpointer a,
              gconstpointer b)
{
        return ((MediaPlayer *)a)->time < ((MediaPlayer *)b)->time;
}

/*
 * Register a new media player. Most applications will want to call
 * this with time = GDK_CURRENT_TIME. This way, the last registered
 * player will receive media events. In some cases, applications
 * may want to register with a lower priority (usually 1), to grab
 * events only nobody is interested.
 */
gboolean
gsd_media_keys_manager_grab_media_player_keys (GsdMediaKeysManager *manager,
                                               const char          *application,
                                               guint32              time,
                                               GError             **error)
{
        GList       *iter;
        MediaPlayer *media_player;

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
                        g_free (((MediaPlayer *)iter->data)->application);
                        g_free (iter->data);
                        manager->priv->media_players = g_list_delete_link (manager->priv->media_players, iter);
                } else {
                        return TRUE;
                }
        }

        g_debug ("Registering %s at %u", application, time);
        media_player = g_new0 (MediaPlayer, 1);
        media_player->application = g_strdup (application);
        media_player->time = time;

        manager->priv->media_players = g_list_insert_sorted (manager->priv->media_players,
                                                             media_player,
                                                             find_by_time);

        return TRUE;
}

gboolean
gsd_media_keys_manager_release_media_player_keys (GsdMediaKeysManager *manager,
                                                  const char          *application,
                                                  GError             **error)
{
        GList *iter;

        iter = g_list_find_custom (manager->priv->media_players,
                                   application,
                                   find_by_application);

        if (iter != NULL) {
                g_debug ("Deregistering %s", application);
                g_free (((MediaPlayer *)iter->data)->application);
                g_free (iter->data);
                manager->priv->media_players = g_list_delete_link (manager->priv->media_players, iter);
        }

        return TRUE;
}

static gboolean
gsd_media_player_key_pressed (GsdMediaKeysManager *manager,
                              const char          *key)
{
        const char *application = NULL;
        gboolean    have_listeners;

        have_listeners = (manager->priv->media_players != NULL);

        if (have_listeners) {
                application = ((MediaPlayer *)manager->priv->media_players->data)->application;
        }

        g_signal_emit (manager, signals[MEDIA_PLAYER_KEY_PRESSED], 0, application, key);

        return !have_listeners;
}

static gboolean
do_multimedia_player_action (GsdMediaKeysManager *manager,
                             const char          *key)
{
        return gsd_media_player_key_pressed (manager, key);
}

static gboolean
do_action (GsdMediaKeysManager *manager,
           int                  type)
{
        char *cmd;
        char *path;

	if (type != ESC_KEY)
		manager->priv->type_for_hide = type;

        switch (type) {
        case MUTE_KEY:
        case VOLUME_DOWN_KEY:
        case VOLUME_UP_KEY:
#ifdef HAVE_PULSE
                do_sound_action (manager, type);
#endif /* HAVE_PULSE */
                break;
        case POWER_KEY:
                do_exit_action (manager);
                break;
        case EJECT_KEY:
                do_eject_action (manager);
                break;
        case HOME_KEY:
                path = g_shell_quote (g_get_home_dir ());
                cmd = g_strconcat ("nautilus --no-desktop ", path, NULL);
                g_free (path);
                execute (manager, cmd, FALSE, FALSE);
                g_free (cmd);
                break;
        case SEARCH_KEY:
                cmd = NULL;
                if ((cmd = g_find_program_in_path ("beagle-search"))) {
                        execute (manager, "beagle-search", FALSE, FALSE);
                } else if ((cmd = g_find_program_in_path ("tracker-search-tool"))) {
                        execute (manager, "tracker-search-tool", FALSE, FALSE);
                } else {
                        execute (manager, "gnome-search-tool", FALSE, FALSE);
                }
                g_free (cmd);
                break;
        case EMAIL_KEY:
                do_mail_action (manager);
                break;
        case SLEEP_KEY:
                do_sleep_action (manager, SLEEP_COMMAND, "xset dpms force off");
                break;
        case SCREENSAVER_KEY:
                if ((cmd = g_find_program_in_path ("gnome-screensaver-command"))) {
                        execute (manager, "gnome-screensaver-command --lock", FALSE, FALSE);
                } else {
                        execute (manager, "xscreensaver-command -lock", FALSE, FALSE);
                }

                g_free (cmd);
                break;
        case HELP_KEY:
                do_help_action (manager);
                break;
        case WWW_KEY:
                do_www_action (manager, NULL);
                break;
        case MEDIA_KEY:
                do_media_action (manager);
                break;
        case CALCULATOR_KEY:
                execute (manager, "gcalctool", FALSE, FALSE);
                break;
        case PLAY_KEY:
                return do_multimedia_player_action (manager, "Play");
                break;
        case PAUSE_KEY:
                return do_multimedia_player_action (manager, "Pause");
                break;
        case STOP_KEY:
                return do_multimedia_player_action (manager, "Stop");
                break;
        case PREVIOUS_KEY:
                return do_multimedia_player_action (manager, "Previous");
                break;
        case NEXT_KEY:
                return do_multimedia_player_action (manager, "Next");
                break;
        case NUM_LOCK_KEY:
                do_num_lock_action (manager);
                break;
        case SCROLL_LOCK_KEY:
                do_scroll_lock_action (manager);
                break;
        case WIFI_KEY:
                do_wifi_action (manager);
                break;
        case TOUCHPAD_ON_KEY:
        case TOUCHPAD_OFF_KEY:
                do_touchpad_action (manager, type);
                break;
        case XRANDR_KEY:
                do_xrandr_action (manager);
                break;
        case CPU_GOVERNOR_KEY:
                do_cpu_governor_action (manager);
                break;
        case BACKLIGHT_KEY:
                do_backlight_action (manager);
                break;
        case PERFORMANCE_KEY:
                do_performance_action (manager);
                break;
        case ESC_KEY:
                do_esc_action (manager);
                break;
        case WEBCAM_KEY:
                do_webcam_action (manager);
                break;
        case BLUETOOTH_KEY:
                do_bluetooth_action (manager);
                break;
        default:
                g_assert_not_reached ();
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

                        if (do_action (manager, keys[i].key_type) == FALSE) {
                                return GDK_FILTER_REMOVE;
                        } else {
                                return GDK_FILTER_CONTINUE;
                        }
                }
        }

        return GDK_FILTER_CONTINUE;
}

static void
_hk_device_condition_cb (HalDevice   *device,
                         const gchar *condition,
                         const gchar *details,
                         GsdMediaKeysManager *manager)
{
        int i;

        if (!g_str_equal (condition, "ButtonPressed"))
        {
                /* Not expecting another event...*/
                return;
        }

        for (i = 0; i < HANDLED_KEYS; i++) {
                if ((keys[i].hk_cond_detail != NULL)
                    && !strcmp (keys[i].hk_cond_detail, details)) {
                        g_debug ("HAL keys[i].key_type: '%d'", keys[i].key_type);
                        do_action (manager, keys[i].key_type);
                }
        }
}

static void
_setup_button_devices (GsdMediaKeysManager *self)
{
        //GsdMediaKeysManagerPrivate *priv = GET_PRIVATE (self);
        gchar **names;
        GError *error = NULL;
        gchar *udi;
        gint i;
        HalDevice *device;
        gchar *type;

        if (!hal_manager_find_capability (self->priv->manager,
                                        "button",
                                        &names,
                                        &error))
        {
                g_warning (G_STRLOC ": Unable to find devices with capability 'button': %s",
               error->message);
                g_clear_error (&error);
                return;
        }

        for (i = 0, udi = names[i]; udi != NULL; i++, udi = names[i])
        {
                type = NULL;

                device = hal_device_new ();
                hal_device_set_udi (device, udi);

                hal_device_watch_condition (device);
                g_signal_connect (device,
                                "device-condition",
                                (GCallback)_hk_device_condition_cb,
                                self);
                self->priv->devices = g_list_append (self->priv->devices, device);
        }

        hal_manager_free_capability (names);
}

static void
_cleanup_button_devices (GsdMediaKeysManager *self)
{
  	GList *l = NULL;
  	HalDevice *device = NULL;

  	for (l = self->priv->devices; l; l = g_list_delete_link (l, l))
  	{
    		device = (HalDevice *)l->data;

    		if (device)
    		{
      			g_object_unref (device);
    		}
  	}

  	self->priv->devices = l;
}

static gboolean
start_media_keys_idle_cb (GsdMediaKeysManager *manager)
{
	char   *cust_theme_name;
        GSList *l;
        GError  *gconf_error = NULL;

        g_debug ("Starting media_keys manager");
        gnome_settings_profile_start (NULL);
        manager->priv->conf_client = gconf_client_get_default ();

        gconf_client_add_dir (manager->priv->conf_client,
                              GCONF_BINDING_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);

        init_screens (manager);
        init_kbd (manager);

	/* Initial a bit variables */
	manager->priv->osd_window_showing = FALSE;
	manager->priv->esc_post_action = FALSE;

	/* Load OSD window parameter */
        manager->priv->draw_osd_box = gconf_client_get_bool (manager->priv->conf_client,
                                             		   GCONF_MISC_DIR "/draw_osd_box",
                                             		   &gconf_error);
        if (gconf_error) {
                manager->priv->draw_osd_box = TRUE;
                g_error_free (gconf_error);
        }

        manager->priv->osd_window_size = gconf_client_get_int (manager->priv->conf_client,
                                                           GCONF_MISC_DIR "/osd_window_size",
                                                           &gconf_error);
        if (gconf_error) {
                manager->priv->osd_window_size = 130;
                g_error_free (gconf_error);
        }

        manager->priv->volume_step_icons = gconf_client_get_bool (manager->priv->conf_client,
                                                           GCONF_MISC_DIR "/volume_step_icons",
                                                           &gconf_error);
        if (gconf_error) {
                manager->priv->volume_step_icons = FALSE;
                g_error_free (gconf_error);
        }

	/* use xrandr turn screen on/off when backlight control enable */
        manager->priv->backlight_xrandr = gconf_client_get_bool (manager->priv->conf_client,
                                                           GCONF_MISC_DIR "/backlight_xrandr",
                                                           &gconf_error);
        if (gconf_error) {
                manager->priv->backlight_xrandr = FALSE;
                g_error_free (gconf_error);
        }

	/* setup cpu governor powersave switch  */
        manager->priv->cpu_governor_powersave = gconf_client_get_bool (manager->priv->conf_client,
                                                           GCONF_MISC_DIR "/cpu_governor_powersave",
                                                           &gconf_error);
        if (gconf_error) {
                manager->priv->cpu_governor_powersave = FALSE;
                g_error_free (gconf_error);
        }

	/* get wifi networkmanager disable or not   */
        manager->priv->wifi_nm_sw_kill = gconf_client_get_bool (manager->priv->conf_client,
                                                           GCONF_MISC_DIR "/wifi_nm_sw_kill",
                                                           &gconf_error);
        if (gconf_error) {
                manager->priv->wifi_nm_sw_kill = FALSE;
                g_error_free (gconf_error);
        }

	/* get rfkill udi */
        manager->priv->wifi_hw_rfkill_udi = gconf_client_get_string (manager->priv->conf_client,
                                                           	  GCONF_MISC_DIR "/wifi_hw_rfkill_udi",
                                                           	  &gconf_error);
        if (gconf_error) {
                manager->priv->wifi_hw_rfkill_udi = NULL;
                manager->priv->wifi_hw_rfkill = FALSE;
                g_error_free (gconf_error);
        } else if ((manager->priv->wifi_hw_rfkill_udi != NULL) &&
		   (strcmp (manager->priv->wifi_hw_rfkill_udi, "") != 0)) {
                manager->priv->wifi_hw_rfkill = TRUE;
	}

	/* use xrandr turn screen on/off when backlight control enable */
	manager->priv->touchpad_enabled = TRUE;
        manager->priv->toggle_touchpad = gconf_client_get_bool (manager->priv->conf_client,
                                                           GCONF_MISC_DIR "/toggle_touchpad",
                                                           &gconf_error);
        if (gconf_error) {
                manager->priv->toggle_touchpad = TRUE;
                g_error_free (gconf_error);
        }


	/* get performance level  */
	manager->priv->p_level = -1;
	manager->priv->p_num_levels = -1;
  	GError *error_per = NULL;
        HalDevice *performance_device = hal_device_new ();
        hal_device_set_udi (performance_device,
                	    "/org/freedesktop/Hal/devices/performance");
  	if (!hal_device_get_int (performance_device,
                           	 "performance.num_levels",
                           	 &manager->priv->p_num_levels,
                           	 &error_per))
  	{
    		g_warning (G_STRLOC ": Error getting number of performance levels: %s",
               		   error_per->message);
    		g_error_free (error_per);
	}
	/* remove HalDevice */
	if (performance_device)
		g_object_unref (performance_device);

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

	/* Get customer OSD theme */
        cust_theme_name = gconf_client_get_string (manager->priv->conf_client,
                                                           	  GCONF_MISC_DIR "/cust_theme",
                                                           	  &gconf_error);
        if (gconf_error) {
                cust_theme_name = NULL;
                g_error_free (gconf_error);
        }

	if (cust_theme_name != NULL) {
        	manager->priv->cust_theme = gtk_icon_theme_new ();
        	gtk_icon_theme_set_custom_theme(manager->priv->cust_theme, cust_theme_name);
	}

	g_free(cust_theme_name);

        gnome_settings_profile_end (NULL);

        return FALSE;
}

gboolean
gsd_media_keys_manager_start (GsdMediaKeysManager *manager,
                              GError             **error)
{
        gnome_settings_profile_start (NULL);

#ifdef HAVE_PULSE
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

        gvc_mixer_control_open (manager->priv->volume);

        gnome_settings_profile_end ("gvc_mixer_control_new");
#endif /* HAVE_PULSE */
        g_idle_add ((GSourceFunc) start_media_keys_idle_cb, manager);

        gnome_settings_profile_end (NULL);

        /* Start monitor hal key event*/
	_setup_button_devices (manager);

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

	/* remove HalDevice */
  	_cleanup_button_devices (manager);

        for (ls = priv->screens; ls != NULL; ls = ls->next) {
                gdk_window_remove_filter (gdk_screen_get_root_window (ls->data),
                                          (GdkFilterFunc) acme_filter_events,
                                          manager);
        }

        if (priv->conf_client) {
                gconf_client_remove_dir (priv->conf_client,
                                         GCONF_BINDING_DIR,
                                         NULL);

                for (i = 0; i < HANDLED_KEYS; ++i) {
                        if (priv->notify[i] != 0) {
                                gconf_client_notify_remove (priv->conf_client, priv->notify[i]);
                                priv->notify[i] = 0;
                        }
                }

                g_object_unref (priv->conf_client);
                priv->conf_client = NULL;
        }

        if (priv->connection != NULL) {
                dbus_g_connection_unref (priv->connection);
                priv->connection = NULL;
        }

        if (priv->sys_connection != NULL) {
                dbus_g_connection_unref (priv->sys_connection);
                priv->sys_connection = NULL;
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

                        g_free (keys[i].hk_cond_detail);
                        keys[i].hk_cond_detail = NULL;
                }
        }

        if (need_flush)
                gdk_flush ();
        gdk_error_trap_pop ();

        g_slist_free (priv->screens);
        priv->screens = NULL;

#ifdef HAVE_PULSE
        if (priv->stream) {
                g_object_unref (priv->stream);
                priv->stream = NULL;
        }

        if (priv->volume) {
                g_object_unref (priv->volume);
                priv->volume = NULL;
        }
#endif /* HAVE_PULSE */

        if (priv->dialog != NULL) {
                gtk_widget_destroy (priv->dialog);
                priv->dialog = NULL;
        }

        for (l = priv->media_players; l; l = l->next) {
                MediaPlayer *mp = l->data;
                g_free (mp->application);
                g_free (mp);
        }
        g_list_free (priv->media_players);
        priv->media_players = NULL;

	//TODO: free draw_osd_box?
	if (priv->cust_theme != NULL)
	{
		g_object_unref (priv->cust_theme);
		priv->cust_theme = NULL;
	}
}

static void
gsd_media_keys_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdMediaKeysManager *self;

        self = GSD_MEDIA_KEYS_MANAGER (object);

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
        GsdMediaKeysManager *self;

        self = GSD_MEDIA_KEYS_MANAGER (object);

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
        GsdMediaKeysManagerClass *klass;

        klass = GSD_MEDIA_KEYS_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_MEDIA_KEYS_MANAGER));

        media_keys_manager = GSD_MEDIA_KEYS_MANAGER (G_OBJECT_CLASS (gsd_media_keys_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (media_keys_manager);
}

static void
gsd_media_keys_manager_dispose (GObject *object)
{
        GsdMediaKeysManager *media_keys_manager;

        media_keys_manager = GSD_MEDIA_KEYS_MANAGER (object);

        G_OBJECT_CLASS (gsd_media_keys_manager_parent_class)->dispose (object);
}

static void
gsd_media_keys_manager_class_init (GsdMediaKeysManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_media_keys_manager_get_property;
        object_class->set_property = gsd_media_keys_manager_set_property;
        object_class->constructor = gsd_media_keys_manager_constructor;
        object_class->dispose = gsd_media_keys_manager_dispose;
        object_class->finalize = gsd_media_keys_manager_finalize;

       signals[MEDIA_PLAYER_KEY_PRESSED] =
               g_signal_new ("media-player-key-pressed",
                             G_OBJECT_CLASS_TYPE (klass),
                             G_SIGNAL_RUN_LAST,
                             G_STRUCT_OFFSET (GsdMediaKeysManagerClass, media_player_key_pressed),
                             NULL,
                             NULL,
                             gsd_marshal_VOID__STRING_STRING,
                             G_TYPE_NONE,
                             2,
                             G_TYPE_STRING,
                             G_TYPE_STRING);

        dbus_g_object_type_install_info (GSD_TYPE_MEDIA_KEYS_MANAGER, &dbus_glib_gsd_media_keys_manager_object_info);

        g_type_class_add_private (klass, sizeof (GsdMediaKeysManagerPrivate));
}

static void
gsd_media_keys_manager_init (GsdMediaKeysManager *manager)
{
        manager->priv = GSD_MEDIA_KEYS_MANAGER_GET_PRIVATE (manager);
	manager->priv->manager = hal_manager_new ();
}

static void
gsd_media_keys_manager_finalize (GObject *object)
{
        GsdMediaKeysManager *media_keys_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_MEDIA_KEYS_MANAGER (object));

        media_keys_manager = GSD_MEDIA_KEYS_MANAGER (object);

        g_return_if_fail (media_keys_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_media_keys_manager_parent_class)->finalize (object);
}

static gboolean
register_manager (GsdMediaKeysManager *manager)
{
        GError *error = NULL;

        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_error ("Error getting session bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        dbus_g_connection_register_g_object (manager->priv->connection, GSD_MEDIA_KEYS_DBUS_PATH, G_OBJECT (manager));

	//get system bus connection
	manager->priv->sys_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (manager->priv->sys_connection == NULL) {
		if (error != NULL) {
		    g_printerr ("Error getting system bus: %s\n", error->message);
		    g_error_free (error);
		}
	    	return FALSE;
	}

        return TRUE;
}

GsdMediaKeysManager *
gsd_media_keys_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (GSD_TYPE_MEDIA_KEYS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return GSD_MEDIA_KEYS_MANAGER (manager_object);
}
