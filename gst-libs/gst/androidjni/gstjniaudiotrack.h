/*
 * Copyright (C) 2020, Fluendo S.A.
 *   Author: Andoni Morales <amorales@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_JNI_AUDIO_TRACK_H__
#define __GST_JNI_AUDIO_TRACK_H__

#include <glib-object.h>
#include <jni.h>
#include "gstjniaudiotrack.h"

G_BEGIN_DECLS
#define GST_TYPE_JNI_AUDIO_TRACK                  (gst_jni_audio_track_get_type ())
#define GST_JNI_AUDIO_TRACK(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_JNI_AUDIO_TRACK, GstJniAudioTrack))
#define GST_IS_JNI_AUDIO_TRACK(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_JNI_AUDIO_TRACK))
#define GST_JNI_AUDIO_TRACK_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_JNI_AUDIO_TRACK, GstJniAudioTrackClass))
#define GST_IS_JNI_AUDIO_TRACK_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_JNI_AUDIO_TRACK))
#define GST_JNI_AUDIO_TRACK_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_JNI_AUDIO_TRACK, GstJniAudioTrackClass))
typedef struct _GstJniAudioTrack GstJniAudioTrack;
typedef struct _GstJniAudioTrackClass GstJniAudioTrackClass;

#define GST_JNI_AUDIO_TRACK_SESSION_ID_GENERATE 0

typedef enum
{
  GST_AUDIO_TRACK_PLAY_STATE_STOPPED = 1,
  GST_AUDIO_TRACK_PLAY_STATE_PAUSED = 2,
  GST_AUDIO_TRACK_PLAY_STATE_PLAYING = 3,
} GstAudioTrackPlayState;

typedef enum
{
  GST_AUDIO_TRACK_WRITE_BLOCKING = 0,
  GST_AUDIO_TRACK_WRITE_NON_BLOCKING = 1,
} GstAudioTrackWriteMode;

typedef enum
{
  GST_AUDIO_TRACK_ERROR = -1,
  GST_AUDIO_TRACK_ERROR_BAD_VALUE = -2,
  GST_AUDIO_TRACK_ERROR_ERROR_INVALID_OPERATION = -3,
  GST_AUDIO_TRACK_ERROR_DEAD_OBJECT = -6,
} GstAudioTrackError;

struct _GstJniAudioTrack
{
  GObject parent_instance;

  /* instance members */
  jobject jobject;
};

struct _GstJniAudioTrackClass
{
  GObjectClass parent_class;

  /* AudioAttributes.Builder */
  jclass audio_attr_builder_klass;
  jmethodID audio_attr_builder_ctor;
  jmethodID audio_attr_set_legacy_stream_type;
  jmethodID audio_attr_set_flags;
  jmethodID audio_attr_build;

  /* AudioFormat.Builder */
  jclass audio_format_builder_klass;
  jmethodID audio_format_builder_ctor;
  jmethodID audio_format_set_channel_mask;
  jmethodID audio_format_set_encoding;
  jmethodID audio_format_set_sample_rate;
  jmethodID audio_format_build;
};

GType gst_jni_audio_track_get_type (void);

gint gst_jni_audio_track_get_min_buffer_size (gint rate, gint channels,
    gint width);

GstJniAudioTrack *gst_jni_audio_track_new (gint rate,
    gint channels, gint width, gint buffer_size, gint session_id);

void gst_jni_audio_track_flush (GstJniAudioTrack * audio_track);

GstAudioTrackPlayState gst_jni_audio_track_get_play_state (GstJniAudioTrack *
    audio_track);

gint gst_jni_audio_track_get_playback_head_position (GstJniAudioTrack *
    audio_track);

void gst_jni_audio_track_stop (GstJniAudioTrack * audio_track);

void gst_jni_audio_track_pause (GstJniAudioTrack * audio_track);

void gst_jni_audio_track_play (GstJniAudioTrack * audio_track);

void gst_jni_audio_track_set_volume (GstJniAudioTrack * audio_track,
    float volume);

GstAudioTrackError gst_jni_audio_track_write (GstJniAudioTrack * audio_track,
    jobject jbuffer, gint size, GstAudioTrackWriteMode mode);

GstAudioTrackError gst_jni_audio_track_write_hw_sync (GstJniAudioTrack *
    audio_track, jobject jbuffer, gint size,
    GstAudioTrackWriteMode mode, GstClockTime ts);

void gst_jni_audio_track_set_playback_params (GstJniAudioTrack * audio_track,
    gfloat speed, gfloat pitch);

G_END_DECLS
#endif
