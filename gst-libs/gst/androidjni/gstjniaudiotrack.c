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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/androidjni/gstjniutils.h>
#include "gstjniaudiotrack.h"

#define AUDIO_MANAGER_STREAM_MUSIC 3
#define AUDIO_ATTRIBUTES_FLAG_HW_AV_SYNC 16
#define AUDIO_FORMAT_ENCODING_PCM_8BIT 3
#define AUDIO_FORMAT_ENCODING_PCM_16BIT 2
#define AUDIO_FORMAT_CHANNEL_OUT_MONO 4
#define AUDIO_FORMAT_CHANNEL_OUT_STEREO 12
#define AUDIO_TRACK_MODE_STREAM 1
#define AUDIO_TRACK_WRITE_BLOCKING 0

G_DEFINE_TYPE (GstJniAudioTrack, gst_jni_audio_track, G_TYPE_OBJECT);

static gpointer parent_class = NULL;
static void gst_jni_audio_track_dispose (GObject * object);

static struct
{
  /* class members */
  jclass klass;
  jmethodID constructor;

  /* instance methods */
  jmethodID flush;
  jmethodID get_play_state;
  jmethodID get_playback_head_position;
  jmethodID stop;
  jmethodID pause;
  jmethodID play;
  jmethodID release;
  jmethodID set_playback_params;
  jmethodID set_volume;
  jmethodID write_float;
  jmethodID write_short;
  jmethodID write_buffer;
  jmethodID write_buffer_hw_sync;

  /* static methods */
  jmethodID get_min_buffer_size;
} audio_track;

static struct
{
  /* class members */
  jclass klass;
  jmethodID constructor;

  jmethodID set_legacy_stream_type;
  jmethodID set_flags;
  jmethodID build;
} audio_attributes_builder;
static struct
{
  /* class members */
  jclass klass;
  jmethodID constructor;

  jmethodID set_channel_mask;
  jmethodID set_encoding;
  jmethodID set_sample_rate;
  jmethodID build;
} audio_format_builder;

static struct
{
  /* class members */
  jclass klass;
  jmethodID constructor;

  jmethodID set_pitch;
  jmethodID set_speed;
} playback_params;

static gboolean
_cache_java_class ()
{
  JNIEnv *env;

  gst_jni_initialize (NULL);
  env = gst_jni_get_env ();

  audio_track.klass = gst_jni_get_class (env, "android/media/AudioTrack");
  if (!audio_track.klass)
    return FALSE;

  J_INIT_METHOD_ID (audio_track, constructor,
      "<init>",
      "(Landroid/media/AudioAttributes;Landroid/media/AudioFormat;III)V");
  J_INIT_METHOD_ID (audio_track, flush, "flush", "()V");
  J_INIT_METHOD_ID (audio_track, get_play_state, "getPlayState", "()I");
  J_INIT_METHOD_ID (audio_track, get_playback_head_position,
      "getPlaybackHeadPosition", "()I");
  J_INIT_METHOD_ID (audio_track, stop, "stop", "()V");
  J_INIT_METHOD_ID (audio_track, pause, "pause", "()V");
  J_INIT_METHOD_ID (audio_track, play, "play", "()V");
  J_INIT_METHOD_ID (audio_track, release, "release", "()V");
  J_INIT_METHOD_ID (audio_track, set_playback_params, "setPlaybackParams",
      "(Landroid/media/PlaybackParams;)V");
  J_INIT_METHOD_ID (audio_track, set_volume, "setVolume", "(F)I");
  J_INIT_METHOD_ID (audio_track, write_float, "write", "([FIII)I");
  J_INIT_METHOD_ID (audio_track, write_short, "write", "([SIII)I");
  J_INIT_METHOD_ID (audio_track, write_buffer, "write",
      "(Ljava/nio/ByteBuffer;II)I");
  J_INIT_METHOD_ID (audio_track, write_buffer_hw_sync, "write",
      "(Ljava/nio/ByteBuffer;IIJ)I");

  audio_track.get_min_buffer_size = gst_jni_get_static_method (env,
      audio_track.klass, "getMinBufferSize", "(III)I");

  /* Cache AudioAttributes.Builder */
  audio_attributes_builder.klass = gst_jni_get_class (env,
      "android/media/AudioAttributes$Builder");
  if (!audio_attributes_builder.klass)
    return FALSE;

  J_INIT_METHOD_ID (audio_attributes_builder, constructor, "<init>", "()V");
  J_INIT_METHOD_ID (audio_attributes_builder, set_legacy_stream_type,
      "setLegacyStreamType", "(I)Landroid/media/AudioAttributes$Builder;");
  J_INIT_METHOD_ID (audio_attributes_builder, set_flags, "setFlags",
      "(I)Landroid/media/AudioAttributes$Builder;");
  J_INIT_METHOD_ID (audio_attributes_builder, build, "build",
      "()Landroid/media/AudioAttributes;");

  /* Cache AudioFormat.Builder */
  audio_format_builder.klass = gst_jni_get_class (env,
      "android/media/AudioFormat$Builder");
  if (!audio_format_builder.klass)
    return FALSE;

  J_INIT_METHOD_ID (audio_format_builder, constructor, "<init>", "()V");
  J_INIT_METHOD_ID (audio_format_builder, set_channel_mask,
      "setChannelMask", "(I)Landroid/media/AudioFormat$Builder;");
  J_INIT_METHOD_ID (audio_format_builder, set_encoding,
      "setEncoding", "(I)Landroid/media/AudioFormat$Builder;");
  J_INIT_METHOD_ID (audio_format_builder, set_sample_rate,
      "setSampleRate", "(I)Landroid/media/AudioFormat$Builder;");
  J_INIT_METHOD_ID (audio_format_builder, build, "build",
      "()Landroid/media/AudioFormat;");

  /* Cache AudioParams */
  playback_params.klass = gst_jni_get_class (env,
      "android/media/PlaybackParams");
  if (!playback_params.klass)
    return FALSE;

  J_INIT_METHOD_ID (playback_params, constructor, "<init>", "()V");
  J_INIT_METHOD_ID (playback_params, set_pitch,
      "setPitch", "(F)Landroid/media/PlaybackParams;");
  J_INIT_METHOD_ID (playback_params, set_speed,
      "setSpeed", "(F)Landroid/media/PlaybackParams;");
  return TRUE;

error:
  return FALSE;
}

static void
gst_jni_audio_track_init (GstJniAudioTrack * self)
{
}

static void
gst_jni_audio_track_class_init (GstJniAudioTrackClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->dispose = gst_jni_audio_track_dispose;

  if (audio_track.klass == NULL) {
    if (!_cache_java_class ()) {
      g_critical ("Could not cache java class android/media/AudioTrack");
    }
  }
}

static void
gst_jni_audio_track_dispose (GObject * object)
{
  GstJniAudioTrack *self;
  GstJniAudioTrackClass *klass;
  JNIEnv *env;

  self = GST_JNI_AUDIO_TRACK (object);
  klass = GST_JNI_AUDIO_TRACK_GET_CLASS (self);
  env = gst_jni_get_env ();

  /* unref the object */
  if (self->jobject) {
    (*env)->CallVoidMethod (env, self->jobject, audio_track.release);
    gst_jni_object_unref (env, self->jobject);
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

GstJniAudioTrack *
gst_jni_audio_track_new (gint rate, gint channels, gint width,
    gint buffer_size, gint audio_session_id)
{
  GstJniAudioTrack *audio_track_obj = NULL;
  jobject audio_attr_builder_obj;
  jobject audio_attr_obj;
  jobject audio_format_builder_obj;
  jobject audio_format_obj;
  JNIEnv *env;

  if (channels > 2) {
    g_critical ("FIXME: needs to implement more than 2 audio channels");
    channels = 2;
  }

  env = gst_jni_get_env ();

  GST_DEBUG ("JOOO new track %d %d %d %d %d", rate, channels, width,
      buffer_size, audio_session_id);

  audio_track_obj = g_object_new (GST_TYPE_JNI_AUDIO_TRACK, NULL);

  audio_attr_builder_obj = gst_jni_new_object (env,
      audio_attributes_builder.klass, audio_attributes_builder.constructor);
  gst_jni_call_object_method (env, audio_attr_builder_obj,
      audio_attributes_builder.set_legacy_stream_type,
      AUDIO_MANAGER_STREAM_MUSIC);
  if (audio_session_id) {
    gst_jni_call_object_method (env, audio_attr_builder_obj,
        audio_attributes_builder.set_flags, AUDIO_ATTRIBUTES_FLAG_HW_AV_SYNC);
  }
  audio_attr_obj = gst_jni_call_object_method (env, audio_attr_builder_obj,
      audio_attributes_builder.build);

  audio_format_builder_obj = gst_jni_new_object (env,
      audio_format_builder.klass, audio_format_builder.constructor);

  gst_jni_call_object_method (env, audio_format_builder_obj,
      audio_format_builder.set_channel_mask,
      channels == 1 ? AUDIO_FORMAT_CHANNEL_OUT_MONO :
      AUDIO_FORMAT_CHANNEL_OUT_STEREO);
  gst_jni_call_object_method (env, audio_format_builder_obj,
      audio_format_builder.set_encoding,
      width == 8 ? AUDIO_FORMAT_ENCODING_PCM_8BIT :
      AUDIO_FORMAT_ENCODING_PCM_16BIT);
  gst_jni_call_object_method (env, audio_format_builder_obj,
      audio_format_builder.set_sample_rate, rate);

  audio_format_obj = gst_jni_call_object_method (env, audio_format_builder_obj,
      audio_format_builder.build);

  audio_track_obj->jobject = gst_jni_new_object (env, audio_track.klass,
      audio_track.constructor, audio_attr_obj, audio_format_obj,
      buffer_size, AUDIO_TRACK_MODE_STREAM, audio_session_id);

  if (audio_track_obj->jobject == NULL) {
    GST_ERROR ("Error creating track, check the input parameters");
    goto error;
  }

done:
  return audio_track_obj;

error:
  if (audio_track_obj)
    g_object_unref (audio_track_obj);
  audio_track_obj = NULL;
  goto done;
}

void
gst_jni_audio_track_flush (GstJniAudioTrack * self)
{
  JNIEnv *env;

  env = gst_jni_get_env ();
  gst_jni_call_void_method (env, self->jobject, audio_track.flush);
}

GstAudioTrackPlayState
gst_jni_audio_track_get_play_state (GstJniAudioTrack * self)
{
  JNIEnv *env;

  env = gst_jni_get_env ();
  return (gint) gst_jni_call_int_method (env,
      self->jobject, audio_track.get_play_state);
}

gint
gst_jni_audio_track_get_playback_head_position (GstJniAudioTrack * self)
{
  JNIEnv *env;

  env = gst_jni_get_env ();
  return (gint) gst_jni_call_int_method (env,
      self->jobject, audio_track.get_playback_head_position);
}

void
gst_jni_audio_track_stop (GstJniAudioTrack * self)
{
  JNIEnv *env;

  env = gst_jni_get_env ();
  gst_jni_call_void_method (env, self->jobject, audio_track.stop);
}

void
gst_jni_audio_track_pause (GstJniAudioTrack * self)
{
  JNIEnv *env;

  env = gst_jni_get_env ();
  gst_jni_call_void_method (env, self->jobject, audio_track.pause);
}

void
gst_jni_audio_track_play (GstJniAudioTrack * self)
{
  JNIEnv *env;

  env = gst_jni_get_env ();
  gst_jni_call_void_method (env, self->jobject, audio_track.play);
}

void
gst_jni_audio_track_set_playback_params (GstJniAudioTrack * self, gfloat speed,
    gfloat pitch)
{
  JNIEnv *env;
  jobject playback_params_obj;

  env = gst_jni_get_env ();
  playback_params_obj = gst_jni_new_object (env, playback_params.klass,
      playback_params.constructor);
  gst_jni_call_object_method (env, playback_params_obj,
      playback_params.set_speed, speed);
  gst_jni_call_object_method (env, playback_params_obj,
      playback_params.set_pitch, pitch);
  gst_jni_call_void_method (env, self->jobject, audio_track.set_playback_params,
      playback_params_obj);
  gst_jni_object_unref (env, playback_params_obj);
}

void
gst_jni_audio_track_set_volume (GstJniAudioTrack * self, gfloat volume)
{
  JNIEnv *env;

  env = gst_jni_get_env ();
  gst_jni_call_void_method (env, self->jobject, audio_track.set_volume, volume);
}

gint
gst_jni_audio_track_get_min_buffer_size (gint rate, gint channels, gint width)
{
  JNIEnv *env;

  /* This method is static and might be called before the class initialization */
  if (audio_track.klass == NULL) {
    _cache_java_class ();
  }

  env = gst_jni_get_env ();
  return (gint) (*env)->CallStaticIntMethod (env, audio_track.klass,
      audio_track.get_min_buffer_size, rate,
      channels ==
      1 ? AUDIO_FORMAT_CHANNEL_OUT_MONO : AUDIO_FORMAT_CHANNEL_OUT_STEREO,
      width ==
      8 ? AUDIO_FORMAT_ENCODING_PCM_8BIT : AUDIO_FORMAT_ENCODING_PCM_16BIT);
}

GstAudioTrackError
gst_jni_audio_track_write (GstJniAudioTrack * self, GstBuffer * buf)
{
  JNIEnv *env;
  jobject j_buffer;
  GstAudioTrackError ret;

  env = gst_jni_get_env ();
  j_buffer = (*env)->NewDirectByteBuffer (env, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  ret = (GstAudioTrackError) gst_jni_call_int_method (env, self->jobject,
      audio_track.write_buffer, j_buffer, GST_BUFFER_SIZE (buf),
      AUDIO_TRACK_WRITE_BLOCKING);
  gst_jni_object_local_unref (env, j_buffer);

  return ret;
}

GstAudioTrackError
gst_jni_audio_track_write_hw_sync (GstJniAudioTrack * self, GstBuffer * buf)
{
  JNIEnv *env;
  jobject j_buffer;
  GstAudioTrackError ret;

  env = gst_jni_get_env ();
  j_buffer = (*env)->NewDirectByteBuffer (env, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  ret = (GstAudioTrackError) gst_jni_call_int_method (env, self->jobject,
      audio_track.write_buffer_hw_sync, j_buffer, GST_BUFFER_SIZE (buf),
      AUDIO_TRACK_WRITE_BLOCKING, GST_BUFFER_TIMESTAMP (buf));
  gst_jni_object_local_unref (env, j_buffer);
  return ret;
}
