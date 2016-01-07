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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XKB.h>
#include <gdk/gdkkeysyms.h>

#include "gsd-keygrab.h"

void
grab_button (int        deviceid,
             gboolean   grab,
             GdkScreen *screen)
{
	GdkWindow *root;
	XIGrabModifiers mods;

	root = gdk_screen_get_root_window (screen);
	mods.modifiers = XIAnyModifier;

	if (grab) {
		XIEventMask evmask;
		unsigned char mask[(XI_LASTEVENT + 7)/8];

		memset (mask, 0, sizeof (mask));
		XISetMask (mask, XI_ButtonRelease);
		XISetMask (mask, XI_ButtonPress);

		evmask.deviceid = deviceid;
		evmask.mask_len = sizeof (mask);
		evmask.mask = mask;

		gdk_error_trap_push();
		XIGrabButton (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
			      deviceid,
			      XIAnyButton,
			      GDK_WINDOW_XID (root),
			      None,
			      GrabModeAsync,
			      GrabModeAsync,
			      False,
			      &evmask,
			      1,
			      &mods);
		gdk_error_trap_pop_ignored ();
	} else {
		gdk_error_trap_push();
		XIUngrabButton (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
				deviceid,
				XIAnyButton,
		                GDK_WINDOW_XID (root),
				1, &mods);
		gdk_error_trap_pop_ignored ();
	}
}
