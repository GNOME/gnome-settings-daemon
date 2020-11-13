/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Ray Strode
 */

#ifndef __GSD_SMARTCARD_SERVICE_H__
#define __GSD_SMARTCARD_SERVICE_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include "gsd-smartcard-manager.h"

#include "org.gnome.SettingsDaemon.Smartcard.h"

G_BEGIN_DECLS

#define GSD_TYPE_SMARTCARD_SERVICE (gsd_smartcard_service_get_type ())

G_DECLARE_FINAL_TYPE (GsdSmartcardService, gsd_smartcard_service, GSD, SMARTCARD_SERVICE, GsdSmartcardServiceManagerSkeleton)

void  gsd_smartcard_service_new_async (GsdSmartcardManager  *manager,
                                       GCancellable         *cancellable,
                                       GAsyncReadyCallback   callback,
                                       gpointer              user_data);
GsdSmartcardService *gsd_smartcard_service_new_finish (GAsyncResult         *result,
                                                       GError              **error);

void  gsd_smartcard_service_register_driver (GsdSmartcardService *service,
                                             GckModule           *module);
void  gsd_smartcard_service_sync_token (GsdSmartcardService  *service,
                                        GckSlot              *card_slot,
                                        GCancellable         *cancellable);


G_END_DECLS

#endif /* __GSD_SMARTCARD_SERVICE_H__ */
