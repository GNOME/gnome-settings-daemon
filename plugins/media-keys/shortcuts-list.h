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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SHORTCUTS_LIST_H__
#define __SHORTCUTS_LIST_H__

#include "shell-action-modes.h"
#include "media-keys.h"

#define SETTINGS_BINDING_DIR "org.gnome.settings-daemon.plugins.media-keys"

#define GSD_ACTION_MODE_LAUNCHER (SHELL_ACTION_MODE_NORMAL | \
                                  SHELL_ACTION_MODE_OVERVIEW)
#define SCREENSAVER_MODE SHELL_ACTION_MODE_ALL & ~SHELL_ACTION_MODE_UNLOCK_SCREEN
#define NO_LOCK_MODE SCREENSAVER_MODE & ~SHELL_ACTION_MODE_LOCK_SCREEN
#define POWER_KEYS_MODE_NO_DIALOG (SHELL_ACTION_MODE_LOCK_SCREEN | \
				   SHELL_ACTION_MODE_UNLOCK_SCREEN)
#define POWER_KEYS_MODE (SHELL_ACTION_MODE_NORMAL | \
			 SHELL_ACTION_MODE_OVERVIEW | \
			 SHELL_ACTION_MODE_LOGIN_SCREEN |\
                         POWER_KEYS_MODE_NO_DIALOG)

static struct {
        MediaKeyType key_type;
        const char *settings_key;
        const char *key_name;
        const char *hard_coded;
        ShellActionMode modes;
        MetaKeyBindingFlags grab_flags;
} media_keys[] = {
        { TOUCHPAD_KEY, NULL, N_("Touchpad toggle") ,"XF86TouchpadToggle", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { TOUCHPAD_KEY, NULL, N_("Touchpad toggle") ,"<Ctrl><Super>XF86TouchpadToggle", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { TOUCHPAD_ON_KEY, NULL, N_("Touchpad On"), "XF86TouchpadOn", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { TOUCHPAD_OFF_KEY, NULL, N_("Touchpad Off"), "XF86TouchpadOff", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { MUTE_KEY, "volume-mute", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { VOLUME_DOWN_KEY, "volume-down", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { VOLUME_UP_KEY, "volume-up", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { MIC_MUTE_KEY, "mic-mute", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { MUTE_QUIET_KEY, NULL, N_("Quiet Volume Mute"), "<Alt>XF86AudioMute", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { VOLUME_DOWN_QUIET_KEY, NULL, N_("Quiet Volume Down"), "<Alt>XF86AudioLowerVolume", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { VOLUME_UP_QUIET_KEY, NULL, N_("Quiet Volume Up"), "<Alt>XF86AudioRaiseVolume", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { VOLUME_DOWN_PRECISE_KEY, NULL, N_("Precise Volume Down"), "<Shift>XF86AudioLowerVolume", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { VOLUME_UP_PRECISE_KEY, NULL, N_("Precise Volume Up"), "<Shift>XF86AudioRaiseVolume", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { LOGOUT_KEY, "logout", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { EJECT_KEY, "eject", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { HOME_KEY, "home", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { MEDIA_KEY, "media", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { CALCULATOR_KEY, "calculator", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { SEARCH_KEY, "search", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { EMAIL_KEY, "email", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { CONTROL_CENTER_KEY, "control-center", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { SCREENSAVER_KEY, "screensaver", NULL, NULL, SCREENSAVER_MODE, META_KEY_BINDING_NONE},
        { SCREENSAVER_KEY, NULL, N_("Lock Screen"), "XF86ScreenSaver", SCREENSAVER_MODE, META_KEY_BINDING_NONE},
        { HELP_KEY, "help", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { HELP_KEY, NULL, N_("Help"), "<Super>F1", GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { SCREENSHOT_KEY, "screenshot", NULL, NULL, NO_LOCK_MODE, META_KEY_BINDING_NONE},
        { WINDOW_SCREENSHOT_KEY, "window-screenshot", NULL, NULL, NO_LOCK_MODE, META_KEY_BINDING_NONE},
        { AREA_SCREENSHOT_KEY, "area-screenshot", NULL, NULL, NO_LOCK_MODE, META_KEY_BINDING_NONE},
        { SCREENSHOT_CLIP_KEY, "screenshot-clip", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { WINDOW_SCREENSHOT_CLIP_KEY, "window-screenshot-clip", NULL, NULL, SHELL_ACTION_MODE_NORMAL, META_KEY_BINDING_NONE},
        { AREA_SCREENSHOT_CLIP_KEY, "area-screenshot-clip", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { SCREENCAST_KEY, "screencast", NULL, NULL, NO_LOCK_MODE, META_KEY_BINDING_NONE},
        { WWW_KEY, "www", NULL, NULL, GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { PLAY_KEY, "play", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { PAUSE_KEY, "pause", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { STOP_KEY, "stop", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { PREVIOUS_KEY, "previous", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { NEXT_KEY, "next", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { REWIND_KEY, NULL, N_("Rewind"), "XF86AudioRewind", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { FORWARD_KEY, NULL, N_("Forward"), "XF86AudioForward", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { REPEAT_KEY, NULL, N_("Repeat"), "XF86AudioRepeat", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { RANDOM_KEY, NULL, N_("Random Play"), "XF86AudioRandomPlay", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { ROTATE_VIDEO_LOCK_KEY, NULL, N_("Orientation Lock"), "<Super>o", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { MAGNIFIER_KEY, "magnifier", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { SCREENREADER_KEY, "screenreader", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { ON_SCREEN_KEYBOARD_KEY, "on-screen-keyboard", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { INCREASE_TEXT_KEY, "increase-text-size", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { DECREASE_TEXT_KEY, "decrease-text-size", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { TOGGLE_CONTRAST_KEY, "toggle-contrast", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { MAGNIFIER_ZOOM_IN_KEY, "magnifier-zoom-in", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { MAGNIFIER_ZOOM_OUT_KEY, "magnifier-zoom-out", NULL, NULL, SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { POWER_KEY, NULL, N_("Power Off"), "XF86PowerOff", POWER_KEYS_MODE, META_KEY_BINDING_NONE},
        /* the kernel / Xorg names really are like this... */
        /* translators: "Sleep" means putting the machine to sleep, either through hibernate or suspend */
        { SLEEP_KEY, NULL, N_("Sleep"), "XF86Suspend", POWER_KEYS_MODE, META_KEY_BINDING_NONE},
        { SUSPEND_KEY, NULL, N_("Suspend"), "XF86Sleep", POWER_KEYS_MODE, META_KEY_BINDING_NONE},
        { HIBERNATE_KEY, NULL, N_("Hibernate"), "XF86Hibernate", POWER_KEYS_MODE, META_KEY_BINDING_NONE},
        { SCREEN_BRIGHTNESS_UP_KEY, NULL, N_("Brightness Up"), "XF86MonBrightnessUp", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { SCREEN_BRIGHTNESS_DOWN_KEY, NULL, N_("Brightness Down"), "XF86MonBrightnessDown", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { KEYBOARD_BRIGHTNESS_UP_KEY, NULL, N_("Keyboard Brightness Up"), "XF86KbdBrightnessUp", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { KEYBOARD_BRIGHTNESS_DOWN_KEY, NULL, N_("Keyboard Brightness Down"), "XF86KbdBrightnessDown", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { KEYBOARD_BRIGHTNESS_TOGGLE_KEY, NULL, N_("Keyboard Brightness Toggle"), "XF86KbdLightOnOff", SHELL_ACTION_MODE_ALL, META_KEY_BINDING_NONE},
        { BATTERY_KEY, NULL, N_("Battery Status"), "XF86Battery", GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { RFKILL_KEY, NULL, N_("Toggle Airplane Mode"), "XF86WLAN", GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { RFKILL_KEY, NULL, N_("Toggle Airplane Mode"), "XF86UWB", GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { RFKILL_KEY, NULL, N_("Toggle Airplane Mode"), "XF86RFKill", GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE},
        { BLUETOOTH_RFKILL_KEY, NULL, N_("Toggle Bluetooth"), "XF86Bluetooth", GSD_ACTION_MODE_LAUNCHER, META_KEY_BINDING_NONE}
};

#undef SCREENSAVER_MODE

#endif /* __SHORTCUTS_LIST_H__ */
