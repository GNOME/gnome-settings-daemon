/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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
#include <time.h>

#include <X11/Xatom.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include <libgnome-desktop/gnome-rr-config.h>
#include <libgnome-desktop/gnome-rr.h>
#include <libgnome-desktop/gnome-pnp-ids.h>

#include "gnome-settings-profile.h"
#include "gsd-enums.h"
#include "gsd-xsettings-manager.h"
#include "gsd-xsettings-gtk.h"
#include "xsettings-manager.h"
#include "fontconfig-monitor.h"
#include "gsd-remote-display-manager.h"
#include "wm-button-layout-translation.h"

#define GNOME_XSETTINGS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GNOME_TYPE_XSETTINGS_MANAGER, GnomeXSettingsManagerPrivate))

#define MOUSE_SETTINGS_SCHEMA     "org.gnome.settings-daemon.peripherals.mouse"
#define BACKGROUND_SETTINGS_SCHEMA "org.gnome.desktop.background"
#define INTERFACE_SETTINGS_SCHEMA "org.gnome.desktop.interface"
#define SOUND_SETTINGS_SCHEMA     "org.gnome.desktop.sound"
#define PRIVACY_SETTINGS_SCHEMA     "org.gnome.desktop.privacy"
#define WM_SETTINGS_SCHEMA        "org.gnome.desktop.wm.preferences"
#define A11Y_SCHEMA               "org.gnome.desktop.a11y"
#define CLASSIC_WM_SETTINGS_SCHEMA "org.gnome.shell.extensions.classic-overrides"

#define XSETTINGS_PLUGIN_SCHEMA "org.gnome.settings-daemon.plugins.xsettings"
#define XSETTINGS_OVERRIDE_KEY  "overrides"

#define GTK_MODULES_DISABLED_KEY "disabled-gtk-modules"
#define GTK_MODULES_ENABLED_KEY  "enabled-gtk-modules"

#define TEXT_SCALING_FACTOR_KEY "text-scaling-factor"
#define SCALING_FACTOR_KEY "scaling-factor"
#define CURSOR_SIZE_KEY "cursor-size"
#define CURSOR_THEME_KEY "cursor-theme"

#define FONT_ANTIALIASING_KEY "antialiasing"
#define FONT_HINTING_KEY      "hinting"
#define FONT_RGBA_ORDER_KEY   "rgba-order"

/* As we cannot rely on the X server giving us good DPI information, and
 * that we don't want multi-monitor screens to have different DPIs (thus
 * different text sizes), we'll hard-code the value of the DPI
 *
 * See also:
 * https://bugzilla.novell.com/show_bug.cgi?id=217790•
 * https://bugzilla.gnome.org/show_bug.cgi?id=643704
 *
 * http://lists.fedoraproject.org/pipermail/devel/2011-October/157671.html
 * Why EDID is not trustworthy for DPI
 * Adam Jackson ajax at redhat.com
 * Tue Oct 4 17:54:57 UTC 2011
 * 
 *     Previous message: GNOME 3 - font point sizes now scaled?
 *     Next message: Why EDID is not trustworthy for DPI
 *     Messages sorted by: [ date ] [ thread ] [ subject ] [ author ]
 * 
 * On Tue, 2011-10-04 at 11:46 -0400, Kaleb S. KEITHLEY wrote:
 * 
 * > Grovelling around in the F15 xorg-server sources and reviewing the Xorg 
 * > log file on my F15 box, I see, with _modern hardware_ at least, that we 
 * > do have the monitor geometry available from DDC or EDIC, and obviously 
 * > it is trivial to compute the actual, correct DPI for each screen.
 * 
 * I am clearly going to have to explain this one more time, forever.
 * Let's see if I can't write it authoritatively once and simply answer
 * with a URL from here out.  (As always, use of the second person "you"
 * herein is plural, not singular.)
 * 
 * EDID does not reliably give you the size of the display.
 * 
 * Base EDID has at least two different places where you can give a
 * physical size (before considering extensions that aren't widely deployed
 * so whatever).  The first is a global property, measured in centimeters,
 * of the physical size of the glass.  The second is attached to your (zero
 * or more) detailed timing specifications, and reflects the size of the
 * mode, in millimeters.
 * 
 * So, how does this screw you?
 * 
 * a) Glass size is too coarse.  On a large display that cm roundoff isn't
 * a big deal, but on subnotebooks it's a different game.  The 11" MBA is
 * 25.68x14.44 cm, so that gives you a range of 52.54-54.64 dpcm horizontal
 * and 51.20-54.86 dpcm vertical (133.4-138.8 dpi h and 130.0-139.3 dpi v).
 * Which is optimistic, because that's doing the math forward from knowing
 * the actual size, and you as the EDID parser can't know which way the
 * manufacturer rounded.
 * 
 * b) Glass size need not be non-zero.  This is in fact the usual case for
 * projectors, which don't have a fixed display size since it's a function
 * of how far away the wall is from the lens.
 * 
 * c) Glass size could be partially non-zero.  Yes, really.  EDID 1.4
 * defines a method of using these two bytes to encode aspect ratio, where
 * if vertical size is 0 then the aspect ratio is computed as (horizontal
 * value + 99) / 100 in portrait mode (and the obvious reverse thing if
 * horizontal is zero).  Admittedly, unlike every other item in this list,
 * I've never seen this in the wild.  But it's legal.
 * 
 * d) Glass size could be a direct encoding of the aspect ratio.  Base EDID
 * doesn't condone this behaviour, but the CEA spec (to which all HDMI
 * monitors must conform) does allow-but-not-require it, which means your
 * 1920x1080 TV could claim to be 16 "cm" by 9 "cm".  So of course that's
 * what TV manufacturers do because that way they don't have to modify the
 * EDID info when physical construction changes, and that's cheaper.
 * 
 * e) You could use mode size to get size in millimeters, but you might not
 * have any detailed timings.
 * 
 * f) You could use mode size, but mode size is explicitly _not_ glass
 * size.  It's the size that the display chooses to present that mode.
 * Sometimes those are the same, and sometimes they're not.  You could be
 * scaled or {letter,pillar}boxed, and that's not necessarily something you
 * can control from the host side.
 * 
 * g) You could use mode size, but it could be an encoded aspect ratio, as
 * in case d above, because CEA says that's okay.
 * 
 * h) You could use mode size, but it could be the aspect ratio from case d
 * multiplied by 10 in each direction (because, of course, you gave size in
 * centimeters and so your authoring tool just multiplied it up).
 * 
 * i) Any or all of the above could be complete and utter garbage, because
 * - and I really, really need you to understand this - there is no
 * requirements program for any commercial OS or industry standard that
 * requires honesty here, as far as I'm aware.  There is every incentive
 * for there to _never_ be one, because it would make the manufacturing
 * process more expensive.
 * 
 * So from this point the suggestion is usually "well come up with some
 * heuristic to make a good guess assuming there's some correlation between
 * the various numbers you're given".  I have in fact written heuristics
 * for this, and they're in your kernel and your X server, and they still
 * encounter a huge number of cases where we simply _cannot_ know from EDID
 * anything like a physical size, because - to pick only one example - the
 * consumer electronics industry are cheap bastards, because you the
 * consumer demanded that they be cheap.
 * 
 * And then your only recourse is to an external database, and now you're
 * up the creek again because the identifying information here is a
 * vendor/model/serial tuple, and the vendor can and does change physical
 * construction without changing model number.  Now you get to play the
 * guessing game of how big the serial number range is for each subvariant,
 * assuming they bothered to encode a serial number - and they didn't.  Or,
 * if they bothered to encode week/year of manufacturer correctly - and
 * they didn't - which weeks meant which models.  And then you still have
 * to go out and buy one of every TV at Fry's, and that covers you for one
 * market, for three months.
 * 
 * If someone wants to write something better, please, by all means.  If
 * it's kernel code, send it to dri-devel at lists.freedesktop.org and cc me
 * and I will happily review it.  Likewise xorg-devel@ for X server
 * changes.
 * 
 * I gently suggest that doing so is a waste of time.
 * 
 * But if there's one thing free software has taught me, it's that you can
 * not tell people something is a bad idea and have any expectation they
 * will believe you.
 * 
 * > Obviously in a multi-screen set-up using Xinerama this has the potential 
 * > to be a Hard Problem if the monitors differ greatly in their DPI.
 * > 
 * > If the major resistance is over what to do with older hardware that 
 * > doesn't have this data available, then yes, punt; use a hard-coded 
 * > default. Likewise, if the two monitors really differ greatly, then punt.
 * 
 * I'm going to limit myself to observing that "greatly" is a matter of
 * opinion, and that in order to be really useful you'd need some way of
 * communicating "I punted" to the desktop.
 * 
 * Beyond that, sure, pick a heuristic, accept that it's going to be
 * insufficient for someone, and then sit back and wait to get
 * second-guessed on it over and over.
 * 
 * > And it wouldn't be so hard to to add something like -dpi:0, -dpi:1, 
 * > -dpi:2 command line options to specify per-screen dpi. I kinda thought I 
 * > did that a long, long time ago, but maybe I only thought about doing it 
 * > and never actually got around to it.
 * 
 * The RANDR extension as of version 1.2 does allow you to override
 * physical size on a per-output basis at runtime.  We even try pretty hard
 * to set them as honestly as we can up front.  The 96dpi thing people
 * complain about is from the per-screen info, which is simply a default
 * because of all the tl;dr above; because you have N outputs per screen
 * which means a single number is in general useless; and because there is
 * no way to refresh the per-screen info at runtime, as it's only ever sent
 * in the initial connection handshake.
 * 
 * - ajax
 * 
 */
#define DPI_FALLBACK 96

/* The minimum resolution at which we turn on a window-scale of 2 */
#define HIDPI_LIMIT (DPI_FALLBACK * 2)

/* The minimum screen height at which we turn on a window-scale of 2;
 * below this there just isn't enough vertical real estate for GNOME
 * apps to work, and it's better to just be tiny */
#define HIDPI_MIN_HEIGHT 1200

/* From http://en.wikipedia.org/wiki/4K_resolution#Resolutions_of_common_formats */
#define SMALLEST_4K_WIDTH 3656

typedef struct _TranslationEntry TranslationEntry;
typedef void (* TranslationFunc) (GnomeXSettingsManager *manager,
                                  TranslationEntry      *trans,
                                  GVariant              *value);

struct _TranslationEntry {
        const char     *gsettings_schema;
        const char     *gsettings_key;
        const char     *xsetting_name;

        TranslationFunc translate;
};

typedef struct _FixedEntry FixedEntry;
typedef void (* FixedFunc) (GnomeXSettingsManager *manager,
                            FixedEntry            *fixed);
struct _FixedEntry {
        const char     *xsetting_name;
        FixedFunc       func;
};

struct GnomeXSettingsManagerPrivate
{
        guint              start_idle_id;
        XSettingsManager  *manager;
        GHashTable        *settings;

        GSettings         *plugin_settings;
        fontconfig_monitor_handle_t *fontconfig_handle;

        GsdXSettingsGtk   *gtk;

        GsdRemoteDisplayManager *remote_display;

        GnomeRRScreen     *rr_screen;

        guint              shell_name_watch_id;
        gboolean           have_shell;

        guint              notify_idle_id;
};

#define GSD_XSETTINGS_ERROR gsd_xsettings_error_quark ()

enum {
        GSD_XSETTINGS_ERROR_INIT
};

static void     gnome_xsettings_manager_class_init  (GnomeXSettingsManagerClass *klass);
static void     gnome_xsettings_manager_init        (GnomeXSettingsManager      *xsettings_manager);
static void     gnome_xsettings_manager_finalize    (GObject                  *object);

G_DEFINE_TYPE (GnomeXSettingsManager, gnome_xsettings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static GQuark
gsd_xsettings_error_quark (void)
{
        return g_quark_from_static_string ("gsd-xsettings-error-quark");
}

static void
translate_bool_int (GnomeXSettingsManager *manager,
                    TranslationEntry      *trans,
                    GVariant              *value)
{
        xsettings_manager_set_int (manager->priv->manager, trans->xsetting_name,
                                   g_variant_get_boolean (value));
}

static void
translate_int_int (GnomeXSettingsManager *manager,
                   TranslationEntry      *trans,
                   GVariant              *value)
{
        xsettings_manager_set_int (manager->priv->manager, trans->xsetting_name,
                                   g_variant_get_int32 (value));
}

static void
translate_string_string (GnomeXSettingsManager *manager,
                         TranslationEntry      *trans,
                         GVariant              *value)
{
        xsettings_manager_set_string (manager->priv->manager,
                                      trans->xsetting_name,
                                      g_variant_get_string (value, NULL));
}

static void
translate_button_layout (GnomeXSettingsManager *manager,
                         TranslationEntry      *trans,
                         GVariant              *value)
{
        GSettings *classic_settings;
        GVariant *classic_value = NULL;
        char *layout;

        /* Hack: until we get session-dependent defaults in GSettings,
         *       swap out the usual schema for the "classic" one when
         *       running in classic mode
         */
        classic_settings = g_hash_table_lookup (manager->priv->settings,
                                                CLASSIC_WM_SETTINGS_SCHEMA);
        if (classic_settings) {
                classic_value = g_settings_get_value (classic_settings, "button-layout");
                layout = g_variant_dup_string (classic_value, NULL);
        } else {
                layout = g_variant_dup_string (value, NULL);
        }

        translate_wm_button_layout_to_gtk (layout);

        xsettings_manager_set_string (manager->priv->manager,
                                      trans->xsetting_name,
                                      layout);

        if (classic_value)
                g_variant_unref (classic_value);
        g_free (layout);
}

static void
fixed_false_int (GnomeXSettingsManager *manager,
                 FixedEntry            *fixed)
{
        xsettings_manager_set_int (manager->priv->manager, fixed->xsetting_name, FALSE);
}

static void
fixed_true_int (GnomeXSettingsManager *manager,
                FixedEntry            *fixed)
{
        xsettings_manager_set_int (manager->priv->manager, fixed->xsetting_name, TRUE);
}

static void
fixed_bus_id (GnomeXSettingsManager *manager,
              FixedEntry            *fixed)
{
        const gchar *id;
        GDBusConnection *bus;
        GVariant *res;

        bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
        res = g_dbus_connection_call_sync (bus,
                                           "org.freedesktop.DBus",
                                           "/org/freedesktop/DBus",
                                           "org.freedesktop.DBus",
                                           "GetId",
                                           NULL,
                                           NULL,
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL,
                                           NULL);

        if (res) {
                g_variant_get (res, "(&s)", &id);

                xsettings_manager_set_string (manager->priv->manager, fixed->xsetting_name, id);
                g_variant_unref (res);
        }

        g_object_unref (bus);
}

static FixedEntry fixed_entries [] = {
        { "Gtk/MenuImages",          fixed_false_int },
        { "Gtk/ButtonImages",        fixed_false_int },
        { "Gtk/ShowInputMethodMenu", fixed_false_int },
        { "Gtk/ShowUnicodeMenu",     fixed_false_int },
        { "Gtk/AutoMnemonics",       fixed_true_int },
        { "Gtk/EnablePrimaryPaste",  fixed_true_int },
        { "Gtk/DialogsUseHeader",    fixed_true_int },
        { "Gtk/SessionBusId",        fixed_bus_id },
};

static TranslationEntry translations [] = {
        { "org.gnome.settings-daemon.peripherals.mouse", "double-click",   "Net/DoubleClickTime",  translate_int_int },
        { "org.gnome.settings-daemon.peripherals.mouse", "drag-threshold", "Net/DndDragThreshold", translate_int_int },

        { "org.gnome.desktop.background", "show-desktop-icons",    "Gtk/ShellShowsDesktop",   translate_bool_int },

        { "org.gnome.desktop.interface", "gtk-color-palette",      "Gtk/ColorPalette",        translate_string_string },
        { "org.gnome.desktop.interface", "font-name",              "Gtk/FontName",            translate_string_string },
        { "org.gnome.desktop.interface", "gtk-key-theme",          "Gtk/KeyThemeName",        translate_string_string },
        { "org.gnome.desktop.interface", "toolbar-style",          "Gtk/ToolbarStyle",        translate_string_string },
        { "org.gnome.desktop.interface", "toolbar-icons-size",     "Gtk/ToolbarIconSize",     translate_string_string },
        { "org.gnome.desktop.interface", "can-change-accels",      "Gtk/CanChangeAccels",     translate_bool_int },
        { "org.gnome.desktop.interface", "cursor-blink",           "Net/CursorBlink",         translate_bool_int },
        { "org.gnome.desktop.interface", "cursor-blink-time",      "Net/CursorBlinkTime",     translate_int_int },
        { "org.gnome.desktop.interface", "cursor-blink-timeout",   "Gtk/CursorBlinkTimeout",  translate_int_int },
        { "org.gnome.desktop.interface", "gtk-theme",              "Net/ThemeName",           translate_string_string },
        { "org.gnome.desktop.interface", "gtk-timeout-initial",    "Gtk/TimeoutInitial",      translate_int_int },
        { "org.gnome.desktop.interface", "gtk-timeout-repeat",     "Gtk/TimeoutRepeat",       translate_int_int },
        { "org.gnome.desktop.interface", "gtk-color-scheme",       "Gtk/ColorScheme",         translate_string_string },
        { "org.gnome.desktop.interface", "gtk-im-preedit-style",   "Gtk/IMPreeditStyle",      translate_string_string },
        { "org.gnome.desktop.interface", "gtk-im-status-style",    "Gtk/IMStatusStyle",       translate_string_string },
        { "org.gnome.desktop.interface", "gtk-im-module",          "Gtk/IMModule",            translate_string_string },
        { "org.gnome.desktop.interface", "icon-theme",             "Net/IconThemeName",       translate_string_string },
        { "org.gnome.desktop.interface", "menubar-accel",          "Gtk/MenuBarAccel",        translate_string_string },
        { "org.gnome.desktop.interface", "cursor-theme",           "Gtk/CursorThemeName",     translate_string_string },
        /* cursor-size is handled via the Xft side as it needs the scaling factor */

        { "org.gnome.desktop.sound", "theme-name",                 "Net/SoundThemeName",            translate_string_string },
        { "org.gnome.desktop.sound", "event-sounds",               "Net/EnableEventSounds" ,        translate_bool_int },
        { "org.gnome.desktop.sound", "input-feedback-sounds",      "Net/EnableInputFeedbackSounds", translate_bool_int },

        { "org.gnome.desktop.privacy", "recent-files-max-age",      "Gtk/RecentFilesMaxAge", translate_int_int },
        { "org.gnome.desktop.privacy", "remember-recent-files",    "Gtk/RecentFilesEnabled", translate_bool_int },
        { "org.gnome.desktop.wm.preferences", "button-layout",     "Gtk/DecorationLayout", translate_button_layout },
        { "org.gnome.desktop.wm.preferences", "action-double-click-titlebar",  "Gtk/TitlebarDoubleClick",     translate_string_string },
        { "org.gnome.desktop.wm.preferences", "action-middle-click-titlebar",  "Gtk/TitlebarMiddleClick",     translate_string_string },
        { "org.gnome.desktop.wm.preferences", "action-right-click-titlebar",  "Gtk/TitlebarRightClick",     translate_string_string },
        { "org.gnome.desktop.a11y", "always-show-text-caret",       "Gtk/KeynavUseCaret",         translate_bool_int }
};

static gboolean
notify_idle (gpointer data)
{
        GnomeXSettingsManager *manager = data;
        xsettings_manager_notify (manager->priv->manager);
        manager->priv->notify_idle_id = 0;
        return G_SOURCE_REMOVE;
}

static void
queue_notify (GnomeXSettingsManager *manager)
{
        if (manager->priv->notify_idle_id != 0)
                return;

        manager->priv->notify_idle_id = g_idle_add (notify_idle, manager);
        g_source_set_name_by_id (manager->priv->notify_idle_id, "[gnome-settings-daemon] notify_idle");
}

static double
get_dpi_from_gsettings (GnomeXSettingsManager *manager)
{
	GSettings  *interface_settings;
        double      dpi;
        double      factor;

	interface_settings = g_hash_table_lookup (manager->priv->settings, INTERFACE_SETTINGS_SCHEMA);
        factor = g_settings_get_double (interface_settings, TEXT_SCALING_FACTOR_KEY);

	dpi = DPI_FALLBACK;

        return dpi * factor;
}

static GnomeRROutput *
get_primary_output (GnomeRRScreen *screen)
{
        GnomeRROutput *primary = NULL;
        GnomeRROutput **outputs;
        guint i;

        outputs = gnome_rr_screen_list_outputs (screen);
        if (outputs == NULL || outputs[0] == NULL)
                return NULL;
        for (i = 0; outputs[i] != NULL; i++) {
                if (gnome_rr_output_get_is_primary (outputs[i])) {
                        primary = outputs[i];
                        break;
                }
        }
        if (primary == NULL)
                primary = outputs[0];

        return primary;
}

static gboolean
primary_monitor_is_4k (GnomeRROutput *primary)
{
        GnomeRRMode *mode;

        mode = gnome_rr_output_get_current_mode (primary);
        if (gnome_rr_mode_get_width (mode) >= SMALLEST_4K_WIDTH)
                return TRUE;
        return FALSE;
}

static gboolean
primary_monitor_on_hdmi (GnomeRROutput *primary)
{
        const char *name;

        name = gnome_rr_output_get_name (primary);
        if (name == NULL ||
            strstr (name, "HDMI") == NULL)
                return FALSE;
        return TRUE;
}

static gboolean
primary_monitor_should_skip_resolution_check (GnomeRROutput *primary)
{
        if (!primary_monitor_is_4k (primary) && primary_monitor_on_hdmi (primary))
                return TRUE;

        return FALSE;
}

static void
get_dimensions_xrandr (GnomeRROutput *primary,
                       int           *width,
                       int           *height,
                       int           *width_mm,
                       int           *height_mm)
{
        GnomeRRMode *mode;

        mode = gnome_rr_output_get_current_mode (primary);
        *width = gnome_rr_mode_get_width (mode);
        *height = gnome_rr_mode_get_height (mode);

        gnome_rr_output_get_physical_size (primary,
                                           width_mm,
                                           height_mm);
}

static void
get_dimensions_gdk (int *width,
                    int *height,
                    int *width_mm,
                    int *height_mm)
{
        GdkDisplay *display;
        GdkScreen *screen;
        GdkRectangle rect;
        int primary;
        int monitor_scale;

        display = gdk_display_get_default ();
        screen = gdk_display_get_default_screen (display);
        primary = gdk_screen_get_primary_monitor (screen);
        gdk_screen_get_monitor_geometry (screen, primary, &rect);
        monitor_scale = gdk_screen_get_monitor_scale_factor (screen, primary);

        *width = rect.width * monitor_scale;
        *height = rect.height * monitor_scale;

        *width_mm = gdk_screen_get_monitor_width_mm (screen, primary);
        *height_mm = gdk_screen_get_monitor_height_mm (screen, primary);
}

static int
get_window_scale (GnomeXSettingsManager *manager)
{
	GSettings  *interface_settings;
        int window_scale;
        int width, height;
        int width_mm, height_mm;
        double dpi_x, dpi_y;

	interface_settings = g_hash_table_lookup (manager->priv->settings, INTERFACE_SETTINGS_SCHEMA);
        window_scale =
                g_settings_get_uint (interface_settings, SCALING_FACTOR_KEY);
        if (window_scale == 0) {
                GnomeRROutput *output = NULL;

                window_scale = 1;

                if (manager->priv->rr_screen)
                        output = get_primary_output (manager->priv->rr_screen);

                if (output) {
                        if (primary_monitor_should_skip_resolution_check (output))
                                goto out;

                        get_dimensions_xrandr (output,
                                               &width, &height,
                                               &width_mm, &height_mm);
                } else {
                        /* Before the D-Bus DisplayConfig service exported by
                         * Mutter becomes available, use the current information
                         * that GDK has from the X server; in simple cases, this
                         * will hopefully keep us from switching the window_scale
                         * during startup.
                         */
                        get_dimensions_gdk (&width, &height,
                                            &width_mm, &height_mm);
                }

                if (height < HIDPI_MIN_HEIGHT)
                        goto out;

                /* Somebody encoded the aspect ratio (16/9 or 16/10)
                 * instead of the physical size */
                if ((width_mm == 160 && height_mm == 90) ||
                    (width_mm == 160 && height_mm == 100) ||
                    (width_mm == 16 && height_mm == 9) ||
                    (width_mm == 16 && height_mm == 10))
                        goto out;

                window_scale = 1;
                if (width_mm > 0 && height_mm > 0) {
                        dpi_x = (double)width / (width_mm / 25.4);
                        dpi_y = (double)height / (height_mm / 25.4);
                        /* We don't completely trust these values so both
                           must be high, and never pick higher ratio than
                           2 automatically */
                        if (dpi_x > HIDPI_LIMIT && dpi_y > HIDPI_LIMIT)
                                window_scale = 2;
                }
        }

out:
        return window_scale;
}

typedef struct {
        gboolean    antialias;
        gboolean    hinting;
        int         scaled_dpi;
        int         dpi;
        int         window_scale;
        int         cursor_size;
        char       *cursor_theme;
        const char *rgba;
        const char *hintstyle;
} GnomeXftSettings;

/* Read GSettings and determine the appropriate Xft settings based on them. */
static void
xft_settings_get (GnomeXSettingsManager *manager,
                  GnomeXftSettings      *settings)
{
	GSettings  *interface_settings;
        GsdFontAntialiasingMode antialiasing;
        GsdFontHinting hinting;
        GsdFontRgbaOrder order;
        gboolean use_rgba = FALSE;
        double dpi;
        int cursor_size;

	interface_settings = g_hash_table_lookup (manager->priv->settings, INTERFACE_SETTINGS_SCHEMA);

        antialiasing = g_settings_get_enum (manager->priv->plugin_settings, FONT_ANTIALIASING_KEY);
        hinting = g_settings_get_enum (manager->priv->plugin_settings, FONT_HINTING_KEY);
        order = g_settings_get_enum (manager->priv->plugin_settings, FONT_RGBA_ORDER_KEY);

        settings->antialias = (antialiasing != GSD_FONT_ANTIALIASING_MODE_NONE);
        settings->hinting = (hinting != GSD_FONT_HINTING_NONE);
        settings->window_scale = get_window_scale (manager);
        dpi = get_dpi_from_gsettings (manager);
        settings->dpi = dpi * 1024; /* Xft wants 1/1024ths of an inch */
        settings->scaled_dpi = dpi * settings->window_scale * 1024;
        cursor_size = g_settings_get_int (interface_settings, CURSOR_SIZE_KEY);
        settings->cursor_size = cursor_size * settings->window_scale;
        settings->cursor_theme = g_settings_get_string (interface_settings, CURSOR_THEME_KEY);
        settings->rgba = "rgb";
        settings->hintstyle = "hintfull";

        switch (hinting) {
        case GSD_FONT_HINTING_NONE:
                settings->hintstyle = "hintnone";
                break;
        case GSD_FONT_HINTING_SLIGHT:
                settings->hintstyle = "hintslight";
                break;
        case GSD_FONT_HINTING_MEDIUM:
                settings->hintstyle = "hintmedium";
                break;
        case GSD_FONT_HINTING_FULL:
                settings->hintstyle = "hintfull";
                break;
        }

        switch (order) {
        case GSD_FONT_RGBA_ORDER_RGBA:
                settings->rgba = "rgba";
                break;
        case GSD_FONT_RGBA_ORDER_RGB:
                settings->rgba = "rgb";
                break;
        case GSD_FONT_RGBA_ORDER_BGR:
                settings->rgba = "bgr";
                break;
        case GSD_FONT_RGBA_ORDER_VRGB:
                settings->rgba = "vrgb";
                break;
        case GSD_FONT_RGBA_ORDER_VBGR:
                settings->rgba = "vbgr";
                break;
        }

        switch (antialiasing) {
        case GSD_FONT_ANTIALIASING_MODE_NONE:
                settings->antialias = 0;
                break;
        case GSD_FONT_ANTIALIASING_MODE_GRAYSCALE:
                settings->antialias = 1;
                break;
        case GSD_FONT_ANTIALIASING_MODE_RGBA:
                settings->antialias = 1;
                use_rgba = TRUE;
        }

        if (!use_rgba) {
                settings->rgba = "none";
        }
}

static void
xft_settings_clear (GnomeXftSettings *settings)
{
        g_free (settings->cursor_theme);
}

static void
xft_settings_set_xsettings (GnomeXSettingsManager *manager,
                            GnomeXftSettings      *settings)
{
        gnome_settings_profile_start (NULL);

        xsettings_manager_set_int (manager->priv->manager, "Xft/Antialias", settings->antialias);
        xsettings_manager_set_int (manager->priv->manager, "Xft/Hinting", settings->hinting);
        xsettings_manager_set_string (manager->priv->manager, "Xft/HintStyle", settings->hintstyle);
        xsettings_manager_set_int (manager->priv->manager, "Gdk/WindowScalingFactor", settings->window_scale);
        xsettings_manager_set_int (manager->priv->manager, "Gdk/UnscaledDPI", settings->dpi);
        xsettings_manager_set_int (manager->priv->manager, "Xft/DPI", settings->scaled_dpi);
        xsettings_manager_set_string (manager->priv->manager, "Xft/RGBA", settings->rgba);
        xsettings_manager_set_int (manager->priv->manager, "Gtk/CursorThemeSize", settings->cursor_size);
        xsettings_manager_set_string (manager->priv->manager, "Gtk/CursorThemeName", settings->cursor_theme);

        gnome_settings_profile_end (NULL);
}

static void
update_property (GString *props, const gchar* key, const gchar* value)
{
        gchar* needle;
        size_t needle_len;
        gchar* found = NULL;

        /* update an existing property */
        needle = g_strconcat (key, ":", NULL);
        needle_len = strlen (needle);
        if (g_str_has_prefix (props->str, needle))
                found = props->str;
        else 
            found = strstr (props->str, needle);

        if (found) {
                size_t value_index;
                gchar* end;

                end = strchr (found, '\n');
                value_index = (found - props->str) + needle_len + 1;
                g_string_erase (props, value_index, end ? (end - found - needle_len) : -1);
                g_string_insert (props, value_index, "\n");
                g_string_insert (props, value_index, value);
        } else {
                g_string_append_printf (props, "%s:\t%s\n", key, value);
        }

	g_free (needle);
}

static void
xft_settings_set_xresources (GnomeXftSettings *settings)
{
        GString    *add_string;
        char        dpibuf[G_ASCII_DTOSTR_BUF_SIZE];
        Display    *dpy;

        gnome_settings_profile_start (NULL);

        /* get existing properties */
        dpy = XOpenDisplay (NULL);
        g_return_if_fail (dpy != NULL);
        add_string = g_string_new (XResourceManagerString (dpy));

        g_debug("xft_settings_set_xresources: orig res '%s'", add_string->str);

        update_property (add_string, "Xft.dpi",
                                g_ascii_dtostr (dpibuf, sizeof (dpibuf), (double) settings->scaled_dpi / 1024.0));
        update_property (add_string, "Xft.antialias",
                                settings->antialias ? "1" : "0");
        update_property (add_string, "Xft.hinting",
                                settings->hinting ? "1" : "0");
        update_property (add_string, "Xft.hintstyle",
                                settings->hintstyle);
        update_property (add_string, "Xft.rgba",
                                settings->rgba);
        update_property (add_string, "Xcursor.size",
                                g_ascii_dtostr (dpibuf, sizeof (dpibuf), (double) settings->cursor_size));
        update_property (add_string, "Xcursor.theme",
                                settings->cursor_theme);

        g_debug("xft_settings_set_xresources: new res '%s'", add_string->str);

        /* Set the new X property */
        XChangeProperty(dpy, RootWindow (dpy, 0),
                        XA_RESOURCE_MANAGER, XA_STRING, 8, PropModeReplace, (const unsigned char *) add_string->str, add_string->len);
        XCloseDisplay (dpy);

        g_string_free (add_string, TRUE);

        gnome_settings_profile_end (NULL);
}

/* We mirror the Xft properties both through XSETTINGS and through
 * X resources
 */
static void
update_xft_settings (GnomeXSettingsManager *manager)
{
        GnomeXftSettings settings;

        gnome_settings_profile_start (NULL);

        xft_settings_get (manager, &settings);
        xft_settings_set_xsettings (manager, &settings);
        xft_settings_set_xresources (&settings);
        xft_settings_clear (&settings);

        gnome_settings_profile_end (NULL);
}

static void
xft_callback (GSettings             *settings,
              const gchar           *key,
              GnomeXSettingsManager *manager)
{
        update_xft_settings (manager);
        queue_notify (manager);
}

static void
override_callback (GSettings             *settings,
                   const gchar           *key,
                   GnomeXSettingsManager *manager)
{
        GVariant *value;

        value = g_settings_get_value (settings, XSETTINGS_OVERRIDE_KEY);

        xsettings_manager_set_overrides (manager->priv->manager, value);
        queue_notify (manager);

        g_variant_unref (value);
}

static void
plugin_callback (GSettings             *settings,
                 const char            *key,
                 GnomeXSettingsManager *manager)
{
        if (g_str_equal (key, GTK_MODULES_DISABLED_KEY) ||
            g_str_equal (key, GTK_MODULES_ENABLED_KEY) ||
            g_str_equal (key, "active")) {
                /* Do nothing, as GsdXsettingsGtk will handle it */
        } else if (g_str_equal (key, XSETTINGS_OVERRIDE_KEY)) {
                override_callback (settings, key, manager);
        } else {
                xft_callback (settings, key, manager);
        }
}

static void
gtk_modules_callback (GsdXSettingsGtk       *gtk,
                      GParamSpec            *spec,
                      GnomeXSettingsManager *manager)
{
        const char *modules = gsd_xsettings_gtk_get_modules (manager->priv->gtk);

        if (modules == NULL) {
                xsettings_manager_delete_setting (manager->priv->manager, "Gtk/Modules");
        } else {
                g_debug ("Setting GTK modules '%s'", modules);
                xsettings_manager_set_string (manager->priv->manager,
                                              "Gtk/Modules",
                                              modules);
        }

        queue_notify (manager);
}

static void
fontconfig_callback (fontconfig_monitor_handle_t *handle,
                     GnomeXSettingsManager       *manager)
{
        int timestamp = time (NULL);

        gnome_settings_profile_start (NULL);

        xsettings_manager_set_int (manager->priv->manager, "Fontconfig/Timestamp", timestamp);
        queue_notify (manager);
        gnome_settings_profile_end (NULL);
}

static gboolean
start_fontconfig_monitor_idle_cb (GnomeXSettingsManager *manager)
{
        gnome_settings_profile_start (NULL);

        manager->priv->fontconfig_handle = fontconfig_monitor_start ((GFunc) fontconfig_callback, manager);

        gnome_settings_profile_end (NULL);

        manager->priv->start_idle_id = 0;

        return FALSE;
}

static void
start_fontconfig_monitor (GnomeXSettingsManager  *manager)
{
        gnome_settings_profile_start (NULL);

        fontconfig_cache_init ();

        manager->priv->start_idle_id = g_idle_add ((GSourceFunc) start_fontconfig_monitor_idle_cb, manager);
        g_source_set_name_by_id (manager->priv->start_idle_id, "[gnome-settings-daemon] start_fontconfig_monitor_idle_cb");

        gnome_settings_profile_end (NULL);
}

static void
stop_fontconfig_monitor (GnomeXSettingsManager  *manager)
{
        if (manager->priv->fontconfig_handle) {
                fontconfig_monitor_stop (manager->priv->fontconfig_handle);
                manager->priv->fontconfig_handle = NULL;
        }
}

static void
notify_have_shell (GnomeXSettingsManager   *manager,
                   gboolean                 have_shell)
{
        gnome_settings_profile_start (NULL);
        if (manager->priv->have_shell == have_shell)
                return;
        manager->priv->have_shell = have_shell;
        xsettings_manager_set_int (manager->priv->manager, "Gtk/ShellShowsAppMenu", have_shell);
        queue_notify (manager);
        gnome_settings_profile_end (NULL);
}

static void
on_shell_appeared (GDBusConnection *connection,
                   const gchar     *name,
                   const gchar     *name_owner,
                   gpointer         user_data)
{
        notify_have_shell (user_data, TRUE);
}

static void
on_shell_disappeared (GDBusConnection *connection,
                      const gchar     *name,
                      gpointer         user_data)
{
        notify_have_shell (user_data, FALSE);
}

static void
process_value (GnomeXSettingsManager *manager,
               TranslationEntry      *trans,
               GVariant              *value)
{
        (* trans->translate) (manager, trans, value);
}

static TranslationEntry *
find_translation_entry (GSettings *settings, const char *key)
{
        guint i;
        char *schema;

        g_object_get (settings, "schema-id", &schema, NULL);

        if (g_str_equal (schema, CLASSIC_WM_SETTINGS_SCHEMA)) {
              g_free (schema);
              schema = g_strdup (WM_SETTINGS_SCHEMA);
        }

        for (i = 0; i < G_N_ELEMENTS (translations); i++) {
                if (g_str_equal (schema, translations[i].gsettings_schema) &&
                    g_str_equal (key, translations[i].gsettings_key)) {
                            g_free (schema);
                        return &translations[i];
                }
        }

        g_free (schema);

        return NULL;
}

static void
xsettings_callback (GSettings             *settings,
                    const char            *key,
                    GnomeXSettingsManager *manager)
{
        TranslationEntry *trans;
        GVariant         *value;

        if (g_str_equal (key, TEXT_SCALING_FACTOR_KEY) ||
            g_str_equal (key, SCALING_FACTOR_KEY) ||
            g_str_equal (key, CURSOR_SIZE_KEY) ||
            g_str_equal (key, CURSOR_THEME_KEY)) {
        	xft_callback (NULL, key, manager);
        	return;
	}

        trans = find_translation_entry (settings, key);
        if (trans == NULL) {
                return;
        }

        value = g_settings_get_value (settings, key);

        process_value (manager, trans, value);

        g_variant_unref (value);

        xsettings_manager_set_string (manager->priv->manager,
                                      "Net/FallbackIconTheme",
                                      "gnome");
        queue_notify (manager);
}

static void
terminate_cb (void *data)
{
        gboolean *terminated = data;

        if (*terminated) {
                return;
        }

        *terminated = TRUE;
        g_warning ("X Settings Manager is terminating");
        gtk_main_quit ();
}

static gboolean
setup_xsettings_managers (GnomeXSettingsManager *manager)
{
        GdkDisplay *display;
        gboolean    res;
        gboolean    terminated;

        display = gdk_display_get_default ();

        res = xsettings_manager_check_running (gdk_x11_display_get_xdisplay (display),
                                               gdk_screen_get_number (gdk_screen_get_default ()));

        if (res) {
                g_warning ("You can only run one xsettings manager at a time; exiting");
                return FALSE;
        }

        terminated = FALSE;
        manager->priv->manager = xsettings_manager_new (gdk_x11_display_get_xdisplay (display),
                                                        gdk_screen_get_number (gdk_screen_get_default ()),
                                                        terminate_cb,
                                                        &terminated);
        if (! manager->priv->manager) {
                g_warning ("Could not create xsettings manager!");
                return FALSE;
        }

        return TRUE;
}

static void
start_shell_monitor (GnomeXSettingsManager *manager)
{
        notify_have_shell (manager, TRUE);
        manager->priv->have_shell = TRUE;
        manager->priv->shell_name_watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                                               "org.gnome.Shell",
                                                               0,
                                                               on_shell_appeared,
                                                               on_shell_disappeared,
                                                               manager,
                                                               NULL);
}

static void
force_disable_animation_changed (GObject    *gobject,
                                 GParamSpec *pspec,
                                 GnomeXSettingsManager *manager)
{
        gboolean force_disable, value;

        g_object_get (gobject, "force-disable-animations", &force_disable, NULL);
        if (force_disable)
                value = FALSE;
        else {
                GSettings *settings;

                settings = g_hash_table_lookup (manager->priv->settings, "org.gnome.desktop.interface");
                value = g_settings_get_boolean (settings, "enable-animations");
        }

        xsettings_manager_set_int (manager->priv->manager, "Gtk/EnableAnimations", value);

        queue_notify (manager);
}

static void
enable_animations_changed_cb (GSettings             *settings,
                              gchar                 *key,
                              GnomeXSettingsManager *manager)
{
        force_disable_animation_changed (G_OBJECT (manager->priv->remote_display), NULL, manager);
}

static void
on_rr_screen_changed (GnomeRRScreen         *screen,
                      GnomeXSettingsManager *manager)
{
        update_xft_settings (manager);
        queue_notify (manager);
}

static void
on_rr_screen_acquired (GObject      *object,
                       GAsyncResult *result,
                       gpointer      data)
{
        GnomeXSettingsManager *manager = data;
        GnomeRRScreen *rr_screen;

        rr_screen = gnome_rr_screen_new_finish (result, NULL);
        if (!rr_screen)
                return;

        manager->priv->rr_screen = rr_screen;
        g_signal_connect (rr_screen, "changed",
                          G_CALLBACK (on_rr_screen_changed), manager);

        on_rr_screen_changed (rr_screen, manager);
}

gboolean
gnome_xsettings_manager_start (GnomeXSettingsManager *manager,
                               GError               **error)
{
        GVariant    *overrides;
        guint        i;
        GList       *list, *l;
        const char  *session;

        g_debug ("Starting xsettings manager");
        gnome_settings_profile_start (NULL);

        if (!setup_xsettings_managers (manager)) {
                g_set_error (error, GSD_XSETTINGS_ERROR,
                             GSD_XSETTINGS_ERROR_INIT,
                             "Could not initialize xsettings manager.");
                return FALSE;
        }

        manager->priv->remote_display = gsd_remote_display_manager_new ();
        g_signal_connect (G_OBJECT (manager->priv->remote_display), "notify::force-disable-animations",
                          G_CALLBACK (force_disable_animation_changed), manager);

        gnome_rr_screen_new_async (gdk_screen_get_default (),
                                   on_rr_screen_acquired,
                                   manager);

        manager->priv->settings = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                         NULL, (GDestroyNotify) g_object_unref);

        g_hash_table_insert (manager->priv->settings,
                             MOUSE_SETTINGS_SCHEMA, g_settings_new (MOUSE_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             BACKGROUND_SETTINGS_SCHEMA, g_settings_new (BACKGROUND_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             INTERFACE_SETTINGS_SCHEMA, g_settings_new (INTERFACE_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             SOUND_SETTINGS_SCHEMA, g_settings_new (SOUND_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             PRIVACY_SETTINGS_SCHEMA, g_settings_new (PRIVACY_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             WM_SETTINGS_SCHEMA, g_settings_new (WM_SETTINGS_SCHEMA));
        g_hash_table_insert (manager->priv->settings,
                             A11Y_SCHEMA, g_settings_new (A11Y_SCHEMA));

        session = g_getenv ("XDG_CURRENT_DESKTOP");
        if (session && strstr (session, "GNOME-Classic")) {
                GSettingsSchema *schema;

                schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                                  CLASSIC_WM_SETTINGS_SCHEMA, FALSE);
                if (schema) {
                        g_hash_table_insert (manager->priv->settings,
                                             CLASSIC_WM_SETTINGS_SCHEMA,
                                             g_settings_new_full (schema, NULL, NULL));
                        g_settings_schema_unref (schema);
                }
        }

        g_signal_connect (G_OBJECT (g_hash_table_lookup (manager->priv->settings, INTERFACE_SETTINGS_SCHEMA)), "changed::enable-animations",
                          G_CALLBACK (enable_animations_changed_cb), manager);

        for (i = 0; i < G_N_ELEMENTS (fixed_entries); i++) {
                FixedEntry *fixed = &fixed_entries[i];
                (* fixed->func) (manager, fixed);
        }

        list = g_hash_table_get_values (manager->priv->settings);
        for (l = list; l != NULL; l = l->next) {
                g_signal_connect_object (G_OBJECT (l->data), "changed", G_CALLBACK (xsettings_callback), manager, 0);
        }
        g_list_free (list);

        for (i = 0; i < G_N_ELEMENTS (translations); i++) {
                GVariant *val;
                GSettings *settings;

                settings = g_hash_table_lookup (manager->priv->settings,
                                                translations[i].gsettings_schema);
                if (settings == NULL) {
                        g_warning ("Schemas '%s' has not been setup", translations[i].gsettings_schema);
                        continue;
                }

                val = g_settings_get_value (settings, translations[i].gsettings_key);

                process_value (manager, &translations[i], val);
                g_variant_unref (val);
        }

        /* Plugin settings (GTK modules and Xft) */
        manager->priv->plugin_settings = g_settings_new (XSETTINGS_PLUGIN_SCHEMA);
        g_signal_connect_object (manager->priv->plugin_settings, "changed", G_CALLBACK (plugin_callback), manager, 0);

        manager->priv->gtk = gsd_xsettings_gtk_new ();
        g_signal_connect (G_OBJECT (manager->priv->gtk), "notify::gtk-modules",
                          G_CALLBACK (gtk_modules_callback), manager);
        gtk_modules_callback (manager->priv->gtk, NULL, manager);

        /* Animation settings */
        force_disable_animation_changed (G_OBJECT (manager->priv->remote_display), NULL, manager);

        /* Xft settings */
        update_xft_settings (manager);

        start_fontconfig_monitor (manager);

        start_shell_monitor (manager);

        xsettings_manager_set_string (manager->priv->manager,
                                      "Net/FallbackIconTheme",
                                      "gnome");

        overrides = g_settings_get_value (manager->priv->plugin_settings, XSETTINGS_OVERRIDE_KEY);
        xsettings_manager_set_overrides (manager->priv->manager, overrides);
        queue_notify (manager);
        g_variant_unref (overrides);


        gnome_settings_profile_end (NULL);

        return TRUE;
}

void
gnome_xsettings_manager_stop (GnomeXSettingsManager *manager)
{
        GnomeXSettingsManagerPrivate *p = manager->priv;

        g_debug ("Stopping xsettings manager");

        g_clear_object (&manager->priv->remote_display);

        if (manager->priv->rr_screen != NULL) {
                g_signal_handlers_disconnect_by_func (manager->priv->rr_screen,
                                                      (gpointer) on_rr_screen_changed,
                                                      manager);
                g_clear_object (&manager->priv->rr_screen);
        }

        if (p->shell_name_watch_id > 0) {
                g_bus_unwatch_name (p->shell_name_watch_id);
                p->shell_name_watch_id = 0;
        }

        if (p->manager != NULL) {
                xsettings_manager_destroy (p->manager);
                p->manager = NULL;
        }

        if (p->plugin_settings != NULL) {
                g_signal_handlers_disconnect_by_data (p->plugin_settings, manager);
                g_object_unref (p->plugin_settings);
                p->plugin_settings = NULL;
        }

        stop_fontconfig_monitor (manager);

        if (p->settings != NULL) {
                g_hash_table_destroy (p->settings);
                p->settings = NULL;
        }

        if (p->gtk != NULL) {
                g_object_unref (p->gtk);
                p->gtk = NULL;
        }
}

static void
gnome_xsettings_manager_class_init (GnomeXSettingsManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gnome_xsettings_manager_finalize;

        g_type_class_add_private (klass, sizeof (GnomeXSettingsManagerPrivate));
}

static void
gnome_xsettings_manager_init (GnomeXSettingsManager *manager)
{
        manager->priv = GNOME_XSETTINGS_MANAGER_GET_PRIVATE (manager);
}

static void
gnome_xsettings_manager_finalize (GObject *object)
{
        GnomeXSettingsManager *xsettings_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GNOME_IS_XSETTINGS_MANAGER (object));

        xsettings_manager = GNOME_XSETTINGS_MANAGER (object);

        g_return_if_fail (xsettings_manager->priv != NULL);

        gnome_xsettings_manager_stop (xsettings_manager);

        if (xsettings_manager->priv->start_idle_id != 0)
                g_source_remove (xsettings_manager->priv->start_idle_id);

        G_OBJECT_CLASS (gnome_xsettings_manager_parent_class)->finalize (object);
}

GnomeXSettingsManager *
gnome_xsettings_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GNOME_TYPE_XSETTINGS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GNOME_XSETTINGS_MANAGER (manager_object);
}
