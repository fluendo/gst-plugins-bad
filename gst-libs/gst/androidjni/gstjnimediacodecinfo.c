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
#include <gst/androidjni/gstjnimediacodec.h>
#include <gst/androidjni/gstjnimediaformat.h>
#include "gstjnimediacodecinfo.h"

static gboolean _initialized = FALSE;

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

gboolean
gst_jni_media_codec_info_init (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;

  if (_initialized) {
    return TRUE;
  }

  env = gst_jni_get_env ();
  media_codec_info.klass =
      gst_jni_get_class (env, "android/media/MediaCodecInfo");

  J_INIT_METHOD_ID (media_codec_info, get_capabilities_for_type,
      "getCapabilitiesForType",
      "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");

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

done:
  _initialized = ret;
  return ret;
error:
  ret = FALSE;
  GST_ERROR ("Could not initialize android/media/MediaCodecList");
  goto done;
}

gboolean
gst_jni_media_codec_info_get_capabilities_for_type (jobject capabilities,
    jobject codec_info, GstAmcFormat * format)
{
  jstring jtmpstr = NULL;
  JNIEnv *env;

  env = gst_jni_get_env ();

  AMC_CHK (gst_amc_format_get_jstring (format, "mime", &jtmpstr));

  J_CALL_OBJ (capabilities /* = */ , codec_info,
      media_codec_info.get_capabilities_for_type, jtmpstr);
  J_DELETE_LOCAL_REF (jtmpstr);

  return TRUE;
error:
  J_DELETE_LOCAL_REF (jtmpstr);
  return FALSE;
}

gboolean
gst_jni_media_codec_info_is_size_supported (jboolean * supported,
    jobject video_caps, jint max_height, jint max_width)
{
  JNIEnv *env;
  env = gst_jni_get_env ();

  J_CALL_BOOL (*supported /* = */ , video_caps,
      media_codec_info.video_caps.is_size_supported, max_width, max_height);

  return TRUE;
error:
  return FALSE;
}


gboolean
gst_jni_media_codec_info_get_supported_heights (jobject heights,
    jobject video_caps)
{
  JNIEnv *env;
  env = gst_jni_get_env ();

  J_CALL_OBJ (heights /* = */ , video_caps,
      media_codec_info.video_caps.get_supported_heights);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_info_get_supported_widths_for (jobject widths,
    jobject video_caps, jint max_height)
{
  JNIEnv *env;
  env = gst_jni_get_env ();

  J_CALL_OBJ (widths /* = */ , video_caps,
      media_codec_info.video_caps.get_supported_widths_for, max_height);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_jni_media_codec_info_video_caps_supported (void)
{
  return media_codec_info.video_caps.klass ? TRUE : FALSE;
}
