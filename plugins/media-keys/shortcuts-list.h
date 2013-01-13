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
        MUTE_QUIET_KEY,
        VOLUME_DOWN_QUIET_KEY,
        VOLUME_UP_QUIET_KEY,
        MIC_MUTE_KEY,
        LOGOUT_KEY,
        EJECT_KEY,
        HOME_KEY,
        MEDIA_KEY,
        CALCULATOR_KEY,
        SEARCH_KEY,
        EMAIL_KEY,
        SCREENSAVER_KEY,
        HELP_KEY,
        SCREENSHOT_KEY,
        WINDOW_SCREENSHOT_KEY,
        AREA_SCREENSHOT_KEY,
        SCREENSHOT_CLIP_KEY,
        WINDOW_SCREENSHOT_CLIP_KEY,
        AREA_SCREENSHOT_CLIP_KEY,
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
        ROTATE_VIDEO_KEY,
        MAGNIFIER_KEY,
        SCREENREADER_KEY,
        ON_SCREEN_KEYBOARD_KEY,
        INCREASE_TEXT_KEY,
        DECREASE_TEXT_KEY,
        TOGGLE_CONTRAST_KEY,
        MAGNIFIER_ZOOM_IN_KEY,
        MAGNIFIER_ZOOM_OUT_KEY,
        POWER_KEY,
        SLEEP_KEY,
        SUSPEND_KEY,
        HIBERNATE_KEY,
        SCREEN_BRIGHTNESS_UP_KEY,
        SCREEN_BRIGHTNESS_DOWN_KEY,
        KEYBOARD_BRIGHTNESS_UP_KEY,
        KEYBOARD_BRIGHTNESS_DOWN_KEY,
        KEYBOARD_BRIGHTNESS_TOGGLE_KEY,
        BATTERY_KEY,
        CUSTOM_KEY
} MediaKeyType;

static struct {
        MediaKeyType key_type;
        const char *settings_key;
        const char *key_name;
        const char *hard_coded;
} media_keys[] = {
        { TOUCHPAD_KEY, NULL, N_("Touchpad toggle"), "XF86TouchpadToggle" },
        { TOUCHPAD_ON_KEY, NULL, N_("Touchpad On"), "XF86TouchpadOn" },
        { TOUCHPAD_OFF_KEY, NULL, N_("Touchpad Off"), "XF86TouchpadOff" },
        { MUTE_KEY, "volume-mute", NULL, NULL },
        { VOLUME_DOWN_KEY, "volume-down", NULL, NULL },
        { VOLUME_UP_KEY, "volume-up", NULL, NULL },
        { MIC_MUTE_KEY, NULL, N_("Microphone Mute"), "F2" },
        { MUTE_QUIET_KEY, NULL, N_("Quiet Volume Mute"), "<Alt>XF86AudioMute" },
        { VOLUME_DOWN_QUIET_KEY, NULL, N_("Quiet Volume Down"), "<Alt>XF86AudioLowerVolume" },
        { VOLUME_UP_QUIET_KEY, NULL, N_("Quiet Volume Up"), "<Alt>XF86AudioRaiseVolume" },
        { LOGOUT_KEY, "logout", NULL },
        { EJECT_KEY, "eject", NULL },
        { HOME_KEY, "home", NULL },
        { MEDIA_KEY, "media", NULL },
        { CALCULATOR_KEY, "calculator", NULL },
        { SEARCH_KEY, "search", NULL },
        { EMAIL_KEY, "email", NULL },
        { SCREENSAVER_KEY, "screensaver", NULL },
        { SCREENSAVER_KEY, NULL, N_("Lock Screen"), "XF86ScreenSaver" },
        { HELP_KEY, "help", NULL },
        { SCREENSHOT_KEY, "screenshot", NULL },
        { WINDOW_SCREENSHOT_KEY, "window-screenshot", NULL },
        { AREA_SCREENSHOT_KEY, "area-screenshot", NULL },
        { SCREENSHOT_CLIP_KEY, "screenshot-clip", NULL },
        { WINDOW_SCREENSHOT_CLIP_KEY, "window-screenshot-clip", NULL },
        { AREA_SCREENSHOT_CLIP_KEY, "area-screenshot-clip", NULL },
        { WWW_KEY, "www", NULL },
        { PLAY_KEY, "play", NULL },
        { PAUSE_KEY, "pause", NULL },
        { STOP_KEY, "stop", NULL },
        { PREVIOUS_KEY, "previous", NULL },
        { NEXT_KEY, "next", NULL },
        { REWIND_KEY, NULL, N_("Rewind"), "XF86AudioRewind" },
        { FORWARD_KEY, NULL, N_("Forward"), "XF86AudioForward" },
        { REPEAT_KEY, NULL, N_("Repeat"), "XF86AudioRepeat" },
        { RANDOM_KEY, NULL, N_("Random Play"), "XF86AudioRandomPlay"},
        { VIDEO_OUT_KEY, NULL, N_("Video Out"), "<Super>p" },
        /* Key code of the XF86Display key (Fn-F7 on Thinkpads, Fn-F4 on HP machines, etc.) */
        { VIDEO_OUT_KEY, NULL, N_("Video Out"), "XF86Display" },
        /* Key code of the XF86RotateWindows key (present on some tablets) */
        { ROTATE_VIDEO_KEY, NULL, N_("Rotate Screen"), "XF86RotateWindows" },
        { MAGNIFIER_KEY, "magnifier", NULL },
        { SCREENREADER_KEY, "screenreader", NULL },
        { ON_SCREEN_KEYBOARD_KEY, "on-screen-keyboard", NULL },
        { INCREASE_TEXT_KEY, "increase-text-size", NULL },
        { DECREASE_TEXT_KEY, "decrease-text-size", NULL },
        { TOGGLE_CONTRAST_KEY, "toggle-contrast", NULL },
        { MAGNIFIER_ZOOM_IN_KEY, "magnifier-zoom-in", NULL },
        { MAGNIFIER_ZOOM_OUT_KEY, "magnifier-zoom-out", NULL },
        { POWER_KEY, NULL, N_("Power Off"), "XF86PowerOff" },
        /* the kernel / Xorg names really are like this... */
        { SLEEP_KEY, NULL, N_("Sleep"), "XF86Suspend" },
        { SUSPEND_KEY, NULL, N_("Suspend"), "XF86Sleep" },
        { HIBERNATE_KEY, NULL, N_("Hibernate"), "XF86Hibernate" },
        { SCREEN_BRIGHTNESS_UP_KEY, NULL, N_("Brightness Up"), "XF86MonBrightnessUp" },
        { SCREEN_BRIGHTNESS_DOWN_KEY, NULL, N_("Brightness Down"), "XF86MonBrightnessDown" },
        { KEYBOARD_BRIGHTNESS_UP_KEY, NULL, N_("Keyboard Brightness Up"), "XF86KbdBrightnessUp" },
        { KEYBOARD_BRIGHTNESS_DOWN_KEY, NULL, N_("Keyboard Brightness Down"), "XF86KbdBrightnessDown" },
        { KEYBOARD_BRIGHTNESS_TOGGLE_KEY, NULL, N_("Keyboard Brightness Toggle"), "XF86KbdLightOnOff" },
        { BATTERY_KEY, NULL, N_("Battery Status"), "XF86Battery" },
};

#endif /* __ACME_H__ */
