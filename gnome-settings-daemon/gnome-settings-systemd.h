/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Red Hat
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
 */

#ifndef __GNOME_SETTINGS_SYSTEMD_H
#define __GNOME_SETTINGS_SYSTEMD_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

gboolean gnome_settings_have_systemd (void);

void gnome_settings_systemd_manage_unit (GDBusConnection     *connection,
                                         const char          *unit,
                                         gboolean             running,
                                         gboolean             change_enabled,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data);

gboolean gnome_settings_systemd_manage_unit_finish (GDBusConnection  *connection,
                                                    GAsyncResult     *result,
                                                    GError          **error);

G_END_DECLS

#endif /* __GNOME_SETTINGS_SYSTEMD_H */
