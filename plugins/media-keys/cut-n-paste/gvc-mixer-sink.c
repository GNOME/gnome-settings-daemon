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

#include "gvc-mixer-sink.h"

#define GVC_MIXER_SINK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GVC_TYPE_MIXER_SINK, GvcMixerSinkPrivate))

struct GvcMixerSinkPrivate
{
        gpointer dummy;
};

static void     gvc_mixer_sink_class_init (GvcMixerSinkClass *klass);
static void     gvc_mixer_sink_init       (GvcMixerSink      *mixer_sink);
static void     gvc_mixer_sink_finalize   (GObject            *object);

G_DEFINE_TYPE (GvcMixerSink, gvc_mixer_sink, GVC_TYPE_MIXER_STREAM)

static gboolean
gvc_mixer_sink_change_volume (GvcMixerStream *stream,
                              guint           volume)
{
        pa_operation      *o;
        guint              index;
        GvcChannelMap     *map;
        pa_context        *context;
        pa_cvolume         cv;
        guint              i;
        guint              num_channels;
        gdouble           *gains;

        index = gvc_mixer_stream_get_index (stream);


        map = gvc_mixer_stream_get_channel_map (stream);
        num_channels = gvc_channel_map_get_num_channels (map);
        gains = gvc_channel_map_get_gains (map);

        g_debug ("Changing volume for sink: n=%d vol=%u", num_channels, (guint)volume);

        /* set all values to nominal level */
        pa_cvolume_set (&cv, num_channels, (pa_volume_t)volume);

        /* apply channel gain mapping */
        for (i = 0; i < num_channels; i++) {
                pa_volume_t v;
                v = (double) volume * gains[i];
                g_debug ("Channel %d v=%u", i, v);
                cv.values[i] = v;
        }

        context = gvc_mixer_stream_get_pa_context (stream);

        o = pa_context_set_sink_volume_by_index (context,
                                                 index,
                                                 &cv,
                                                 NULL,
                                                 NULL);

        if (o == NULL) {
                g_warning ("pa_context_set_sink_volume_by_index() failed");
                return FALSE;
        }

        pa_operation_unref(o);

        return TRUE;
}

static gboolean
gvc_mixer_sink_change_is_muted (GvcMixerStream *stream,
                                gboolean        is_muted)
{
        pa_operation *o;
        guint         index;
        pa_context   *context;

        index = gvc_mixer_stream_get_index (stream);
        context = gvc_mixer_stream_get_pa_context (stream);

        o = pa_context_set_sink_mute_by_index (context,
                                               index,
                                               is_muted,
                                               NULL,
                                               NULL);

        if (o == NULL) {
                g_warning ("pa_context_set_sink_mute_by_index() failed");
                return FALSE;
        }

        pa_operation_unref(o);

        return TRUE;
}

static GObject *
gvc_mixer_sink_constructor (GType                  type,
                            guint                  n_construct_properties,
                            GObjectConstructParam *construct_params)
{
        GObject       *object;
        GvcMixerSink *self;

        object = G_OBJECT_CLASS (gvc_mixer_sink_parent_class)->constructor (type, n_construct_properties, construct_params);

        self = GVC_MIXER_SINK (object);

        return object;
}

static void
gvc_mixer_sink_class_init (GvcMixerSinkClass *klass)
{
        GObjectClass        *object_class = G_OBJECT_CLASS (klass);
        GvcMixerStreamClass *stream_class = GVC_MIXER_STREAM_CLASS (klass);

        object_class->constructor = gvc_mixer_sink_constructor;
        object_class->finalize = gvc_mixer_sink_finalize;

        stream_class->change_volume = gvc_mixer_sink_change_volume;
        stream_class->change_is_muted = gvc_mixer_sink_change_is_muted;

        g_type_class_add_private (klass, sizeof (GvcMixerSinkPrivate));
}

static void
gvc_mixer_sink_init (GvcMixerSink *sink)
{
        sink->priv = GVC_MIXER_SINK_GET_PRIVATE (sink);

}

static void
gvc_mixer_sink_finalize (GObject *object)
{
        GvcMixerSink *mixer_sink;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GVC_IS_MIXER_SINK (object));

        mixer_sink = GVC_MIXER_SINK (object);

        g_return_if_fail (mixer_sink->priv != NULL);
        G_OBJECT_CLASS (gvc_mixer_sink_parent_class)->finalize (object);
}

GvcMixerStream *
gvc_mixer_sink_new (pa_context    *context,
                    guint          index,
                    GvcChannelMap *channel_map)

{
        GObject *object;

        object = g_object_new (GVC_TYPE_MIXER_SINK,
                               "pa-context", context,
                               "index", index,
                               "channel-map", channel_map,
                               NULL);

        return GVC_MIXER_STREAM (object);
}
