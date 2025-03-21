/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2010 Red Hat, Inc.
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

#ifndef __GSD_SMARTCARD_MANAGER_H
#define __GSD_SMARTCARD_MANAGER_H

#include <gck/gck.h>

#include "gsd-application.h"

G_BEGIN_DECLS

#define GSD_TYPE_SMARTCARD_MANAGER         (gsd_smartcard_manager_get_type ())
#define GSD_SMARTCARD_MANAGER_ERROR        (gsd_smartcard_manager_error_quark ())

G_DECLARE_FINAL_TYPE (GsdSmartcardManager, gsd_smartcard_manager, GSD, SMARTCARD_MANAGER, GsdApplication)

typedef enum
{
         GSD_SMARTCARD_MANAGER_ERROR_GENERIC = 0,
         GSD_SMARTCARD_MANAGER_ERROR_WITH_P11KIT,
         GSD_SMARTCARD_MANAGER_ERROR_LOADING_DRIVER,
         GSD_SMARTCARD_MANAGER_ERROR_WATCHING_FOR_EVENTS,
         GSD_SMARTCARD_MANAGER_ERROR_REPORTING_EVENTS,
         GSD_SMARTCARD_MANAGER_ERROR_FINDING_SMARTCARD,
         GSD_SMARTCARD_MANAGER_ERROR_NO_DRIVERS,
} GsdSmartcardManagerError;

GQuark                  gsd_smartcard_manager_error_quark (void);


GckSlot *               gsd_smartcard_manager_get_login_token (GsdSmartcardManager *manager);
GList *                 gsd_smartcard_manager_get_inserted_tokens (GsdSmartcardManager *manager,
                                                                   gsize               *num_tokens);
void                    gsd_smartcard_manager_do_remove_action (GsdSmartcardManager *manager);

G_END_DECLS

#endif /* __GSD_SMARTCARD_MANAGER_H */
