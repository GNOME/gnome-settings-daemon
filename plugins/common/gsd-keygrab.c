/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2001-2003 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2008 Jens Granseuer <jensgr@gmx.net>
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

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#ifdef HAVE_X11_EXTENSIONS_XKB_H
#include <X11/XKBlib.h>
#include <X11/extensions/XKB.h>
#include <gdk/gdkkeysyms.h>
#endif

#include "gsd-keygrab.h"

/* we exclude shift, GDK_CONTROL_MASK and GDK_MOD1_MASK since we know what
   these modifiers mean
   these are the mods whose combinations are bound by the keygrabbing code */
#define GSD_IGNORED_MODS (0x2000 /*Xkb modifier*/ | GDK_LOCK_MASK  | \
        GDK_MOD2_MASK | GDK_MOD3_MASK | GDK_MOD4_MASK | GDK_MOD5_MASK)
/* these are the ones we actually use for global keys, we always only check
 * for these set */
#define GSD_USED_MODS (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)


static gboolean
grab_key_real (guint      keycode,
               GdkWindow *root,
               gboolean   grab,
               int        mask)
{
        gdk_error_trap_push ();
        if (grab) {
                XGrabKey (GDK_DISPLAY (),
                          keycode,
                          mask,
                          GDK_WINDOW_XID (root),
                          True,
                          GrabModeAsync,
                          GrabModeAsync);
        } else {
                XUngrabKey (GDK_DISPLAY (),
                            keycode,
                            mask,
                            GDK_WINDOW_XID (root));
        }

        gdk_flush ();
        return gdk_error_trap_pop () == 0;
}

/* Grab the key. In order to ignore GSD_IGNORED_MODS we need to grab
 * all combinations of the ignored modifiers and those actually used
 * for the binding (if any).
 *
 * inspired by all_combinations from gnome-panel/gnome-panel/global-keys.c */
#define N_BITS 32
void
grab_key (Key                 *key,
          gboolean             grab,
          GSList              *screens)
{
        int   indexes[N_BITS]; /* indexes of bits we need to flip */
        int   i;
        int   bit;
        int   bits_set_cnt;
        int   uppervalue;
        guint mask = GSD_IGNORED_MODS & ~key->state & GDK_MODIFIER_MASK;

        bit = 0;
        /* store the indexes of all set bits in mask in the array */
        for (i = 0; mask; ++i, mask >>= 1) {
                if (mask & 0x1) {
                        indexes[bit++] = i;
                }
        }

        bits_set_cnt = bit;

        uppervalue = 1 << bits_set_cnt;
        /* grab all possible modifier combinations for our mask */
        for (i = 0; i < uppervalue; ++i) {
                GSList *l;
                int     j;
                int     result = 0;

                /* map bits in the counter to those in the mask */
                for (j = 0; j < bits_set_cnt; ++j) {
                        if (i & (1 << j)) {
                                result |= (1 << indexes[j]);
                        }
                }

                for (l = screens; l; l = l->next) {
                        GdkScreen *screen = l->data;
                        if (!grab_key_real (key->keycode,
                                            gdk_screen_get_root_window (screen),
                                            grab,
                                            result | key->state)) {
                                g_warning ("Grab failed, another application may already have access to key '%u'",
                                           key->keycode);
                                return;
                        }
                }
        }
}

static gboolean
have_xkb (Display *dpy)
{
	static int have_xkb = -1;

	if (have_xkb == -1) {
#ifdef HAVE_X11_EXTENSIONS_XKB_H
		int opcode, error_base, major, minor, xkb_event_base;

		gdk_error_trap_push ();
		have_xkb = XkbQueryExtension (dpy,
					      &opcode,
					      &xkb_event_base,
					      &error_base,
					      &major,
					      &minor)
			&& XkbUseExtension (dpy, &major, &minor);
		gdk_error_trap_pop ();
#else
		have_xkb = 0;
#endif
	}

	return have_xkb;
}

gboolean
match_key (Key *key, XEvent *event)
{
	GdkKeymap *keymap;
	guint keyval;
	GdkModifierType consumed;
	gint group;

	if (key == NULL)
		return FALSE;

	keymap = gdk_keymap_get_default ();
	if (have_xkb (event->xkey.display))
		group = XkbGroupForCoreState (event->xkey.state);
	else
		group = (event->xkey.state & GDK_Mode_switch) ? 1 : 0;
	/* Check if we find a keysym that matches our current state */
	if (gdk_keymap_translate_keyboard_state (keymap, event->xkey.keycode,
					     event->xkey.state, group,
					     &keyval, NULL, NULL, &consumed)) {
		guint lower, upper;

		gdk_keyval_convert_case (keyval, &lower, &upper);
		return ((lower == key->keysym || upper == key->keysym)
			&& (key->state & ~consumed & GSD_USED_MODS) == key->state);
	}

	/* The key we passed doesn't have a keysym, so try with just the keycode */
        return (key != NULL
                && key->keycode == event->xkey.keycode
                && key->state == (event->xkey.state & GSD_USED_MODS));
}
