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

#ifndef __GSD_USBPROTECTION_MANAGER_H
#define __GSD_USBPROTECTION_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_USBPROTECTION_MANAGER         (gsd_usbprotection_manager_get_type ())
#define GSD_USBPROTECTION_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GSD_TYPE_USBPROTECTION_MANAGER, GsdUSBProtectionManager))
#define GSD_USBPROTECTION_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GSD_TYPE_USBPROTECTION_MANAGER, GsdUSBProtectionManagerClass))
#define GSD_IS_USBPROTECTION_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GSD_TYPE_USBPROTECTION_MANAGER))
#define GSD_IS_USBPROTECTION_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GSD_TYPE_USBPROTECTION_MANAGER))
#define GSD_USBPROTECTION_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GSD_TYPE_USBPROTECTION_MANAGER, GsdUSBProtectionManagerClass))

typedef struct GsdUSBProtectionManagerPrivate GsdUSBProtectionManagerPrivate;

typedef struct
{
        GObject                         parent;
        GsdUSBProtectionManagerPrivate *priv;
} GsdUSBProtectionManager;

typedef struct
{
        GObjectClass   parent_class;
} GsdUSBProtectionManagerClass;

GType                       gsd_usbprotection_manager_get_type        (void);

GsdUSBProtectionManager *   gsd_usbprotection_manager_new             (void);
gboolean                    gsd_usbprotection_manager_start           (GsdUSBProtectionManager *manager,
                                                                       GError                 **error);
void                        gsd_usbprotection_manager_stop            (GsdUSBProtectionManager *manager);

G_END_DECLS

#endif /* __GSD_USBPROTECTION_MANAGER_H */
