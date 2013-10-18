/*
 * Copyright (C) 2013, Fluendo S.A.
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

#include "gstjniutils.h"
#include "gstjnimediacodec.h"
#include "gstjnibufferinfo.h"
#include "gstjnimediaformat.h"
#include "gstjnibytebuffer.h"

G_DEFINE_TYPE (GstJniMediaCodec, gst_jni_media_codec, G_TYPE_OBJECT);

static gpointer parent_class = NULL;
static void gst_jni_media_codec_dispose (GObject * object);

static gboolean
_cache_java_class (GstJniMediaCodecClass * klass)
{
  JNIEnv *env;

  gst_jni_initialize ();

  env = gst_jni_get_env ();

  klass->jklass = gst_jni_get_class (env, "android/media/MediaCodec");
  if (!klass->jklass)
    return FALSE;

  klass->create_by_codec_name = gst_jni_get_static_method (env,
      klass->jklass, "createByCodecName",
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  klass->configure =
      (*env)->GetMethodID (env, klass->jklass, "configure",
      "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");
  klass->dequeue_input_buffer = gst_jni_get_method (env, klass->jklass,
      "dequeueInputBuffer", "(J)I");
  klass->dequeue_output_buffer = gst_jni_get_method (env, klass->jklass,
      "dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I");
  klass->get_input_buffers = gst_jni_get_method (env, klass->jklass,
      "getInputBuffers", "()[Ljava/nio/ByteBuffer;");
  klass->get_output_buffers = gst_jni_get_method (env, klass->jklass,
      "getOutputBuffers", "()[Ljava/nio/ByteBuffer;");
  klass->get_output_format = gst_jni_get_method (env, klass->jklass,
      "getOutputFormat", "()Landroid/media/MediaFormat;");
  klass->queue_input_buffer = gst_jni_get_method (env, klass->jklass,
      "queueInputBuffer", "(IIIJI)V");
  klass->release_output_buffer = gst_jni_get_method (env, klass->jklass,
      "releaseOutputBuffer", "(IZ)V");
  klass->release = gst_jni_get_method (env, klass->jklass, "release", "()V");
  klass->start = gst_jni_get_method (env, klass->jklass, "start", "()V");
  klass->stop = gst_jni_get_method (env, klass->jklass, "stop", "()V");
  klass->flush = gst_jni_get_method (env, klass->jklass, "flush", "()V");

  if (!klass->configure ||
      !klass->create_by_codec_name ||
      !klass->dequeue_input_buffer ||
      !klass->dequeue_output_buffer ||
      !klass->get_input_buffers ||
      !klass->get_output_buffers ||
      !klass->get_output_format ||
      !klass->queue_input_buffer ||
      !klass->release_output_buffer ||
      !klass->release ||
      !klass->start ||
      !klass->stop ||
      !klass->flush) {
    return FALSE;
  }
  return TRUE;
}


static void
gst_jni_media_codec_class_init (GstJniMediaCodecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->dispose = gst_jni_media_codec_dispose;

  klass->java_cached = _cache_java_class (klass);
  if (!klass->java_cached) {
    g_critical ("Could not cache java class android/media/MediaCodec");
  }
}

static void
gst_jni_media_codec_init (GstJniMediaCodec * self)
{
  /* initialize the object */
}

static void
gst_jni_media_codec_dispose (GObject * object)
{
  GstJniMediaCodec *self;

  self = GST_JNI_MEDIA_CODEC (object);

  if (self->jobject) {
    JNIEnv *env;
    env = gst_jni_get_env ();

    gst_jni_media_codec_release (self);
    gst_jni_release_object (env, self->jobject);
    self->jobject = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

GstJniMediaCodec *
gst_jni_media_codec_new (const gchar * name)
{
  GstJniMediaCodec *media_codec;
  GstJniMediaCodecClass *klass;
  jstring name_str = NULL;
  JNIEnv *env;

  g_return_val_if_fail (name != NULL, NULL);

  env = gst_jni_get_env ();

  media_codec = g_object_new (GST_TYPE_JNI_MEDIA_CODEC, NULL);
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (media_codec);
  if (!klass->java_cached)
    goto error;

  name_str = gst_jni_string_from_gchar (env, name);
  if (name_str == NULL)
    goto error;

  media_codec->jobject = gst_jni_new_object_from_static (env, klass->jklass,
      klass->create_by_codec_name, name_str);
  if (!media_codec->jobject) {
    goto error;
  }

done:
  if (name_str)
    gst_jni_release_local_object (env, name_str);
  return media_codec;

error:
  if (media_codec) {
    g_object_unref (media_codec);
    media_codec = NULL;
  }
  goto done;
}

gboolean
gst_jni_media_codec_configure (GstJniMediaCodec * self,
    GstJniMediaFormat * format, GstJniSurface * surface, gint flags)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;
  jobject jsurface = NULL;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  if (surface != NULL) {
    jsurface = surface->jobject;
  }
  return gst_jni_call_void_method (env, self->jobject, klass->configure,
      format->jobject, jsurface, NULL, flags);
}

GstJniMediaFormat *
gst_jni_media_codec_get_output_format (GstJniMediaCodec * self)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;
  GstJniMediaFormat *ret = NULL;
  jobject object = NULL;

  g_return_val_if_fail (self != NULL, NULL);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  object = gst_jni_call_object_method (env, self->jobject,
      klass->get_output_format);
  if (object) {
    ret = gst_jni_media_format_new (object);
  }
  return ret;
}

gboolean
gst_jni_media_codec_start (GstJniMediaCodec * self)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;

  g_return_val_if_fail (self != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  return gst_jni_call_void_method (env, self->jobject, klass->start);
}

gboolean
gst_jni_media_codec_stop (GstJniMediaCodec * self)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;

  g_return_val_if_fail (self != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  return gst_jni_call_void_method (env, self->jobject, klass->stop);
}

gboolean
gst_jni_media_codec_flush (GstJniMediaCodec * self)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;

  g_return_val_if_fail (self != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  return gst_jni_call_void_method (env, self->jobject, klass->flush);
}

gboolean
gst_jni_media_codec_release (GstJniMediaCodec * self)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;

  g_return_val_if_fail (self != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  return gst_jni_call_void_method (env, self->jobject, klass->release);
}

GList *
gst_jni_media_codec_get_output_buffers (GstJniMediaCodec * self)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;
  jobject output_buffers = NULL;
  jsize n_output_buffers;
  GList *ret = NULL;
  jsize i;

  g_return_val_if_fail (self != NULL, NULL);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  output_buffers = gst_jni_call_object_method (env, self->jobject,
      klass->get_output_buffers);
  if (!output_buffers) {
    goto done;
  }

  n_output_buffers = (*env)->GetArrayLength (env, output_buffers);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get output buffers array length");
    goto done;
  }

  for (i = 0; i < n_output_buffers; i++) {
    jobject buffer = NULL;
    GstJniByteBuffer *buf;

    buffer = (*env)->GetObjectArrayElement (env, output_buffers, i);
    if ((*env)->ExceptionCheck (env) || !buffer) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get output buffer %d", i);
      goto error;
    }

    buf = gst_jni_byte_buffer_new (buffer);
    if (buf == NULL)
      goto error;
    ret = g_list_append (ret, buf);
  }

done:
  if (output_buffers)
    gst_jni_release_local_object (env, output_buffers);
  return ret;

error:
  if (ret)
    g_list_free_full (ret, (GDestroyNotify) g_object_unref);
  ret = NULL;
  goto done;
}

GList *
gst_jni_media_codec_get_input_buffers (GstJniMediaCodec * self)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;
  jobject input_buffers = NULL;
  jsize n_input_buffers;
  GList *ret = NULL;
  jsize i;

  g_return_val_if_fail (self != NULL, NULL);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  input_buffers = gst_jni_call_object_method (env,
      self->jobject, klass->get_input_buffers);
  if (!input_buffers) {
    goto done;
  }

  n_input_buffers = (*env)->GetArrayLength (env, input_buffers);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get input buffers array length");
    goto done;
  }

  for (i = 0; i < n_input_buffers; i++) {
    jobject buffer = NULL;
    GstJniByteBuffer *buf;

    buffer = (*env)->GetObjectArrayElement (env, input_buffers, i);
    if ((*env)->ExceptionCheck (env) || !buffer) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get input buffer %d", i);
      goto error;
    }

    buf = gst_jni_byte_buffer_new (buffer);
    if (!buf) {
      goto error;
    }
    ret = g_list_append (ret, buf);
  }

done:
  if (input_buffers)
    gst_jni_release_local_object (env, input_buffers);
  input_buffers = NULL;

  return ret;

error:
  if (ret)
    g_list_free_full (ret, (GDestroyNotify) g_object_unref);
  ret = NULL;
  goto done;
}

gint
gst_jni_media_codec_dequeue_input_buffer (GstJniMediaCodec * self,
    gint64 timeout_us)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;

  g_return_val_if_fail (self != NULL, G_MININT);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  return gst_jni_call_int_method (env, self->jobject,
      klass->dequeue_input_buffer, timeout_us);
}


gint
gst_jni_media_codec_dequeue_output_buffer (GstJniMediaCodec * self,
    GstJniBufferInfo ** buffer_info, gint64 timeout_us)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;
  GstJniJbufferInfo *info;
  gint ret = G_MININT;

  g_return_val_if_fail (self != NULL, ret);
  g_return_val_if_fail (buffer_info != NULL, ret);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  info = gst_jni_jbuffer_info_new ();
  if (info == NULL)
    return ret;

  ret = gst_jni_call_int_method (env, self->jobject,
      klass->dequeue_output_buffer, info->jobject, timeout_us);

  *buffer_info = gst_jni_buffer_info_new ();
  gst_jni_buffer_info_fill (*buffer_info, info);
  g_object_unref (info);

  return ret;
}

gboolean
gst_jni_media_codec_queue_input_buffer (GstJniMediaCodec * self, gint index,
    GstJniBufferInfo * info)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  return gst_jni_call_void_method (env, self->jobject,
      klass->queue_input_buffer, index,
      info->offset, info->size, info->pts, info->flags);
}

gboolean
gst_jni_media_codec_release_output_buffer (GstJniMediaCodec * self,
    gint index, gboolean render)
{
  JNIEnv *env;
  GstJniMediaCodecClass *klass;

  g_return_val_if_fail (self != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_CODEC_GET_CLASS (self);

  return gst_jni_call_void_method (env, self->jobject,
      klass->release_output_buffer);
}
