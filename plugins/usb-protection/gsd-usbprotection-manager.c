/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Ludovico de Nittis <denittis@gnome.org>
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

#include <string.h>
#include <locale.h>

#include <glib-object.h>

#include "gnome-settings-profile.h"
#include "gsd-usbprotection-manager.h"

#define GSD_USBPROTECTION_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSD_TYPE_USBPROTECTION_MANAGER, GsdUSBProtectionManagerPrivate))

struct GsdUSBProtectionManagerPrivate
{
        gboolean padding;
};

enum {
        PROP_0,
};

static void gsd_usbprotection_manager_class_init (GsdUSBProtectionManagerClass *klass);
static void gsd_usbprotection_manager_init       (GsdUSBProtectionManager      *usbprotection_manager);
static void gsd_usbprotection_manager_finalize   (GObject                      *object);

G_DEFINE_TYPE (GsdUSBProtectionManager, gsd_usbprotection_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

gboolean
gsd_usbprotection_manager_start (GsdUSBProtectionManager *manager,
                                 GError                 **error)
{
        g_debug ("Starting usbprotection manager");
        gnome_settings_profile_start (NULL);
        gnome_settings_profile_end (NULL);
        return TRUE;
}

void
gsd_usbprotection_manager_stop (GsdUSBProtectionManager *manager)
{
        g_debug ("Stopping usbprotection manager");
}

static void
gsd_usbprotection_manager_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_usbprotection_manager_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gsd_usbprotection_manager_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
        GsdUSBProtectionManager *usbprotection_manager;

        usbprotection_manager = GSD_USBPROTECTION_MANAGER (G_OBJECT_CLASS (gsd_usbprotection_manager_parent_class)->constructor (type,
                                                                                                                                 n_construct_properties,
                                                                                                                                 construct_properties));

        return G_OBJECT (usbprotection_manager);
}

static void
gsd_usbprotection_manager_dispose (GObject *object)
{
        G_OBJECT_CLASS (gsd_usbprotection_manager_parent_class)->dispose (object);
}

static void
gsd_usbprotection_manager_class_init (GsdUSBProtectionManagerClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_usbprotection_manager_get_property;
        object_class->set_property = gsd_usbprotection_manager_set_property;
        object_class->constructor = gsd_usbprotection_manager_constructor;
        object_class->dispose = gsd_usbprotection_manager_dispose;
        object_class->finalize = gsd_usbprotection_manager_finalize;

        g_type_class_add_private (klass, sizeof (GsdUSBProtectionManagerPrivate));
}

static void
gsd_usbprotection_manager_init (GsdUSBProtectionManager *manager)
{
        manager->priv = GSD_USBPROTECTION_MANAGER_GET_PRIVATE (manager);

}

static void
gsd_usbprotection_manager_finalize (GObject *object)
{
        GsdUSBProtectionManager *usbprotection_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GSD_IS_USBPROTECTION_MANAGER (object));

        usbprotection_manager = GSD_USBPROTECTION_MANAGER (object);

        g_return_if_fail (usbprotection_manager->priv != NULL);

        G_OBJECT_CLASS (gsd_usbprotection_manager_parent_class)->finalize (object);
}

GsdUSBProtectionManager *
gsd_usbprotection_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GSD_TYPE_USBPROTECTION_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GSD_USBPROTECTION_MANAGER (manager_object);
}
