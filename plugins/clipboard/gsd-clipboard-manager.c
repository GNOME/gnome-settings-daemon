/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Matthias Clasen
 * Copyright (C) 2007 Anders Carlsson
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
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "xutils.h"
#include "list.h"

#include "gnome-settings-profile.h"
#include "gsd-clipboard-manager.h"

struct _GsdClipboardManager
{
        GObject  parent;

        guint    start_idle_id;
        Display *display;
        Window   window;
        Time     timestamp;

        List    *contents;
        List    *conversions;

        Window   requestor;
        Atom     property;
        Time     time;
};

typedef struct
{
        unsigned char *data;
        int            length;
        Atom           target;
        Atom           type;
        int            format;
        int            refcount;
} TargetData;

typedef struct
{
        Atom        target;
        TargetData *data;
        Atom        property;
        Window      requestor;
        int         offset;
} IncrConversion;

static void     gsd_clipboard_manager_class_init  (GsdClipboardManagerClass *klass);
static void     gsd_clipboard_manager_init        (GsdClipboardManager      *clipboard_manager);
static void     gsd_clipboard_manager_finalize    (GObject                  *object);

static void     clipboard_manager_watch_cb        (GsdClipboardManager *manager,
                                                   Window               window,
                                                   Bool                 is_start,
                                                   long                 mask,
                                                   void                *cb_data);

G_DEFINE_TYPE (GsdClipboardManager, gsd_clipboard_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

/* We need to use reference counting for the target data, since we may
 * need to keep the data around after loosing the CLIPBOARD ownership
 * to complete incremental transfers.
 */
static TargetData *
target_data_ref (TargetData *data)
{
        data->refcount++;
        return data;
}

static void
target_data_unref (TargetData *data)
{
        data->refcount--;
        if (data->refcount == 0) {
                free (data->data);
                free (data);
        }
}

static void
conversion_free (IncrConversion *rdata)
{
        if (rdata->data) {
                target_data_unref (rdata->data);
        }
        free (rdata);
}

static void
send_selection_notify (GsdClipboardManager *manager,
                       Bool                 success)
{
        XSelectionEvent notify;
        GdkDisplay *display = gdk_display_get_default ();

        notify.type = SelectionNotify;
        notify.serial = 0;
        notify.send_event = True;
        notify.display = manager->display;
        notify.requestor = manager->requestor;
        notify.selection = XA_CLIPBOARD_MANAGER;
        notify.target = XA_SAVE_TARGETS;
        notify.property = success ? manager->property : None;
        notify.time = manager->time;

        gdk_x11_display_error_trap_push (display);

        XSendEvent (manager->display,
                    manager->requestor,
                    False,
                    NoEventMask,
                    (XEvent *)&notify);
        XSync (manager->display, False);

        gdk_x11_display_error_trap_pop_ignored (display);
}

static void
finish_selection_request (GsdClipboardManager *manager,
                          XEvent              *xev,
                          Bool                 success)
{
        XSelectionEvent notify;
        GdkDisplay *display = gdk_display_get_default ();

        notify.type = SelectionNotify;
        notify.serial = 0;
        notify.send_event = True;
        notify.display = xev->xselectionrequest.display;
        notify.requestor = xev->xselectionrequest.requestor;
        notify.selection = xev->xselectionrequest.selection;
        notify.target = xev->xselectionrequest.target;
        notify.property = success ? xev->xselectionrequest.property : None;
        notify.time = xev->xselectionrequest.time;

        gdk_x11_display_error_trap_push (display);

        XSendEvent (xev->xselectionrequest.display,
                    xev->xselectionrequest.requestor,
                    False, NoEventMask, (XEvent *) &notify);
        XSync (manager->display, False);

        gdk_x11_display_error_trap_pop_ignored (display);
}

static gsize
clipboard_bytes_per_item (int format)
{
        switch (format) {
        case 8: return sizeof (char);
        case 16: return sizeof (short);
        case 32: return sizeof (long);
        default: ;
        }

        return 0;
}

static void
free_contents (GsdClipboardManager *manager)
{
        list_foreach (manager->contents, (Callback)target_data_unref, NULL);
        list_free (manager->contents);
        manager->contents = NULL;
}

static void
save_targets (GsdClipboardManager *manager,
              Atom                *save_targets,
              int                  nitems)
{
        int         nout, i;
        Atom       *multiple;
        TargetData *tdata;

        multiple = (Atom *) malloc (2 * nitems * sizeof (Atom));

        nout = 0;
        for (i = 0; i < nitems; i++) {
                if (save_targets[i] != XA_TARGETS &&
                    save_targets[i] != XA_MULTIPLE &&
                    save_targets[i] != XA_DELETE &&
                    save_targets[i] != XA_INSERT_PROPERTY &&
                    save_targets[i] != XA_INSERT_SELECTION &&
                    save_targets[i] != XA_PIXMAP) {
                        tdata = (TargetData *) malloc (sizeof (TargetData));
                        tdata->data = NULL;
                        tdata->length = 0;
                        tdata->target = save_targets[i];
                        tdata->type = None;
                        tdata->format = 0;
                        tdata->refcount = 1;
                        manager->contents = list_prepend (manager->contents, tdata);

                        multiple[nout++] = save_targets[i];
                        multiple[nout++] = save_targets[i];
                }
        }

        XFree (save_targets);

        XChangeProperty (manager->display, manager->window,
                         XA_MULTIPLE, XA_ATOM_PAIR,
                         32, PropModeReplace, (const unsigned char *) multiple, nout);
        free (multiple);

        XConvertSelection (manager->display, XA_CLIPBOARD,
                           XA_MULTIPLE, XA_MULTIPLE,
                           manager->window, manager->time);
}

static int
find_content_target (TargetData *tdata,
                     Atom        target)
{
        return tdata->target == target;
}

static int
find_content_type (TargetData *tdata,
                   Atom        type)
{
        return tdata->type == type;
}

static int
find_conversion_requestor (IncrConversion *rdata,
                           XEvent         *xev)
{
        return (rdata->requestor == xev->xproperty.window &&
                rdata->property == xev->xproperty.atom);
}

static void
get_property (TargetData          *tdata,
              GsdClipboardManager *manager)
{
        Atom           type;
        int            format;
        unsigned long  length;
        unsigned long  remaining;
        unsigned char *data;

        XGetWindowProperty (manager->display,
                            manager->window,
                            tdata->target,
                            0,
                            0x1FFFFFFF,
                            True,
                            AnyPropertyType,
                            &type,
                            &format,
                            &length,
                            &remaining,
                            &data);

        if (type == None) {
                manager->contents = list_remove (manager->contents, tdata);
                free (tdata);
        } else if (type == XA_INCR) {
                tdata->type = type;
                tdata->length = 0;
                XFree (data);
        } else {
                tdata->type = type;
                tdata->data = data;
                tdata->length = length * clipboard_bytes_per_item (format);
                tdata->format = format;
        }
}

static Bool
receive_incrementally (GsdClipboardManager *manager,
                       XEvent              *xev)
{
        List          *list;
        TargetData    *tdata;
        Atom           type;
        int            format;
        unsigned long  length, nitems, remaining;
        unsigned char *data;

        if (xev->xproperty.window != manager->window)
                return False;

        list = list_find (manager->contents,
                          (ListFindFunc) find_content_target, (void *) xev->xproperty.atom);

        if (!list)
                return False;

        tdata = (TargetData *) list->data;

        if (tdata->type != XA_INCR)
                return False;

        XGetWindowProperty (xev->xproperty.display,
                            xev->xproperty.window,
                            xev->xproperty.atom,
                            0, 0x1FFFFFFF, True, AnyPropertyType,
                            &type, &format, &nitems, &remaining, &data);

        length = nitems * clipboard_bytes_per_item (format);
        if (length == 0) {
                tdata->type = type;
                tdata->format = format;

                if (!list_find (manager->contents,
                                (ListFindFunc) find_content_type, (void *)XA_INCR)) {
                        /* all incremental transfers done */
                        send_selection_notify (manager, True);
                        manager->requestor = None;
                }

                XFree (data);
        } else {
                if (!tdata->data) {
                        tdata->data = data;
                        tdata->length = length;
                } else {
                        tdata->data = realloc (tdata->data, tdata->length + length + 1);
                        memcpy (tdata->data + tdata->length, data, length + 1);
                        tdata->length += length;
                        XFree (data);
                }
        }

        return True;
}

static Bool
send_incrementally (GsdClipboardManager *manager,
                    XEvent              *xev)
{
        List           *list;
        IncrConversion *rdata;
        unsigned long   length;
        unsigned long   items;
        unsigned char  *data;
        gsize           bytes_per_item;

        list = list_find (manager->conversions,
                          (ListFindFunc) find_conversion_requestor, xev);
        if (list == NULL)
                return False;

        rdata = (IncrConversion *) list->data;

        bytes_per_item = clipboard_bytes_per_item (rdata->data->format);
        if (bytes_per_item == 0)
                return False;

        data = rdata->data->data + rdata->offset;
        length = rdata->data->length - rdata->offset;
        if (length > SELECTION_MAX_SIZE)
                length = SELECTION_MAX_SIZE;

        rdata->offset += length;

        items = length / bytes_per_item;
        XChangeProperty (manager->display, rdata->requestor,
                         rdata->property, rdata->data->type,
                         rdata->data->format, PropModeAppend,
                         data, items);

        if (length == 0) {
                clipboard_manager_watch_cb (manager,
                                            rdata->requestor,
                                            False,
                                            PropertyChangeMask,
                                            NULL);

                manager->conversions = list_remove (manager->conversions, rdata);
                conversion_free (rdata);
        }

        return True;
}

static void
convert_clipboard_manager (GsdClipboardManager *manager,
                           XEvent              *xev)
{
        Atom          type = None;
        int           format;
        unsigned long nitems;
        unsigned long remaining;
        Atom         *targets = NULL;
        GdkDisplay   *display = gdk_display_get_default ();

        if (xev->xselectionrequest.target == XA_SAVE_TARGETS) {
                if (manager->requestor != None || manager->contents != NULL) {
                        /* We're in the middle of a conversion request, or own
                         * the CLIPBOARD already
                         */
                        finish_selection_request (manager, xev, False);
                } else {
                        gdk_x11_display_error_trap_push (display);

                        clipboard_manager_watch_cb (manager,
                                                    xev->xselectionrequest.requestor,
                                                    True,
                                                    StructureNotifyMask,
                                                    NULL);
                        XSelectInput (manager->display,
                                      xev->xselectionrequest.requestor,
                                      StructureNotifyMask);
                        XSync (manager->display, False);

                        if (gdk_x11_display_error_trap_pop (display) != Success)
                                return;

                        if (xev->xselectionrequest.property != None) {
                                gdk_x11_display_error_trap_push (display);

                                XGetWindowProperty (manager->display,
                                                    xev->xselectionrequest.requestor,
                                                    xev->xselectionrequest.property,
                                                    0, 0x1FFFFFFF, False, XA_ATOM,
                                                    &type, &format, &nitems, &remaining,
                                                    (unsigned char **) &targets);

                                if (gdk_x11_display_error_trap_pop (display) != Success) {
                                        if (targets)
                                                XFree (targets);

                                        return;
                                }
                        }

                        manager->requestor = xev->xselectionrequest.requestor;
                        manager->property = xev->xselectionrequest.property;
                        manager->time = xev->xselectionrequest.time;

                        if (type == None)
                                XConvertSelection (manager->display, XA_CLIPBOARD,
                                                   XA_TARGETS, XA_TARGETS,
                                                   manager->window, manager->time);
                        else
                                save_targets (manager, targets, nitems);
                }
        } else if (xev->xselectionrequest.target == XA_TIMESTAMP) {
                XChangeProperty (manager->display,
                                 xev->xselectionrequest.requestor,
                                 xev->xselectionrequest.property,
                                 XA_INTEGER, 32, PropModeReplace,
                                 (unsigned char *) &manager->timestamp, 1);

                finish_selection_request (manager, xev, True);
        } else if (xev->xselectionrequest.target == XA_TARGETS) {
                int  n_targets = 0;
                Atom targets[3];

                targets[n_targets++] = XA_TARGETS;
                targets[n_targets++] = XA_TIMESTAMP;
                targets[n_targets++] = XA_SAVE_TARGETS;

                XChangeProperty (manager->display,
                                 xev->xselectionrequest.requestor,
                                 xev->xselectionrequest.property,
                                 XA_ATOM, 32, PropModeReplace,
                                 (unsigned char *) targets, n_targets);

                finish_selection_request (manager, xev, True);
        } else
                finish_selection_request (manager, xev, False);
}

static void
convert_clipboard_target (IncrConversion      *rdata,
                          GsdClipboardManager *manager)
{
        TargetData       *tdata;
        Atom             *targets;
        int               n_targets;
        List             *list;
        unsigned long     items;
        XWindowAttributes atts;
        GdkDisplay       *display = gdk_display_get_default ();

        if (rdata->target == XA_TARGETS) {
                n_targets = list_length (manager->contents) + 2;
                targets = (Atom *) malloc (n_targets * sizeof (Atom));

                n_targets = 0;

                targets[n_targets++] = XA_TARGETS;
                targets[n_targets++] = XA_MULTIPLE;

                for (list = manager->contents; list; list = list->next) {
                        tdata = (TargetData *) list->data;
                        targets[n_targets++] = tdata->target;
                }

                XChangeProperty (manager->display, rdata->requestor,
                                 rdata->property,
                                 XA_ATOM, 32, PropModeReplace,
                                 (unsigned char *) targets, n_targets);
                free (targets);
        } else  {
                gsize bytes_per_item;

                /* Convert from stored CLIPBOARD data */
                list = list_find (manager->contents,
                                  (ListFindFunc) find_content_target, (void *) rdata->target);

                /* We got a target that we don't support */
                if (!list)
                        return;

                tdata = (TargetData *)list->data;
                if (tdata->type == XA_INCR) {
                        /* we haven't completely received this target yet  */
                        rdata->property = None;
                        return;
                }

                bytes_per_item = clipboard_bytes_per_item (tdata->format);
                if (bytes_per_item == 0)
                        return;

                rdata->data = target_data_ref (tdata);
                items = tdata->length / bytes_per_item;
                if (tdata->length <= SELECTION_MAX_SIZE)
                        XChangeProperty (manager->display, rdata->requestor,
                                         rdata->property,
                                         tdata->type, tdata->format, PropModeReplace,
                                         tdata->data, items);
                else {
                        /* start incremental transfer */
                        rdata->offset = 0;

                        gdk_x11_display_error_trap_push (display);

                        XGetWindowAttributes (manager->display, rdata->requestor, &atts);

                        clipboard_manager_watch_cb (manager,
                                                    rdata->requestor,
                                                    True,
                                                    PropertyChangeMask,
                                                    NULL);

                        XSelectInput (manager->display, rdata->requestor,
                                      atts.your_event_mask | PropertyChangeMask);

                        XChangeProperty (manager->display, rdata->requestor,
                                         rdata->property,
                                         XA_INCR, 32, PropModeReplace,
                                         (unsigned char *) &items, 1);

                        XSync (manager->display, False);

                        gdk_x11_display_error_trap_pop_ignored (display);
                }
        }
}

static void
collect_incremental (IncrConversion      *rdata,
                     GsdClipboardManager *manager)
{
        if (rdata->offset >= 0)
                manager->conversions = list_prepend (manager->conversions, rdata);
        else {
                if (rdata->data) {
                        target_data_unref (rdata->data);
                        rdata->data = NULL;
                }
                free (rdata);
        }
}

static void
convert_clipboard (GsdClipboardManager *manager,
                   XEvent              *xev)
{
        List           *list;
        List           *conversions;
        IncrConversion *rdata;
        Atom            type;
        int             i;
        int             format;
        unsigned long   nitems;
        unsigned long   remaining;
        Atom           *multiple;

        conversions = NULL;
        type = None;

        if (xev->xselectionrequest.target == XA_MULTIPLE) {
                XGetWindowProperty (xev->xselectionrequest.display,
                                    xev->xselectionrequest.requestor,
                                    xev->xselectionrequest.property,
                                    0, 0x1FFFFFFF, False, XA_ATOM_PAIR,
                                    &type, &format, &nitems, &remaining,
                                    (unsigned char **) &multiple);

                if (type != XA_ATOM_PAIR || nitems == 0) {
                        if (multiple)
                                free (multiple);
                        return;
                }

                for (i = 0; i < nitems; i += 2) {
                        rdata = (IncrConversion *) malloc (sizeof (IncrConversion));
                        rdata->requestor = xev->xselectionrequest.requestor;
                        rdata->target = multiple[i];
                        rdata->property = multiple[i+1];
                        rdata->data = NULL;
                        rdata->offset = -1;
                        conversions = list_prepend (conversions, rdata);
                }
        } else {
                multiple = NULL;

                rdata = (IncrConversion *) malloc (sizeof (IncrConversion));
                rdata->requestor = xev->xselectionrequest.requestor;
                rdata->target = xev->xselectionrequest.target;
                rdata->property = xev->xselectionrequest.property;
                rdata->data = NULL;
                rdata->offset = -1;
                conversions = list_prepend (conversions, rdata);
        }

        list_foreach (conversions, (Callback) convert_clipboard_target, manager);

        if (conversions->next == NULL &&
            ((IncrConversion *) conversions->data)->property == None) {
                finish_selection_request (manager, xev, False);
        } else {
                if (multiple) {
                        i = 0;
                        for (list = conversions; list; list = list->next) {
                                rdata = (IncrConversion *)list->data;
                                multiple[i++] = rdata->target;
                                multiple[i++] = rdata->property;
                        }
                        XChangeProperty (xev->xselectionrequest.display,
                                         xev->xselectionrequest.requestor,
                                         xev->xselectionrequest.property,
                                         XA_ATOM_PAIR, 32, PropModeReplace,
                                         (unsigned char *) multiple, nitems);
                }
                finish_selection_request (manager, xev, True);
        }

        list_foreach (conversions, (Callback) collect_incremental, manager);
        list_free (conversions);

        if (multiple)
                free (multiple);
}

static Bool
clipboard_manager_process_event (GsdClipboardManager *manager,
                                 XEvent              *xev)
{
        Atom          type;
        int           format;
        unsigned long nitems;
        unsigned long remaining;
        Atom         *targets;

        targets = NULL;

        switch (xev->xany.type) {
        case DestroyNotify:
                if (xev->xdestroywindow.window == manager->requestor) {
                        free_contents (manager);

                        clipboard_manager_watch_cb (manager,
                                                    manager->requestor,
                                                    False,
                                                    0,
                                                    NULL);
                        manager->requestor = None;
                }
                break;
        case PropertyNotify:
                if (xev->xproperty.state == PropertyNewValue) {
                        return receive_incrementally (manager, xev);
                } else {
                        return send_incrementally (manager, xev);
                }

        case SelectionClear:
                if (xev->xany.window != manager->window)
                        return False;

                if (xev->xselectionclear.selection == XA_CLIPBOARD_MANAGER) {
                        /* We lost the manager selection */
                        if (manager->contents) {
                                free_contents (manager);

                                XSetSelectionOwner (manager->display,
                                                    XA_CLIPBOARD,
                                                    None, manager->time);
                        }

                        return True;
                }
                if (xev->xselectionclear.selection == XA_CLIPBOARD) {
                        /* We lost the clipboard selection */
                        free_contents (manager);
                        clipboard_manager_watch_cb (manager,
                                                    manager->requestor,
                                                    False,
                                                    0,
                                                    NULL);
                        manager->requestor = None;

                        return True;
                }
                break;

        case SelectionNotify:
                if (xev->xany.window != manager->window)
                        return False;

                if (xev->xselection.selection == XA_CLIPBOARD) {
                        /* a CLIPBOARD conversion is done */
                        if (xev->xselection.property == XA_TARGETS) {
                                XGetWindowProperty (xev->xselection.display,
                                                    xev->xselection.requestor,
                                                    xev->xselection.property,
                                                    0, 0x1FFFFFFF, True, XA_ATOM,
                                                    &type, &format, &nitems, &remaining,
                                                    (unsigned char **) &targets);

                                save_targets (manager, targets, nitems);
                        } else if (xev->xselection.property == XA_MULTIPLE) {
                                List *tmp;

                                tmp = list_copy (manager->contents);
                                list_foreach (tmp, (Callback) get_property, manager);
                                list_free (tmp);

                                manager->time = xev->xselection.time;
                                XSetSelectionOwner (manager->display, XA_CLIPBOARD,
                                                    manager->window, manager->time);

                                if (manager->property != None)
                                        XChangeProperty (manager->display,
                                                         manager->requestor,
                                                         manager->property,
                                                         XA_ATOM, 32, PropModeReplace,
                                                         (unsigned char *)&XA_NULL, 1);

                                if (!list_find (manager->contents,
                                                (ListFindFunc)find_content_type, (void *)XA_INCR)) {
                                        /* all transfers done */
                                        send_selection_notify (manager, True);
                                        clipboard_manager_watch_cb (manager,
                                                                    manager->requestor,
                                                                    False,
                                                                    0,
                                                                    NULL);
                                        manager->requestor = None;
                                }
                        }
                        else if (xev->xselection.property == None) {
                                send_selection_notify (manager, False);

                                free_contents (manager);
                                clipboard_manager_watch_cb (manager,
                                                            manager->requestor,
                                                            False,
                                                            0,
                                                            NULL);
                                manager->requestor = None;
                        }

                        return True;
                }
                break;

        case SelectionRequest:
                if (xev->xany.window != manager->window) {
                        return False;
                }

                if (xev->xselectionrequest.selection == XA_CLIPBOARD_MANAGER) {
                        convert_clipboard_manager (manager, xev);
                        return True;
                } else if (xev->xselectionrequest.selection == XA_CLIPBOARD) {
                        convert_clipboard (manager, xev);
                        return True;
                }
                break;

        default: ;
        }

        return False;
}

static GdkFilterReturn
clipboard_manager_event_filter (GdkXEvent           *xevent,
                                GdkEvent            *event,
                                GsdClipboardManager *manager)
{
        if (clipboard_manager_process_event (manager, (XEvent *)xevent)) {
                return GDK_FILTER_REMOVE;
        } else {
                return GDK_FILTER_CONTINUE;
        }
}

static void
clipboard_manager_watch_cb (GsdClipboardManager *manager,
                            Window               window,
                            Bool                 is_start,
                            long                 mask,
                            void                *cb_data)
{
        GdkWindow  *gdkwin;
        GdkDisplay *display;

        display = gdk_display_get_default ();
        gdkwin = gdk_x11_window_lookup_for_display (display, window);

        if (is_start) {
                if (gdkwin == NULL) {
                        gdkwin = gdk_x11_window_foreign_new_for_display (display, window);
                } else {
                        g_object_ref (gdkwin);
                }

                gdk_window_add_filter (gdkwin,
                                       (GdkFilterFunc)clipboard_manager_event_filter,
                                       manager);
        } else {
                if (gdkwin == NULL) {
                        return;
                }
                gdk_window_remove_filter (gdkwin,
                                          (GdkFilterFunc)clipboard_manager_event_filter,
                                          manager);
                g_object_unref (gdkwin);
        }
}

static gboolean
start_clipboard_idle_cb (GsdClipboardManager *manager)
{
        XClientMessageEvent xev;


        gnome_settings_profile_start (NULL);

        init_atoms (manager->display);

        /* check if there is a clipboard manager running */
        if (XGetSelectionOwner (manager->display, XA_CLIPBOARD_MANAGER)) {
                g_warning ("Clipboard manager is already running.");
                return FALSE;
        }

        manager->contents = NULL;
        manager->conversions = NULL;
        manager->requestor = None;

        manager->window = XCreateSimpleWindow (manager->display,
                                                     DefaultRootWindow (manager->display),
                                                     0, 0, 10, 10, 0,
                                                     WhitePixel (manager->display,
                                                                 DefaultScreen (manager->display)),
                                                     WhitePixel (manager->display,
                                                                 DefaultScreen (manager->display)));
        clipboard_manager_watch_cb (manager,
                                    manager->window,
                                    True,
                                    PropertyChangeMask,
                                    NULL);
        XSelectInput (manager->display,
                      manager->window,
                      PropertyChangeMask);
        manager->timestamp = get_server_time (manager->display, manager->window);

        XSetSelectionOwner (manager->display,
                            XA_CLIPBOARD_MANAGER,
                            manager->window,
                            manager->timestamp);

        /* Check to see if we managed to claim the selection. If not,
         * we treat it as if we got it then immediately lost it
         */
        if (XGetSelectionOwner (manager->display, XA_CLIPBOARD_MANAGER) == manager->window) {
                xev.type = ClientMessage;
                xev.window = DefaultRootWindow (manager->display);
                xev.message_type = XA_MANAGER;
                xev.format = 32;
                xev.data.l[0] = manager->timestamp;
                xev.data.l[1] = XA_CLIPBOARD_MANAGER;
                xev.data.l[2] = manager->window;
                xev.data.l[3] = 0;      /* manager specific data */
                xev.data.l[4] = 0;      /* manager specific data */

                XSendEvent (manager->display,
                            DefaultRootWindow (manager->display),
                            False,
                            StructureNotifyMask,
                            (XEvent *)&xev);
        } else {
                clipboard_manager_watch_cb (manager,
                                            manager->window,
                                            False,
                                            0,
                                            NULL);
                /* FIXME: manager->terminate (manager->cb_data); */
        }

        gnome_settings_profile_end (NULL);

        manager->start_idle_id = 0;

        return FALSE;
}

gboolean
gsd_clipboard_manager_start (GsdClipboardManager *manager,
                             GError             **error)
{
        gnome_settings_profile_start (NULL);

        manager->start_idle_id = g_idle_add ((GSourceFunc) start_clipboard_idle_cb, manager);
        g_source_set_name_by_id (manager->start_idle_id, "[gnome-settings-daemon] start_clipboard_idle_cb");

        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gsd_clipboard_manager_stop (GsdClipboardManager *manager)
{
        g_debug ("Stopping clipboard manager");

        if (manager->window != None) {
                clipboard_manager_watch_cb (manager,
                                            manager->window,
                                            FALSE,
                                            0,
                                            NULL);
                XDestroyWindow (manager->display, manager->window);
                manager->window = None;
        }

        if (manager->conversions != NULL) {
                list_foreach (manager->conversions, (Callback) conversion_free, NULL);
                list_free (manager->conversions);
                manager->conversions = NULL;
        }

        free_contents (manager);
}

static void
gsd_clipboard_manager_class_init (GsdClipboardManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_clipboard_manager_finalize;
}

static void
gsd_clipboard_manager_init (GsdClipboardManager *manager)
{
        manager->display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

}

static void
gsd_clipboard_manager_finalize (GObject *object)
{
        GsdClipboardManager *clipboard_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_CLIPBOARD_MANAGER (object));

        clipboard_manager = GSD_CLIPBOARD_MANAGER (object);

        gsd_clipboard_manager_stop (clipboard_manager);

        if (clipboard_manager->start_idle_id !=0)
                g_source_remove (clipboard_manager->start_idle_id);

        G_OBJECT_CLASS (gsd_clipboard_manager_parent_class)->finalize (object);
}

GsdClipboardManager *
gsd_clipboard_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_CLIPBOARD_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_CLIPBOARD_MANAGER (manager_object);
}
