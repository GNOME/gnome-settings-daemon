/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Test helper program to send a "left shift key" event via XTest, to reset the
 * idle timer.
 *
 * Copyright (C) 2013 Canonical Ltd.
 * Author: Martin Pitt <martin.pitt@ubuntu.com>
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

#include <stdio.h>

#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

int
main()
{
        Display *display = NULL;
        int event_base, error_base, major_version, minor_version;
        KeyCode keycode;

        display = XOpenDisplay (NULL);

        if (display == NULL) {
                fputs ("Error: Cannot open display\n", stderr);
                return 1;
        }

        if (!XTestQueryExtension (display, &event_base, &error_base, &major_version, &minor_version)) {
                fputs ("Error: No XTest extension\n", stderr);
                return 1;
        }

        /* send a left shift key; first press, then release */
        keycode = XKeysymToKeycode (display, XK_Shift_L);
        XTestFakeKeyEvent (display, keycode, True, 0);
        XTestFakeKeyEvent (display, keycode, False, 0);

        XCloseDisplay (display);
        return 0;
}
