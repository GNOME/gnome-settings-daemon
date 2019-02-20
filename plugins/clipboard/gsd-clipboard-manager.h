/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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

#ifndef __GSD_CLIPBOARD_MANAGER_H
#define __GSD_CLIPBOARD_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GSD_TYPE_CLIPBOARD_MANAGER         (gsd_clipboard_manager_get_type ())
G_DECLARE_FINAL_TYPE (GsdClipboardManager, gsd_clipboard_manager, GSD, CLIPBOARD_MANAGER, GObject)

GsdClipboardManager *       gsd_clipboard_manager_new                 (void);
gboolean                gsd_clipboard_manager_start               (GsdClipboardManager *manager,
                                                               GError         **error);
void                    gsd_clipboard_manager_stop                (GsdClipboardManager *manager);

G_END_DECLS

#endif /* __GSD_CLIPBOARD_MANAGER_H */
