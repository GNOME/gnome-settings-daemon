/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <pulse/pulseaudio.h>

#include "gvc-channel-map.h"

#define GVC_CHANNEL_MAP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GVC_TYPE_CHANNEL_MAP, GvcChannelMapPrivate))

struct GvcChannelMapPrivate
{
        guint                 num_channels;
        pa_channel_position_t positions[PA_CHANNELS_MAX];
        gdouble               gains[PA_CHANNELS_MAX];
};

enum {
        GAINS_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gvc_channel_map_class_init (GvcChannelMapClass *klass);
static void     gvc_channel_map_init       (GvcChannelMap      *channel_map);
static void     gvc_channel_map_finalize   (GObject            *object);

G_DEFINE_TYPE (GvcChannelMap, gvc_channel_map, G_TYPE_OBJECT)

guint
gvc_channel_map_get_num_channels (GvcChannelMap *map)
{
        g_return_val_if_fail (GVC_IS_CHANNEL_MAP (map), 0);
        return map->priv->num_channels;
}

gdouble *
gvc_channel_map_get_gains (GvcChannelMap *map)
{
        g_return_val_if_fail (GVC_IS_CHANNEL_MAP (map), NULL);
        return map->priv->gains;
}

pa_channel_position_t *
gvc_channel_map_get_positions (GvcChannelMap *map)
{
        g_return_val_if_fail (GVC_IS_CHANNEL_MAP (map), NULL);
        return map->priv->positions;
}

static void
gvc_channel_map_class_init (GvcChannelMapClass *klass)
{
        GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->finalize = gvc_channel_map_finalize;

        signals [GAINS_CHANGED] =
                g_signal_new ("gains-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GvcChannelMapClass, gains_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        g_type_class_add_private (klass, sizeof (GvcChannelMapPrivate));
}

void
gvc_channel_map_gains_changed (GvcChannelMap *map)
{
        g_return_if_fail (GVC_IS_CHANNEL_MAP (map));
        g_signal_emit (map, signals[GAINS_CHANGED], 0);
}

static void
gvc_channel_map_init (GvcChannelMap *map)
{
        map->priv = GVC_CHANNEL_MAP_GET_PRIVATE (map);
}

static void
gvc_channel_map_finalize (GObject *object)
{
        GvcChannelMap *channel_map;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GVC_IS_CHANNEL_MAP (object));

        channel_map = GVC_CHANNEL_MAP (object);

        g_return_if_fail (channel_map->priv != NULL);

        G_OBJECT_CLASS (gvc_channel_map_parent_class)->finalize (object);
}

GvcChannelMap *
gvc_channel_map_new (void)
{
        GObject *map;
        map = g_object_new (GVC_TYPE_CHANNEL_MAP, NULL);
        return GVC_CHANNEL_MAP (map);
}

static void
set_from_pa_map (GvcChannelMap        *map,
                 const pa_channel_map *pa_map)
{
        guint i;

        map->priv->num_channels = pa_map->channels;
        for (i = 0; i < pa_map->channels; i++) {
                map->priv->positions[i] = pa_map->map[i];
                map->priv->gains[i] = 1.0;
        }
}

GvcChannelMap *
gvc_channel_map_new_from_pa_channel_map (const pa_channel_map *pa_map)
{
        GObject *map;
        map = g_object_new (GVC_TYPE_CHANNEL_MAP, NULL);

        set_from_pa_map (GVC_CHANNEL_MAP (map), pa_map);

        return GVC_CHANNEL_MAP (map);
}
