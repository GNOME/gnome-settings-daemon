/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2001 Bastien Nocera <hadess@hadess.net>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 */

#ifndef __ACME_H__
#define __ACME_H__

#include "gsd-keygrab.h"

#define SETTINGS_BINDING_DIR "org.gnome.settings-daemon.plugins.media-keys"

typedef enum {
        TOUCHPAD_KEY,
        TOUCHPAD_ON_KEY,
        TOUCHPAD_OFF_KEY,
        MUTE_KEY,
        VOLUME_DOWN_KEY,
        VOLUME_UP_KEY,
        LOGOUT_KEY,
        EJECT_KEY,
        HOME_KEY,
        MEDIA_KEY,
        CALCULATOR_KEY,
        SEARCH_KEY,
        EMAIL_KEY,
        SCREENSAVER_KEY,
        HELP_KEY,
        WWW_KEY,
        PLAY_KEY,
        PAUSE_KEY,
        STOP_KEY,
        PREVIOUS_KEY,
        NEXT_KEY,
        REWIND_KEY,
        FORWARD_KEY,
        REPEAT_KEY,
        RANDOM_KEY,
        VIDEO_OUT_KEY,
        VIDEO_OUT2_KEY,
        ROTATE_VIDEO_KEY,
        MAGNIFIER_KEY,
        SCREENREADER_KEY,
        ON_SCREEN_KEYBOARD_KEY,
        INCREASE_TEXT_KEY,
        DECREASE_TEXT_KEY,
        TOGGLE_CONTRAST_KEY,
        HANDLED_KEYS
} MediaKeyType;

static struct {
        int key_type;
        const char *settings_key;
        const char *hard_coded;
        Key *key;
} keys[HANDLED_KEYS] = {
        { TOUCHPAD_KEY, "touchpad", NULL, NULL },
	{ TOUCHPAD_ON_KEY, NULL, "XF86TouchpadOn", NULL },
	{ TOUCHPAD_OFF_KEY, NULL, "XF86TouchpadOff", NULL },
        { MUTE_KEY, "volume-mute",NULL, NULL },
        { VOLUME_DOWN_KEY, "volume-down", NULL, NULL },
        { VOLUME_UP_KEY, "volume-up", NULL, NULL },
        { LOGOUT_KEY, "logout", NULL, NULL },
        { EJECT_KEY, "eject", NULL, NULL },
        { HOME_KEY, "home", NULL, NULL },
        { MEDIA_KEY, "media", NULL, NULL },
        { CALCULATOR_KEY, "calculator", NULL, NULL },
        { SEARCH_KEY, "search", NULL, NULL },
        { EMAIL_KEY, "email", NULL, NULL },
        { SCREENSAVER_KEY, "screensaver", NULL, NULL },
        { HELP_KEY, "help", NULL, NULL },
        { WWW_KEY, "www", NULL, NULL },
        { PLAY_KEY, "play", NULL, NULL },
        { PAUSE_KEY, "pause", NULL, NULL },
        { STOP_KEY, "stop", NULL, NULL },
        { PREVIOUS_KEY, "previous", NULL, NULL },
        { NEXT_KEY, "next", NULL, NULL },
        /* Those are not configurable in the UI */
        { REWIND_KEY, NULL, "XF86AudioRewind", NULL },
        { FORWARD_KEY, NULL, "XF86AudioForward", NULL },
        { REPEAT_KEY, NULL, "XF86AudioRepeat", NULL },
        { RANDOM_KEY, NULL, "XF86AudioRandomPlay", NULL},
        { VIDEO_OUT_KEY, NULL, "<Super>p", NULL },
        /* Key code of the XF86Display key (Fn-F7 on Thinkpads, Fn-F4 on HP machines, etc.) */
        { VIDEO_OUT2_KEY, NULL, "XF86Display", NULL },
        /* Key code of the XF86RotateWindows key (present on some tablets) */
        { ROTATE_VIDEO_KEY, NULL, "XF86RotateWindows", NULL },
	{ MAGNIFIER_KEY, "magnifier", NULL, NULL },
	{ SCREENREADER_KEY, "screenreader", NULL, NULL },
	{ ON_SCREEN_KEYBOARD_KEY, "on-screen-keyboard", NULL, NULL },
	{ INCREASE_TEXT_KEY, "increase-text-size", NULL, NULL },
	{ DECREASE_TEXT_KEY, "decrease-text-size", NULL, NULL },
	{ TOGGLE_CONTRAST_KEY, "toggle-contrast", NULL, NULL },
};

#endif /* __ACME_H__ */
