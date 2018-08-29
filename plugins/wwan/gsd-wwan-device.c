/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Purism SPC
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
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 */

#include "config.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <libmm-glib.h>

#include "gsd-wwan-device.h"


struct _GsdWwanDevice
{
        GObject parent;

        MMModem *mm_modem;
        MMSim *mm_sim;
        MMObject *mm_object;
};


enum {
        PROP_0,
        PROP_MM_OBJECT,
        PROP_MM_MODEM,
        PROP_MM_SIM,
        PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
	SIM_NEEDS_UNLOCK,
	N_SIGNALS
};
static guint signals[N_SIGNALS];

G_DEFINE_TYPE (GsdWwanDevice, gsd_wwan_device, G_TYPE_OBJECT)


static void
modem_get_sim_ready (MMModem *modem, GAsyncResult *res, GsdWwanDevice *self)
{
        self->mm_sim = mm_modem_get_sim_finish (modem, res, NULL);
        g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MM_SIM]);
        g_return_if_fail (MM_IS_SIM (self->mm_sim));

        g_debug ("Need to unlock sim %s (%s)",
                 mm_sim_get_path (self->mm_sim),
                 mm_sim_get_identifier (self->mm_sim));
        g_signal_emit(self, signals[SIM_NEEDS_UNLOCK], 0);
}


static void
fetch_modem_info (GsdWwanDevice *self)
{
        self->mm_modem = mm_object_get_modem (MM_OBJECT(self->mm_object));
        g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MM_MODEM]);
        g_return_if_fail (self->mm_modem);

        g_debug ("Found modem %s (%s)",
                 mm_modem_get_path (self->mm_modem),
                 mm_modem_get_device (self->mm_modem));

        if (mm_modem_get_state (self->mm_modem) != MM_MODEM_STATE_LOCKED)
                return;

        /* The sim card will be valid as long as the modem exists */
        mm_modem_get_sim (self->mm_modem, NULL, (GAsyncReadyCallback)modem_get_sim_ready, self);
}


static void
gsd_wwan_device_set_property (GObject        *object,
                               guint           prop_id,
                               const GValue   *value,
                               GParamSpec     *pspec)
{
        GsdWwanDevice *self = GSD_WWAN_DEVICE (object);

        switch (prop_id) {
        case PROP_MM_OBJECT:
                self->mm_object = g_value_dup_object (value);
                fetch_modem_info (self);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_wwan_device_get_property (GObject        *object,
                               guint           prop_id,
                               GValue         *value,
                               GParamSpec     *pspec)
{
        GsdWwanDevice *self = GSD_WWAN_DEVICE (object);

        switch (prop_id) {
        case PROP_MM_OBJECT:
                g_value_set_object (value, self->mm_object);
                break;
        case PROP_MM_MODEM:
                g_value_set_object (value, self->mm_modem);
                break;
        case PROP_MM_SIM:
                g_value_set_object (value, self->mm_sim);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gsd_wwan_device_dispose (GObject *object)
{
        GsdWwanDevice *self = GSD_WWAN_DEVICE (object);

        g_clear_object (&self->mm_modem);
        g_clear_object (&self->mm_sim);
        g_clear_object (&self->mm_object);

        G_OBJECT_CLASS (gsd_wwan_device_parent_class)->dispose (object);
}

static void
gsd_wwan_device_class_init (GsdWwanDeviceClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gsd_wwan_device_get_property;
        object_class->set_property = gsd_wwan_device_set_property;
        object_class->dispose = gsd_wwan_device_dispose;

        signals[SIM_NEEDS_UNLOCK] =
                g_signal_new ("sim-needs-unlock",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST, 0,
                              NULL, NULL, NULL,
                              G_TYPE_NONE, 0);

        props[PROP_MM_OBJECT] =
                g_param_spec_object ("mm-object",
                                     "mm-object",
                                     "The MMObject representing a modem",
                                     MM_TYPE_OBJECT,
                                     G_PARAM_READWRITE |
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_STATIC_STRINGS);
        props[PROP_MM_MODEM] =
                g_param_spec_object ("mm-modem",
                                     "mm-modem",
                                     "The MMModem interface object",
                                     MM_TYPE_MODEM,
                                     G_PARAM_READABLE |
                                     G_PARAM_EXPLICIT_NOTIFY |
                                     G_PARAM_STATIC_STRINGS);
        props[PROP_MM_SIM] =
                g_param_spec_object ("mm-sim",
                                     "mm-sim",
                                     "The MMSim interface object",
                                     MM_TYPE_SIM,
                                     G_PARAM_READABLE |
                                     G_PARAM_EXPLICIT_NOTIFY |
                                     G_PARAM_STATIC_STRINGS);
        g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

static void
gsd_wwan_device_init (GsdWwanDevice *self)
{
}

GsdWwanDevice *
gsd_wwan_device_new (MMObject *object)
{
        return g_object_new (GSD_TYPE_WWAN_DEVICE, "mm-object", object, NULL);
}


MMObject *
gsd_wwan_device_get_mm_object (GsdWwanDevice *self)
{
        g_return_val_if_fail (GSD_IS_WWAN_DEVICE (self), NULL);

        return self->mm_object;
}


MMModem *
gsd_wwan_device_get_mm_modem (GsdWwanDevice *self)
{
        g_return_val_if_fail (GSD_IS_WWAN_DEVICE (self), NULL);

        return self->mm_modem;
}


MMSim *
gsd_wwan_device_get_mm_sim (GsdWwanDevice *self)
{
        g_return_val_if_fail (GSD_IS_WWAN_DEVICE (self), NULL);

        return self->mm_sim;
}

gboolean
gsd_wwan_device_needs_unlock (GsdWwanDevice *self)
{
        g_return_val_if_fail (GSD_IS_WWAN_DEVICE (self), FALSE);

        return (mm_modem_get_state (self->mm_modem) == MM_MODEM_STATE_LOCKED &&
                self->mm_sim);
}
