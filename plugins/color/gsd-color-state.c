/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 NVIDIA CORPORATION
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

#include "config.h"

#include "gsd-color-manager.h"
#include "gsd-color-state.h"

struct _GsdColorState
{
        GObject          parent;

        guint            color_temperature;
};

static void     gsd_color_state_class_init  (GsdColorStateClass *klass);
static void     gsd_color_state_init        (GsdColorState      *color_state);

G_DEFINE_TYPE (GsdColorState, gsd_color_state, G_TYPE_OBJECT)

void
gsd_color_state_set_temperature (GsdColorState *state, guint temperature)
{
        g_return_if_fail (GSD_IS_COLOR_STATE (state));

        state->color_temperature = temperature;
}

guint
gsd_color_state_get_temperature (GsdColorState *state)
{
        g_return_val_if_fail (GSD_IS_COLOR_STATE (state), 0);
        return state->color_temperature;
}

void
gsd_color_state_start (GsdColorState *state)
{
}

void
gsd_color_state_stop (GsdColorState *state)
{
}

static void
gsd_color_state_class_init (GsdColorStateClass *klass)
{
}

static void
gsd_color_state_init (GsdColorState *state)
{
        /* default color temperature */
        state->color_temperature = GSD_COLOR_TEMPERATURE_DEFAULT;
}

GsdColorState *
gsd_color_state_new (void)
{
        GsdColorState *state;
        state = g_object_new (GSD_TYPE_COLOR_STATE, NULL);
        return GSD_COLOR_STATE (state);
}
