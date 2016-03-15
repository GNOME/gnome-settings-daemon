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
#include <gdk/gdk.h>

#include "gnome-settings-plugin.h"
#include "gnome-settings-profile.h"
#include "gsd-color-calibrate.h"
#include "gsd-color-manager.h"
#include "gsd-color-profiles.h"
#include "gsd-color-state.h"

#define GSD_COLOR_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_COLOR_MANAGER, GsdColorManagerPrivate))

struct GsdColorManagerPrivate
{
        GsdColorCalibrate *calibrate;
        GsdColorProfiles  *profiles;
        GsdColorState     *state;
};

enum {
        PROP_0,
};

static void     gsd_color_manager_class_init  (GsdColorManagerClass *klass);
static void     gsd_color_manager_init        (GsdColorManager      *color_manager);
static void     gsd_color_manager_finalize    (GObject             *object);

G_DEFINE_TYPE (GsdColorManager, gsd_color_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

GQuark
gsd_color_manager_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("gsd_color_manager_error");
        return quark;
}

gboolean
gsd_color_manager_start (GsdColorManager *manager,
                         GError          **error)
{
        GsdColorManagerPrivate *priv = manager->priv;
        gboolean ret;

        g_debug ("Starting color manager");
        gnome_settings_profile_start (NULL);

        /* start the device probing */
        gsd_color_state_start (priv->state);

        /* start the profiles collection */
        ret = gsd_color_profiles_start (priv->profiles, error);
        if (!ret)
                goto out;
out:
        gnome_settings_profile_end (NULL);
        return ret;
}

void
gsd_color_manager_stop (GsdColorManager *manager)
{
        GsdColorManagerPrivate *priv = manager->priv;
        g_debug ("Stopping color manager");
        gsd_color_state_stop (priv->state);
        gsd_color_profiles_stop (priv->profiles);
}

static void
gsd_color_manager_class_init (GsdColorManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_color_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdColorManagerPrivate));
}

static void
gsd_color_manager_init (GsdColorManager *manager)
{
        GsdColorManagerPrivate *priv;
        priv = manager->priv = GSD_COLOR_MANAGER_GET_PRIVATE (manager);

        /* setup calibration features */
        priv->calibrate = gsd_color_calibrate_new ();
        priv->profiles = gsd_color_profiles_new ();
        priv->state = gsd_color_state_new ();
}

static void
gsd_color_manager_finalize (GObject *object)
{
        GsdColorManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_COLOR_MANAGER (object));

        manager = GSD_COLOR_MANAGER (object);

        gsd_color_manager_stop (manager);

        g_clear_object (&manager->priv->calibrate);
        g_clear_object (&manager->priv->profiles);
        g_clear_object (&manager->priv->state);

        G_OBJECT_CLASS (gsd_color_manager_parent_class)->finalize (object);
}

GsdColorManager *
gsd_color_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_COLOR_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_COLOR_MANAGER (manager_object);
}
