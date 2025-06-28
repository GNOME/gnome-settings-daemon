/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Red Hat Inc.
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
 * Author: Carlos Garnacho <carlosg@gnome.org>
 */

#include "gsd-application.h"

typedef struct _GsdApplicationPrivate GsdApplicationPrivate;
struct _GsdApplicationPrivate
{
        GSettings *sound_settings;
        ca_context *ca;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsdApplication, gsd_application, G_TYPE_APPLICATION)

static void
gsd_application_finalize (GObject *object)
{
        GsdApplication *app = GSD_APPLICATION (object);
        GsdApplicationPrivate *priv =
                gsd_application_get_instance_private (app);

        g_clear_object (&priv->sound_settings);
        g_clear_pointer (&priv->ca, ca_context_destroy);

        G_OBJECT_CLASS (gsd_application_parent_class)->finalize (object);
}

static void
gsd_application_real_pre_shutdown (GsdApplication *app)
{
	g_application_release (G_APPLICATION (app));
}

static void
gsd_application_class_init (GsdApplicationClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = gsd_application_finalize;

	klass->pre_shutdown = gsd_application_real_pre_shutdown;
}

static void
gsd_application_init (GsdApplication *app)
{
}

void
gsd_application_pre_shutdown (GsdApplication *app)
{
	GSD_APPLICATION_GET_CLASS (app)->pre_shutdown (app);
}

static void
sound_theme_changed (GsdApplication *app)
{
        GsdApplicationPrivate *priv =
                gsd_application_get_instance_private (app);
        g_autofree char *sound_theme;

        sound_theme = g_settings_get_string (priv->sound_settings, "theme-name");

        if (priv->ca) {
                ca_context_change_props (priv->ca,
                                         CA_PROP_CANBERRA_XDG_THEME_NAME, sound_theme,
                                         NULL);
        }
}

ca_context *
gsd_application_get_ca_context (GsdApplication *app)
{
        GsdApplicationPrivate *priv =
                gsd_application_get_instance_private (app);

        if (!priv->sound_settings) {
                priv->sound_settings = g_settings_new ("org.gnome.desktop.sound");
                g_signal_connect_swapped (priv->sound_settings,
                                          "changed::theme-name",
                                          G_CALLBACK (sound_theme_changed),
                                          app);
        }

        if (!priv->ca) {
                ca_context_create (&priv->ca);
                ca_context_set_driver (priv->ca, "pulse");
                ca_context_change_props (priv->ca, 0,
                                         CA_PROP_APPLICATION_ID,
                                         "org.gnome.VolumeControl",
                                         NULL);
                sound_theme_changed (app);
        }

        return priv->ca;
}
