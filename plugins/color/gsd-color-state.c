/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 NVIDIA CORPORATION
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

#include <glib/gi18n.h>
#include <colord.h>
#include <gdk/gdk.h>
#include <stdlib.h>
#include <lcms2.h>
#include <canberra-gtk.h>
#include <math.h>
#include <libgnome-desktop/gnome-pnp-ids.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-rr.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "gnome-settings-bus.h"

#include "gsd-color-manager.h"
#include "gsd-color-state.h"
#include "gcm-edid.h"

static void gcm_session_set_gamma_for_all_devices (GsdColorState *state);

struct _GsdColorState
{
        GObject          parent;

        GCancellable    *cancellable;
        GsdSessionManager *session;
        CdClient        *client;
        GnomeRRScreen   *state_screen;
        GdkWindow       *gdk_window;
        gboolean         session_is_active;
        GHashTable      *device_assign_hash;
        guint            color_temperature;
};

static void     gsd_color_state_class_init  (GsdColorStateClass *klass);
static void     gsd_color_state_init        (GsdColorState      *color_state);
static void     gsd_color_state_finalize    (GObject            *object);

G_DEFINE_TYPE (GsdColorState, gsd_color_state, G_TYPE_OBJECT)

/* see http://www.oyranos.org/wiki/index.php?title=ICC_Profiles_in_X_Specification_0.3 */
#define GCM_ICC_PROFILE_IN_X_VERSION_MAJOR      0
#define GCM_ICC_PROFILE_IN_X_VERSION_MINOR      3

GQuark
gsd_color_state_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("gsd_color_state_error");
        return quark;
}

static CdIcc *
gcm_get_srgb_icc (GError **error)
{
        /* load the reference sRGB profile */
        CdIcc *srgb_icc = cd_icc_new ();
        if (!srgb_icc)
                return NULL;

        if (!cd_icc_create_default_full (srgb_icc,
                                         CD_ICC_LOAD_FLAGS_PRIMARIES,
                                         error)) {
                g_object_unref (srgb_icc);
                return NULL;
        }

        return srgb_icc;
}

static char *
gcm_get_srgb_bytes (gsize *length,
                    GError **error)
{
        g_autoptr(CdIcc) srgb_icc = NULL;
        GBytes *bytes;

        srgb_icc = gcm_get_srgb_icc (error);
        if (srgb_icc == NULL)
                return NULL;

        bytes = cd_icc_save_data (srgb_icc,
                                  CD_ICC_SAVE_FLAGS_NONE,
                                  error);
        if (bytes == NULL)
                return NULL;

        return g_bytes_unref_to_data (bytes, length);
}

static gboolean
gcm_session_screen_set_icc_profile (GsdColorState *state,
                                    GnomeRROutput *output,
                                    const gchar *filename,
                                    GError **error)
{
        gchar *data = NULL;
        gsize length;
        guint version_data;

        g_return_val_if_fail (filename != NULL, FALSE);

        /* wayland */
        if (state->gdk_window == NULL) {
                g_debug ("not setting atom as running under wayland");
                return TRUE;
        }

        g_debug ("setting root window ICC profile atom from %s", filename);

        if (gnome_rr_output_supports_color_transform (output)) {
                /*
                 * If the window system supports color transforms, then
                 * applications should use the standard sRGB color profile and
                 * the window system will take care of converting colors to
                 * match the output device's measured color profile.
                 */
                data = gcm_get_srgb_bytes (&length, error);
                if (data == NULL)
                        return FALSE;
        } else {
                /* get contents of file */
                if (!g_file_get_contents (filename, &data, &length, error))
                        return FALSE;
        }

        /* set profile property */
        gdk_property_change (state->gdk_window,
                             gdk_atom_intern_static_string ("_ICC_PROFILE"),
                             gdk_atom_intern_static_string ("CARDINAL"),
                             8,
                             GDK_PROP_MODE_REPLACE,
                             (const guchar *) data, length);

        /* set version property */
        version_data = GCM_ICC_PROFILE_IN_X_VERSION_MAJOR * 100 +
                        GCM_ICC_PROFILE_IN_X_VERSION_MINOR * 1;
        gdk_property_change (state->gdk_window,
                             gdk_atom_intern_static_string ("_ICC_PROFILE_IN_X_VERSION"),
                             gdk_atom_intern_static_string ("CARDINAL"),
                             8,
                             GDK_PROP_MODE_REPLACE,
                             (const guchar *) &version_data, 1);

        g_free (data);
        return TRUE;
}

void
gsd_color_state_set_temperature (GsdColorState *state, guint temperature)
{
        g_return_if_fail (GSD_IS_COLOR_STATE (state));

        if (state->color_temperature == temperature)
                return;

        state->color_temperature = temperature;
        gcm_session_set_gamma_for_all_devices (state);
}

guint
gsd_color_state_get_temperature (GsdColorState *state)
{
        g_return_val_if_fail (GSD_IS_COLOR_STATE (state), 0);
        return state->color_temperature;
}

typedef struct {
        GsdColorState         *state;
        CdProfile               *profile;
        CdDevice                *device;
        guint32                  output_id;
} GcmSessionAsyncHelper;

static void
gcm_session_async_helper_free (GcmSessionAsyncHelper *helper)
{
        if (helper->state != NULL)
                g_object_unref (helper->state);
        if (helper->profile != NULL)
                g_object_unref (helper->profile);
        if (helper->device != NULL)
                g_object_unref (helper->device);
        g_free (helper);
}

static uint64_t
gcm_double_to_ctmval (double value)
{
        uint64_t sign = value < 0;
        double integer, fractional;

        if (sign)
                value = -value;

        fractional = modf (value, &integer);

        return sign << 63 |
               (uint64_t) integer << 32 |
               (uint64_t) (fractional * 0xffffffffUL);
}

static GnomeRRCTM
gcm_mat33_to_ctm (CdMat3x3 matrix)
{
        GnomeRRCTM ctm;

        /*
         * libcolord generates a matrix containing double values. RandR's CTM
         * property expects values in S31.32 fixed-point sign-magnitude format
         */
        ctm.matrix[0] = gcm_double_to_ctmval (matrix.m00);
        ctm.matrix[1] = gcm_double_to_ctmval (matrix.m01);
        ctm.matrix[2] = gcm_double_to_ctmval (matrix.m02);
        ctm.matrix[3] = gcm_double_to_ctmval (matrix.m10);
        ctm.matrix[4] = gcm_double_to_ctmval (matrix.m11);
        ctm.matrix[5] = gcm_double_to_ctmval (matrix.m12);
        ctm.matrix[6] = gcm_double_to_ctmval (matrix.m20);
        ctm.matrix[7] = gcm_double_to_ctmval (matrix.m21);
        ctm.matrix[8] = gcm_double_to_ctmval (matrix.m22);

        return ctm;
}

static gboolean
gcm_set_csc_matrix (GnomeRROutput *output,
                    CdProfile *profile,
                    GError **error)
{
        g_autoptr(CdIcc) icc = NULL;
        g_autoptr(CdIcc) srgb_icc = NULL;
        CdMat3x3 csc;
        GnomeRRCTM ctm;

        /* open file */
        icc = cd_profile_load_icc (profile, CD_ICC_LOAD_FLAGS_PRIMARIES, NULL,
                                   error);
        if (icc == NULL)
                return FALSE;

        srgb_icc = gcm_get_srgb_icc (error);
        if (srgb_icc == NULL)
                return FALSE;

        if (!cd_icc_utils_get_adaptation_matrix (icc, srgb_icc, &csc, error))
                return FALSE;

        ctm = gcm_mat33_to_ctm (csc);
        return gnome_rr_output_set_color_transform (output, ctm, error);
}

static GnomeRROutput *
gcm_session_get_state_output_by_device (GsdColorState *state,
                                        CdDevice *device,
                                        GError **error)
{
        GnomeRROutput *output = NULL;
        GnomeRROutput **outputs = NULL;
        GnomePnpIds *pnp_ids;
        guint i;

        pnp_ids = gnome_pnp_ids_new ();

        /* search all STATE outputs for the device id */
        outputs = gnome_rr_screen_list_outputs (state->state_screen);
        if (outputs == NULL) {
                g_set_error_literal (error,
                                     GSD_COLOR_MANAGER_ERROR,
                                     GSD_COLOR_MANAGER_ERROR_FAILED,
                                     "Failed to get outputs");
                goto out;
        }
        for (i = 0; outputs[i] != NULL && output == NULL; i++) {
                g_autofree char *vendor = NULL;
                g_autofree char *vendor_name = NULL;
                g_autofree char *vendor_name_quirked = NULL;
                g_autofree char *product = NULL;
                g_autofree char *serial = NULL;

                gnome_rr_output_get_ids_from_edid (outputs[i],
                                                   &vendor,
                                                   &product,
                                                   &serial);
                vendor_name = gnome_pnp_ids_get_pnp_id (pnp_ids, vendor);
                if (vendor_name)
                        vendor_name_quirked = cd_quirk_vendor_name (vendor_name);
                else
                        vendor_name_quirked = g_strdup (vendor);

                if (g_strcmp0 (vendor_name_quirked, cd_device_get_vendor (device)) == 0 &&
                    g_strcmp0 (product, cd_device_get_model (device)) == 0 &&
                    g_strcmp0 (serial, cd_device_get_serial (device)) == 0)
                        output = outputs[i];
        }
        if (output == NULL) {
                g_set_error (error,
                             GSD_COLOR_MANAGER_ERROR,
                             GSD_COLOR_MANAGER_ERROR_FAILED,
                             "Failed to find output");
        }
out:
        g_object_unref (pnp_ids);
        return output;
}

/* this function is more complicated than it should be, due to the
 * fact that XOrg sometimes assigns no primary devices when using
 * "xrandr --auto" or when the version of RANDR is < 1.3 */
static gboolean
gcm_session_use_output_profile_for_screen (GsdColorState *state,
                                           GnomeRROutput *output)
{
        gboolean has_laptop = FALSE;
        gboolean has_primary = FALSE;
        GnomeRROutput **outputs;
        GnomeRROutput *connected = NULL;
        guint i;

        /* do we have any screens marked as primary */
        outputs = gnome_rr_screen_list_outputs (state->state_screen);
        if (outputs == NULL || outputs[0] == NULL) {
                g_warning ("failed to get outputs");
                return FALSE;
        }
        for (i = 0; outputs[i] != NULL; i++) {
                if (connected == NULL)
                        connected = outputs[i];
                if (gnome_rr_output_get_is_primary (outputs[i]))
                        has_primary = TRUE;
                if (gnome_rr_output_is_builtin_display (outputs[i]))
                        has_laptop = TRUE;
        }

        /* we have an assigned primary device, are we that? */
        if (has_primary)
                return gnome_rr_output_get_is_primary (output);

        /* choosing the internal panel is probably sane */
        if (has_laptop)
                return gnome_rr_output_is_builtin_display (output);

        /* we have to choose one, so go for the first connected device */
        if (connected != NULL)
                return gnome_rr_output_get_id (connected) == gnome_rr_output_get_id (output);

        return FALSE;
}

static void
gcm_session_device_assign_profile_connect_cb (GObject *object,
                                              GAsyncResult *res,
                                              gpointer user_data)
{
        CdProfile *profile = CD_PROFILE (object);
        const gchar *filename;
        gboolean ret;
        GError *error = NULL;
        GnomeRROutput *output;
        GcmSessionAsyncHelper *helper = (GcmSessionAsyncHelper *) user_data;
        GsdColorState *state = GSD_COLOR_STATE (helper->state);

        /* get properties */
        ret = cd_profile_connect_finish (profile, res, &error);
        if (!ret) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("failed to connect to profile: %s", error->message);
                g_error_free (error);
                goto out;
        }

        /* get the filename */
        filename = cd_profile_get_filename (profile);
        g_assert (filename != NULL);

        /* get the output (can't save in helper as GnomeRROutput isn't
         * a GObject, just a pointer */
        output = gnome_rr_screen_get_output_by_id (state->state_screen,
                                                   helper->output_id);
        if (output == NULL)
                goto out;

        /* set the _ICC_PROFILE atom */
        ret = gcm_session_use_output_profile_for_screen (state, output);
        if (ret) {
                ret = gcm_session_screen_set_icc_profile (state,
                                                          output,
                                                          filename,
                                                          &error);
                if (!ret) {
                        g_warning ("failed to set screen _ICC_PROFILE: %s",
                                   error->message);
                        g_clear_error (&error);
                }
        }

        if (gnome_rr_output_supports_color_transform (output)) {
                ret = gcm_set_csc_matrix (output,
                                          profile,
                                          &error);
                if (!ret) {
                        g_warning ("failed to set %s color transform matrix: %s",
                                   cd_device_get_id (helper->device),
                                   error->message);
                        g_error_free (error);
                        goto out;
                }
        }

out:
        gcm_session_async_helper_free (helper);
}

static void
gcm_session_device_assign_connect_cb (GObject *object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
        CdDeviceKind kind;
        CdProfile *profile = NULL;
        gboolean ret;
        GnomeRROutput *output = NULL;
        GError *error = NULL;
        GcmSessionAsyncHelper *helper;
        CdDevice *device = CD_DEVICE (object);
        GsdColorState *state = GSD_COLOR_STATE (user_data);

        /* remove from assign array */
        g_hash_table_remove (state->device_assign_hash,
                             cd_device_get_object_path (device));

        /* get properties */
        ret = cd_device_connect_finish (device, res, &error);
        if (!ret) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("failed to connect to device: %s", error->message);
                g_error_free (error);
                goto out;
        }

        /* check we care */
        kind = cd_device_get_kind (device);
        if (kind != CD_DEVICE_KIND_DISPLAY)
                goto out;

        g_debug ("need to assign display device %s",
                 cd_device_get_id (device));

        /* get the GnomeRROutput for the device */
        output = gcm_session_get_state_output_by_device (state,
                                                         device,
                                                         &error);
        if (output == NULL) {
                g_warning ("no %s device found: %s",
                           cd_device_get_id (device),
                           error->message);
                g_error_free (error);
                goto out;
        }

        /* get the default profile for the device */
        profile = cd_device_get_default_profile (device);
        if (profile == NULL) {
                g_debug ("%s has no default profile to set",
                         cd_device_get_id (device));

                /* the default output? */
                if (gnome_rr_output_get_is_primary (output) &&
                    state->gdk_window != NULL) {
                        gdk_property_delete (state->gdk_window,
                                             gdk_atom_intern_static_string ("_ICC_PROFILE"));
                        gdk_property_delete (state->gdk_window,
                                             gdk_atom_intern_static_string ("_ICC_PROFILE_IN_X_VERSION"));
                }
                goto out;
        }

        /* get properties */
        helper = g_new0 (GcmSessionAsyncHelper, 1);
        helper->output_id = gnome_rr_output_get_id (output);
        helper->state = g_object_ref (state);
        helper->device = g_object_ref (device);
        cd_profile_connect (profile,
                            state->cancellable,
                            gcm_session_device_assign_profile_connect_cb,
                            helper);
out:
        if (profile != NULL)
                g_object_unref (profile);
}

static void
gcm_session_device_assign (GsdColorState *state, CdDevice *device)
{
        const gchar *key;
        gpointer found;

        /* are we already assigning this device */
        key = cd_device_get_object_path (device);
        found = g_hash_table_lookup (state->device_assign_hash, key);
        if (found != NULL) {
                g_debug ("assign for %s already in progress", key);
                return;
        }
        g_hash_table_insert (state->device_assign_hash,
                             g_strdup (key),
                             GINT_TO_POINTER (TRUE));
        cd_device_connect (device,
                           state->cancellable,
                           gcm_session_device_assign_connect_cb,
                           state);
}

static void
gcm_session_device_added_assign_cb (CdClient *client,
                                    CdDevice *device,
                                    GsdColorState *state)
{
        gcm_session_device_assign (state, device);
}

static void
gcm_session_device_changed_assign_cb (CdClient *client,
                                      CdDevice *device,
                                      GsdColorState *state)
{
        g_debug ("%s changed", cd_device_get_object_path (device));
        gcm_session_device_assign (state, device);
}

static void
gcm_session_get_devices_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
        CdDevice *device;
        GError *error = NULL;
        GPtrArray *array;
        guint i;
        GsdColorState *state = GSD_COLOR_STATE (user_data);

        array = cd_client_get_devices_finish (CD_CLIENT (object), res, &error);
        if (array == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("failed to get devices: %s", error->message);
                g_error_free (error);
                return;
        }
        for (i = 0; i < array->len; i++) {
                device = g_ptr_array_index (array, i);
                gcm_session_device_assign (state, device);
        }

        if (array != NULL)
                g_ptr_array_unref (array);
}

static void
gcm_session_profile_gamma_find_device_cb (GObject *object,
                                          GAsyncResult *res,
                                          gpointer user_data)
{
        CdClient *client = CD_CLIENT (object);
        CdDevice *device = NULL;
        GError *error = NULL;
        GsdColorState *state = GSD_COLOR_STATE (user_data);

        device = cd_client_find_device_by_property_finish (client,
                                                           res,
                                                           &error);
        if (device == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("could not find device: %s", error->message);
                g_error_free (error);
                return;
        }

        /* get properties */
        cd_device_connect (device,
                           state->cancellable,
                           gcm_session_device_assign_connect_cb,
                           state);

        if (device != NULL)
                g_object_unref (device);
}

static void
gcm_session_set_gamma_for_all_devices (GsdColorState *state)
{
        GnomeRROutput **outputs;
        guint i;

        /* setting the temperature before we get the list of devices is fine,
         * as we use the temperature in the calculation */
        if (state->state_screen == NULL)
                return;

        /* get STATE outputs */
        outputs = gnome_rr_screen_list_outputs (state->state_screen);
        if (outputs == NULL) {
                g_warning ("failed to get outputs");
                return;
        }
        for (i = 0; outputs[i] != NULL; i++) {
                /* get CdDevice for this output */
                cd_client_find_device_by_property (state->client,
                                                   CD_DEVICE_METADATA_XRANDR_NAME,
                                                   gnome_rr_output_get_name (outputs[i]),
                                                   state->cancellable,
                                                   gcm_session_profile_gamma_find_device_cb,
                                                   state);
        }
}

/* We have to reset the gamma tables each time as if the primary output
 * has changed then different crtcs are going to be used.
 * See https://bugzilla.gnome.org/show_bug.cgi?id=660164 for an example */
static void
gnome_rr_screen_output_changed_cb (GnomeRRScreen *screen,
                                   GsdColorState *state)
{
        gcm_session_set_gamma_for_all_devices (state);
}

static gboolean
has_changed (char       **strv,
	     const char  *str)
{
        guint i;
        for (i = 0; strv[i] != NULL; i++) {
                if (g_str_equal (str, strv[i]))
                        return TRUE;
        }
        return FALSE;
}

static void
gcm_session_active_changed_cb (GDBusProxy      *session,
                               GVariant        *changed,
                               char           **invalidated,
                               GsdColorState *state)
{
        GVariant *active_v = NULL;
        gboolean is_active;

        if (has_changed (invalidated, "SessionIsActive"))
                return;

        /* not yet connected to the daemon */
        if (!cd_client_get_connected (state->client))
                return;

        active_v = g_dbus_proxy_get_cached_property (session, "SessionIsActive");
        g_return_if_fail (active_v != NULL);
        is_active = g_variant_get_boolean (active_v);
        g_variant_unref (active_v);

        /* When doing the fast-user-switch into a new account, load the
         * new users chosen profiles.
         *
         * If this is the first time the GnomeSettingsSession has been
         * loaded, then we'll get a change from unknown to active
         * and we want to avoid reprobing the devices for that.
         */
        if (is_active && !state->session_is_active) {
                g_debug ("Done switch to new account, reload devices");
                cd_client_get_devices (state->client,
                                       state->cancellable,
                                       gcm_session_get_devices_cb,
                                       state);
        }
        state->session_is_active = is_active;
}

static void
gcm_session_client_connect_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        GsdColorState *state = GSD_COLOR_STATE (user_data);

        /* connected */
        ret = cd_client_connect_finish (state->client, res, &error);
        if (!ret) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("failed to connect to colord: %s", error->message);
                g_error_free (error);
                return;
        }

        /* is there an available colord instance? */
        ret = cd_client_get_has_server (state->client);
        if (!ret) {
                g_warning ("There is no colord server available");
                return;
        }

        /* watch if sessions change */
        g_signal_connect_object (state->session, "g-properties-changed",
                                 G_CALLBACK (gcm_session_active_changed_cb),
                                 state, 0);

        g_signal_connect (state->state_screen, "changed",
                          G_CALLBACK (gnome_rr_screen_output_changed_cb),
                          state);

        g_signal_connect (state->client, "device-added",
                          G_CALLBACK (gcm_session_device_added_assign_cb),
                          state);
        g_signal_connect (state->client, "device-changed",
                          G_CALLBACK (gcm_session_device_changed_assign_cb),
                          state);

        /* set for each device that already exist */
        cd_client_get_devices (state->client,
                               state->cancellable,
                               gcm_session_get_devices_cb,
                               state);
}

static void
on_rr_screen_acquired (GObject      *object,
                       GAsyncResult *result,
                       gpointer      data)
{
        GsdColorState *state = data;
        GnomeRRScreen *screen;
        GError *error = NULL;

        /* gnome_rr_screen_new_async() does not take a GCancellable */
        if (g_cancellable_is_cancelled (state->cancellable))
                goto out;

        screen = gnome_rr_screen_new_finish (result, &error);
        if (screen == NULL) {
                g_warning ("failed to get screens: %s", error->message);
                g_error_free (error);
                goto out;
        }

        state->state_screen = screen;

        cd_client_connect (state->client,
                           state->cancellable,
                           gcm_session_client_connect_cb,
                           state);
out:
        /* manually added */
        g_object_unref (state);
}

void
gsd_color_state_start (GsdColorState *state)
{
        /* use a fresh cancellable for each start->stop operation */
        g_cancellable_cancel (state->cancellable);
        g_clear_object (&state->cancellable);
        state->cancellable = g_cancellable_new ();

        /* coldplug the list of screens */
        gnome_rr_screen_new_async (gdk_screen_get_default (),
                                   on_rr_screen_acquired,
                                   g_object_ref (state));
}

void
gsd_color_state_stop (GsdColorState *state)
{
        g_cancellable_cancel (state->cancellable);
}

static void
gsd_color_state_class_init (GsdColorStateClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_color_state_finalize;
}

static void
gsd_color_state_init (GsdColorState *state)
{
        /* track the active session */
        state->session = gnome_settings_bus_get_session_proxy ();

#ifdef GDK_WINDOWING_X11
        /* set the _ICC_PROFILE atoms on the root screen */
        if (GDK_IS_X11_DISPLAY (gdk_display_get_default ()))
                state->gdk_window = gdk_screen_get_root_window (gdk_screen_get_default ());
#endif

        /* we don't want to assign devices multiple times at startup */
        state->device_assign_hash = g_hash_table_new_full (g_str_hash,
                                                          g_str_equal,
                                                          g_free,
                                                          NULL);

        /* default color temperature */
        state->color_temperature = GSD_COLOR_TEMPERATURE_DEFAULT;

        state->client = cd_client_new ();
}

static void
gsd_color_state_finalize (GObject *object)
{
        GsdColorState *state;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_COLOR_STATE (object));

        state = GSD_COLOR_STATE (object);

        g_cancellable_cancel (state->cancellable);
        g_clear_object (&state->cancellable);
        g_clear_object (&state->client);
        g_clear_object (&state->session);
        g_clear_pointer (&state->device_assign_hash, g_hash_table_destroy);
        g_clear_object (&state->state_screen);

        G_OBJECT_CLASS (gsd_color_state_parent_class)->finalize (object);
}

GsdColorState *
gsd_color_state_new (void)
{
        GsdColorState *state;
        state = g_object_new (GSD_TYPE_COLOR_STATE, NULL);
        return GSD_COLOR_STATE (state);
}
