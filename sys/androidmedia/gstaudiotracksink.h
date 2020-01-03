/* GStreamer
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __AUDIOTRACKSINK_H__
#define __AUDIOTRACKSINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/androidjni/gstjniaudiotrack.h>

G_BEGIN_DECLS
#define GST_TYPE_AUDIOTRACK_SINK \
  (gst_audiotrack_sink_get_type())
#define GST_AUDIOTRACK_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOTRACK_SINK,GstAudioTrackSink))
#define GST_AUDIOTRACK_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOTRACK_SINK,GstAudioTrackSinkClass))
typedef struct _GstAudioTrackSink GstAudioTrackSink;
typedef struct _GstAudioTrackSinkClass GstAudioTrackSinkClass;

struct _GstAudioTrackSink
{
  GstBaseSink sink;

  /* Internal */
  GstJniAudioTrack *audio_track;
  gboolean needs_start;

  /* Properties */
  gfloat volume;
  gboolean mute;
  gint audio_session_id;

  /* Stream caps */
  gint rate;
  gint channels;
  gint width;
  gint64 latency;
};

struct _GstAudioTrackSinkClass
{
  GstBaseSinkClass parent_class;
};

GType gst_audiotrack_sink_get_type (void);

G_END_DECLS
#endif /* __AUDIOTRACKSINK_H__ */
