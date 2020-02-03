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
#include <gst/androidjni/gstjnimediaformat.h>
#include <gst/androidjni/gstjnimediacodeclist.h>
#include "gstjniamcutils.h"

const gchar *
gst_jni_amc_video_caps_to_mime (const GstCaps * caps)
{
  GstStructure *s;
  const gchar *name;

  s = gst_caps_get_structure (caps, 0);
  if (!s)
    return NULL;

  name = gst_structure_get_name (s);

  if (strcmp (name, "video/mpeg") == 0) {
    gint mpegversion;

    if (!gst_structure_get_int (s, "mpegversion", &mpegversion))
      return NULL;

    if (mpegversion == 4)
      return "video/mp4v-es";
    else if (mpegversion == 1 || mpegversion == 2)
      return "video/mpeg2";
  } else if (strcmp (name, "video/x-h263") == 0) {
    return "video/3gpp";
  } else if (strcmp (name, "video/x-h264") == 0) {
    return "video/avc";
  } else if (strcmp (name, "video/x-h265") == 0) {
    return "video/hevc";
  } else if (strcmp (name, "video/x-vp8") == 0) {
    return "video/x-vnd.on2.vp8";
  } else if (strcmp (name, "video/x-divx") == 0) {
    return "video/mp4v-es";
  } else if (strcmp (name, "video/x-xvid") == 0) {
    return "video/mp4v-es";
  } else if (strcmp (name, "video/x-3ivx") == 0) {
    return "video/mp4v-es";
  }

  return NULL;
}

gchar *
gst_jni_amc_get_tunneled_playback_decoder_name (const GstCaps * caps,
    gint width, gint height)
{
  GstAmcFormat *format = NULL;
  GstJniMediaCodecList *codec_list = NULL;
  gchar *codec_name = NULL;

  format = gst_amc_format_new_video (gst_jni_amc_video_caps_to_mime (caps),
      width, height);
  if (format == NULL) {
    GST_ERROR ("Could not create format");
    goto done;
  }
  gst_amc_format_set_feature_enabled (format,
      GST_AMC_MEDIA_FORMAT_TUNNELED_PLAYBACK, TRUE);

  codec_list = gst_jni_media_codec_list_new ();
  if (codec_list == NULL) {
    GST_ERROR ("Could not get codec list");
    goto done;
  }
  codec_name =
      gst_jni_media_codec_list_find_decoder_for_format (codec_list, format);

done:
  if (format != NULL) {
    gst_amc_format_free (format);
  }
  if (codec_list != NULL) {
    gst_jni_media_codec_list_free (codec_list);
  }
  return codec_name;
}


gchar *
gst_jni_amc_decoder_to_gst_plugin_name (gchar * codec_name)
{
#define PREFIX_LEN 10
  gchar *element_name;
  gint i, k;
  gint codec_name_len;
  const gchar *prefix = "amcviddec-";

  // This is copied from gstamc.c to get the element name from the codec name
  codec_name_len = strlen (codec_name);
  element_name = g_new0 (gchar, PREFIX_LEN + strlen (codec_name) + 1);
  memcpy (element_name, prefix, PREFIX_LEN);

  for (i = 0, k = 0; i < codec_name_len; i++) {
    if (g_ascii_isalnum (codec_name[i])) {
      element_name[PREFIX_LEN + k++] = g_ascii_tolower (codec_name[i]);
    }
    /* Skip all non-alnum chars */
  }
#undef PREFIX_LEN

  return element_name;
}

GList *
gst_jni_amc_get_decoders_with_feature (const GstCaps * caps,
    const gchar * feature)
{
  GList *list_ret = NULL;
  GstJniMediaCodecList *codec_list = NULL;

  jobjectArray jcodec_infos;
  jint codec_count, i;

  jobject codec_info = NULL;
  jclass codec_info_class = NULL;
  jmethodID get_capabilities_for_type_id;
  jmethodID get_name_id;
  jmethodID is_encoder_id;

  jobject capabilities = NULL;
  jclass capabilities_class = NULL;
  jmethodID is_feature_supported;

  jstring feature_jstring = NULL;
  jstring type_jstring = NULL;

  const gchar *type = gst_jni_amc_video_caps_to_mime (caps);
  JNIEnv *env = gst_jni_get_env ();

  codec_info_class = (*env)->FindClass (env, "android/media/MediaCodecInfo");
  if (!codec_info_class) {
    GST_ERROR ("Can't find android/media/MediaCodecInfo class");
    goto next_codec;
  }

  capabilities_class =
      (*env)->FindClass (env, "android/media/MediaCodecInfo$CodecCapabilities");
  if (!capabilities_class) {
    GST_ERROR
        ("Can't find android/media/MediaCodecInfo$CodecCapabilities class");
    goto next_codec;
  }
  get_capabilities_for_type_id =
      (*env)->GetMethodID (env, codec_info_class, "getCapabilitiesForType",
      "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");

  get_name_id =
      (*env)->GetMethodID (env, codec_info_class, "getName",
      "()Ljava/lang/String;");

  is_encoder_id =
      (*env)->GetMethodID (env, codec_info_class, "isEncoder", "()Z");

  is_feature_supported =
      (*env)->GetMethodID (env, capabilities_class, "isFeatureSupported",
      "(Ljava/lang/String;)Z");

  GST_ERROR
      ("methods: get_capabilities_for_type_id %d\tget_name_id %d\tis_encoder_id %d\tis_feature_supported %d",
      get_capabilities_for_type_id, get_name_id, is_encoder_id,
      is_feature_supported);

  if (!is_feature_supported || !get_capabilities_for_type_id || !get_name_id
      || !is_encoder_id) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec info method IDs");
    goto done;
  }

  type_jstring = (*env)->NewStringUTF (env, type);
  feature_jstring = (*env)->NewStringUTF (env, feature);

  codec_list = gst_jni_media_codec_list_new ();
  if (codec_list == NULL) {
    GST_ERROR ("Could not get codec list");
    goto done;
  }

  jcodec_infos = gst_jni_media_codec_list_get_codec_infos (codec_list);
  codec_count = (*env)->GetArrayLength (env, jcodec_infos);

  for (i = 0; i < codec_count; i++) {
    jobject codec_info = NULL;
    gboolean supported = FALSE;
    jstring name_jstring = NULL;
    const gchar *name = NULL;

    codec_info = (*env)->GetObjectArrayElement (env, jcodec_infos, i);
    if ((*env)->ExceptionCheck (env) || !codec_info) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info %d", i);
      goto next_codec;
    }

    name_jstring = (*env)->CallObjectMethod (env, codec_info, get_name_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec name");
      goto next_codec;
    }
    name = (*env)->GetStringUTFChars (env, name_jstring, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to convert codec name to UTF8");
      goto next_codec;
    }


    if ((*env)->CallBooleanMethod (env, codec_info, is_encoder_id)) {
      GST_ERROR ("Not a decoder %s", name);
      goto next_codec;
    }

    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to detect if codec is an encoder %s", name);
      goto next_codec;
    }

    capabilities =
        (*env)->CallObjectMethod (env, codec_info,
        get_capabilities_for_type_id, type_jstring);

    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get capabilities %s for %s", type, name);
      goto next_codec;
    }

    if (!capabilities) {
      GST_ERROR ("Can't find capabilities for %s", name);
      goto next_codec;
    }

    GST_ERROR ("Checking %s for codec %s", feature, name);
    supported = (*env)->CallBooleanMethod (env, capabilities,
        is_feature_supported, feature_jstring);

    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get feature supported");
      goto next_codec;
    }

    if (!supported)
      goto next_codec;

    GST_ERROR ("Adding codec to the %s list %s", feature, name);
    list_ret =
        g_list_append (list_ret, gst_jni_amc_decoder_to_gst_plugin_name (name));

  next_codec:
    J_DELETE_LOCAL_REF (capabilities);
    (*env)->ReleaseStringUTFChars (env, name_jstring, name);
    J_DELETE_LOCAL_REF (name_jstring);
  }

done:
  J_DELETE_LOCAL_REF (codec_info_class);
  J_DELETE_LOCAL_REF (capabilities_class);
  J_DELETE_LOCAL_REF (type_jstring);
  J_DELETE_LOCAL_REF (feature_jstring);
  if (codec_list != NULL) {
    gst_jni_media_codec_list_free (codec_list);
  }
  return list_ret;
}
