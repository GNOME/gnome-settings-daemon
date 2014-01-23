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

#include <prerror.h>
#include <prinit.h>
#include <nss.h>
#include <pk11func.h>
#include <secmod.h>
#include <secerr.h>

G_BEGIN_DECLS

#define GSD_TYPE_SMARTCARD_SERVICE (gsd_smartcard_service_get_type ())
#define GSD_SMARTCARD_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST (obj, GSD_TYPE_SMARTCARD_SERVICE, GsdSmartcardService))
#define GSD_SMARTCARD_SERVICE_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST (cls, GSD_TYPE_SMARTCARD_SERVICE, GsdSmartcardServiceClass))
#define GSD_IS_SMARTCARD_SERVICE(obj) (G_TYPE_CHECK_INSTANCE_TYPE (obj, GSD_TYPE_SMARTCARD_SERVICE))
#define GSD_IS_SMARTCARD_SERVICE_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE (obj, GSD_TYPE_SMARTCARD_SERVICE))
#define GSD_SMARTCARD_SERVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GSD_TYPE_SMARTCARD_SERVICE, GsdSmartcardServiceClass))

typedef struct _GsdSmartcardService GsdSmartcardService;
typedef struct _GsdSmartcardServiceClass GsdSmartcardServiceClass;
typedef struct _GsdSmartcardServicePrivate GsdSmartcardServicePrivate;

struct _GsdSmartcardService
{
        GsdSmartcardServiceManagerSkeleton parent_instance;
        GsdSmartcardServicePrivate *priv;
};

struct _GsdSmartcardServiceClass
{
        GsdSmartcardServiceManagerSkeletonClass parent_class;
};

GType gsd_smartcard_service_get_type (void);
void  gsd_smartcard_service_new_async (GsdSmartcardManager  *manager,
                                       GCancellable         *cancellable,
                                       GAsyncReadyCallback   callback,
                                       gpointer              user_data);
GsdSmartcardService *gsd_smartcard_service_new_finish (GAsyncResult         *result,
                                                       GError              **error);

void  gsd_smartcard_service_register_driver (GsdSmartcardService  *service,
                                             SECMODModule         *driver);
void  gsd_smartcard_service_sync_token (GsdSmartcardService  *service,
                                        PK11SlotInfo         *slot_info,
                                        GCancellable         *cancellable);


G_END_DECLS

#endif /* __GSD_SMARTCARD_SERVICE_H__ */
