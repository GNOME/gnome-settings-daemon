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

# pragma once

#include <glib-object.h>
#include <libmm-glib.h>

G_BEGIN_DECLS

#define GSD_TYPE_WWAN_DEVICE (gsd_wwan_device_get_type())
G_DECLARE_FINAL_TYPE (GsdWwanDevice, gsd_wwan_device, GSD, WWAN_DEVICE, GObject)

GsdWwanDevice  *gsd_wwan_device_new           (MMObject *object);
MMObject *gsd_wwan_device_get_mm_object (GsdWwanDevice *self);
MMModem  *gsd_wwan_device_get_mm_modem  (GsdWwanDevice *self);
MMSim    *gsd_wwan_device_get_mm_sim    (GsdWwanDevice *self);
gboolean  gsd_wwan_device_needs_unlock  (GsdWwanDevice *self);

G_END_DECLS
