/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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

#include "gsd-color-profiles.h"

struct _GsdColorProfiles
{
        GObject          parent;

        GCancellable    *cancellable;
        CdClient        *client;
        CdIccStore      *icc_store;
};

static void     gsd_color_profiles_class_init  (GsdColorProfilesClass *klass);
static void     gsd_color_profiles_init        (GsdColorProfiles      *color_profiles);
static void     gsd_color_profiles_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdColorProfiles, gsd_color_profiles, G_TYPE_OBJECT)

static void
gsd_color_profiles_class_init (GsdColorProfilesClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_color_profiles_finalize;
}

static void
gcm_session_client_connect_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        CdClient *client = CD_CLIENT (source_object);
        GsdColorProfiles *profiles;

        /* connected */
        ret = cd_client_connect_finish (client, res, &error);
        if (!ret) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("failed to connect to colord: %s", error->message);
                g_error_free (error);
                return;
        }

        /* is there an available colord instance? */
        profiles = GSD_COLOR_PROFILES (user_data);
        ret = cd_client_get_has_server (profiles->client);
        if (!ret) {
                g_warning ("There is no colord server available");
                return;
        }

        /* add profiles */
        ret = cd_icc_store_search_kind (profiles->icc_store,
                                        CD_ICC_STORE_SEARCH_KIND_USER,
                                        CD_ICC_STORE_SEARCH_FLAGS_CREATE_LOCATION,
                                        profiles->cancellable,
                                        &error);
        if (!ret) {
                g_warning ("failed to add user icc: %s", error->message);
                g_error_free (error);
        }
}

gboolean
gsd_color_profiles_start (GsdColorProfiles *profiles,
                          GError          **error)
{
        /* use a fresh cancellable for each start->stop operation */
        g_cancellable_cancel (profiles->cancellable);
        g_clear_object (&profiles->cancellable);
        profiles->cancellable = g_cancellable_new ();

        cd_client_connect (profiles->client,
                           profiles->cancellable,
                           gcm_session_client_connect_cb,
                           profiles);

        return TRUE;
}

void
gsd_color_profiles_stop (GsdColorProfiles *profiles)
{
        g_cancellable_cancel (profiles->cancellable);
}

static void
gcm_session_create_profile_cb (GObject *object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        CdProfile *profile;
        GError *error = NULL;
        CdClient *client = CD_CLIENT (object);

        profile = cd_client_create_profile_finish (client, res, &error);
        if (profile == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
                    !g_error_matches (error, CD_CLIENT_ERROR, CD_CLIENT_ERROR_ALREADY_EXISTS))
                        g_warning ("%s", error->message);
                g_error_free (error);
                return;
        }
        g_object_unref (profile);
}

static void
gcm_session_icc_store_added_cb (CdIccStore *icc_store,
                                CdIcc *icc,
                                GsdColorProfiles *profiles)
{
        cd_client_create_profile_for_icc (profiles->client,
                                          icc,
                                          CD_OBJECT_SCOPE_TEMP,
                                          profiles->cancellable,
                                          gcm_session_create_profile_cb,
                                          profiles);
}

static void
gcm_session_delete_profile_cb (GObject *object,
                               GAsyncResult *res,
                               gpointer user_data)
{
        gboolean ret;
        GError *error = NULL;
        CdClient *client = CD_CLIENT (object);

        ret = cd_client_delete_profile_finish (client, res, &error);
        if (!ret) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("%s", error->message);
                g_error_free (error);
        }
}

static void
gcm_session_find_profile_by_filename_cb (GObject *object,
                                         GAsyncResult *res,
                                         gpointer user_data)
{
        GError *error = NULL;
        CdProfile *profile;
        CdClient *client = CD_CLIENT (object);
        GsdColorProfiles *profiles = GSD_COLOR_PROFILES (user_data);

        profile = cd_client_find_profile_by_filename_finish (client, res, &error);
        if (profile == NULL) {
                if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                        g_warning ("%s", error->message);
                g_error_free (error);
                goto out;
        }

        /* remove it from colord */
        cd_client_delete_profile (profiles->client,
                                  profile,
                                  profiles->cancellable,
                                  gcm_session_delete_profile_cb,
                                  profiles);
out:
        if (profile != NULL)
                g_object_unref (profile);
}

static void
gcm_session_icc_store_removed_cb (CdIccStore *icc_store,
                                  CdIcc *icc,
                                  GsdColorProfiles *profiles)
{
        /* find the ID for the filename */
        g_debug ("filename %s removed", cd_icc_get_filename (icc));
        cd_client_find_profile_by_filename (profiles->client,
                                            cd_icc_get_filename (icc),
                                            profiles->cancellable,
                                            gcm_session_find_profile_by_filename_cb,
                                            profiles);
}

static void
gsd_color_profiles_init (GsdColorProfiles *profiles)
{
        /* have access to all user profiles */
        profiles->client = cd_client_new ();
        profiles->icc_store = cd_icc_store_new ();
        cd_icc_store_set_load_flags (profiles->icc_store,
                                     CD_ICC_LOAD_FLAGS_FALLBACK_MD5);
        g_signal_connect (profiles->icc_store, "added",
                          G_CALLBACK (gcm_session_icc_store_added_cb),
                          profiles);
        g_signal_connect (profiles->icc_store, "removed",
                          G_CALLBACK (gcm_session_icc_store_removed_cb),
                          profiles);
}

static void
gsd_color_profiles_finalize (GObject *object)
{
        GsdColorProfiles *profiles;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_COLOR_PROFILES (object));

        profiles = GSD_COLOR_PROFILES (object);

        g_cancellable_cancel (profiles->cancellable);
        g_clear_object (&profiles->cancellable);
        g_clear_object (&profiles->icc_store);
        g_clear_object (&profiles->client);

        G_OBJECT_CLASS (gsd_color_profiles_parent_class)->finalize (object);
}

GsdColorProfiles *
gsd_color_profiles_new (void)
{
        GsdColorProfiles *profiles;
        profiles = g_object_new (GSD_TYPE_COLOR_PROFILES, NULL);
        return GSD_COLOR_PROFILES (profiles);
}
