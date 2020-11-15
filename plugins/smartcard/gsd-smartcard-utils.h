/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Red Hat, Inc.
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

#ifndef __GSD_SMARTCARD_UTILS_H
#define __GSD_SMARTCARD_UTILS_H

#include <glib-object.h>
#include <gck/gck.h>
#include <gio/gio.h>

G_BEGIN_DECLS
void             gsd_smartcard_utils_register_error_domain             (GQuark error_domain,
                                                                        GType error_enum);
char *           gsd_smartcard_utils_escape_object_path                (const char *unescaped_string);

const char *     gsd_smartcard_utils_get_login_token_name              (void);

G_END_DECLS

#endif /* __GSD_SMARTCARD_MANAGER_H */
