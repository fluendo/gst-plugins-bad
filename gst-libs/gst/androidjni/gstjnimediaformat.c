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
#include "gstjnimediaformat.h"

static gboolean _initialized = FALSE;

static struct
{
  jclass klass;
  jmethodID create_audio_format;
  jmethodID create_video_format;
  jmethodID to_string;
  jmethodID contains_key;
  jmethodID get_float;
  jmethodID set_float;
  jmethodID get_integer;
  jmethodID set_integer;
  jmethodID get_string;
  jmethodID set_string;
  jmethodID get_byte_buffer;
  jmethodID set_byte_buffer;
  jmethodID set_feature_enabled;
} media_format;

gboolean
gst_amc_media_format_init (void)
{
  gboolean ret = FALSE;
  JNIEnv *env;

  if (_initialized) {
    return TRUE;
  }

  env = gst_jni_get_env ();

  media_format.klass = gst_jni_get_class (env, "android/media/MediaFormat");

  J_INIT_STATIC_METHOD_ID (media_format, create_audio_format,
      "createAudioFormat", "(Ljava/lang/String;II)Landroid/media/MediaFormat;");

  J_INIT_STATIC_METHOD_ID (media_format, create_video_format,
      "createVideoFormat", "(Ljava/lang/String;II)Landroid/media/MediaFormat;");

  J_INIT_METHOD_ID (media_format, to_string, "toString",
      "()Ljava/lang/String;");

  J_INIT_METHOD_ID (media_format, contains_key, "containsKey",
      "(Ljava/lang/String;)Z");

  J_INIT_METHOD_ID (media_format, get_float, "getFloat",
      "(Ljava/lang/String;)F");

  J_INIT_METHOD_ID (media_format, set_float, "setFloat",
      "(Ljava/lang/String;F)V");

  J_INIT_METHOD_ID (media_format, get_integer, "getInteger",
      "(Ljava/lang/String;)I");

  J_INIT_METHOD_ID (media_format, set_integer, "setInteger",
      "(Ljava/lang/String;I)V");

  J_INIT_METHOD_ID (media_format, get_string, "getString",
      "(Ljava/lang/String;)Ljava/lang/String;");

  J_INIT_METHOD_ID (media_format, set_string, "setString",
      "(Ljava/lang/String;Ljava/lang/String;)V");

  J_INIT_METHOD_ID (media_format, get_byte_buffer, "getByteBuffer",
      "(Ljava/lang/String;)Ljava/nio/ByteBuffer;");

  J_INIT_METHOD_ID (media_format, set_byte_buffer, "setByteBuffer",
      "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");

  J_INIT_METHOD_ID (media_format, set_feature_enabled, "setFeatureEnabled",
      "(Ljava/lang/String;Z)V");

  ret = TRUE;
error:
  _initialized = ret;
  return ret;
}

GstAmcFormat *
gst_amc_format_new_audio (const gchar * mime, gint sample_rate, gint channels)
{
  GstAmcFormat *format = NULL;
  jstring mime_str = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (mime);

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_slice_new0 (GstAmcFormat);
  J_CALL_STATIC_OBJ (object /* = */ , media_format, create_audio_format,
      mime_str, sample_rate, channels);
  AMC_CHK (object);

  format->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (format->object);

done:
  J_DELETE_LOCAL_REF (object);
  J_DELETE_LOCAL_REF (mime_str);
  return format;

error:
  GST_ERROR ("Failed to create format '%s'", mime ? mime : "NULL");
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

GstAmcFormat *
gst_amc_format_new_video (const gchar * mime, gint width, gint height)
{
  GstAmcFormat *format = NULL;
  jstring mime_str = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (mime && width && height);

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_slice_new0 (GstAmcFormat);

  J_CALL_STATIC_OBJ (object /* = */ , media_format, create_video_format,
      mime_str, width, height);

  AMC_CHK (object);
  format->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (format->object);

done:
  J_DELETE_LOCAL_REF (object);
  J_DELETE_LOCAL_REF (mime_str);
  return format;

error:
  GST_ERROR ("Failed to create format '%s',"
      " width = %d, height = %d", mime ? mime : "NULL", width, height);
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

void
gst_amc_format_free (GstAmcFormat * format)
{
  JNIEnv *env = gst_jni_get_env ();
  g_return_if_fail (format != NULL);

  J_DELETE_GLOBAL_REF (format->object);
  g_slice_free (GstAmcFormat, format);
}


gchar *
gst_amc_format_to_string (GstAmcFormat * format)
{
  jstring v_str = NULL;
  gchar *ret = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format);

  J_CALL_OBJ (v_str /* = */ , format->object, media_format.to_string);
  ret = gst_jni_string_to_gchar (env, v_str, TRUE);

error:
  return ret;
}

gboolean
gst_amc_format_contains_key (GstAmcFormat * format, const gchar * key)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_BOOL (ret /* = */ , format->object, media_format.contains_key,
      key_str);
error:
  J_DELETE_LOCAL_REF (key_str);
  return ret;
}

gboolean
gst_amc_format_get_float (GstAmcFormat * format, const gchar * key,
    gfloat * value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_FLOAT (*value /* = */ , format->object, media_format.get_float,
      key_str);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  return ret;
}

void
gst_amc_format_set_float (GstAmcFormat * format, const gchar * key,
    gfloat value)
{
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_VOID (format->object, media_format.set_float, key_str, value);
error:
  J_DELETE_LOCAL_REF (key_str);
}

gboolean
gst_amc_format_get_int (const GstAmcFormat * format, const gchar * key,
    gint * value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_INT (*value /* = */ , format->object, media_format.get_integer,
      key_str);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  return ret;
}

void
gst_amc_format_set_int (GstAmcFormat * format, const gchar * key, gint value)
{
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_VOID (format->object, media_format.set_integer, key_str, value);
error:
  J_DELETE_LOCAL_REF (key_str);
}

gboolean
gst_amc_format_get_jstring (GstAmcFormat * format, const gchar * key,
    jstring * value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (format && key && value);
  *value = NULL;

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_OBJ (*value /* = */ , format->object, media_format.get_string,
      key_str);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  return ret;
}

gboolean
gst_amc_format_get_string (GstAmcFormat * format, const gchar * key,
    gchar ** value)
{
  gboolean ret = FALSE;
  jstring tmp_str = NULL;
  JNIEnv *env = gst_jni_get_env ();

  if (!gst_amc_format_get_jstring (format, key, &tmp_str))
    goto error;

  *value = gst_jni_string_to_gchar (env, tmp_str, TRUE);
  AMC_CHK (*value);
  ret = TRUE;
error:
  return ret;
}

void
gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value)
{
  jstring key_str = NULL;
  jstring v_str = NULL;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  v_str = (*env)->NewStringUTF (env, value);
  AMC_CHK (v_str);

  J_CALL_VOID (format->object, media_format.set_string, key_str, v_str);
error:
  J_DELETE_LOCAL_REF (key_str);
  J_DELETE_LOCAL_REF (v_str);
}

gboolean
gst_amc_format_get_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer ** value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jobject v = NULL;
  guint8 *data;
  gsize size;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  *value = NULL;

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_OBJ (v /* = */ , format->object, media_format.get_byte_buffer,
      key_str);

  data = (*env)->GetDirectBufferAddress (env, v);
  AMC_CHK (data);

  size = (*env)->GetDirectBufferCapacity (env, v);
  *value = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (*value), data, size);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  J_DELETE_LOCAL_REF (v);
  return ret;
}

void
gst_amc_format_set_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer * value)
{
  jstring key_str = NULL;
  jobject v = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  /* FIXME: The buffer must remain valid until the codec is stopped */
  v = (*env)->NewDirectByteBuffer (env, GST_BUFFER_DATA (value),
      GST_BUFFER_SIZE (value));
  AMC_CHK (v);

  J_CALL_VOID (format->object, media_format.set_byte_buffer, key_str, v);
error:
  J_DELETE_LOCAL_REF (key_str);
  J_DELETE_LOCAL_REF (v);
}

void
gst_amc_format_set_feature_enabled (GstAmcFormat * format,
    const gchar * feature, gboolean enabled)
{
  jstring jtmpstr;

  JNIEnv *env = gst_jni_get_env ();

  jtmpstr = (*env)->NewStringUTF (env, feature);
  J_CALL_VOID (format->object, media_format.set_feature_enabled, jtmpstr,
      enabled);

error:
  J_DELETE_LOCAL_REF (jtmpstr);
}
