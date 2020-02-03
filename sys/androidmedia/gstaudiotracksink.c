/* GStreamer
 * Copyright (C) 2018 Fluendo S.A. <support@fluendo.com>
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

/**
 * SECTION:element-audiotracksink
 *
 * This element renders raw audio samples using the AudioTrack API in Android OS.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=music.ogg ! oggdemux ! vorbisdec ! audioconvert ! audioresample ! audiotracksink
 * ]| Play an Ogg/Vorbis file.
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/androidjni/gstjniutils.h>
#include "gstaudiotracksink.h"


GST_DEBUG_CATEGORY_STATIC (audiotrack_sink_debug);
#define GST_CAT_DEFAULT audiotrack_sink_debug

enum
{
  PROP_0,
  PROP_VOLUME,
  PROP_MUTE,
  PROP_AUDIO_SESSION_ID,
  PROP_LAST
};

#define DEFAULT_VOLUME 1.0
#define DEFAULT_MUTE   FALSE
#define BUFFER_SIZE_FACTOR 3

/* According to Android's NDK doc the following are the supported rates */
#define RATES "8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [8000, 48000], "
        "channels = (int) [1, 2];"
        "audio/x-raw-int, "
        "endianness = (int) {" G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [8000, 48000], " "channels = (int) [1, 2]")
    );

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (audiotrack_sink_debug, "audiotracksink", 0,
      "Audio Track Sink");
}

GST_BOILERPLATE_FULL (GstAudioTrackSink, gst_audiotrack_sink, GstBaseSink,
    GST_TYPE_BASE_SINK, _do_init);


static gboolean
gst_audio_track_sink_query (GstElement * element, GstQuery * query)
{
  gboolean res = FALSE;
  GstBaseSink *basesink;
  GstAudioTrackSink *sink;

  basesink = GST_BASE_SINK (element);
  sink = GST_AUDIOTRACK_SINK (element);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean live, us_live;
      GstClockTime min_l, max_l;

      GST_DEBUG_OBJECT (sink, "handling latency query");

      /* ask parent first, it will do an upstream query for us. */
      if ((res =
              gst_base_sink_query_latency (GST_BASE_SINK_CAST (basesink), &live,
                  &us_live, &min_l, &max_l))) {
        GstClockTime min_latency, max_latency;

        /* we and upstream are both live, adjust the min_latency */
        if (live && us_live) {
          GST_OBJECT_LOCK (sink);
          if (sink->audio_track == NULL) {
            GST_OBJECT_UNLOCK (sink);

            GST_DEBUG_OBJECT (sink,
                "we are not yet negotiated, can't report latency yet");
            res = FALSE;
            goto done;
          }
          GST_OBJECT_UNLOCK (sink);
          /* we cannot go lower than the buffer size and the min peer latency */
          min_latency = sink->latency + min_l;
          /* the max latency is the max of the peer, we can delay an infinite
           * amount of time. */
          max_latency = (max_l == -1) ? -1 : (sink->latency + max_l);

          GST_DEBUG_OBJECT (basesink,
              "peer min %" GST_TIME_FORMAT ", our min latency: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (min_l),
              GST_TIME_ARGS (min_latency));
          GST_DEBUG_OBJECT (basesink,
              "peer max %" GST_TIME_FORMAT ", our max latency: %"
              GST_TIME_FORMAT, GST_TIME_ARGS (max_l),
              GST_TIME_ARGS (max_latency));
        } else {
          GST_DEBUG_OBJECT (basesink,
              "peer or we are not live, don't care about latency");
          min_latency = min_l;
          max_latency = max_l;
        }
        gst_query_set_latency (query, live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = GST_ELEMENT_CLASS (parent_class)->query (element, query);
      break;
  }

done:
  return res;
}


static gboolean
gst_audio_track_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      if (sink->audio_track) {
        gst_jni_audio_track_flush (sink->audio_track);
      }
      break;
    case GST_EVENT_EOS:
      /* now wait till we played everything */
      // FIXME: WAIT UNTIL LAST SAMPLE IS PLAYED
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gdouble rate;

      gst_event_parse_new_segment_full (event, NULL, &rate, NULL, NULL,
          NULL, NULL, NULL);

      GST_DEBUG_OBJECT (sink, "new segment rate of %f", rate);
      gst_jni_audio_track_set_playback_params (sink->audio_track, rate, 1);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static void
gst_audio_track_sink_get_times (GstBaseSink * bsink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* Synchronization is handled by the SoC implementation for
   * tunneled video playback with the audio session id
   * We disable it for now as only * need to fill AudioTrack's
   * internal buffer */
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static void
gst_audio_track_sink_fixate (GstBaseSink * bsink, GstCaps * caps)
{
  GstStructure *s;
  gint width, depth;

  s = gst_caps_get_structure (caps, 0);

  /* fields for all formats */
  gst_structure_fixate_field_nearest_int (s, "rate", 44100);
  gst_structure_fixate_field_nearest_int (s, "channels", 2);
  gst_structure_fixate_field_nearest_int (s, "width", 16);

  /* fields for int */
  if (gst_structure_has_field (s, "depth")) {
    gst_structure_get_int (s, "width", &width);
    /* round width to nearest multiple of 8 for the depth */
    depth = GST_ROUND_UP_8 (width);
    gst_structure_fixate_field_nearest_int (s, "depth", depth);
  }
  if (gst_structure_has_field (s, "signed"))
    gst_structure_fixate_field_boolean (s, "signed", TRUE);
  if (gst_structure_has_field (s, "endianness"))
    gst_structure_fixate_field_nearest_int (s, "endianness", G_BYTE_ORDER);
}

static gboolean
gst_audio_track_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (bsink);
  GstStructure *s;
  gint buffer_size;
  gint latency_frames;

  if (sink->audio_track)
    return FALSE;

  GST_DEBUG_OBJECT (sink, "parsing caps");

  s = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (s, "width", &sink->width);
  gst_structure_get_int (s, "rate", &sink->rate);
  gst_structure_get_int (s, "channels", &sink->channels);

  buffer_size = gst_jni_audio_track_get_min_buffer_size (sink->rate,
      sink->channels, sink->width);
  latency_frames =
      buffer_size / (sink->width / 8) / sink->channels * BUFFER_SIZE_FACTOR;
  sink->latency = GST_SECOND * latency_frames / sink->rate;
  sink->audio_track =
      gst_jni_audio_track_new (sink->rate, sink->channels, sink->width,
      buffer_size * BUFFER_SIZE_FACTOR, sink->audio_session_id);
  if (sink->audio_track == NULL) {
    GST_ELEMENT_ERROR (sink, LIBRARY, SETTINGS,
        ("failed to create AudioTrack, incorrect settings"),
        ("failed to create AudioTrack, incorrect settings"));
    return FALSE;
  }
  GST_INFO_OBJECT (sink,
      "Created AudioTrack: min_buffer_size=%d latency=%lld session_id=%d",
      buffer_size * 3, sink->latency, sink->audio_session_id);
  return TRUE;
}

static GstStateChangeReturn
gst_audio_track_sink_async_play (GstBaseSink * basesink)
{
  GstAudioTrackSink *sink;

  sink = GST_AUDIOTRACK_SINK (basesink);

  GST_DEBUG_OBJECT (sink, "async play");
  sink->needs_start = TRUE;

  return GST_STATE_CHANGE_SUCCESS;
}

static GstStateChangeReturn
gst_audio_track_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      gboolean eos;

      GST_DEBUG_OBJECT (sink,
          "changing state to playing, AudioTrack can play.");

      GST_OBJECT_LOCK (sink);
      eos = GST_BASE_SINK (sink)->eos;
      // We can now play the AudioTrack in the sample sample
      sink->needs_start = TRUE;
      GST_OBJECT_UNLOCK (sink);
      if (eos) {
        /* sync rendering on eos needs running clock,
         * and others need running clock when finished rendering eos */
        GST_DEBUG_OBJECT (sink, "Playing AudioTrack");
        gst_jni_audio_track_play (sink->audio_track);
      }
      break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_DEBUG_OBJECT (sink, "Pausing AudioTrack");
      gst_jni_audio_track_pause (sink->audio_track);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (sink, "Stopping AudioTrack");
      gst_jni_audio_track_stop (sink->audio_track);
      g_object_unref (sink->audio_track);
      sink->audio_track = NULL;
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_audio_track_sink_preroll (GstBaseSink * bsink, GstBuffer * buf)
{
  /* FIXME: The preroll function needs to be set. From some reason,
   * the preroll function is set/overwritten to a invalid pointer
   * and it ends up segfaulting
   */
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_track_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (bsink);
  JNIEnv *env;
  GstAudioTrackError res;
  jobject jbuffer;
  gint remaining;
  GstFlowReturn ret = GST_FLOW_OK;

  env = gst_jni_get_env ();

  if (G_UNLIKELY (sink->needs_start)) {
    GST_DEBUG_OBJECT (sink, "Playing AudioTrack");
    gst_jni_audio_track_play (sink->audio_track);
    sink->needs_start = FALSE;
  }

  GST_DEBUG_OBJECT (sink, "Writting buffer to AudioTrack PTS:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  remaining = GST_BUFFER_SIZE (buf);
  jbuffer = (*env)->NewDirectByteBuffer (env, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));

write:
  if (sink->audio_session_id > 0) {
    res = gst_jni_audio_track_write_hw_sync (sink->audio_track, jbuffer,
        remaining, GST_AUDIO_TRACK_WRITE_NON_BLOCKING,
        GST_BUFFER_TIMESTAMP (buf));
  } else {
    // FIXME: synchronization is completly broken, it's only enabled here
    // for debugging
    res = gst_jni_audio_track_write (sink->audio_track, jbuffer,
        remaining, GST_AUDIO_TRACK_WRITE_NON_BLOCKING);
  }

  if (res < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED,
        ("failed to write buffer"), ("failed to write buffer error:%d", res));
    ret = GST_FLOW_ERROR;
    goto exit;
  }

  remaining -= res;

  GST_DEBUG_OBJECT (sink, "Written %d out of %d, remaining %d",
      res, GST_BUFFER_SIZE (buf), remaining);

  if (remaining > 0) {
    gint64 end_time;

    /* Wait until there is space in the AudioTrack */
    end_time = g_get_monotonic_time () + 10 * G_TIME_SPAN_MILLISECOND;
    g_mutex_lock (&sink->render_lock);
    if (sink->unlocking) {
      g_mutex_unlock (&sink->render_lock);
      goto exit;
    }
    if (!g_cond_wait_until (&sink->render_cond, &sink->render_lock, end_time)) {
      GST_DEBUG_OBJECT (sink, "Trying to write remaining data %d", remaining);
      g_mutex_unlock (&sink->render_lock);
      goto write;
    } else {
      GST_DEBUG_OBJECT (sink, "Woken up to unlock");
      g_mutex_unlock (&sink->render_lock);
      goto exit;
    }
    goto write;
  }

exit:
  gst_jni_object_local_unref (env, jbuffer);
  GST_DEBUG_OBJECT (sink, "Writting buffer to AudioTrack done");
  return ret;
}


static gboolean
gst_audio_track_sink_unlock (GstBaseSink * bsink)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (bsink);
  GST_DEBUG_OBJECT (sink, "Unlock");

  g_mutex_lock (&sink->render_lock);
  sink->unlocking = TRUE;
  g_cond_signal (&sink->render_cond);
  g_mutex_unlock (&sink->render_lock);
  return TRUE;
}

static gboolean
gst_audio_track_sink_unlock_stop (GstBaseSink * bsink)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (bsink);
  GST_DEBUG_OBJECT (sink, "Unlock stop");
  g_mutex_lock (&sink->render_lock);
  sink->unlocking = FALSE;
  g_mutex_unlock (&sink->render_lock);
  return TRUE;
}

static void
gst_audiotrack_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "AudioTrack Sink",
      "Sink/Audio",
      "Output sound using the Audio Track APIs",
      "Andoni Morales <support@fluendo.com>");
}

static void
gst_audio_track_sink_update_volume (GstAudioTrackSink * sink)
{
  if (!sink->audio_track) {
    return;
  }

  if (sink->mute) {
    gst_jni_audio_track_set_volume (sink->audio_track, 0);
  } else {
    gst_jni_audio_track_set_volume (sink->audio_track, sink->volume);
  }
}

static void
gst_audiotrack_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (object);

  switch (prop_id) {
    case PROP_VOLUME:
      sink->volume = g_value_get_double (value);
      gst_audio_track_sink_update_volume (sink);
      break;
    case PROP_MUTE:
      sink->mute = g_value_get_boolean (value);
      gst_audio_track_sink_update_volume (sink);
      break;
    case PROP_AUDIO_SESSION_ID:
      sink->audio_session_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audiotrack_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (object);
  switch (prop_id) {
    case PROP_VOLUME:
      g_value_set_double (value, sink->volume);
      break;
    case PROP_MUTE:
      g_value_set_boolean (value, sink->mute);
      break;
    case PROP_AUDIO_SESSION_ID:
      g_value_set_int (value, sink->audio_session_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audiotrack_sink_dispose (GObject * gobject)
{
  GstAudioTrackSink *sink = GST_AUDIOTRACK_SINK (gobject);

  if (sink->audio_track != NULL) {
    g_object_unref (sink->audio_track);
    sink->audio_track = NULL;
  }
  g_mutex_clear (&sink->render_lock);
  g_cond_clear (&sink->render_cond);

  G_OBJECT_CLASS (parent_class)->dispose (gobject);
}

static void
gst_audiotrack_sink_class_init (GstAudioTrackSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_audiotrack_sink_dispose;
  gobject_class->set_property = gst_audiotrack_sink_set_property;
  gobject_class->get_property = gst_audiotrack_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "Volume of this stream",
          0, 1.0, 1.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "Mute state of this stream",
          DEFAULT_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUDIO_SESSION_ID,
      g_param_spec_int ("audio-session-id", "Audio Session ID",
          "Audio Session ID for tunneled video playback",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_audio_track_sink_change_state);
  gstelement_class->query = GST_DEBUG_FUNCPTR (gst_audio_track_sink_query);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_audio_track_sink_event);
  gstbasesink_class->preroll = GST_DEBUG_FUNCPTR (gst_audio_track_sink_preroll);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_audio_track_sink_render);
  gstbasesink_class->get_times =
      GST_DEBUG_FUNCPTR (gst_audio_track_sink_get_times);
  gstbasesink_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_audio_track_sink_setcaps);
  gstbasesink_class->fixate = GST_DEBUG_FUNCPTR (gst_audio_track_sink_fixate);
  gstbasesink_class->async_play =
      GST_DEBUG_FUNCPTR (gst_audio_track_sink_async_play);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_audio_track_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_audio_track_sink_unlock_stop);
}

static void
gst_audiotrack_sink_init (GstAudioTrackSink * sink,
    GstAudioTrackSinkClass * gclass)
{
  sink->volume = DEFAULT_VOLUME;
  sink->mute = DEFAULT_MUTE;
  g_mutex_init (&sink->render_lock);
  g_cond_init (&sink->render_cond);
}
