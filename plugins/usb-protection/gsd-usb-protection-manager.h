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

#ifndef __GSD_USB_PROTECTION_MANAGER_H
#define __GSD_USB_PROTECTION_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_USB_PROTECTION_MANAGER         (gsd_usb_protection_manager_get_type ())
G_DECLARE_FINAL_TYPE (GsdUsbProtectionManager, gsd_usb_protection_manager, GSD, USB_PROTECTION_MANAGER, GObject);

typedef struct
{
        GObjectClass   parent_class;
} _GsdUsbProtectionManagerClass;

GType                       gsd_usb_protection_manager_get_type        (void);

GsdUsbProtectionManager *   gsd_usb_protection_manager_new             (void);
gboolean                    gsd_usb_protection_manager_start           (GsdUsbProtectionManager *manager,
                                                                        GError                 **error);
void                        gsd_usb_protection_manager_stop            (GsdUsbProtectionManager *manager);

G_END_DECLS

#endif /* __GSD_USB_PROTECTION_MANAGER_H */
