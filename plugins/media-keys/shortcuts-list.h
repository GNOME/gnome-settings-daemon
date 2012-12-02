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
        SWITCH_INPUT_SOURCE_KEY,
        SWITCH_INPUT_SOURCE_BACKWARD_KEY,
        CUSTOM_KEY
} MediaKeyType;


#define ALL_MODES ~0
#define WINDOW_MODES (GSD_KEYGRAB_MODE_NORMAL | GSD_KEYGRAB_MODE_OVERVIEW)
#define SCREENSAVER_MODES ALL_MODES & ~(GSD_KEYGRAB_MODE_LOCK_SCREEN | \
                                        GSD_KEYGRAB_MODE_UNLOCK_SCREEN)

static struct {
        MediaKeyType key_type;
        const char *settings_key;
        const char *hard_coded;
        GsdKeygrabModes modes;
} media_keys[] = {
        { TOUCHPAD_KEY, NULL, "XF86TouchpadToggle", ALL_MODES },
        { TOUCHPAD_ON_KEY, NULL, "XF86TouchpadOn", ALL_MODES },
        { TOUCHPAD_OFF_KEY, NULL, "XF86TouchpadOff", ALL_MODES },
        { MUTE_KEY, "volume-mute", NULL, ALL_MODES },
        { VOLUME_DOWN_KEY, "volume-down", NULL, ALL_MODES },
        { VOLUME_UP_KEY, "volume-up", NULL, ALL_MODES },
        { MUTE_QUIET_KEY, NULL, "<Alt>XF86AudioMute", ALL_MODES },
        { VOLUME_DOWN_QUIET_KEY, NULL, "<Alt>XF86AudioLowerVolume", ALL_MODES },
        { VOLUME_UP_QUIET_KEY, NULL, "<Alt>XF86AudioRaiseVolume", ALL_MODES },
        { LOGOUT_KEY, "logout", NULL, WINDOW_MODES },
        { EJECT_KEY, "eject", NULL, ALL_MODES },
        { HOME_KEY, "home", NULL, WINDOW_MODES },
        { MEDIA_KEY, "media", NULL, WINDOW_MODES },
        { CALCULATOR_KEY, "calculator", NULL, WINDOW_MODES },
        { SEARCH_KEY, "search", NULL, WINDOW_MODES },
        { EMAIL_KEY, "email", NULL, WINDOW_MODES },
        { SCREENSAVER_KEY, "screensaver", NULL, SCREENSAVER_MODES },
        { SCREENSAVER_KEY, NULL, "XF86ScreenSaver", SCREENSAVER_MODES },
        { HELP_KEY, "help", NULL, WINDOW_MODES },
        { SCREENSHOT_KEY, "screenshot", NULL, ALL_MODES },
        { WINDOW_SCREENSHOT_KEY, "window-screenshot", NULL, GSD_KEYGRAB_MODE_NORMAL },
        { AREA_SCREENSHOT_KEY, "area-screenshot", NULL, ALL_MODES },
        { SCREENSHOT_CLIP_KEY, "screenshot-clip", NULL, ALL_MODES },
        { WINDOW_SCREENSHOT_CLIP_KEY, "window-screenshot-clip", NULL, GSD_KEYGRAB_MODE_NORMAL },
        { AREA_SCREENSHOT_CLIP_KEY, "area-screenshot-clip", NULL, ALL_MODES },
        { WWW_KEY, "www", NULL, WINDOW_MODES },
        { PLAY_KEY, "play", NULL, ALL_MODES },
        { PAUSE_KEY, "pause", NULL, ALL_MODES },
        { STOP_KEY, "stop", NULL, ALL_MODES },
        { PREVIOUS_KEY, "previous", NULL, ALL_MODES },
        { NEXT_KEY, "next", NULL, ALL_MODES },
        { REWIND_KEY, NULL, "XF86AudioRewind", ALL_MODES },
        { FORWARD_KEY, NULL, "XF86AudioForward", ALL_MODES },
        { REPEAT_KEY, NULL, "XF86AudioRepeat", ALL_MODES },
        { RANDOM_KEY, NULL, "XF86AudioRandomPlay", ALL_MODES },
        { VIDEO_OUT_KEY, NULL, "<Super>p", ALL_MODES },
        /* Key code of the XF86Display key (Fn-F7 on Thinkpads, Fn-F4 on HP machines, etc.) */
        { VIDEO_OUT_KEY, NULL, "XF86Display", ALL_MODES },
        /* Key code of the XF86RotateWindows key (present on some tablets) */
        { ROTATE_VIDEO_KEY, NULL, "XF86RotateWindows", GSD_KEYGRAB_MODE_NORMAL },
        { MAGNIFIER_KEY, "magnifier", NULL, ALL_MODES },
        { SCREENREADER_KEY, "screenreader", NULL, ALL_MODES },
        { ON_SCREEN_KEYBOARD_KEY, "on-screen-keyboard", NULL, ALL_MODES },
        { INCREASE_TEXT_KEY, "increase-text-size", NULL, ALL_MODES },
        { DECREASE_TEXT_KEY, "decrease-text-size", NULL, ALL_MODES },
        { TOGGLE_CONTRAST_KEY, "toggle-contrast", NULL, ALL_MODES },
        { MAGNIFIER_ZOOM_IN_KEY, "magnifier-zoom-in", NULL, ALL_MODES },
        { MAGNIFIER_ZOOM_OUT_KEY, "magnifier-zoom-out", NULL, ALL_MODES },
        { POWER_KEY, NULL, "XF86PowerOff", WINDOW_MODES },
        /* the kernel / Xorg names really are like this... */
        { SLEEP_KEY, NULL, "XF86Suspend", ALL_MODES },
        { SUSPEND_KEY, NULL, "XF86Sleep", ALL_MODES },
        { HIBERNATE_KEY, NULL, "XF86Hibernate", ALL_MODES },
        { SCREEN_BRIGHTNESS_UP_KEY, NULL, "XF86MonBrightnessUp", ALL_MODES },
        { SCREEN_BRIGHTNESS_DOWN_KEY, NULL, "XF86MonBrightnessDown", ALL_MODES },
        { KEYBOARD_BRIGHTNESS_UP_KEY, NULL, "XF86KbdBrightnessUp", ALL_MODES },
        { KEYBOARD_BRIGHTNESS_DOWN_KEY, NULL, "XF86KbdBrightnessDown", ALL_MODES },
        { KEYBOARD_BRIGHTNESS_TOGGLE_KEY, NULL, "XF86KbdLightOnOff", ALL_MODES },
        { SWITCH_INPUT_SOURCE_KEY, "switch-input-source", NULL, ALL_MODES },
        { SWITCH_INPUT_SOURCE_BACKWARD_KEY, "switch-input-source-backward", NULL, ALL_MODES },
        { BATTERY_KEY, NULL, "XF86Battery", WINDOW_MODES },
};

#undef ALL_MODES
#undef WINDOW_MODES
#undef SCREENSAVER_MODES

#endif /* __ACME_H__ */
