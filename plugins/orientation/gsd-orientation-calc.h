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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __GSD_ORIENTATION_CALC_H
#define __GSD_ORIENTATION_CALC_H

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	ORIENTATION_UNDEFINED,
	ORIENTATION_NORMAL,
	ORIENTATION_BOTTOM_UP,
	ORIENTATION_LEFT_UP,
	ORIENTATION_RIGHT_UP
} OrientationUp;

#define ORIENTATION_UP_UP ORIENTATION_NORMAL

const char *  gsd_orientation_to_string (OrientationUp orientation);
OrientationUp gsd_orientation_calc      (OrientationUp prev,
					 int x, int y, int z);

G_END_DECLS

#endif /* __GSD_ORIENTATION_CALC_H */
