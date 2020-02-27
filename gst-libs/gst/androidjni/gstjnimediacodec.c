/*
 * Copyright (C) 2020, Fluendo S.A.
 *   Author: John Judd <jjudd@fluendo.com>
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
#include "gstjnimediacodec.h"

static gboolean _initialized = FALSE;

static struct
{
  jclass klass;
  jmethodID configure;
  jmethodID create_by_codec_name;
  jmethodID dequeue_input_buffer;
  jmethodID dequeue_output_buffer;
  jmethodID flush;
  jmethodID get_input_buffers;
  jmethodID get_output_buffers;
  jmethodID get_output_format;
  jmethodID queue_input_buffer;
  jmethodID release;
  jmethodID release_output_buffer;
  jmethodID release_output_buffer_ts;
  jmethodID set_output_surface;
  jmethodID start;
  jmethodID stop;
  jmethodID queue_secure_input_buffer;
  jmethodID get_codec_info;
} media_codec;

gboolean
gst_jni_media_codec_init (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;

  if (_initialized) {
    return TRUE;
  }

  env = gst_jni_get_env ();

  media_codec.klass = gst_jni_get_class (env, "android/media/MediaCodec");

  J_INIT_METHOD_ID (media_codec, queue_secure_input_buffer,
      "queueSecureInputBuffer", "(IILandroid/media/MediaCodec$CryptoInfo;JI)V");

  J_INIT_STATIC_METHOD_ID (media_codec, create_by_codec_name,
      "createByCodecName", "(Ljava/lang/String;)Landroid/media/MediaCodec;");

  J_INIT_METHOD_ID (media_codec, configure, "configure",
      "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");

  J_INIT_METHOD_ID (media_codec, dequeue_input_buffer, "dequeueInputBuffer",
      "(J)I");

  J_INIT_METHOD_ID (media_codec, dequeue_output_buffer, "dequeueOutputBuffer",
      "(Landroid/media/MediaCodec$BufferInfo;J)I");

  J_INIT_METHOD_ID (media_codec, flush, "flush", "()V");

  J_INIT_METHOD_ID (media_codec, get_input_buffers, "getInputBuffers",
      "()[Ljava/nio/ByteBuffer;");


  J_INIT_METHOD_ID (media_codec, get_output_buffers, "getOutputBuffers",
      "()[Ljava/nio/ByteBuffer;");

  J_INIT_METHOD_ID (media_codec, get_output_format, "getOutputFormat",
      "()Landroid/media/MediaFormat;");

  J_INIT_METHOD_ID (media_codec, queue_input_buffer, "queueInputBuffer",
      "(IIIJI)V");

  J_INIT_METHOD_ID (media_codec, release, "release", "()V");

  J_INIT_METHOD_ID (media_codec, release_output_buffer, "releaseOutputBuffer",
      "(IZ)V");

  J_INIT_METHOD_ID (media_codec, release_output_buffer_ts,
      "releaseOutputBuffer", "(IJ)V");

  J_INIT_METHOD_ID (media_codec, set_output_surface, "setOutputSurface",
      "(Landroid/view/Surface;)V");

  J_INIT_METHOD_ID (media_codec, start, "start", "()V");

  J_INIT_METHOD_ID (media_codec, stop, "stop", "()V");

  J_INIT_METHOD_ID (media_codec, get_codec_info, "getCodecInfo",
      "()Landroid/media/MediaCodecInfo;");

done:
  _initialized = ret;
  return ret;
error:
  ret = FALSE;
  GST_ERROR ("Could not initialize android/media/MediaCodec");
  goto done;
}

gboolean
gst_jni_media_codec_configure (jobject codec_obj, jobject format,
    guint8 * surface, jobject mcrypto, gint flags)
{
  JNIEnv *env = gst_jni_get_env ();

  J_CALL_VOID (codec_obj, media_codec.configure,
      format, surface, mcrypto, flags);

  return TRUE;
error:
  return FALSE;
}


gboolean
gst_jni_media_codec_create_codec_by_name (jobject codec_obj, const gchar * name)
{
  gboolean success = TRUE;
  jstring name_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (name);

  name_str = (*env)->NewStringUTF (env, name);
  AMC_CHK (name_str);

  J_CALL_STATIC_OBJ (codec_obj /* = */ , media_codec, create_by_codec_name,
      name_str);

done:
  J_DELETE_LOCAL_REF (name_str);
  return success;
error:
  success = FALSE;
  goto done;
}

gboolean
gst_jni_media_codec_dequeue_input_buffer (jobject codec_obj, gint64 timeoutUs,
    gint * ret)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_INT (*ret /* = */ , codec_obj, media_codec.dequeue_input_buffer,
      timeoutUs);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_dequeue_output_buffer (jobject codec_obj, jobject info_obj,
    gint64 timeout_us, gint * buf_idx)
{
  JNIEnv *env;
  env = gst_jni_get_env ();

  J_CALL_INT (*buf_idx /* = */ , codec_obj,
      media_codec.dequeue_output_buffer, info_obj, timeout_us);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_flush (jobject * codec_obj)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_VOID (codec_obj, media_codec.flush);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_get_input_buffers (jobject codec_obj, jobject input_buffers)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_OBJ (input_buffers /* = */ , codec_obj,
      media_codec.get_input_buffers);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_get_output_buffers (jobject codec_obj,
    jobject output_buffers)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_OBJ (output_buffers /* = */ , codec_obj,
      media_codec.get_output_buffers);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_get_output_format (jobject codec_obj, jobject object)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_OBJ (object /* = */ , codec_obj, media_codec.get_output_format);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_queue_input_buffer (jobject codec_obj, gint index,
    gint offset, gint size, gint64 presentation_time_us, gint flags)
{
  JNIEnv *env = gst_jni_get_env ();

  J_CALL_VOID (codec_obj, media_codec.queue_input_buffer, index, offset,
      size, presentation_time_us, flags);

  return TRUE;
error:
  return FALSE;
}


gboolean
gst_jni_media_codec_release (jobject codec_obj)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_VOID (codec_obj, media_codec.release);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_release_output_buffer (jobject codec_obj, gint index)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_VOID (codec_obj, media_codec.release_output_buffer, index, JNI_FALSE);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_release_output_buffer_ts (jobject codec_obj, gint index,
    GstClockTime ts)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_VOID (codec_obj, media_codec.release_output_buffer_ts, index, ts);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_set_output_surface (jobject codec_obj, guint8 * surface)
{
  JNIEnv *env = gst_jni_get_env ();
  GST_DEBUG ("Set surface %p to codec %p", surface, codec_obj);
  J_CALL_VOID (codec_obj, media_codec.set_output_surface, surface);

  return TRUE;
error:
  GST_ERROR ("Failed to call MediaCodec.setOutputSurface (%p)", surface);
  return FALSE;
}

gboolean
gst_jni_media_codec_start (jobject codec_obj)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_VOID (codec_obj, media_codec.start);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_stop (jobject codec_obj)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_VOID (codec_obj, media_codec.stop);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_queue_secure_input_buffer (jobject codec_obj, gint index,
    gint offset, jobject crypto_info, gint64 presentation_time_us, gint flags)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_VOID (codec_obj, media_codec.queue_secure_input_buffer, index,
      offset, crypto_info, presentation_time_us, flags);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_get_codec_info (jobject codec_obj, jobject codec_info)
{
  JNIEnv *env = gst_jni_get_env ();
  J_CALL_OBJ (codec_info /* = */ , codec_obj, media_codec.get_codec_info);

  return TRUE;
error:
  return FALSE;
}

jmethodID
gst_jni_media_codec_get_release_ts_method_id (void)
{
  return media_codec.release_output_buffer_ts;
}

jmethodID
gst_jni_media_codec_get_release_method_id (void)
{
  return media_codec.release_output_buffer;
}
