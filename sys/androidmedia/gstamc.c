/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "gstamc.h"
#include "gstamc-constants.h"

#include "gstamcvideodec.h"
#include "gstamcaudiodec.h"
#include "gstamcvideosink.h"
#include "gstaudiotracksink.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/androidjni/gstjniutils.h>
#include <gst/androidjni/gstjnimediacodeclist.h>
#include <string.h>
#include <jni.h>


GST_DEBUG_CATEGORY (gst_amc_debug);
#define GST_CAT_DEFAULT gst_amc_debug

GQuark gst_amc_codec_info_quark = 0;

static GList *codec_infos = NULL;
static GList *registered_codecs = NULL;
#ifdef GST_AMC_IGNORE_UNKNOWN_COLOR_FORMATS
static gboolean ignore_unknown_color_formats = TRUE;
#else
static gboolean ignore_unknown_color_formats = FALSE;
#endif

static gboolean accepted_color_formats (GstAmcCodecType * type,
    gboolean is_encoder);

/* Global cached references */
static struct
{
  jclass klass;
  jmethodID constructor;
} java_string;

static struct
{
  jclass klass;
  jmethodID int_value;
} java_int;

static struct
{
  jclass klass;
  jmethodID get_upper;
} android_range;

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

static struct
{
  jclass klass;
  jmethodID get_capabilities_for_type;
  struct
  {
    jclass klass;
    jmethodID is_size_supported;
    jmethodID get_supported_heights;
    jmethodID get_supported_widths_for;
  } video_caps;
} media_codec_info;

static struct
{
  jclass klass;
  jmethodID is_feature_supported;
  jmethodID get_video_caps;
} codec_capabilities;

static struct
{
  jclass klass;
  jmethodID constructor;
  jfieldID flags;
  jfieldID offset;
  jfieldID presentation_time_us;
  jfieldID size;
} media_codec_buffer_info;

static struct
{
  jclass klass;
  jmethodID from_string;
} uuid;

static const gchar *features_to_check[] = {
  "adaptive-playback",
  "secure-playback",
  "tunneled-playback",
  NULL
};


jbyteArray
jbyte_arr_from_data (JNIEnv * env, const guchar * data, gsize size)
{
  jbyteArray arr = (*env)->NewByteArray (env, size);
  AMC_CHK (arr);
  (*env)->SetByteArrayRegion (env, arr, 0, size, (const jbyte *) data);
  J_EXCEPTION_CHECK ("SetByteArrayRegion");

  return arr;
error:
  J_DELETE_LOCAL_REF (arr);
  return NULL;
}


gchar *
gst_amc_get_string_utf8 (JNIEnv * env, jstring v_str)
{
  const gchar *v = NULL;
  gchar *ret = NULL;
  AMC_CHK (v_str);

  v = (*env)->GetStringUTFChars (env, v_str, NULL);
  AMC_CHK (v);

  ret = g_strdup (v);
error:
  if (v)
    (*env)->ReleaseStringUTFChars (env, v_str, v);
  return ret;
}


GstAmcCodec *
gst_amc_codec_new (const gchar * name)
{
  GstAmcCodec *codec = NULL;
  jstring name_str = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (name);

  name_str = (*env)->NewStringUTF (env, name);
  AMC_CHK (name_str);

  J_CALL_STATIC_OBJ (object /* = */ , media_codec, create_by_codec_name,
      name_str);

  codec = g_slice_new0 (GstAmcCodec);
  codec->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (codec->object);

  g_mutex_init (&codec->buffers_lock);
  codec->ref_count = 1;
done:
  J_DELETE_LOCAL_REF (object);
  J_DELETE_LOCAL_REF (name_str);
  return codec;
error:
  if (codec)
    g_slice_free (GstAmcCodec, codec);
  codec = NULL;
  goto done;
}


static void
gst_amc_codec_free (GstAmcCodec * codec)
{
  JNIEnv *env = gst_jni_get_env ();

  J_DELETE_GLOBAL_REF (codec->object);
  g_mutex_clear (&codec->buffers_lock);
  g_slice_free (GstAmcCodec, codec);
}

GstAmcCodec *
gst_amc_codec_ref (GstAmcCodec * codec)
{
  g_return_val_if_fail (codec, NULL);

  g_atomic_int_inc (&codec->ref_count);

  return codec;
}

void
gst_amc_codec_unref (GstAmcCodec * codec)
{
  g_return_if_fail (codec != NULL);

  if (g_atomic_int_dec_and_test (&codec->ref_count))
    gst_amc_codec_free (codec);
}

jmethodID
gst_amc_codec_get_release_ts_method_id (GstAmcCodec * codec)
{
  return media_codec.release_output_buffer_ts;
}

jmethodID
gst_amc_codec_get_release_method_id (GstAmcCodec * codec)
{
  return media_codec.release_output_buffer;
}

gboolean
gst_amc_codec_is_feature_supported (GstAmcCodec * codec,
    GstAmcFormat * format, const gchar * feature)
{
  gboolean is_feature_supported = FALSE;
  jstring jtmpstr = NULL;
  jobject codec_info = NULL;
  jobject capabilities = NULL;

  JNIEnv *env = gst_jni_get_env ();

  J_CALL_OBJ (codec_info /* = */ , codec->object, media_codec.get_codec_info);

  AMC_CHK (gst_amc_format_get_jstring (format, "mime", &jtmpstr));

  J_CALL_OBJ (capabilities /* = */ , codec_info,
      media_codec_info.get_capabilities_for_type, jtmpstr);
  J_DELETE_LOCAL_REF (jtmpstr);

  jtmpstr = (*env)->NewStringUTF (env, feature);

  J_CALL_BOOL (is_feature_supported /* = */ , capabilities,
      codec_capabilities.is_feature_supported, jtmpstr);

error:
  J_DELETE_LOCAL_REF (jtmpstr);
  J_DELETE_LOCAL_REF (capabilities);
  J_DELETE_LOCAL_REF (codec_info);

  GST_DEBUG ("Feature %s %ssupported", feature,
      (!is_feature_supported) ? "not " : "");
  return is_feature_supported;
}

gboolean
gst_amc_codec_enable_adaptive_playback (GstAmcCodec * codec,
    GstAmcFormat * format)
{
  gboolean supported;
  gboolean enabled = FALSE;
  /* default max size (4K UHD) if unlikely we cannot retrieve it */
  jint max_height = 2160;
  jint max_width = 3840;

  supported = gst_amc_codec_is_feature_supported (codec, format,
      "adaptive-playback");

  if (supported) {
    JNIEnv *env = gst_jni_get_env ();

    if (!media_codec_info.video_caps.klass) {
      GST_ERROR ("Video caps not supported, requires API 21");
    } else {
      jobject codec_info = NULL;
      jobject capabilities = NULL;
      jobject video_caps = NULL;
      jstring jtmpstr = NULL;
      jboolean supported;
      jobject heights = NULL;
      jobject widths = NULL;
      jobject upper = NULL;

      J_CALL_OBJ (codec_info /* = */ , codec->object,
          media_codec.get_codec_info);

      AMC_CHK (gst_amc_format_get_jstring (format, "mime", &jtmpstr));
      J_CALL_OBJ (capabilities /* = */ , codec_info,
          media_codec_info.get_capabilities_for_type, jtmpstr);

      J_CALL_OBJ (video_caps /* = */ , capabilities,
          codec_capabilities.get_video_caps);

      /* NOTE: We tried getSupportedHeights and getSupportedWidthsFor
       *  but we obtained non standard resolutions like 1072x8688.
       *  So we implemented the other way below to obtain the maximum standard
       *  size supported, trying 8K, then 4K DCI, then 4K UHD and using FHD
       *  otherwise.
       *  For now we keep this unneeded code because it was tricky to have it
       *  working (Range is a template class) and we want the values reported
       *  logged for every board. It may be safely deleted if we finally find
       *  it unnecessary. */
      {
        J_CALL_OBJ (heights /* = */ , video_caps,
            media_codec_info.video_caps.get_supported_heights);
        J_CALL_OBJ (upper /* = */ , heights,
            android_range.get_upper);
        J_CALL_INT (max_height /* = */ , upper, java_int.int_value);
        J_DELETE_LOCAL_REF (upper);
        upper = NULL;

        J_CALL_OBJ (widths /* = */ , video_caps,
            media_codec_info.video_caps.get_supported_widths_for, max_height);
        J_CALL_OBJ (upper /* = */ , widths,
            android_range.get_upper);
        J_CALL_INT (max_width /* = */ , upper, java_int.int_value);
        /* FIXME: This is not an error, but we want to force this log
         * and for now we can only achieve it in android using GST_ERROR */
        GST_ERROR ("supported size reported by old method (ignored): %dx%d",
            max_width, max_height);
      }

      /* This is the new approach to obtain the max standard size. */
      for (;;) {
        max_height = 4320;
        max_width = 7680;
        J_CALL_BOOL (supported /* = */ , video_caps,
            media_codec_info.video_caps.is_size_supported, max_width,
            max_height);
        if (supported)
          break;

        max_height = 2160;
        max_width = 4096;
        J_CALL_BOOL (supported /* = */ , video_caps,
            media_codec_info.video_caps.is_size_supported, max_width,
            max_height);
        if (supported)
          break;

        max_width = 3840;
        J_CALL_BOOL (supported /* = */ , video_caps,
            media_codec_info.video_caps.is_size_supported, max_width,
            max_height);
        if (supported)
          break;

        max_height = 1080;
        max_width = 1920;
        break;

      error:
        GST_ERROR ("Could not retrieve maximum frame size supported,"
            " using defaults");
        break;
      }
      J_DELETE_LOCAL_REF (upper);
      J_DELETE_LOCAL_REF (widths);
      J_DELETE_LOCAL_REF (heights);
      J_DELETE_LOCAL_REF (jtmpstr);
      J_DELETE_LOCAL_REF (video_caps);
      J_DELETE_LOCAL_REF (capabilities);
      J_DELETE_LOCAL_REF (codec_info);
    }

    gst_amc_format_set_int (format, "max-height", max_height);
    gst_amc_format_set_int (format, "max-width", max_width);
    gst_amc_format_set_int (format, "adaptive-playback", 1);
    enabled = TRUE;
  }
  codec->adaptive_enabled = enabled;
  /* FIXME: This is not an error, but we want to force this log
   * and for now we can only achieve it in android using GST_ERROR */
  GST_ERROR ("Adaptive: supported=%d enabled=%d max_width=%d, max_height=%d",
      supported, enabled, max_width, max_height);
  return enabled;
}

gboolean
gst_amc_codec_enable_tunneled_video_playback (GstAmcCodec * codec,
    GstAmcFormat * format, gint audio_session_id)
{
  gboolean supported;
  gboolean enabled = FALSE;

  supported = gst_amc_codec_is_feature_supported (codec, format,
      GST_AMC_MEDIA_FORMAT_TUNNELED_PLAYBACK);

  if (supported && audio_session_id) {
    gst_amc_format_set_feature_enabled (format,
        GST_AMC_MEDIA_FORMAT_TUNNELED_PLAYBACK, TRUE);
    gst_amc_format_set_int (format, "tunneled-playback", 1);
    gst_amc_format_set_int (format, "audio-hw-sync", audio_session_id);
    gst_amc_format_set_int (format, "audio-session-id", audio_session_id);
    enabled = TRUE;
  }

  codec->tunneled_playback_enabled = enabled;
  /* FIXME: This is not an error, but we want to force this log
   * and for now we can only achieve it in android using GST_ERROR */
  GST_ERROR ("tunneled: supported=%d enabled=%d audio_id=%d", supported,
      enabled, audio_session_id);
  return enabled;
}

gboolean
gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format,
    guint8 * surface, GstAmcCrypto * drm_ctx, gint flags,
    gint audio_session_id, gboolean enable_adaptive_playback)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();
  jobject mcrypto = NULL;

  AMC_CHK (codec && format);

  if (drm_ctx) {
    mcrypto = gst_amc_drm_mcrypto_get (drm_ctx);
    gst_amc_format_set_int (format, "secure-playback", 1);
  }

  if (enable_adaptive_playback)
    gst_amc_codec_enable_adaptive_playback (codec, format);

  if (audio_session_id) {
    GST_DEBUG ("Enabling tunneled playback with session id %d",
        audio_session_id);
    gst_amc_codec_enable_tunneled_video_playback (codec, format,
        audio_session_id);
  }

  /* FIXME: This is not an error, but we want to force this log
   * and for now we can only achieve it in android using GST_ERROR */
  GST_ERROR ("Configure: tunneled=%d, adaptive=%d, mcrypto=%p",
      codec->tunneled_playback_enabled, codec->adaptive_enabled, mcrypto);

  J_CALL_VOID (codec->object, media_codec.configure,
      format->object, surface, mcrypto, flags);
  ret = TRUE;
error:
  return ret;
}

GstAmcFormat *
gst_amc_codec_get_output_format (GstAmcCodec * codec)
{
  GstAmcFormat *ret = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (codec);

  J_CALL_OBJ (object /* = */ , codec->object, media_codec.get_output_format);

  ret = g_slice_new0 (GstAmcFormat);
  ret->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (ret->object);
done:
  J_DELETE_LOCAL_REF (object);
  return ret;
error:
  if (ret)
    g_slice_free (GstAmcFormat, ret);
  ret = NULL;
  goto done;

}

gboolean
gst_amc_codec_start (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec);
  J_CALL_VOID (codec->object, media_codec.start);
  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_stop (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec);
  J_CALL_VOID (codec->object, media_codec.stop);
  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_flush (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();
  /* !! be careful with AMC_CHK and J_CALL macros here: they may jump
   * to "error" label */
  if (G_UNLIKELY (!codec))
    return FALSE;

  /* Before we flush, we "invalidate all previously pushed buffers,
   * because it's incorrect to call releaseOutputBuffer after flush. */
  g_mutex_lock (&codec->buffers_lock);
  /* Now buffers with previous flush-id won't be ever released */
  codec->flush_id++;
  J_CALL_VOID (codec->object, media_codec.flush);
  ret = TRUE;
error:
  g_mutex_unlock (&codec->buffers_lock);
  return ret;
}

gboolean
gst_amc_codec_release (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec);
  J_CALL_VOID (codec->object, media_codec.release);
  ret = TRUE;
error:
  return ret;
}

void
gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers)
{
  if (buffers) {
    JNIEnv *env = gst_jni_get_env ();
    jsize i;
    for (i = 0; i < n_buffers; i++) {
      J_DELETE_GLOBAL_REF (buffers[i].object);
    }
    g_free (buffers);
  }
}

GstAmcBuffer *
gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  jobject output_buffers = NULL;
  jsize n_output_buffers = 0;
  GstAmcBuffer *ret = NULL;
  jsize i;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec && n_buffers);
  *n_buffers = 0;

  J_CALL_OBJ (output_buffers /* = */ , codec->object,
      media_codec.get_output_buffers);
  AMC_CHK (output_buffers);

  n_output_buffers = (*env)->GetArrayLength (env, output_buffers);
  J_EXCEPTION_CHECK ("(get output buffers array length)");
  AMC_CHK (n_output_buffers);

  ret = g_new0 (GstAmcBuffer, n_output_buffers);

  for (i = 0; i < n_output_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, output_buffers, i);
    AMC_CHK (buffer);

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    J_DELETE_LOCAL_REF (buffer);
    AMC_CHK (ret[i].object);

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    AMC_CHK (ret[i].data);
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

  *n_buffers = n_output_buffers;
  GST_DEBUG ("Created %" G_GSIZE_FORMAT, *n_buffers);
done:
  J_DELETE_LOCAL_REF (output_buffers);
  return ret;
error:
  gst_amc_codec_free_buffers (ret, n_output_buffers);
  ret = NULL;
  goto done;
}

GstAmcBuffer *
gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  jobject input_buffers = NULL;
  jsize n_input_buffers = 0;
  GstAmcBuffer *ret = NULL;
  jsize i;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (codec && n_buffers);

  *n_buffers = 0;

  J_CALL_OBJ (input_buffers /* = */ , codec->object,
      media_codec.get_input_buffers);
  AMC_CHK (input_buffers);

  n_input_buffers = (*env)->GetArrayLength (env, input_buffers);
  J_EXCEPTION_CHECK ("(get input buffers array length)");

  ret = g_new0 (GstAmcBuffer, n_input_buffers);

  for (i = 0; i < n_input_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, input_buffers, i);
    AMC_CHK (buffer);

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    J_DELETE_LOCAL_REF (buffer);
    AMC_CHK (ret[i].object);

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    AMC_CHK (ret[i].data);
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

  *n_buffers = n_input_buffers;
done:
  J_DELETE_LOCAL_REF (input_buffers);
  return ret;
error:
  gst_amc_codec_free_buffers (ret, n_input_buffers);
  ret = NULL;
  goto done;
}

gint
gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs)
{
  gint ret = G_MININT;
  JNIEnv *env = gst_jni_get_env ();
  g_return_val_if_fail (codec != NULL, G_MININT);

  J_CALL_INT (ret /* = */ , codec->object, media_codec.dequeue_input_buffer,
      timeoutUs);
error:
  return ret;
}

static gboolean
gst_amc_codec_fill_buffer_info (JNIEnv * env, jobject buffer_info,
    GstAmcBufferInfo * info)
{
  AMC_CHK (buffer_info);

  info->flags =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.flags);
  J_EXCEPTION_CHECK ("(get buffer info field <flags>)");

  info->offset =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.offset);
  J_EXCEPTION_CHECK ("(get buffer info field <offset>)");

  info->presentation_time_us =
      (*env)->GetLongField (env, buffer_info,
      media_codec_buffer_info.presentation_time_us);
  J_EXCEPTION_CHECK ("(get buffer info field <presentation_time_us>)");

  info->size =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.size);
  J_EXCEPTION_CHECK ("(get buffer info field <size>)");

  return TRUE;
error:
  return FALSE;
}

gint
gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec,
    GstAmcBufferInfo * info, gint64 timeoutUs)
{
  JNIEnv *env;
  gint ret = G_MININT, buf_idx;
  jobject info_o = NULL;

  g_return_val_if_fail (codec != NULL, G_MININT);

  env = gst_jni_get_env ();

  info_o =
      (*env)->NewObject (env, media_codec_buffer_info.klass,
      media_codec_buffer_info.constructor);
  AMC_CHK (info_o);

  J_CALL_INT (buf_idx /* = */ , codec->object,
      media_codec.dequeue_output_buffer, info_o, timeoutUs);

  AMC_CHK (gst_amc_codec_fill_buffer_info (env, info_o, info));

  ret = buf_idx;
error:
  J_DELETE_LOCAL_REF (info_o);
  return ret;
}

static gboolean
gst_amc_codec_queue_secure_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info, const GstBuffer * drmbuf, JNIEnv * env,
    GstAmcCrypto * ctx)
{
  gboolean ret = FALSE;
  jobject crypto_info = NULL;

  crypto_info = gst_amc_drm_get_crypto_info (ctx, drmbuf);
  if (!crypto_info) {
    GST_ERROR ("Couldn't create MediaCodec.CryptoInfo object"
        " or parse cenc structure");
    return FALSE;
  }

  /* queueSecureInputBuffer */
  (*env)->CallVoidMethod (env, codec->object,
      media_codec.queue_secure_input_buffer, index, info->offset, crypto_info,
      info->presentation_time_us, info->flags);

  if (gst_amc_drm_crypto_exception_check (env,
          "media_codec.queue_secure_input_buffer"))
    goto error;

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (crypto_info);
  return ret;
}


gboolean
gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info, const GstBuffer * drmbuf,
    GstAmcCrypto * drmctx)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  if (drmctx && drmbuf) {
    return gst_amc_codec_queue_secure_input_buffer (codec, index, info, drmbuf,
        env, drmctx);
  }

  J_CALL_VOID (codec->object, media_codec.queue_input_buffer,
      index, info->offset, info->size, info->presentation_time_us, info->flags);

  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  J_CALL_VOID (codec->object, media_codec.release_output_buffer,
      index, JNI_FALSE);

  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_render_output_buffer (GstAmcCodec * codec, gint index,
    GstClockTime ts)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  J_CALL_VOID (codec->object, media_codec.release_output_buffer_ts, index, ts);

  ret = TRUE;
error:
  return ret;
}

static gboolean
get_java_classes (void)
{
  gboolean ret = FALSE;
  JNIEnv *env;

  GST_DEBUG ("Retrieving Java classes");

  env = gst_jni_get_env ();

  java_string.klass = gst_jni_get_class (env, "java/lang/String");
  J_INIT_METHOD_ID (java_string, constructor, "<init>", "([C)V");

  java_int.klass = gst_jni_get_class (env, "java/lang/Integer");
  J_INIT_METHOD_ID (java_int, int_value, "intValue", "()I");

  android_range.klass = gst_jni_get_class (env, "android/util/Range");

  if (!android_range.klass) {
    GST_ERROR ("android/util/Range not found (requires API 21)");
  } else {
    android_range.get_upper = gst_jni_get_method (env, android_range.klass,
        "getUpper", "()Ljava/lang/Comparable;");
  }

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

  media_codec_buffer_info.klass =
      gst_jni_get_class (env, "android/media/MediaCodec$BufferInfo");
  J_INIT_METHOD_ID (media_codec_buffer_info, constructor, "<init>", "()V");

  media_codec_buffer_info.flags =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "flags", "I");
  media_codec_buffer_info.offset =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "offset", "I");
  media_codec_buffer_info.presentation_time_us =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass,
      "presentationTimeUs", "J");
  media_codec_buffer_info.size =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "size", "I");

  AMC_CHK (media_codec_buffer_info.flags &&
      media_codec_buffer_info.offset &&
      media_codec_buffer_info.presentation_time_us &&
      media_codec_buffer_info.size);

  /* MediaCodecInfo */
  media_codec_info.klass =
      gst_jni_get_class (env, "android/media/MediaCodecInfo");

  J_INIT_METHOD_ID (media_codec_info, get_capabilities_for_type,
      "getCapabilitiesForType",
      "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");

  codec_capabilities.klass =
      gst_jni_get_class (env, "android/media/MediaCodecInfo$CodecCapabilities");

  J_INIT_METHOD_ID (codec_capabilities, is_feature_supported,
      "isFeatureSupported", "(Ljava/lang/String;)Z");
  J_INIT_METHOD_ID (codec_capabilities, get_video_caps,
      "getVideoCapabilities",
      "()Landroid/media/MediaCodecInfo$VideoCapabilities;");

  media_codec_info.video_caps.klass = gst_jni_get_class (env,
      "android/media/MediaCodecInfo$VideoCapabilities");
  if (!media_codec_info.video_caps.klass) {
    GST_ERROR ("android/media/MediaCodecInfo$VideoCapabilities not found"
        " (requires API 21)");
  } else {
    J_INIT_METHOD_ID (media_codec_info.video_caps, is_size_supported,
        "isSizeSupported", "(II)Z");
    J_INIT_METHOD_ID (media_codec_info.video_caps, get_supported_heights,
        "getSupportedHeights", "()Landroid/util/Range;");
    J_INIT_METHOD_ID (media_codec_info.video_caps, get_supported_widths_for,
        "getSupportedWidthsFor", "(I)Landroid/util/Range;");
  }

  /* Drm classes & methods */
  AMC_CHK (gst_amc_drm_jni_init (env));

  /* UUID */
  uuid.klass = gst_jni_get_class (env, "java/util/UUID");

  J_INIT_STATIC_METHOD_ID (uuid, from_string, "fromString",
      "(Ljava/lang/String;)Ljava/util/UUID;");

  ret = TRUE;
error:
  return ret;
}

#ifdef GST_PLUGIN_BUILD_STATIC
static gchar *
get_cache_file (void)
{
  const gchar *cache_dir;
  gchar *cache_file = NULL;

  cache_dir = g_getenv ("XDG_CACHE_HOME");
  if (cache_dir) {
    cache_file = g_build_filename (cache_dir, "amccodecs.bin", NULL);
  }

  return cache_file;
}
#endif

static const GstStructure *
load_codecs (GstPlugin * plugin)
{
#ifdef GST_PLUGIN_BUILD_STATIC
  const GstStructure *cache_data = NULL;
  gchar *cache_file = get_cache_file ();
  if (cache_file) {
    gchar *cache_contents;

    g_file_get_contents (cache_file, &cache_contents, NULL, NULL);
    if (cache_contents) {
      cache_data = gst_structure_from_string (cache_contents, NULL);
      g_free (cache_contents);
    }
    g_free (cache_file);
  }
  return cache_data;
#else
  return gst_plugin_get_cache_data (plugin);
#endif
}

static void
save_codecs (GstPlugin * plugin, GstStructure * cache_data)
{
#ifdef GST_PLUGIN_BUILD_STATIC
  gchar *cache_file;

  cache_file = get_cache_file ();
  if (cache_file) {
    gchar *cache_contents;

    cache_contents = gst_structure_to_string (cache_data);
    g_file_set_contents (cache_file, cache_contents, -1, NULL);
    g_free (cache_contents);
  }
  gst_structure_free (cache_data);
#else
  gst_plugin_set_cache_data (plugin, cache_data);
#endif
}


jobject
juuid_from_utf8 (JNIEnv * env, const gchar * uuid_utf8)
{
  jobject juuid = NULL;
  jstring juuid_string = NULL;

  juuid_string = (*env)->NewStringUTF (env, uuid_utf8);
  AMC_CHK (juuid_string);

  J_CALL_STATIC_OBJ (juuid /* = */ , uuid, from_string, juuid_string);
  AMC_CHK (juuid);
error:
  J_DELETE_LOCAL_REF (juuid_string);
  return juuid;
}


gboolean
gst_amc_codec_set_output_surface (GstAmcCodec * codec, guint8 * surface)
{
  JNIEnv *env = gst_jni_get_env ();
  GST_DEBUG ("Set surface %p to codec %p", surface, codec->object);
  J_CALL_VOID (codec->object, media_codec.set_output_surface, surface);
  return TRUE;
error:
  GST_ERROR ("Failed to call MediaCodec.setOutputSurface (%p)", surface);
  return FALSE;
}

static GstAmcCodecFeature *
gst_amc_get_codec_feature (JNIEnv * env, jclass capabilities,
    const gchar * feature, GstAmcCodecFeature * codec_feature,
    jobject capabilities_obj)
{
  jmethodID supported_id, required_id;
  jstring jtmpstr;

  codec_feature->supported = codec_feature->required = FALSE;

  supported_id =
      (*env)->GetMethodID (env, capabilities, "isFeatureSupported",
      "(Ljava/lang/String;)Z");

  required_id =
      (*env)->GetMethodID (env, capabilities, "isFeatureRequired",
      "(Ljava/lang/String;)Z");

  jtmpstr = (*env)->NewStringUTF (env, feature);
  codec_feature->supported =
      (*env)->CallBooleanMethod (env, capabilities_obj, supported_id, jtmpstr);
  codec_feature->required =
      (*env)->CallBooleanMethod (env, capabilities_obj, required_id, jtmpstr);
  codec_feature->name = feature;

  J_DELETE_LOCAL_REF (jtmpstr);

  return codec_feature;
}

static gboolean
scan_codecs (GstPlugin * plugin)
{
  gboolean ret = FALSE;
  JNIEnv *env;
  GstJniMediaCodecList *codec_list = NULL;
  jint codec_count, i;
  jobjectArray jcodec_infos;
  const GstStructure *cache_data = NULL;

  GST_DEBUG ("Scanning codecs");

  cache_data = load_codecs (plugin);
  if (cache_data) {
    const GValue *arr = gst_structure_get_value (cache_data, "codecs");
    guint i, n;

    GST_DEBUG ("Getting codecs from cache");
    n = gst_value_array_get_size (arr);
    for (i = 0; i < n; i++) {
      const GValue *cv = gst_value_array_get_value (arr, i);
      const GstStructure *cs = gst_value_get_structure (cv);
      const gchar *name;
      gboolean is_encoder;
      const GValue *starr;
      guint j, n2;
      GstAmcCodecInfo *gst_codec_info;

      gst_codec_info = g_new0 (GstAmcCodecInfo, 1);

      name = gst_structure_get_string (cs, "name");
      gst_structure_get_boolean (cs, "is-encoder", &is_encoder);
      gst_codec_info->name = g_strdup (name);
      gst_codec_info->is_encoder = is_encoder;

      starr = gst_structure_get_value (cs, "supported-types");
      n2 = gst_value_array_get_size (starr);

      gst_codec_info->n_supported_types = n2;
      gst_codec_info->supported_types = g_new0 (GstAmcCodecType, n2);

      for (j = 0; j < n2; j++) {
        const GValue *stv = gst_value_array_get_value (starr, j);
        const GstStructure *sts = gst_value_get_structure (stv);
        const gchar *mime;
        const GValue *cfarr;
        const GValue *plarr;
        const GValue *farr;
        guint k, n3;
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[j];

        mime = gst_structure_get_string (sts, "mime");
        gst_codec_type->mime = g_strdup (mime);

        //GST_ERROR ("&&& Found mime: %s", mime);

        cfarr = gst_structure_get_value (sts, "color-formats");
        n3 = gst_value_array_get_size (cfarr);

        gst_codec_type->n_color_formats = n3;
        gst_codec_type->color_formats = g_new0 (gint, n3);

        for (k = 0; k < n3; k++) {
          const GValue *cfv = gst_value_array_get_value (cfarr, k);
          gint cf = g_value_get_int (cfv);

          gst_codec_type->color_formats[k] = cf;
        }

        plarr = gst_structure_get_value (sts, "profile-levels");
        n3 = gst_value_array_get_size (plarr);

        gst_codec_type->n_profile_levels = n3;
        gst_codec_type->profile_levels =
            g_malloc0 (sizeof (gst_codec_type->profile_levels[0]) * n3);

        for (k = 0; k < n3; k++) {
          const GValue *plv = gst_value_array_get_value (plarr, k);
          const GValue *p, *l;

          p = gst_value_array_get_value (plv, 0);
          l = gst_value_array_get_value (plv, 1);
          gst_codec_type->profile_levels[k].profile = g_value_get_int (p);
          gst_codec_type->profile_levels[k].level = g_value_get_int (l);
        }

        farr = gst_structure_get_value (sts, "features");
        n3 = gst_value_array_get_size (farr);
        gst_codec_type->n_features = n3;

        gst_codec_type->features = g_new0 (GstAmcCodecFeature, n3);
        for (k = 0; k < n3; k++) {
          const GValue *fv = gst_value_array_get_value (farr, k);
          const GstStructure *fs = gst_value_get_structure (fv);
          const gchar *name;
          GstAmcCodecFeature *gst_codec_feature = &gst_codec_type->features[k];

          name = gst_structure_get_string (fs, "name");
          gst_codec_feature->name = g_strdup (name);

          gst_structure_get_boolean (fs, "supported",
              &gst_codec_feature->supported);

          gst_structure_get_boolean (fs, "required",
              &gst_codec_feature->required);
        }

      }

      codec_infos = g_list_append (codec_infos, gst_codec_info);
    }

    return TRUE;
  }

  env = gst_jni_get_env ();

  codec_list = gst_jni_media_codec_list_new ();
  jcodec_infos = gst_jni_media_codec_list_get_codec_infos (codec_list);
  gst_jni_media_codec_list_free (codec_list);

  codec_count = (*env)->GetArrayLength (env, jcodec_infos);

  GST_LOG ("Found %d available codecs", codec_count);

  for (i = 0; i < codec_count; i++) {
    GstAmcCodecInfo *gst_codec_info;
    jobject codec_info = NULL;
    jclass codec_info_class = NULL;
    jmethodID get_capabilities_for_type_id, get_name_id;
    jmethodID get_supported_types_id, is_encoder_id;
    jobject name = NULL;
    const gchar *name_str = NULL;
    jboolean is_encoder;
    jarray supported_types = NULL;
    jsize n_supported_types;
    jsize j;
    gboolean valid_codec = TRUE;

    gst_codec_info = g_new0 (GstAmcCodecInfo, 1);

    codec_info = (*env)->GetObjectArrayElement (env, jcodec_infos, i);
    if ((*env)->ExceptionCheck (env) || !codec_info) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info %d", i);
      valid_codec = FALSE;
      goto next_codec;
    }

    codec_info_class = (*env)->GetObjectClass (env, codec_info);
    if (!codec_info_class) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info class");
      valid_codec = FALSE;
      goto next_codec;
    }

    get_capabilities_for_type_id =
        (*env)->GetMethodID (env, codec_info_class, "getCapabilitiesForType",
        "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
    get_name_id =
        (*env)->GetMethodID (env, codec_info_class, "getName",
        "()Ljava/lang/String;");
    get_supported_types_id =
        (*env)->GetMethodID (env, codec_info_class, "getSupportedTypes",
        "()[Ljava/lang/String;");
    is_encoder_id =
        (*env)->GetMethodID (env, codec_info_class, "isEncoder", "()Z");

    if (!get_capabilities_for_type_id || !get_name_id
        || !get_supported_types_id || !is_encoder_id) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info method IDs");
      valid_codec = FALSE;
      goto next_codec;
    }

    name = (*env)->CallObjectMethod (env, codec_info, get_name_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec name");
      valid_codec = FALSE;
      goto next_codec;
    }
    name_str = (*env)->GetStringUTFChars (env, name, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to convert codec name to UTF8");
      valid_codec = FALSE;
      goto next_codec;
    }

    GST_INFO ("Checking codec '%s'", name_str);

    /* Compatibility codec names */
    /* The MP3Decoder is found on Sony Xperia Z but it fails
     * when creating the element and triggers a SEGV
     * The OMX.SEC.avcdec Found on Samsung Galaxy S3 gives errors
     */
    if (strcmp (name_str, "AACEncoder") == 0 ||
        strcmp (name_str, "AACDecoder") == 0 ||
        strcmp (name_str, "MP3Decoder") == 0 ||
        strcmp (name_str, "OMX.SEC.avcdec") == 0 ||
        strcmp (name_str, "OMX.google.raw.decoder") == 0) {
      GST_INFO ("Skipping compatibility codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }
#if 0
    if (g_str_has_suffix (name_str, ".secure")) {
      GST_INFO ("Skipping DRM codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }
#endif

    /* FIXME: Non-Google codecs usually just don't work and hang forever
     * or crash when not used from a process that started the Java
     * VM via the non-public AndroidRuntime class. Can we somehow
     * initialize all this?
     */
    if (gst_jni_is_vm_started () && !g_str_has_prefix (name_str, "OMX.google.")) {
      GST_INFO ("Skipping non-Google codec '%s' in standalone mode", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    if (g_str_has_prefix (name_str, "OMX.ARICENT.") ||
        /* OMX.MTK.AUDIO.DECODER.DSPAAC doesn't work fine on some boards,
         * whether on others doesn't seem to fail.
         * In any case it's behaviour is very different from other decoders:
         * it outputs big chunks of raw data and works only in sync mode. */
        g_str_has_prefix (name_str, "OMX.MTK.AUDIO.DECODER.DSPAAC")) {
      GST_INFO ("Skipping possible broken codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    /* FIXME:
     *   - Vorbis: Generates clicks for multi-channel streams
     *   - *Law: Generates output with too low frequencies
     */
    if (strcmp (name_str, "OMX.google.vorbis.decoder") == 0 ||
        strcmp (name_str, "OMX.google.g711.alaw.decoder") == 0 ||
        strcmp (name_str, "OMX.google.g711.mlaw.decoder") == 0) {
      GST_INFO ("Skipping known broken codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }
    gst_codec_info->name = g_strdup (name_str);

    is_encoder = (*env)->CallBooleanMethod (env, codec_info, is_encoder_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to detect if codec is an encoder");
      valid_codec = FALSE;
      goto next_codec;
    }

    /* We do not support encoders. There are issues with a Sony Xperia P
     * where getting the capabilities of a type for an encoder takes around 5s
     */
    if (is_encoder) {
      GST_INFO ("Skipping encoder '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    gst_codec_info->is_encoder = is_encoder;

    supported_types =
        (*env)->CallObjectMethod (env, codec_info, get_supported_types_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types");
      valid_codec = FALSE;
      goto next_codec;
    }

    n_supported_types = (*env)->GetArrayLength (env, supported_types);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types array length");
      valid_codec = FALSE;
      goto next_codec;
    }

    GST_INFO ("Codec '%s' has %d supported types", name_str, n_supported_types);

    gst_codec_info->supported_types =
        g_new0 (GstAmcCodecType, n_supported_types);
    gst_codec_info->n_supported_types = n_supported_types;

    if (n_supported_types == 0) {
      valid_codec = FALSE;
      GST_ERROR ("Codec has no supported types");
      goto next_codec;
    }

    for (j = 0; j < n_supported_types; j++) {
      GstAmcCodecType *gst_codec_type;
      gint feature_idx = 0;
      jobject supported_type = NULL;
      gchar *supported_type_str = NULL;
      jobject capabilities = NULL;
      jclass capabilities_class = NULL;
      jfieldID color_formats_id, profile_levels_id;
      jobject color_formats = NULL;
      jobject profile_levels = NULL;
      jint *color_formats_elems = NULL;
      jsize n_elems, k;

      gst_codec_type = &gst_codec_info->supported_types[j];

      supported_type = (*env)->GetObjectArrayElement (env, supported_types, j);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get %d-th supported type", j);
        valid_codec = FALSE;
        goto next_supported_type;
      }

      supported_type_str = gst_amc_get_string_utf8 (env, supported_type);
      if (!supported_type_str) {
        GST_ERROR ("Failed to convert supported type to UTF8");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      GST_INFO ("Supported type '%s'", supported_type_str);
      gst_codec_type->mime = supported_type_str;

      capabilities =
          (*env)->CallObjectMethod (env, codec_info,
          get_capabilities_for_type_id, supported_type);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities for supported type");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      capabilities_class = (*env)->GetObjectClass (env, capabilities);
      if (!capabilities_class) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities class");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      gst_codec_type->features = g_new0 (GstAmcCodecFeature, FEATURE_COUNT);
      gst_codec_type->n_features = FEATURE_COUNT;
      while (features_to_check[feature_idx]) {
        gst_amc_get_codec_feature (env, capabilities_class,
            features_to_check[feature_idx],
            &gst_codec_type->features[feature_idx], capabilities);
        feature_idx++;
      }

      color_formats_id =
          (*env)->GetFieldID (env, capabilities_class, "colorFormats", "[I");
      profile_levels_id =
          (*env)->GetFieldID (env, capabilities_class, "profileLevels",
          "[Landroid/media/MediaCodecInfo$CodecProfileLevel;");
      if (!color_formats_id || !profile_levels_id) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities field IDs");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      color_formats =
          (*env)->GetObjectField (env, capabilities, color_formats_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color formats");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, color_formats);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color formats array length");
        valid_codec = FALSE;
        goto next_supported_type;
      }
      gst_codec_type->n_color_formats = n_elems;
      gst_codec_type->color_formats = g_new0 (gint, n_elems);
      color_formats_elems =
          (*env)->GetIntArrayElements (env, color_formats, NULL);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color format elements");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      for (k = 0; k < n_elems; k++) {
        GST_INFO ("Color format %d: %d", k, color_formats_elems[k]);
        gst_codec_type->color_formats[k] = color_formats_elems[k];
      }

      if (g_str_has_prefix (gst_codec_type->mime, "video/")) {
        if (!n_elems) {
          GST_ERROR ("No supported color formats for video codec");
          valid_codec = FALSE;
          goto next_supported_type;
        }

        if (!ignore_unknown_color_formats
            && !accepted_color_formats (gst_codec_type, is_encoder)) {
          GST_ERROR ("Codec has unknown color formats, ignoring");
          valid_codec = FALSE;
          g_assert_not_reached ();
          goto next_supported_type;
        }
      }

      profile_levels =
          (*env)->GetObjectField (env, capabilities, profile_levels_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get profile/levels");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, profile_levels);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get profile/levels array length");
        valid_codec = FALSE;
        goto next_supported_type;
      }
      gst_codec_type->n_profile_levels = n_elems;
      gst_codec_type->profile_levels =
          g_malloc0 (sizeof (gst_codec_type->profile_levels[0]) * n_elems);
      for (k = 0; k < n_elems; k++) {
        jobject profile_level = NULL;
        jclass profile_level_class = NULL;
        jfieldID level_id, profile_id;
        jint level, profile;

        profile_level = (*env)->GetObjectArrayElement (env, profile_levels, k);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get %d-th profile/level", k);
          valid_codec = FALSE;
          goto next_profile_level;
        }

        profile_level_class = (*env)->GetObjectClass (env, profile_level);
        if (!profile_level_class) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level class");
          valid_codec = FALSE;
          goto next_profile_level;
        }

        level_id = (*env)->GetFieldID (env, profile_level_class, "level", "I");
        profile_id =
            (*env)->GetFieldID (env, profile_level_class, "profile", "I");
        if (!level_id || !profile_id) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level field IDs");
          valid_codec = FALSE;
          goto next_profile_level;
        }

        level = (*env)->GetIntField (env, profile_level, level_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get level");
          valid_codec = FALSE;
          goto next_profile_level;
        }
        GST_INFO ("Level %d: 0x%08x", k, level);
        gst_codec_type->profile_levels[k].level = level;

        profile = (*env)->GetIntField (env, profile_level, profile_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile");
          valid_codec = FALSE;
          goto next_profile_level;
        }
        GST_INFO ("Profile %d: 0x%08x", k, profile);
        gst_codec_type->profile_levels[k].profile = profile;

      next_profile_level:
        J_DELETE_LOCAL_REF (profile_level);
        J_DELETE_LOCAL_REF (profile_level_class);
        if (!valid_codec)
          break;
      }

    next_supported_type:
      if (color_formats_elems)
        (*env)->ReleaseIntArrayElements (env, color_formats,
            color_formats_elems, JNI_ABORT);
      color_formats_elems = NULL;

      J_DELETE_LOCAL_REF (profile_levels);
      J_DELETE_LOCAL_REF (color_formats);
      J_DELETE_LOCAL_REF (capabilities);
      J_DELETE_LOCAL_REF (capabilities_class);
      J_DELETE_LOCAL_REF (supported_type);

      if (!valid_codec)
        break;
    }

    /* We need at least a valid supported type */
    if (valid_codec) {
      GST_LOG ("Successfully scanned codec '%s'", name_str);
      codec_infos = g_list_append (codec_infos, gst_codec_info);
      gst_codec_info = NULL;
    }

    /* Clean up of all local references we got */
  next_codec:
    if (name_str)
      (*env)->ReleaseStringUTFChars (env, name, name_str);
    name_str = NULL;
    J_DELETE_LOCAL_REF (name);
    J_DELETE_LOCAL_REF (supported_types);
    J_DELETE_LOCAL_REF (codec_info);
    J_DELETE_LOCAL_REF (codec_info_class);
    if (gst_codec_info) {
      gint j;

      for (j = 0; j < gst_codec_info->n_supported_types; j++) {
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[j];

        g_free (gst_codec_type->mime);
        g_free (gst_codec_type->color_formats);
        g_free (gst_codec_type->profile_levels);
        g_free (gst_codec_type->features);

      }
      g_free (gst_codec_info->supported_types);
      g_free (gst_codec_info->name);
      g_free (gst_codec_info);
    }
    gst_codec_info = NULL;
    valid_codec = TRUE;
  }

  ret = codec_infos != NULL;
  //fixme: gst_jni_object_unref (env, codec_infos);

  /* If successful we store a cache of the codec information in
   * the registry. Otherwise we would always load all codecs during
   * plugin initialization which can take quite some time (because
   * of hardware) and also loads lots of shared libraries (which
   * number is limited by 64 in Android).
   */
  if (ret) {
    GstStructure *new_cache_data = gst_structure_empty_new ("gst-amc-cache");
    GList *l;
    GValue arr = { 0, };

    g_value_init (&arr, GST_TYPE_ARRAY);

    for (l = codec_infos; l; l = l->next) {
      GstAmcCodecInfo *gst_codec_info = l->data;
      GValue cv = { 0, };
      GstStructure *cs = gst_structure_empty_new ("gst-amc-codec");
      GValue starr = { 0, };
      gint i;

      gst_structure_set (cs, "name", G_TYPE_STRING, gst_codec_info->name,
          "is-encoder", G_TYPE_BOOLEAN, gst_codec_info->is_encoder, NULL);

      g_value_init (&starr, GST_TYPE_ARRAY);

      for (i = 0; i < gst_codec_info->n_supported_types; i++) {
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[i];
        GstStructure *sts = gst_structure_empty_new ("gst-amc-supported-type");
        GValue stv = { 0, };
        GValue tmparr = { 0, };
        GValue farr = { 0, };
        gint j;

        gst_structure_set (sts, "mime", G_TYPE_STRING, gst_codec_type->mime,
            NULL);

        g_value_init (&farr, GST_TYPE_ARRAY);

        g_value_init (&tmparr, GST_TYPE_ARRAY);
        for (j = 0; j < gst_codec_type->n_color_formats; j++) {
          GValue tmp = { 0, };

          g_value_init (&tmp, G_TYPE_INT);
          g_value_set_int (&tmp, gst_codec_type->color_formats[j]);
          gst_value_array_append_value (&tmparr, &tmp);
          g_value_unset (&tmp);
        }
        gst_structure_set_value (sts, "color-formats", &tmparr);
        g_value_unset (&tmparr);

        g_value_init (&tmparr, GST_TYPE_ARRAY);
        for (j = 0; j < gst_codec_type->n_profile_levels; j++) {
          GValue tmparr2 = { 0, };
          GValue tmp = { 0, };

          g_value_init (&tmparr2, GST_TYPE_ARRAY);
          g_value_init (&tmp, G_TYPE_INT);
          g_value_set_int (&tmp, gst_codec_type->profile_levels[j].profile);
          gst_value_array_append_value (&tmparr2, &tmp);
          g_value_set_int (&tmp, gst_codec_type->profile_levels[j].level);
          gst_value_array_append_value (&tmparr2, &tmp);
          gst_value_array_append_value (&tmparr, &tmparr2);
          g_value_unset (&tmp);
          g_value_unset (&tmparr2);
        }
        gst_structure_set_value (sts, "profile-levels", &tmparr);

        for (j = 0; j < gst_codec_type->n_features; j++) {
          GstAmcCodecFeature *feature = &gst_codec_type->features[j];
          GstStructure *fs = gst_structure_empty_new ("gst-amc-codec-feature");
          GValue fv = { 0, };

          gst_structure_set (fs, "name", G_TYPE_STRING, feature->name, NULL);

          gst_structure_set (fs, "supported", G_TYPE_BOOLEAN,
              feature->supported, NULL);

          gst_structure_set (fs, "required", G_TYPE_BOOLEAN,
              feature->required, NULL);

          g_value_init (&fv, GST_TYPE_STRUCTURE);
          gst_value_set_structure (&fv, fs);
          gst_value_array_append_value (&farr, &fv);
          gst_structure_free (fs);
        }

        gst_structure_set_value (sts, "features", &farr);
        g_value_unset (&farr);

        g_value_init (&stv, GST_TYPE_STRUCTURE);
        gst_value_set_structure (&stv, sts);
        gst_value_array_append_value (&starr, &stv);
        g_value_unset (&tmparr);
        gst_structure_free (sts);
      }

      gst_structure_set_value (cs, "supported-types", &starr);
      g_value_unset (&starr);

      g_value_init (&cv, GST_TYPE_STRUCTURE);
      gst_value_set_structure (&cv, cs);
      gst_value_array_append_value (&arr, &cv);
      g_value_unset (&cv);
      gst_structure_free (cs);
    }

    gst_structure_set_value (new_cache_data, "codecs", &arr);
    g_value_unset (&arr);

    save_codecs (plugin, new_cache_data);
  }

  return TRUE;
}

static const struct
{
  gint color_format;
  GstVideoFormat video_format;
} color_format_mapping_table[] = {
  {
  COLOR_FormatSurface1, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface2, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface3, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface4, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface5, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface6, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface7, GST_VIDEO_FORMAT_ENCODED}, {
  HAL_PIXEL_FORMAT_YCrCb_420_SP, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatYUV420Planar, GST_VIDEO_FORMAT_I420}, {
  COLOR_FormatYUV420Flexible, GST_VIDEO_FORMAT_I420}, {
  COLOR_FormatYUV420SemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_FormatYUV411PackedPlanar, GST_VIDEO_FORMAT_IYU1,}, {
  COLOR_TI_FormatYUV420PackedSemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYUV420SemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYVU420SemiPlanar32m, GST_VIDEO_FORMAT_NV12}
};

static gboolean
accepted_color_formats (GstAmcCodecType * type, gboolean is_encoder)
{
  gint i, j;
  gint accepted = 0, all = type->n_color_formats;

  for (i = 0; i < type->n_color_formats; i++) {
    gboolean found = FALSE;

    /* We ignore this one */
    if (type->color_formats[i] == COLOR_FormatAndroidOpaque)
      all--;
    if (type->color_formats[i] == COLOR_FormatAndroidUndocumented1)
      all--;

    for (j = 0; j < G_N_ELEMENTS (color_format_mapping_table); j++) {
      if (color_format_mapping_table[j].color_format == type->color_formats[i]) {
        found = TRUE;
        break;
      }
    }

    if (found)
      accepted++;
  }

  if (is_encoder)
    return accepted > 0;
  else
    return accepted == all && all > 0;
}

GstVideoFormat
gst_amc_color_format_to_video_format (gint color_format)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (color_format_mapping_table); i++) {
    if (color_format_mapping_table[i].color_format == color_format)
      return color_format_mapping_table[i].video_format;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}


const gchar *
gst_amc_hevc_profile_to_string (gint profile)
{
  static const struct
  {
    gint id;
    const gchar *str;
  } hevc_profile_mapping_table[] = {
    {
    HEVCProfileMain, "main"}, {
    HEVCProfileMain10, "main-10"}, {
    HEVCProfileMain10HDR10, "main-10-hdr10"}    /* <-- this caps not exist in h265parse */
  };


  gint i;
  for (i = 0; i < G_N_ELEMENTS (hevc_profile_mapping_table); i++)
    if (hevc_profile_mapping_table[i].id == profile)
      return hevc_profile_mapping_table[i].str;
  return NULL;
}


gboolean
gst_amc_hevc_level_to_string (gint id, const gchar ** level,
    const gchar ** tier)
{
  static const struct
  {
    gint id;
    const gchar *tier, *level;
  } hevc_level_mapping_table[] = {
    {
    HEVCMainTierLevel1, "1", "main"}, {
    HEVCHighTierLevel1, "1", "high"}, {
    HEVCMainTierLevel2, "2", "main"}, {
    HEVCHighTierLevel2, "2", "high"}, {
    HEVCMainTierLevel21, "2.1", "main"}, {
    HEVCHighTierLevel21, "2.1", "high"}, {
    HEVCMainTierLevel3, "3", "main"}, {
    HEVCHighTierLevel3, "3", "high"}, {
    HEVCMainTierLevel31, "3.1", "main"}, {
    HEVCHighTierLevel31, "3.1", "high"}, {
    HEVCMainTierLevel4, "4", "main"}, {
    HEVCHighTierLevel4, "4", "high"}, {
    HEVCMainTierLevel41, "4.1", "main"}, {
    HEVCHighTierLevel41, "4.1", "high"}, {
    HEVCMainTierLevel5, "5", "main"}, {
    HEVCHighTierLevel5, "5", "high"}, {
    HEVCMainTierLevel51, "5.1", "main"}, {
    HEVCHighTierLevel51, "5.1", "high"}, {
    HEVCMainTierLevel52, "5.2", "main"}, {
    HEVCHighTierLevel52, "5.2", "high"}, {
    HEVCMainTierLevel6, "6", "main"}, {
    HEVCHighTierLevel6, "6", "high"}, {
    HEVCMainTierLevel61, "6.1", "main"}, {
    HEVCHighTierLevel61, "6.1", "high"}, {
    HEVCMainTierLevel62, "6.2", "main"}, {
    HEVCHighTierLevel62, "6.2", "high"}
  };

  gint i;

  for (i = 0; i < G_N_ELEMENTS (hevc_level_mapping_table); i++)
    if (hevc_level_mapping_table[i].id == id) {
      *level = hevc_level_mapping_table[i].level;
      *tier = hevc_level_mapping_table[i].tier;
      return TRUE;
    }

  return FALSE;
}


static const struct
{
  gint id;
  const gchar *str;
  const gchar *alt_str;
} avc_profile_mapping_table[] = {
  {
  AVCProfileBaseline, "baseline", "constrained-baseline"}, {
  AVCProfileMain, "main", NULL}, {
  AVCProfileExtended, "extended", NULL}, {
  AVCProfileHigh, "high"}, {
  AVCProfileHigh10, "high-10", "high-10-intra"}, {
  AVCProfileHigh422, "high-4:2:2", "high-4:2:2-intra"}, {
  AVCProfileHigh444, "high-4:4:4", "high-4:4:4-intra"}
};

const gchar *
gst_amc_avc_profile_to_string (gint profile, const gchar ** alternative)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (avc_profile_mapping_table); i++) {
    if (avc_profile_mapping_table[i].id == profile) {
      *alternative = avc_profile_mapping_table[i].alt_str;
      return avc_profile_mapping_table[i].str;
    }
  }

  return NULL;
}


static const struct
{
  gint id;
  const gchar *str;
} avc_level_mapping_table[] = {
  {
  AVCLevel1, "1"}, {
  AVCLevel1b, "1b"}, {
  AVCLevel11, "1.1"}, {
  AVCLevel12, "1.2"}, {
  AVCLevel13, "1.3"}, {
  AVCLevel2, "2"}, {
  AVCLevel21, "2.1"}, {
  AVCLevel22, "2.2"}, {
  AVCLevel3, "3"}, {
  AVCLevel31, "3.1"}, {
  AVCLevel32, "3.2"}, {
  AVCLevel4, "4"}, {
  AVCLevel41, "4.1"}, {
  AVCLevel42, "4.2"}, {
  AVCLevel5, "5"}, {
  AVCLevel51, "5.1"}
};

const gchar *
gst_amc_avc_level_to_string (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (avc_level_mapping_table); i++) {
    if (avc_level_mapping_table[i].id == level)
      return avc_level_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  gint id;
  gint gst_id;
} h263_profile_mapping_table[] = {
  {
  H263ProfileBaseline, 0}, {
  H263ProfileH320Coding, 1}, {
  H263ProfileBackwardCompatible, 2}, {
  H263ProfileISWV2, 3}, {
  H263ProfileISWV3, 4}, {
  H263ProfileHighCompression, 5}, {
  H263ProfileInternet, 6}, {
  H263ProfileInterlace, 7}, {
  H263ProfileHighLatency, 8}
};

gint
gst_amc_h263_profile_to_gst_id (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_profile_mapping_table); i++) {
    if (h263_profile_mapping_table[i].id == profile)
      return h263_profile_mapping_table[i].gst_id;
  }

  return -1;
}


static const struct
{
  gint id;
  gint gst_id;
} h263_level_mapping_table[] = {
  {
  H263Level10, 10}, {
  H263Level20, 20}, {
  H263Level30, 30}, {
  H263Level40, 40}, {
  H263Level50, 50}, {
  H263Level60, 60}, {
  H263Level70, 70}
};

gint
gst_amc_h263_level_to_gst_id (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_level_mapping_table); i++) {
    if (h263_level_mapping_table[i].id == level)
      return h263_level_mapping_table[i].gst_id;
  }

  return -1;
}


static const struct
{
  gint id;
  const gchar *str;
} mpeg4_profile_mapping_table[] = {
  {
  MPEG4ProfileSimple, "simple"}, {
  MPEG4ProfileSimpleScalable, "simple-scalable"}, {
  MPEG4ProfileCore, "core"}, {
  MPEG4ProfileMain, "main"}, {
  MPEG4ProfileNbit, "n-bit"}, {
  MPEG4ProfileScalableTexture, "scalable"}, {
  MPEG4ProfileSimpleFace, "simple-face"}, {
  MPEG4ProfileSimpleFBA, "simple-fba"}, {
  MPEG4ProfileBasicAnimated, "basic-animated-texture"}, {
  MPEG4ProfileHybrid, "hybrid"}, {
  MPEG4ProfileAdvancedRealTime, "advanced-real-time"}, {
  MPEG4ProfileCoreScalable, "core-scalable"}, {
  MPEG4ProfileAdvancedCoding, "advanced-coding-efficiency"}, {
  MPEG4ProfileAdvancedCore, "advanced-core"}, {
  MPEG4ProfileAdvancedScalable, "advanced-scalable-texture"}, {
  MPEG4ProfileAdvancedSimple, "advanced-simple"}
};

const gchar *
gst_amc_mpeg4_profile_to_string (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (mpeg4_profile_mapping_table); i++) {
    if (mpeg4_profile_mapping_table[i].id == profile)
      return mpeg4_profile_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  gint id;
  const gchar *str;
} mpeg4_level_mapping_table[] = {
  {
  MPEG4Level0, "0"}, {
  MPEG4Level0b, "0b"}, {
  MPEG4Level1, "1"}, {
  MPEG4Level2, "2"}, {
  MPEG4Level3, "3"}, {
  MPEG4Level4, "4"}, {
  MPEG4Level4a, "4a"}, {
MPEG4Level5, "5"},};

const gchar *
gst_amc_mpeg4_level_to_string (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (mpeg4_level_mapping_table); i++) {
    if (mpeg4_level_mapping_table[i].id == level)
      return mpeg4_level_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  gint id;
  const gchar *str;
} aac_profile_mapping_table[] = {
  {
  AACObjectMain, "main"}, {
  AACObjectLC, "lc"}, {
  AACObjectSSR, "ssr"}, {
  AACObjectLTP, "ltp"}
};

const gchar *
gst_amc_aac_profile_to_string (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (aac_profile_mapping_table); i++) {
    if (aac_profile_mapping_table[i].id == profile)
      return aac_profile_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  guint32 mask;
  GstAudioChannelPosition pos;
} channel_mapping_table[] = {
  {
  CHANNEL_OUT_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  CHANNEL_OUT_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
  CHANNEL_OUT_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
  CHANNEL_OUT_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE}, {
  CHANNEL_OUT_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
  CHANNEL_OUT_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
  CHANNEL_OUT_FRONT_LEFT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
  CHANNEL_OUT_FRONT_RIGHT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
  CHANNEL_OUT_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
  CHANNEL_OUT_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
  CHANNEL_OUT_SIDE_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
  CHANNEL_OUT_TOP_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_INVALID}
};

GstAudioChannelPosition *
gst_amc_audio_channel_mask_to_positions (guint32 channel_mask, gint channels)
{
  GstAudioChannelPosition *pos = g_new0 (GstAudioChannelPosition, channels);
  gint i, j;

  if (channel_mask == 0) {
    if (channels == 1) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
      return pos;
    }
    if (channels == 2) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      return pos;
    }

    /* Now let the guesswork begin, these are the
     * AAC default channel assignments for these numbers
     * of channels */
    if (channels == 3) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER;
    } else if (channels == 4) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_CENTER;
    } else if (channels == 5) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT;
    } else if (channels == 6) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT | CHANNEL_OUT_LOW_FREQUENCY;
    } else if (channels == 8) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT | CHANNEL_OUT_LOW_FREQUENCY |
          CHANNEL_OUT_FRONT_LEFT_OF_CENTER | CHANNEL_OUT_FRONT_RIGHT_OF_CENTER;
    }
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (channel_mapping_table); i++) {
    if ((channel_mask & channel_mapping_table[i].mask)) {
      pos[j++] = channel_mapping_table[i].pos;
      if (channel_mapping_table[i].pos == GST_AUDIO_CHANNEL_POSITION_INVALID) {
        g_free (pos);
        GST_ERROR ("Unable to map channel mask 0x%08x",
            channel_mapping_table[i].mask);
        return NULL;
      }
      if (j == channels)
        break;
    }
  }

  if (j != channels) {
    g_free (pos);
    GST_ERROR ("Unable to map all channel positions in mask 0x%08x",
        channel_mask);
    return NULL;
  }

  return pos;
}


static gchar *
create_type_name (const gchar * parent_name, const gchar * codec_name,
    const gchar * mime_name)
{
  gchar *typified_name;
  gint i, k;
  gint parent_name_len = strlen (parent_name);
  gint codec_name_len = strlen (codec_name);
  gint mime_name_len = strlen (mime_name);
  gint typified_name_len = 0;
  gboolean upper = TRUE;

  /* Calculate total len skipping non-alnum */
  typified_name_len =
      parent_name_len + 1 + codec_name_len + 1 + mime_name_len + 1;

  for (i = 0; i < codec_name_len; i++)
    if (!g_ascii_isalnum (codec_name[i]))
      typified_name_len--;

  for (i = 0; i < mime_name_len; i++)
    if (!g_ascii_isalnum (mime_name[i]))
      typified_name_len--;

  typified_name = g_new0 (gchar, typified_name_len);
  memcpy (typified_name, parent_name, parent_name_len);
  typified_name[parent_name_len] = '-';

  for (i = 0, k = parent_name_len + 1; i < codec_name_len; i++) {
    if (g_ascii_isalnum (codec_name[i])) {
      if (upper)
        typified_name[k++] = g_ascii_toupper (codec_name[i]);
      else
        typified_name[k++] = g_ascii_tolower (codec_name[i]);

      upper = FALSE;
    } else {
      /* Skip all non-alnum chars and start a new upper case word */
      upper = TRUE;
    }
  }

  upper = TRUE;
  typified_name[k++] = '-';
  for (i = 0; i < mime_name_len; i++) {
    if (g_ascii_isalnum (mime_name[i])) {
      if (upper)
        typified_name[k++] = g_ascii_toupper (mime_name[i]);
      else
        typified_name[k++] = g_ascii_tolower (mime_name[i]);

      upper = FALSE;
    } else {
      upper = TRUE;
    }
  }

  return typified_name;
}

static gchar *
create_element_name (gboolean video, gboolean encoder, const gchar * codec_name,
    const gchar * mime_name)
{
#define PREFIX_LEN 10
  static const gchar *prefixes[] = {
    "amcviddec-",
    "amcauddec-",
    "amcvidenc-",
    "amcaudenc-"
  };
  gchar *element_name;
  gint i, k;
  gint codec_name_len = strlen (codec_name);
  gint mime_name_len = strlen (mime_name);
  gint element_name_len = 0;
  const gchar *prefix;

  if (video && !encoder)
    prefix = prefixes[0];
  else if (!video && !encoder)
    prefix = prefixes[1];
  else if (video && encoder)
    prefix = prefixes[2];
  else
    prefix = prefixes[3];

  element_name_len = PREFIX_LEN + codec_name_len + mime_name_len + 2;

  for (i = 0; i < codec_name_len; i++)
    if (!g_ascii_isalnum (codec_name[i]))
      element_name_len--;

  for (i = 0; i < mime_name_len; i++)
    if (!g_ascii_isalnum (mime_name[i]))
      element_name_len--;

  element_name = g_new0 (gchar, element_name_len);
  memcpy (element_name, prefix, PREFIX_LEN);

  for (i = 0, k = PREFIX_LEN; i < codec_name_len; i++) {
    if (g_ascii_isalnum (codec_name[i])) {
      element_name[k++] = g_ascii_tolower (codec_name[i]);
    }
    /* Skip all non-alnum chars */
  }

  element_name[k++] = '-';
  for (i = 0; i < mime_name_len; i++) {
    if (g_ascii_isalnum (mime_name[i])) {
      element_name[k++] = g_ascii_tolower (mime_name[i]);
    }
  }

  element_name[k] = '\0';

  return element_name;
}

#undef PREFIX_LEN

static gboolean
register_codecs (GstPlugin * plugin)
{
  gboolean ret = TRUE;
  GList *l;
  gint i;

  GST_DEBUG ("Registering plugins");

  for (l = codec_infos; l; l = l->next) {
    GstAmcCodecInfo *codec_info = l->data;

    for (i = 0; i < codec_info->n_supported_types; i++) {
      GstAmcCodecType *codec_type = &codec_info->supported_types[i];
      GstAmcRegisteredCodec *registered_codec;
      gboolean is_audio = FALSE;
      gboolean is_video = FALSE;

      GTypeQuery type_query;
      GTypeInfo type_info = { 0, };
      GType type, subtype;
      gchar *type_name, *element_name;
      guint rank;

      GST_DEBUG ("Registering codec '%s' with mime type %s", codec_info->name,
          codec_type->mime);

      if (g_str_has_prefix (codec_type->mime, "audio/"))
        is_audio = TRUE;
      else if (g_str_has_prefix (codec_type->mime, "video/"))
        is_video = TRUE;

      if (is_video && !codec_info->is_encoder) {
        type = gst_amc_video_dec_get_type ();
      } else if (is_audio && !codec_info->is_encoder) {
        type = gst_amc_audio_dec_get_type ();
      } else {
        GST_DEBUG ("Skipping unsupported codec type");
        continue;
      }

      g_type_query (type, &type_query);
      memset (&type_info, 0, sizeof (type_info));
      type_info.class_size = type_query.class_size;
      type_info.instance_size = type_query.instance_size;
      type_name =
          create_type_name (type_query.type_name, codec_info->name,
          codec_type->mime);

      if (g_type_from_name (type_name) != G_TYPE_INVALID) {
        GST_ERROR ("Type '%s' already exists for codec '%s' with mime %s",
            type_name, codec_info->name, codec_type->mime);
        g_free (type_name);
        continue;
      }

      registered_codec = g_new0 (GstAmcRegisteredCodec, 1);
      registered_codec->codec_info = codec_info;
      registered_codec->codec_type = codec_type;
      registered_codecs = g_list_append (registered_codecs, registered_codec);

      if (is_video && !codec_info->is_encoder) {
        /* subtype class_init will set the features in regsitered_codec properties */
        subtype =
            g_type_register_static_simple (type, type_name,
            type_query.class_size, gst_amc_video_dec_dynamic_class_init,
            type_query.instance_size, NULL, 0);
      } else {
        subtype = g_type_register_static (type, type_name, &type_info, 0);
      }

      g_free (type_name);

      g_type_set_qdata (subtype, gst_amc_codec_info_quark, registered_codec);

      element_name =
          create_element_name (is_video, codec_info->is_encoder,
          codec_info->name, codec_type->mime);

      /* Give the Google software codec a secondary rank,
       * everything else is likely a hardware codec, except
       * OMX.SEC.*.sw.dec (as seen in Galaxy S4) */
      if (g_str_has_prefix (codec_info->name, "OMX.google") ||
          g_str_has_suffix (codec_info->name, ".sw.dec"))
        rank = GST_RANK_SECONDARY;
      else
        rank = GST_RANK_PRIMARY;

      ret |= gst_element_register (plugin, element_name, rank, subtype);
      g_free (element_name);
    }
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  const gchar *ignore;

  GST_DEBUG_CATEGORY_INIT (gst_amc_debug, "amc", 0, "android-media-codec");

  if (!gst_jni_initialize (NULL))
    return FALSE;

  gst_plugin_add_dependency_simple (plugin, NULL, "/etc", "media_codecs.xml",
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  if (!get_java_classes ())
    return FALSE;

  /* Set this to TRUE to allow registering decoders that have
   * any unknown color formats, or encoders that only have
   * unknown color formats
   */
  ignore = g_getenv ("GST_AMC_IGNORE_UNKNOWN_COLOR_FORMATS");
  if (ignore && strcmp (ignore, "yes") == 0)
    ignore_unknown_color_formats = TRUE;

  if (!scan_codecs (plugin))
    return FALSE;

  gst_amc_codec_info_quark = g_quark_from_static_string ("gst-amc-codec-info");

  gst_amc_drm_log_known_supported_protection_schemes ();

  if (!register_codecs (plugin))
    return FALSE;

  if (!gst_element_register (plugin, "amcvideosink", GST_RANK_PRIMARY,
          GST_TYPE_AMC_VIDEO_SINK)) {
    return FALSE;
  };

  if (!gst_element_register (plugin, "audiotracksink", GST_RANK_SECONDARY,
          GST_TYPE_AUDIOTRACK_SINK)) {
    return FALSE;
  };

  return TRUE;
}

GstAmcDRBuffer *
gst_amc_dr_buffer_new (GstAmcCodec * codec, guint idx)
{
  GstAmcDRBuffer *buf;

  buf = g_new0 (GstAmcDRBuffer, 1);
  buf->codec = gst_amc_codec_ref (codec);
  /* No need to lock because we are sure we don't flush at this moment. */
  buf->flush_id = codec->flush_id;
  buf->idx = idx;
  buf->released = FALSE;

  return buf;
}

gboolean
gst_amc_dr_buffer_render (GstAmcDRBuffer * buf, GstClockTime ts)
{
  gboolean ret = FALSE;

  if (!buf->released) {
    g_mutex_lock (&buf->codec->buffers_lock);
    if (buf->codec->flush_id == buf->flush_id)
      ret = gst_amc_codec_render_output_buffer (buf->codec, buf->idx, ts);
    g_mutex_unlock (&buf->codec->buffers_lock);
    buf->released = TRUE;
  }

  return ret;
}

void
gst_amc_dr_buffer_free (GstAmcDRBuffer * buf)
{
  GST_TRACE ("freeing buffer idx %d of codec %p", buf->idx, buf->codec);
  if (!buf->released) {
    g_mutex_lock (&buf->codec->buffers_lock);
    if (buf->codec->flush_id == buf->flush_id)
      gst_amc_codec_release_output_buffer (buf->codec, buf->idx);
    g_mutex_unlock (&buf->codec->buffers_lock);
  }

  gst_amc_codec_unref (buf->codec);
  g_free (buf);
}

GstQuery *
gst_amc_query_new_surface (void)
{
  GstQuery *query;
  GstQueryType qtype;
  GstStructure *structure;

  query = (GstQuery *) gst_mini_object_new (GST_TYPE_QUERY);

  qtype = gst_query_type_get_by_nick (GST_AMC_SURFACE);
  if (qtype == GST_QUERY_NONE) {
    qtype = gst_query_type_register (GST_AMC_SURFACE,
        "Queries for a surface to render");
  }
  query->type = qtype;
  GST_DEBUG ("creating new query %p %s", query,
      gst_query_type_get_name (qtype));

  structure = gst_structure_id_new (GST_QUARK (GST_AMC_SURFACE),
      GST_QUARK (GST_AMC_SURFACE_POINTER), G_TYPE_POINTER, NULL, NULL);
  query->structure = structure;
  gst_structure_set_parent_refcount (query->structure,
      &query->mini_object.refcount);

  return query;
}

gpointer
gst_amc_query_parse_surface (GstQuery * query)
{
  GstQueryType qtype = gst_query_type_get_by_nick (GST_AMC_SURFACE);

  if (GST_QUERY_TYPE (query) != qtype)
    return NULL;

  return g_value_get_pointer (gst_structure_id_get_value (query->structure,
          GST_QUARK (GST_AMC_SURFACE_POINTER)));
}

gboolean
gst_amc_query_set_surface (GstQuery * query, gpointer surface)
{
  GstQueryType qtype;

  qtype = gst_query_type_get_by_nick (GST_AMC_SURFACE);
  if (GST_QUERY_TYPE (query) != qtype)
    return FALSE;

  gst_structure_id_set (query->structure,
      GST_QUARK (GST_AMC_SURFACE_POINTER), G_TYPE_POINTER, surface, NULL);
  return TRUE;
}

gboolean
gst_amc_event_is_surface (GstEvent * event)
{
  return gst_event_has_name (event, GST_AMC_SURFACE);
}

GstEvent *
gst_amc_event_new_surface (gpointer surface)
{
  GstEvent *event;

  event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
      gst_structure_id_new (GST_QUARK (GST_AMC_SURFACE),
          GST_QUARK (GST_AMC_SURFACE_POINTER), G_TYPE_POINTER, surface, NULL));
  return event;
}

gpointer
gst_amc_event_parse_surface (GstEvent * event)
{
  return
      g_value_get_pointer (gst_structure_id_get_value (gst_event_get_structure
          (event), GST_QUARK (GST_AMC_SURFACE_POINTER)));
}


#ifdef GST_PLUGIN_DEFINE2
GST_PLUGIN_DEFINE2 (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    androidmedia,
    "Android Media plugin",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#else
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "androidmedia",
    "Android Media plugin",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#endif
