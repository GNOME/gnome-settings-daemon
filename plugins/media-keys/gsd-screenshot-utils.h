/* gsd-screenshot-utils.h - utilities to take screenshots
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Adapted from gnome-screenshot code, which is
 *   Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu>
 *   Copyright (C) 2006 Emmanuele Bassi <ebassi@gnome.org>
 *   Copyright (C) 2008-2012 Cosimo Cecchi <cosimoc@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 */

#ifndef __GSD_SCREENSHOT_UTILS_H__
#define __GSD_SCREENSHOT_UTILS_H__

#include "media-keys.h"

G_BEGIN_DECLS

void gsd_screenshot_take (MediaKeyType key_type);

G_END_DECLS

#endif /* __GSD_SCREENSHOT_UTILS_H__ */
