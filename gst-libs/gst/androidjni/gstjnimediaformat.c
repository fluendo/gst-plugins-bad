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

#include "gstjnimediaformat.h"
#include "gstjniutils.h"

G_DEFINE_TYPE (GstJniMediaFormat, gst_jni_media_format, G_TYPE_OBJECT);

static gpointer parent_class = NULL;
static void gst_jni_media_format_dispose (GObject * object);

static gboolean
_cache_java_class (GstJniMediaFormatClass * klass)
{
  JNIEnv *env;

  gst_jni_initialize ();

  env = gst_jni_get_env ();

  klass->jklass = gst_jni_get_class (env, "android/media/MediaFormat");
  if (!klass->jklass)
    return FALSE;

  klass->create_audio_format = gst_jni_get_static_method (env,
      klass->jklass, "createAudioFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  klass->create_video_format = gst_jni_get_static_method (env,
      klass->jklass, "createVideoFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  klass->to_string = gst_jni_get_method (env, klass->jklass, "toString",
      "()Ljava/lang/String;");
  klass->contains_key = (*env)->GetMethodID (env, klass->jklass, "containsKey",
      "(Ljava/lang/String;)Z");
  klass->get_float = gst_jni_get_method (env, klass->jklass, "getFloat",
      "(Ljava/lang/String;)F");
  klass->set_float = gst_jni_get_method (env, klass->jklass, "setFloat",
      "(Ljava/lang/String;F)V");
  klass->get_integer = gst_jni_get_method (env, klass->jklass, "getInteger",
      "(Ljava/lang/String;)I");
  klass->set_integer = gst_jni_get_method (env, klass->jklass, "setInteger",
      "(Ljava/lang/String;I)V");
  klass->get_string = gst_jni_get_method (env, klass->jklass, "getString",
      "(Ljava/lang/String;)Ljava/lang/String;");
  klass->set_string = gst_jni_get_method (env, klass->jklass, "setString",
      "(Ljava/lang/String;Ljava/lang/String;)V");
  klass->get_byte_buffer =
      gst_jni_get_method (env, klass->jklass, "getByteBuffer",
      "(Ljava/lang/String;)Ljava/nio/ByteBuffer;");
  klass->set_byte_buffer =
      gst_jni_get_method (env, klass->jklass, "setByteBuffer",
      "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");

  if (!klass->create_audio_format || !klass->create_video_format
      || !klass->contains_key || !klass->get_float
      || !klass->set_float || !klass->get_integer
      || !klass->set_integer || !klass->get_string
      || !klass->set_string || !klass->get_byte_buffer
      || !klass->set_byte_buffer) {
    return FALSE;
  }
  return TRUE;
}

static void
gst_jni_media_format_class_init (GstJniMediaFormatClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->dispose = gst_jni_media_format_dispose;

  klass->java_cached = _cache_java_class (klass);
  if (!klass->java_cached) {
    g_critical ("Could not cache java class android/media/MediaFormat");
  }
}

static void
gst_jni_media_format_init (GstJniMediaFormat * self)
{
  /* initialize the object */
  self->jobject = NULL;
}

static void
gst_jni_media_format_dispose (GObject * object)
{
  GstJniMediaFormat *self;

  self = GST_JNI_MEDIA_FORMAT (object);

  if (self->jobject) {
    JNIEnv *env;
    env = gst_jni_get_env ();

    gst_jni_release_object (env, self->jobject);
    self->jobject = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

GstJniMediaFormat *
gst_jni_media_format_new (jobject object)
{
  GstJniMediaFormat *format = NULL;
  JNIEnv *env;

  g_return_val_if_fail (object != NULL, NULL);

  env = gst_jni_get_env ();
  format = g_object_new (GST_TYPE_JNI_MEDIA_FORMAT, NULL);

  format->jobject = gst_jni_object_make_global (env, object);
  if (!format->jobject) {
    g_object_unref (format);
    format = NULL;
  }
  return format;
}

GstJniMediaFormat *
gst_jni_media_format_new_audio (const gchar * mime, gint sample_rate,
    gint channels)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  GstJniMediaFormat *format = NULL;
  jstring mime_str;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_jni_get_env ();
  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_object_new (GST_TYPE_JNI_MEDIA_FORMAT, NULL);
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (format);

  format->jobject = gst_jni_new_object_from_static (env, klass->jklass,
      klass->create_audio_format, mime_str, sample_rate, channels);
  if (!format->jobject)
    goto error;

done:
  if (mime_str)
    gst_jni_release_local_object (env, mime_str);
  return format;

error:
  if (format) {
    g_object_unref (format);
    format = NULL;
  }
  goto done;
}

GstJniMediaFormat *
gst_jni_media_format_new_video (const gchar * mime, gint width, gint height)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  GstJniMediaFormat *format = NULL;
  jstring mime_str;

  g_return_val_if_fail (mime != NULL, NULL);

  env = gst_jni_get_env ();

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_object_new (GST_TYPE_JNI_MEDIA_FORMAT, NULL);
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (format);

  format->jobject = gst_jni_new_object_from_static (env, klass->jklass,
      klass->create_video_format, mime_str, width, height);
  if (!format->jobject)
    goto error;

done:
  if (mime_str)
    gst_jni_release_local_object (env, mime_str);
  return format;

error:
  if (format) {
    g_object_unref (format);
    format = NULL;
  }
  goto done;
}

gchar *
gst_jni_media_format_to_string (GstJniMediaFormat * self)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  jstring str = NULL;

  g_return_val_if_fail (self != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  str = gst_jni_call_object_method (env, self->jobject, klass->to_string);
  if (!str)
    return NULL;
  return gst_jni_string_to_gchar (env, str, TRUE);
}

gboolean
gst_jni_media_format_contains_key (GstJniMediaFormat * self, const gchar * key)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  gboolean ret = FALSE;
  jstring key_str = NULL;

  g_return_val_if_fail (self != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    return ret;

  ret = gst_jni_call_boolean_method (env, self->jobject,
      klass->contains_key, key_str);
  gst_jni_release_local_object (env, key_str);
  return ret;
}

gfloat
gst_jni_media_format_get_float (GstJniMediaFormat * self, const gchar * key)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  gfloat ret = 0;
  jstring key_str = NULL;

  g_return_val_if_fail (self != NULL, ret);
  g_return_val_if_fail (key != NULL, ret);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    return ret;

  ret = gst_jni_call_float_method (env, self->jobject, klass->get_float,
      key_str);
  gst_jni_release_local_object (env, key_str);

  return ret;
}

void
gst_jni_media_format_set_float (GstJniMediaFormat * self, const gchar * key,
    gfloat value)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  jstring key_str = NULL;

  g_return_if_fail (self != NULL);
  g_return_if_fail (key != NULL);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    return;

  gst_jni_call_void_method (env, self->jobject, klass->set_float, key_str,
      value);
  gst_jni_release_local_object (env, key_str);
}

gint
gst_jni_media_format_get_int (GstJniMediaFormat * self, const gchar * key)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  gint ret = G_MININT;
  jstring key_str = NULL;

  g_return_val_if_fail (self != NULL, ret);
  g_return_val_if_fail (key != NULL, ret);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    return ret;

  ret = gst_jni_call_int_method (env, self->jobject,
      klass->get_integer, key_str);
  gst_jni_release_local_object (env, key_str);

  return ret;
}

void
gst_jni_media_format_set_int (GstJniMediaFormat * self, const gchar * key,
    gint value)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  jstring key_str = NULL;

  g_return_if_fail (self != NULL);
  g_return_if_fail (key != NULL);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    return;

  gst_jni_call_void_method (env, self->jobject,
      klass->set_integer, key_str, value);
  gst_jni_release_local_object (env, key_str);
}

gchar *
gst_jni_media_format_get_string (GstJniMediaFormat * self, const gchar * key)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  jstring key_str = NULL;
  jstring string = NULL;
  gchar *ret = NULL;

  g_return_val_if_fail (self != NULL, ret);
  g_return_val_if_fail (key != NULL, ret);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    return ret;

  string = gst_jni_call_object_method (env, self->jobject,
      klass->get_string, key_str);
  ret = gst_jni_string_to_gchar (env, string, TRUE);
  gst_jni_release_local_object (env, key_str);

  return ret;
}

void
gst_jni_media_format_set_string (GstJniMediaFormat * self, const gchar * key,
    const gchar * value)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  jstring key_str = NULL;
  jstring v_str = NULL;

  g_return_if_fail (self != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (value != NULL);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    goto done;

  v_str = gst_jni_string_from_gchar (env, value);
  if (!v_str)
    goto done;

  gst_jni_call_void_method (env, self->jobject, klass->set_string, key_str,
      v_str);

done:
  if (key_str)
    gst_jni_release_local_object (env, key_str);
  if (v_str)
    gst_jni_release_local_object (env, v_str);
}

GstBuffer *
gst_jni_media_format_get_buffer (GstJniMediaFormat * self, const gchar * key)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  GstBuffer *ret = NULL;
  jstring key_str = NULL;
  jobject buf = NULL;
  guint8 *data;
  gsize size;

  g_return_val_if_fail (self != NULL, ret);
  g_return_val_if_fail (key != NULL, ret);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    goto done;

  buf = gst_jni_call_object_method (env, self->jobject, klass->get_byte_buffer,
      key_str);
  if (!buf)
    goto done;

  data = (*env)->GetDirectBufferAddress (env, buf);
  if (!data) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer address");
    goto done;
  }
  size = (*env)->GetDirectBufferCapacity (env, buf);
  ret = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (ret), data, size);

done:
  if (key_str)
    gst_jni_release_local_object (env, key_str);
  if (buf)
    gst_jni_release_local_object (env, buf);

  return ret;
}

void
gst_jni_media_format_set_buffer (GstJniMediaFormat * self, const gchar * key,
    GstBuffer * value)
{
  JNIEnv *env;
  GstJniMediaFormatClass *klass;
  jstring key_str = NULL;
  jobject v = NULL;

  g_return_if_fail (self != NULL);
  g_return_if_fail (key != NULL);
  g_return_if_fail (value != NULL);

  env = gst_jni_get_env ();
  klass = GST_JNI_MEDIA_FORMAT_GET_CLASS (self);

  key_str = gst_jni_string_from_gchar (env, key);
  if (!key_str)
    goto done;

  /* FIXME: The buffer must remain valid until the codec is stopped */
  v = (*env)->NewDirectByteBuffer (env, GST_BUFFER_DATA (value),
      GST_BUFFER_SIZE (value));
  if (!v)
    goto done;

  gst_jni_call_void_method (env, self->jobject, klass->set_byte_buffer,
      key_str, v);

done:
  if (key_str)
    gst_jni_release_local_object (env, key_str);
  if (v)
    gst_jni_release_local_object (env, v);
}
