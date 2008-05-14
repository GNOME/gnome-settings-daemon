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
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <X11/keysym.h>

#ifdef HAVE_XINPUT
#include <X11/extensions/XInput.h>
#endif
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include "gnome-settings-profile.h"
#include "gsd-mouse-manager.h"

#include "gsd-locate-pointer.h"

#define GSD_MOUSE_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_MOUSE_MANAGER, GsdMouseManagerPrivate))

#define GCONF_MOUSE_DIR         "/desktop/gnome/peripherals/mouse"
#define GCONF_MOUSE_A11Y_DIR    "/desktop/gnome/accessibility/mouse"

#define KEY_LEFT_HANDED         "/desktop/gnome/peripherals/mouse/left_handed"
#define KEY_MOTION_ACCELERATION "/desktop/gnome/peripherals/mouse/motion_acceleration"
#define KEY_MOTION_THRESHOLD    "/desktop/gnome/peripherals/mouse/motion_threshold"
#define KEY_LOCATE_POINTER      "/desktop/gnome/peripherals/mouse/locate_pointer"
#define KEY_DWELL_ENABLE        "/desktop/gnome/accessibility/mouse/dwell_enable"
#define KEY_DELAY_ENABLE        "/desktop/gnome/accessibility/mouse/delay_enable"

struct GsdMouseManagerPrivate
{
        guint notify;
        guint notify_a11y;
};

static void     gsd_mouse_manager_class_init  (GsdMouseManagerClass *klass);
static void     gsd_mouse_manager_init        (GsdMouseManager      *mouse_manager);
static void     gsd_mouse_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdMouseManager, gsd_mouse_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
gsd_mouse_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdMouseManager *self;

        self = GSD_MOUSE_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_mouse_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdMouseManager *self;

        self = GSD_MOUSE_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_mouse_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdMouseManager      *mouse_manager;
        GsdMouseManagerClass *klass;

        klass = GSD_MOUSE_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_MOUSE_MANAGER));

        mouse_manager = GSD_MOUSE_MANAGER (G_OBJECT_CLASS (gsd_mouse_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (mouse_manager);
}

static void
gsd_mouse_manager_dispose (GObject *object)
{
        GsdMouseManager *mouse_manager;

        mouse_manager = GSD_MOUSE_MANAGER (object);

        G_OBJECT_CLASS (gsd_mouse_manager_parent_class)->dispose (object);
}

static void
gsd_mouse_manager_class_init (GsdMouseManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_mouse_manager_get_property;
        object_class->set_property = gsd_mouse_manager_set_property;
        object_class->constructor = gsd_mouse_manager_constructor;
        object_class->dispose = gsd_mouse_manager_dispose;
        object_class->finalize = gsd_mouse_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdMouseManagerPrivate));
}


#ifdef HAVE_XINPUT
static gboolean
supports_xinput_devices (void)
{
        gint op_code, event, error;

        return XQueryExtension (GDK_DISPLAY (),
                                "XInputExtension",
                                &op_code,
                                &event,
                                &error);
}
#endif

static void
configure_button_layout (guchar   *buttons,
                         gint      n_buttons,
                         gboolean  left_handed)
{
        const gint left_button = 1;
        gint right_button;
        gint i;

        /* if the button is higher than 2 (3rd button) then it's
         * probably one direction of a scroll wheel or something else
         * uninteresting
         */
        right_button = MIN (n_buttons, 3);

        /* If we change things we need to make sure we only swap buttons.
         * If we end up with multiple physical buttons assigned to the same
         * logical button the server will complain. This code assumes physical
         * button 0 is the physical left mouse button, and that the physical
         * button other than 0 currently assigned left_button or right_button
         * is the physical right mouse button.
         */

        /* check if the current mapping satisfies the above assumptions */
        if (buttons[left_button - 1] != left_button &&
            buttons[left_button - 1] != right_button)
                /* The current mapping is weird. Swapping buttons is probably not a
                 * good idea.
                 */
                return;

        /* check if we are left_handed and currently not swapped */
        if (left_handed && buttons[left_button - 1] == left_button) {
                /* find the right button */
                for (i = 0; i < n_buttons; i++) {
                        if (buttons[i] == right_button) {
                                buttons[i] = left_button;
                                break;
                        }
                }
                /* swap the buttons */
                buttons[left_button - 1] = right_button;
        }
        /* check if we are not left_handed but are swapped */
        else if (!left_handed && buttons[left_button - 1] == right_button) {
                /* find the right button */
                for (i = 0; i < n_buttons; i++) {
                        if (buttons[i] == left_button) {
                                buttons[i] = right_button;
                                break;
                        }
                }
                /* swap the buttons */
                buttons[left_button - 1] = left_button;
        }
}

#ifdef HAVE_XINPUT
static gboolean
xinput_device_has_buttons (XDeviceInfo *device_info)
{
        int i;
        XAnyClassInfo *class_info;

        class_info = device_info->inputclassinfo;
        for (i = 0; i < device_info->num_classes; i++) {
                if (class_info->class == ButtonClass) {
                        XButtonInfo *button_info;

                        button_info = (XButtonInfo *) class_info;
                        if (button_info->num_buttons > 0)
                                return TRUE;
                }

                class_info = (XAnyClassInfo *) (((guchar *) class_info) +
                                                class_info->length);
        }
        return FALSE;
}

static void
set_xinput_devices_left_handed (gboolean left_handed)
{
        XDeviceInfo *device_info;
        gint n_devices;
        guchar *buttons;
        gsize buttons_capacity = 16;
        gint n_buttons;
        gint i;

        device_info = XListInputDevices (GDK_DISPLAY (), &n_devices);

        if (n_devices > 0)
                buttons = g_new (guchar, buttons_capacity);
        else
                buttons = NULL;

        for (i = 0; i < n_devices; i++) {
                XDevice *device = NULL;

                if ((device_info[i].use != IsXExtensionDevice) ||
                    (!xinput_device_has_buttons (&device_info[i])))
                        continue;

                gdk_error_trap_push ();

                device = XOpenDevice (GDK_DISPLAY (), device_info[i].id);

                if ((gdk_error_trap_pop () != 0) ||
                    (device == NULL))
                        continue;

                n_buttons = XGetDeviceButtonMapping (GDK_DISPLAY (), device,
                                                     buttons,
                                                     buttons_capacity);

                while (n_buttons > buttons_capacity) {
                        buttons_capacity = n_buttons;
                        buttons = (guchar *) g_realloc (buttons,
                                                        buttons_capacity * sizeof (guchar));

                        n_buttons = XGetDeviceButtonMapping (GDK_DISPLAY (), device,
                                                             buttons,
                                                             buttons_capacity);
                }

                configure_button_layout (buttons, n_buttons, left_handed);

                XSetDeviceButtonMapping (GDK_DISPLAY (), device, buttons, n_buttons);
                XCloseDevice (GDK_DISPLAY (), device);
        }
        g_free (buttons);

        if (device_info != NULL)
                XFreeDeviceList (device_info);
}
#endif

static void
set_left_handed (GsdMouseManager *manager,
                 gboolean         left_handed)
{
        guchar *buttons ;
        gsize buttons_capacity = 16;
        gint n_buttons, i;

#ifdef HAVE_XINPUT
        if (supports_xinput_devices ()) {
                set_xinput_devices_left_handed (left_handed);
        }
#endif

        buttons = g_new (guchar, buttons_capacity);
        n_buttons = XGetPointerMapping (GDK_DISPLAY (),
                                        buttons,
                                        (gint) buttons_capacity);
        while (n_buttons > buttons_capacity) {
                buttons_capacity = n_buttons;
                buttons = (guchar *) g_realloc (buttons,
                                                buttons_capacity * sizeof (guchar));

                n_buttons = XGetPointerMapping (GDK_DISPLAY (),
                                                buttons,
                                                (gint) buttons_capacity);
        }

        configure_button_layout (buttons, n_buttons, left_handed);

        /* X refuses to change the mapping while buttons are engaged,
         * so if this is the case we'll retry a few times
         */
        for (i = 0;
             i < 20 && XSetPointerMapping (GDK_DISPLAY (), buttons, n_buttons) == MappingBusy;
             ++i) {
                g_usleep (300);
        }

        g_free (buttons);
}

static void
set_motion_acceleration (GsdMouseManager *manager,
                         gfloat           motion_acceleration)
{
        gint numerator, denominator;

        if (motion_acceleration >= 1.0) {
                /* we want to get the acceleration, with a resolution of 0.5
                 */
                if ((motion_acceleration - floor (motion_acceleration)) < 0.25) {
                        numerator = floor (motion_acceleration);
                        denominator = 1;
                } else if ((motion_acceleration - floor (motion_acceleration)) < 0.5) {
                        numerator = ceil (2.0 * motion_acceleration);
                        denominator = 2;
                } else if ((motion_acceleration - floor (motion_acceleration)) < 0.75) {
                        numerator = floor (2.0 *motion_acceleration);
                        denominator = 2;
                } else {
                        numerator = ceil (motion_acceleration);
                        denominator = 1;
                }
        } else if (motion_acceleration < 1.0 && motion_acceleration > 0) {
                /* This we do to 1/10ths */
                numerator = floor (motion_acceleration * 10) + 1;
                denominator= 10;
        } else {
                numerator = -1;
                denominator = -1;
        }

        XChangePointerControl (GDK_DISPLAY (), True, False,
                               numerator, denominator,
                               0);
}

static void
set_motion_threshold (GsdMouseManager *manager,
                      int              motion_threshold)
{
        XChangePointerControl (GDK_DISPLAY (), False, True,
                               0, 0, motion_threshold);
}


#define KEYBOARD_GROUP_SHIFT 13
#define KEYBOARD_GROUP_MASK ((1 << 13) | (1 << 14))

/* Owen magic */
static GdkFilterReturn
filter (GdkXEvent *xevent,
        GdkEvent  *event,
        gpointer   data)
{
        XEvent *xev = (XEvent *) xevent;
        guint keyval;
        gint group;

        GdkScreen *screen = (GdkScreen *)data;

        if (xev->type == KeyPress ||
            xev->type == KeyRelease) {
                /* get the keysym */
                group = (xev->xkey.state & KEYBOARD_GROUP_MASK) >> KEYBOARD_GROUP_SHIFT;
                gdk_keymap_translate_keyboard_state (gdk_keymap_get_default (),
                                                     xev->xkey.keycode,
                                                     xev->xkey.state,
                                                     group,
                                                     &keyval,
                                                     NULL, NULL, NULL);
                if (keyval == GDK_Control_L || keyval == GDK_Control_R) {
                        if (xev->type == KeyPress) {
                                XAllowEvents (xev->xkey.display,
                                              SyncKeyboard,
                                              xev->xkey.time);
                        } else {
                                XAllowEvents (xev->xkey.display,
                                              AsyncKeyboard,
                                              xev->xkey.time);
                                gsd_locate_pointer (screen);
                        }
                } else {
                        XAllowEvents (xev->xkey.display,
                                      ReplayKeyboard,
                                      xev->xkey.time);
                }
        }
        return GDK_FILTER_CONTINUE;
}

static void
set_locate_pointer (GsdMouseManager *manager,
                    gboolean         locate_pointer)
{
        GdkKeymapKey *keys;
        GdkDisplay *display;
        int n_screens;
        int n_keys;
        gboolean has_entries;
        static const guint keyvals[] = { GDK_Control_L, GDK_Control_R };
        unsigned j;

        display = gdk_display_get_default ();
        n_screens = gdk_display_get_n_screens (display);

        for (j = 0 ; j < G_N_ELEMENTS (keyvals) ; j++) {
                has_entries = gdk_keymap_get_entries_for_keyval (gdk_keymap_get_default (),
                                                                 keyvals[j],
                                                                 &keys,
                                                                 &n_keys);
                if (has_entries) {
                        gint i, j;

                        for (i = 0; i < n_keys; i++) {
                                for(j=0; j< n_screens; j++) {
                                        GdkScreen *screen = gdk_display_get_screen (display, j);
                                        Window xroot = gdk_x11_drawable_get_xid (gdk_screen_get_root_window (screen));

                                        if (locate_pointer) {
                                                XGrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                          keys[i].keycode,
                                                          0,
                                                          xroot,
                                                          False,
                                                          GrabModeAsync,
                                                          GrabModeSync);
                                                XGrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                          keys[i].keycode,
                                                          LockMask,
                                                          xroot,
                                                          False,
                                                          GrabModeAsync,
                                                          GrabModeSync);
                                                XGrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                          keys[i].keycode,
                                                          Mod2Mask,
                                                          xroot,
                                                          False,
                                                          GrabModeAsync,
                                                          GrabModeSync);
                                                XGrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                          keys[i].keycode,
                                                          Mod4Mask,
                                                          xroot,
                                                          False,
                                                          GrabModeAsync,
                                                          GrabModeSync);
                                        } else {
                                                XUngrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                            keys[i].keycode,
                                                            Mod4Mask,
                                                            xroot);
                                                XUngrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                            keys[i].keycode,
                                                            Mod2Mask,
                                                            xroot);
                                                XUngrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                            keys[i].keycode,
                                                            LockMask,
                                                            xroot);
                                                XUngrabKey (GDK_DISPLAY_XDISPLAY (display),
                                                            keys[i].keycode,
                                                            0,
                                                            xroot);
                                        }
                                }
                        }
                        g_free (keys);
                        if (locate_pointer) {
                                for (i = 0; i < n_screens; i++) {
                                        GdkScreen *screen;
                                        screen = gdk_display_get_screen (display, i);
                                        gdk_window_add_filter (gdk_screen_get_root_window (screen),
                                                               filter,
                                                               screen);
                                }
                        } else {
                                for (i = 0; i < n_screens; i++) {
                                        GdkScreen *screen;
                                        screen = gdk_display_get_screen (display, i);
                                        gdk_window_remove_filter (gdk_screen_get_root_window (screen),
                                                                  filter,
                                                                  screen);
                                }
                        }
                }
        }
}

static void
set_mousetweaks_daemon (GsdMouseManager *manager,
                        gboolean         dwell_enable,
                        gboolean         delay_enable)
{
        GError *error = NULL;
        gchar *comm;

        comm = g_strdup_printf ("mousetweaks %s",
                                (dwell_enable || delay_enable) ? "" : "-s");

        if (! g_spawn_command_line_async (comm, &error)) {
                if (error->code == G_SPAWN_ERROR_NOENT &&
                    (dwell_enable || delay_enable)) {
                        GtkWidget *dialog;
                        GConfClient *client;

                        client = gconf_client_get_default ();
                        if (dwell_enable)
                                gconf_client_set_bool (client,
                                                       KEY_DWELL_ENABLE,
                                                       FALSE, NULL);
                        else if (delay_enable)
                                gconf_client_set_bool (client,
                                                       KEY_DELAY_ENABLE,
                                                       FALSE, NULL);
                        g_object_unref (client);

                        dialog = gtk_message_dialog_new (NULL, 0,
                                                         GTK_MESSAGE_WARNING,
                                                         GTK_BUTTONS_OK,
                                                         _("Could not enable mouse accessibility features"));
                        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                                  _("Mouse accessibility requires the mousetweaks "
                                                                    "daemon to be installed on your system."));
                        gtk_window_set_title (GTK_WINDOW (dialog),
                                              _("Mouse Preferences"));
                        gtk_window_set_icon_name (GTK_WINDOW (dialog),
                                                  "gnome-dev-mouse-optical");
                        gtk_dialog_run (GTK_DIALOG (dialog));
                        gtk_widget_destroy (dialog);
                }
                g_error_free (error);
        }
        g_free (comm);
}

static void
mouse_callback (GConfClient        *client,
                guint               cnxn_id,
                GConfEntry         *entry,
                GsdMouseManager    *manager)
{
        if (! strcmp (entry->key, KEY_LEFT_HANDED)) {
                if (entry->value->type == GCONF_VALUE_BOOL) {
                        set_left_handed (manager, gconf_value_get_bool (entry->value));
                }
        } else if (! strcmp (entry->key, KEY_MOTION_ACCELERATION)) {
                if (entry->value->type == GCONF_VALUE_FLOAT) {
                        set_motion_acceleration (manager, gconf_value_get_float (entry->value));
                }
        } else if (! strcmp (entry->key, KEY_MOTION_THRESHOLD)) {
                if (entry->value->type == GCONF_VALUE_INT) {
                        set_motion_threshold (manager, gconf_value_get_int (entry->value));
                }
        } else if (! strcmp (entry->key, KEY_LOCATE_POINTER)) {
                if (entry->value->type == GCONF_VALUE_BOOL) {
                        set_locate_pointer (manager, gconf_value_get_bool (entry->value));
                }
        } else if (! strcmp (entry->key, KEY_DWELL_ENABLE)) {
                if (entry->value->type == GCONF_VALUE_BOOL) {
                        set_mousetweaks_daemon (manager,
                                                gconf_value_get_bool (entry->value),
                                                gconf_client_get_bool (client, KEY_DELAY_ENABLE, NULL));
                }
        } else if (! strcmp (entry->key, KEY_DELAY_ENABLE)) {
                if (entry->value->type == GCONF_VALUE_BOOL) {
                        set_mousetweaks_daemon (manager,
                                                gconf_client_get_bool (client, KEY_DWELL_ENABLE, NULL),
                                                gconf_value_get_bool (entry->value));
                }
        }
}

static guint
register_config_callback (GsdMouseManager         *manager,
                          GConfClient             *client,
                          const char              *path,
                          GConfClientNotifyFunc    func)
{
        gconf_client_add_dir (client, path, GCONF_CLIENT_PRELOAD_NONE, NULL);
        return gconf_client_notify_add (client, path, func, manager, NULL, NULL);
}

static void
gsd_mouse_manager_init (GsdMouseManager *manager)
{
        manager->priv = GSD_MOUSE_MANAGER_GET_PRIVATE (manager);
}

gboolean
gsd_mouse_manager_start (GsdMouseManager *manager,
                         GError         **error)
{
        GConfClient *client;

        g_debug ("Starting mouse manager");
        gnome_settings_profile_start (NULL);

        client = gconf_client_get_default ();

        manager->priv->notify =
                register_config_callback (manager,
                                          client,
                                          GCONF_MOUSE_DIR,
                                          (GConfClientNotifyFunc) mouse_callback);
        manager->priv->notify_a11y =
                register_config_callback (manager,
                                          client,
                                          GCONF_MOUSE_A11Y_DIR,
                                          (GConfClientNotifyFunc) mouse_callback);

        set_left_handed (manager, gconf_client_get_bool (client, KEY_LEFT_HANDED, NULL));
        set_motion_acceleration (manager, gconf_client_get_float (client, KEY_MOTION_ACCELERATION , NULL));
        set_motion_threshold (manager, gconf_client_get_int (client, KEY_MOTION_THRESHOLD, NULL));
        set_locate_pointer (manager, gconf_client_get_bool (client, KEY_LOCATE_POINTER, NULL));
        set_mousetweaks_daemon (manager,
                                gconf_client_get_bool (client, KEY_DWELL_ENABLE, NULL),
                                gconf_client_get_bool (client, KEY_DELAY_ENABLE, NULL));

        g_object_unref (client);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_mouse_manager_stop (GsdMouseManager *manager)
{
        GsdMouseManagerPrivate *p = manager->priv;
        GConfClient *client;

        g_debug ("Stopping mouse manager");

        client = gconf_client_get_default ();

        if (p->notify != 0) {
                gconf_client_remove_dir (client, GCONF_MOUSE_DIR, NULL);
                gconf_client_notify_remove (client, p->notify);
                p->notify = 0;
        }

        if (p->notify_a11y != 0) {
                gconf_client_remove_dir (client, GCONF_MOUSE_A11Y_DIR, NULL);
                gconf_client_notify_remove (client, p->notify_a11y);
                p->notify_a11y = 0;
        }

        g_object_unref (client);

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

        G_OBJECT_CLASS (gsd_mouse_manager_parent_class)->finalize (object);
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
