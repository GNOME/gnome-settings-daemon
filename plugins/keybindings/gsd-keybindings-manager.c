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

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/keysym.h>
#include <gconf/gconf-client.h>

#include "gnome-settings-profile.h"
#include "gsd-keybindings-manager.h"

#include "eggaccelerators.h"

/* we exclude shift, GDK_CONTROL_MASK and GDK_MOD1_MASK since we know what
   these modifiers mean
   these are the mods whose combinations are bound by the keygrabbing code */
#define IGNORED_MODS (0x2000 /*Xkb modifier*/ | GDK_LOCK_MASK  | \
        GDK_MOD2_MASK | GDK_MOD3_MASK | GDK_MOD4_MASK | GDK_MOD5_MASK)
/* these are the ones we actually use for global keys, we always only check
 * for these set */
#define USED_MODS (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)

#define GCONF_BINDING_DIR "/desktop/gnome/keybindings"

#define GSD_KEYBINDINGS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_KEYBINDINGS_MANAGER, GsdKeybindingsManagerPrivate))

typedef struct {
        guint keysym;
        guint state;
        guint keycode;
} Key;

typedef struct {
        char *binding_str;
        char *action;
        char *gconf_key;
        Key   key;
        Key   previous_key;
} Binding;

struct GsdKeybindingsManagerPrivate
{
        GSList *binding_list;
        GSList *screens;
        guint   notify;
};

static void     gsd_keybindings_manager_class_init  (GsdKeybindingsManagerClass *klass);
static void     gsd_keybindings_manager_init        (GsdKeybindingsManager      *keybindings_manager);
static void     gsd_keybindings_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdKeybindingsManager, gsd_keybindings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static GSList *
get_screens_list (void)
{
        GdkDisplay *display = gdk_display_get_default();
        int         n_screens;
        GSList     *list = NULL;
        int         i;

        n_screens = gdk_display_get_n_screens (display);

        if (n_screens == 1) {
                list = g_slist_append (list, gdk_screen_get_default ());
        } else {
                for (i = 0; i < n_screens; i++) {
                        GdkScreen *screen;

                        screen = gdk_display_get_screen (display, i);
                        if (screen != NULL) {
                                list = g_slist_prepend (list, screen);
                        }
                }
                list = g_slist_reverse (list);
        }

        return list;
}

static char *
entry_get_string (GConfEntry *entry)
{
        GConfValue *value = gconf_entry_get_value (entry);

        if (value == NULL || value->type != GCONF_VALUE_STRING) {
                return NULL;
        }

        return g_strdup (gconf_value_get_string (value));
}

static gboolean
parse_binding (Binding *binding)
{
        g_return_val_if_fail (binding != NULL, FALSE);

        binding->key.keysym = 0;
        binding->key.state = 0;

        if (binding->binding_str == NULL ||
            binding->binding_str[0] == '\0' ||
            strcmp (binding->binding_str, "Disabled") == 0) {
                return FALSE;
        }

        if (egg_accelerator_parse_virtual (binding->binding_str,
                                           &binding->key.keysym,
                                           &binding->key.keycode,
                                           &binding->key.state) == FALSE) {
                return FALSE;
        }

        return TRUE;
}

static gint
compare_bindings (gconstpointer a,
                  gconstpointer b)
{
        Binding *key_a = (Binding *) a;
        char    *key_b = (char *) b;

        return strcmp (key_b, key_a->gconf_key);
}

static gboolean
bindings_get_entry (GsdKeybindingsManager *manager,
                    GConfClient           *client,
                    const char            *subdir)
{
        Binding *new_binding;
        GSList  *tmp_elem;
        GSList  *list;
        GSList  *li;
        char    *gconf_key;
        char    *action = NULL;
        char    *key = NULL;

        g_return_val_if_fail (subdir != NULL, FALSE);

        gconf_key = g_path_get_basename (subdir);

        if (!gconf_key) {
                return FALSE;
        }

        /* Get entries for this binding */
        list = gconf_client_all_entries (client, subdir, NULL);

        for (li = list; li != NULL; li = li->next) {
                GConfEntry *entry = li->data;
                char *key_name = g_path_get_basename (gconf_entry_get_key (entry));

                if (key_name == NULL) {
                        /* ignore entry */
                } else if (strcmp (key_name, "action") == 0) {
                        if (!action) {
                                action = entry_get_string (entry);
                        } else {
                                g_warning (_("Key binding (%s) has its action defined multiple times"),
                                           gconf_key);
                        }
                } else if (strcmp (key_name, "binding") == 0) {
                        if (!key) {
                                key = entry_get_string (entry);
                        } else {
                                g_warning (_("Key binding (%s) has its binding defined multiple times"),
                                           gconf_key);
                        }
                }

                g_free (key_name);
                gconf_entry_free (entry);
        }

        g_slist_free (list);

        if (!action || !key) {
                g_warning (_("Key binding (%s) is incomplete"), gconf_key);
                g_free (gconf_key);
                g_free (action);
                g_free (key);
                return FALSE;
        }

        tmp_elem = g_slist_find_custom (manager->priv->binding_list,
                                        gconf_key,
                                        compare_bindings);

        if (!tmp_elem) {
                new_binding = g_new0 (Binding, 1);
        } else {
                new_binding = (Binding *) tmp_elem->data;
                g_free (new_binding->binding_str);
                g_free (new_binding->action);
                g_free (new_binding->gconf_key);
        }

        new_binding->binding_str = key;
        new_binding->action = action;
        new_binding->gconf_key = gconf_key;

        new_binding->previous_key.keysym = new_binding->key.keysym;
        new_binding->previous_key.state = new_binding->key.state;
        new_binding->previous_key.keycode = new_binding->key.keycode;

        if (parse_binding (new_binding)) {
                if (!tmp_elem)
                        manager->priv->binding_list = g_slist_prepend (manager->priv->binding_list, new_binding);
        } else {
                g_warning (_("Key binding (%s) is invalid"), gconf_key);
                g_free (new_binding->binding_str);
                g_free (new_binding->action);
                g_free (new_binding->gconf_key);
                g_free (new_binding);

                if (tmp_elem)
                        manager->priv->binding_list = g_slist_delete_link (manager->priv->binding_list, tmp_elem);
                return FALSE;
        }

        return TRUE;
}


static gboolean
key_already_used (GsdKeybindingsManager *manager,
                  Binding               *binding)
{
        GSList *li;

        for (li = manager->priv->binding_list; li != NULL; li = li->next) {
                Binding *tmp_binding =  (Binding*) li->data;

                if (tmp_binding != binding &&  tmp_binding->key.keycode == binding->key.keycode &&
                    tmp_binding->key.state == binding->key.state) {
                        return TRUE;
                }
        }

        return FALSE;
}

static void
grab_key (GdkWindow *root,
          Key       *key,
          int        result,
          gboolean   grab)
{
        gdk_error_trap_push ();

        if (grab) {
                XGrabKey (GDK_DISPLAY (),
                          key->keycode,
                          (result | key->state),
                          GDK_WINDOW_XID (root),
                          True,
                          GrabModeAsync,
                          GrabModeAsync);
        } else {
                XUngrabKey (GDK_DISPLAY (),
                            key->keycode,
                            (result | key->state),
                            GDK_WINDOW_XID (root));
        }
        gdk_flush ();
        if (gdk_error_trap_pop ()) {
                g_warning (_("It seems that another application already has access to key '%u'."),
                           key->keycode);
        }
}

/* Grab the key. In order to ignore IGNORED_MODS we need to grab
 * all combinations of the ignored modifiers and those actually used
 * for the binding (if any).
 *
 * inspired by all_combinations from gnome-panel/gnome-panel/global-keys.c */
#define N_BITS 32
static void
do_grab (GsdKeybindingsManager *manager,
         gboolean               grab,
         Key                   *key)
{
        int   indexes[N_BITS];/*indexes of bits we need to flip*/
        int   i;
        int   bit;
        int   bits_set_cnt;
        int   uppervalue;
        guint mask_to_traverse = IGNORED_MODS & ~key->state & GDK_MODIFIER_MASK;

        bit = 0;
        for (i = 0; i < N_BITS; i++) {
                if (mask_to_traverse & (1<<i)) {
                        indexes[bit++]=i;
                }
        }

        bits_set_cnt = bit;

        uppervalue = 1 << bits_set_cnt;
        for (i = 0; i < uppervalue; i++) {
                GSList *l;
                int j, result = 0;

                for (j = 0; j < bits_set_cnt; j++) {
                        if (i & (1<<j)) {
                                result |= (1<<indexes[j]);
                        }
                }

                for (l = manager->priv->screens; l ; l = l->next) {
                  GdkScreen *screen = l->data;
                  grab_key (gdk_screen_get_root_window (screen),
                            key,
                            result,
                            grab);
                }
        }
}

static void
binding_register_keys (GsdKeybindingsManager *manager)
{
        GSList *li;

        gdk_error_trap_push ();

        /* Now check for changes and grab new key if not already used */
        for (li = manager->priv->binding_list ; li != NULL; li = li->next) {
                Binding *binding = (Binding *) li->data;

                if (binding->previous_key.keycode != binding->key.keycode ||
                    binding->previous_key.state != binding->key.state) {
                        /* Ungrab key if it changed and not clashing with previously set binding */
                        if (! key_already_used (manager, binding)) {
                                if (binding->previous_key.keycode) {
                                        do_grab (manager, FALSE, &binding->previous_key);
                                }
                                do_grab (manager, TRUE, &binding->key);

                                binding->previous_key.keysym = binding->key.keysym;
                                binding->previous_key.state = binding->key.state;
                                binding->previous_key.keycode = binding->key.keycode;
                        } else
                                g_warning (_("Key Binding (%s) is already in use"), binding->binding_str);
                }
        }
        gdk_flush ();
        gdk_error_trap_pop ();
}

extern char **environ;

static char *
screen_exec_display_string (GdkScreen *screen)
{
        GString    *str;
        const char *old_display;
        char       *p;

        g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

        old_display = gdk_display_get_name (gdk_screen_get_display (screen));

        str = g_string_new ("DISPLAY=");
        g_string_append (str, old_display);

        p = strrchr (str->str, '.');
        if (p && p >  strchr (str->str, ':')) {
                g_string_truncate (str, p - str->str);
        }

        g_string_append_printf (str, ".%d", gdk_screen_get_number (screen));

        return g_string_free (str, FALSE);
}

/**
 * get_exec_environment:
 *
 * Description: Modifies the current program environment to
 * ensure that $DISPLAY is set such that a launched application
 * inheriting this environment would appear on screen.
 *
 * Returns: a newly-allocated %NULL-terminated array of strings or
 * %NULL on error. Use g_strfreev() to free it.
 *
 * mainly ripped from egg_screen_exec_display_string in
 * gnome-panel/egg-screen-exec.c
 **/
static char **
get_exec_environment (XEvent *xevent)
{
        char     **retval = NULL;
        int        i;
        int        display_index = -1;
        GdkScreen *screen = NULL;
        GdkWindow *window = gdk_xid_table_lookup (xevent->xkey.root);

        if (window) {
                screen = gdk_drawable_get_screen (GDK_DRAWABLE (window));
        }

        g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

        for (i = 0; environ [i]; i++) {
                if (!strncmp (environ [i], "DISPLAY", 7)) {
                        display_index = i;
                }
        }

        if (display_index == -1) {
                display_index = i++;
        }

        retval = g_new (char *, i + 1);

        for (i = 0; environ [i]; i++) {
                if (i == display_index) {
                        retval [i] = screen_exec_display_string (screen);
                } else {
                        retval [i] = g_strdup (environ [i]);
                }
        }

        retval [i] = NULL;

        return retval;
}

static GdkFilterReturn
keybindings_filter (GdkXEvent             *gdk_xevent,
                    GdkEvent              *event,
                    GsdKeybindingsManager *manager)
{
        XEvent *xevent = (XEvent *)gdk_xevent;
        guint   keycode;
        guint   state;
        GSList *li;

        if (xevent->type != KeyPress) {
                return GDK_FILTER_CONTINUE;
        }

        keycode = xevent->xkey.keycode;
        state = xevent->xkey.state;

        for (li = manager->priv->binding_list; li != NULL; li = li->next) {
                Binding *binding = (Binding*) li->data;

                if (keycode == binding->key.keycode &&
                    (state & USED_MODS) == binding->key.state) {
                        GError  *error = NULL;
                        gboolean retval;
                        gchar  **argv = NULL;
                        gchar  **envp = NULL;

                        g_return_val_if_fail (binding->action != NULL, GDK_FILTER_CONTINUE);

                        if (!g_shell_parse_argv (binding->action,
                                                 NULL, &argv,
                                                 &error)) {
                                return GDK_FILTER_CONTINUE;
                        }

                        envp = get_exec_environment (xevent);

                        retval = g_spawn_async (NULL,
                                                argv,
                                                envp,
                                                G_SPAWN_SEARCH_PATH,
                                                NULL,
                                                NULL,
                                                NULL,
                                                &error);
                        g_strfreev (argv);
                        g_strfreev (envp);

                        if (!retval) {
                                GtkWidget *dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_WARNING,
                                                                            GTK_BUTTONS_CLOSE,
                                                                            _("Error while trying to run (%s)\n"\
                                                                              "which is linked to the key (%s)"),
                                                                            binding->action,
                                                                            binding->binding_str);
                                g_signal_connect (dialog,
                                                  "response",
                                                  G_CALLBACK (gtk_widget_destroy),
                                                  NULL);
                                gtk_widget_show (dialog);
                        }
                        return GDK_FILTER_REMOVE;
                }
        }
        return GDK_FILTER_CONTINUE;
}

static void
bindings_callback (GConfClient           *client,
                   guint                  cnxn_id,
                   GConfEntry            *entry,
                   GsdKeybindingsManager *manager)
{
        char** key_elems;
        char* binding_entry;

        /* ensure we get binding dir not a sub component */

        key_elems = g_strsplit (gconf_entry_get_key (entry), "/", 15);
        binding_entry = g_strdup_printf ("/%s/%s/%s/%s",
                                         key_elems[1],
                                         key_elems[2],
                                         key_elems[3],
                                         key_elems[4]);
        g_strfreev (key_elems);

        bindings_get_entry (manager, client, binding_entry);
        g_free (binding_entry);

        binding_register_keys (manager);
}

static guint
register_config_callback (GsdKeybindingsManager   *manager,
                          GConfClient             *client,
                          const char              *path,
                          GConfClientNotifyFunc    func)
{
        gconf_client_add_dir (client, path, GCONF_CLIENT_PRELOAD_NONE, NULL);
        return gconf_client_notify_add (client, path, func, manager, NULL, NULL);
}

gboolean
gsd_keybindings_manager_start (GsdKeybindingsManager *manager,
                               GError               **error)
{
        GConfClient *client;
        GSList      *list;
        GSList      *li;
        GdkDisplay  *dpy;
        GdkScreen   *screen;
        int          screen_num;
        int          i;

        g_debug ("Starting keybindings manager");
        gnome_settings_profile_start (NULL);

        dpy = gdk_display_get_default ();
        screen_num = gdk_display_get_n_screens (dpy);

        for (i = 0; i < screen_num; i++) {
                screen = gdk_display_get_screen (dpy, i);
                gdk_window_add_filter (gdk_screen_get_root_window (screen),
                                       (GdkFilterFunc) keybindings_filter,
                                       manager);
        }

        client = gconf_client_get_default ();

        manager->priv->notify = register_config_callback (manager,
                                                          client,
                                                          GCONF_BINDING_DIR,
                                                          (GConfClientNotifyFunc) bindings_callback);

        list = gconf_client_all_dirs (client, GCONF_BINDING_DIR, NULL);
        manager->priv->screens = get_screens_list ();

        for (li = list; li != NULL; li = li->next) {
                bindings_get_entry (manager, client, li->data);
                g_free (li->data);
        }

        g_slist_free (list);
        g_object_unref (client);

        binding_register_keys (manager);

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_keybindings_manager_stop (GsdKeybindingsManager *manager)
{
        GsdKeybindingsManagerPrivate *p = manager->priv;
        GSList *l;

        g_debug ("Stopping keybindings manager");

        if (p->notify != 0) {
                GConfClient *client = gconf_client_get_default ();
                gconf_client_remove_dir (client, GCONF_BINDING_DIR, NULL);
                gconf_client_notify_remove (client, p->notify);
                g_object_unref (client);
                p->notify = 0;
        }

        for (l = p->screens; l; l = l->next) {
                GdkScreen *screen = l->data;
                gdk_window_remove_filter (gdk_screen_get_root_window (screen),
                                          (GdkFilterFunc) keybindings_filter,
                                          manager);
        }
        g_slist_free (p->screens);
        p->screens = NULL;

        for (l = p->binding_list; l; l = l->next) {
                Binding *b = l->data;
                g_free (b->binding_str);
                g_free (b->action);
                g_free (b->gconf_key);
                g_free (b);
        }
        g_slist_free (p->binding_list);
        p->binding_list = NULL;
}

static void
gsd_keybindings_manager_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdKeybindingsManager *self;

        self = GSD_KEYBINDINGS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_keybindings_manager_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdKeybindingsManager *self;

        self = GSD_KEYBINDINGS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_keybindings_manager_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_properties)
{
        GsdKeybindingsManager      *keybindings_manager;
        GsdKeybindingsManagerClass *klass;

        klass = GSD_KEYBINDINGS_MANAGER_CLASS (g_type_class_peek (GSD_TYPE_KEYBINDINGS_MANAGER));

        keybindings_manager = GSD_KEYBINDINGS_MANAGER (G_OBJECT_CLASS (gsd_keybindings_manager_parent_class)->constructor (type,
                                                                                                      n_construct_properties,
                                                                                                      construct_properties));

        return G_OBJECT (keybindings_manager);
}

static void
gsd_keybindings_manager_dispose (GObject *object)
{
        GsdKeybindingsManager *keybindings_manager;

        keybindings_manager = GSD_KEYBINDINGS_MANAGER (object);

        G_OBJECT_CLASS (gsd_keybindings_manager_parent_class)->dispose (object);
}

static void
gsd_keybindings_manager_class_init (GsdKeybindingsManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_keybindings_manager_get_property;
        object_class->set_property = gsd_keybindings_manager_set_property;
        object_class->constructor = gsd_keybindings_manager_constructor;
        object_class->dispose = gsd_keybindings_manager_dispose;
        object_class->finalize = gsd_keybindings_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdKeybindingsManagerPrivate));
}

static void
gsd_keybindings_manager_init (GsdKeybindingsManager *manager)
{
        manager->priv = GSD_KEYBINDINGS_MANAGER_GET_PRIVATE (manager);

}

static void
gsd_keybindings_manager_finalize (GObject *object)
{
        GsdKeybindingsManager *keybindings_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_KEYBINDINGS_MANAGER (object));

        keybindings_manager = GSD_KEYBINDINGS_MANAGER (object);

        g_return_if_fail (keybindings_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_keybindings_manager_parent_class)->finalize (object);
}

GsdKeybindingsManager *
gsd_keybindings_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_KEYBINDINGS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_KEYBINDINGS_MANAGER (manager_object);
}
