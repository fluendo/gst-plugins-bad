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
#include "gstjnimediacodeclist.h"

static gboolean _initialized = FALSE;

static struct
{
  jclass klass;
  jmethodID constructor;
  jmethodID find_decoder_for_format;
  jmethodID get_codec_infos;
} media_codec_list;


#define MEDIA_CODEC_LIST_REGULAR_CODECS   0
#define MEDIA_CODEC_LIST_ALL_CODECS       1

gboolean
gst_jni_media_codec_list_init (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;

  if (_initialized) {
    return TRUE;
  }

  env = gst_jni_get_env ();

  jobject tmp = (*env)->FindClass (env, "android/media/MediaCodecList");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format class");
    goto done;
  }
  media_codec_list.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_codec_list.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format class global reference");
    goto done;
  }

  J_INIT_METHOD_ID (media_codec_list, constructor, "<init>", "(I)V");
  J_INIT_METHOD_ID (media_codec_list, find_decoder_for_format,
      "findDecoderForFormat",
      "(Landroid/media/MediaFormat;)Ljava/lang/String;");
  J_INIT_METHOD_ID (media_codec_list, get_codec_infos,
      "getCodecInfos", "()[Landroid/media/MediaCodecInfo;");

done:
  _initialized = ret;
  return ret;
error:
  ret = FALSE;
  GST_ERROR ("Could not initialize android/media/MediaCodecList");
  goto done;
}

GstJniMediaCodecList *
gst_jni_media_codec_list_new (void)
{
  GstJniMediaCodecList *codec_list_obj = NULL;

  JNIEnv *env = gst_jni_get_env ();

  codec_list_obj = g_slice_new0 (GstJniMediaCodecList);
  codec_list_obj->object = gst_jni_new_object (env, media_codec_list.klass,
      media_codec_list.constructor, MEDIA_CODEC_LIST_ALL_CODECS);
  if (codec_list_obj->object == NULL) {
    goto error;
  }

done:
  return codec_list_obj;

error:
  if (codec_list_obj)
    g_slice_free (GstJniMediaCodecList, codec_list_obj);
  codec_list_obj = NULL;
  goto done;
}

void
gst_jni_media_codec_list_free (GstJniMediaCodecList * codec_list)
{
  JNIEnv *env = gst_jni_get_env ();

  gst_jni_object_unref (env, codec_list->object);
  g_slice_free (GstJniMediaCodecList, codec_list);
}

gchar *
gst_jni_media_codec_list_find_decoder_for_format (GstJniMediaCodecList * self,
    GstAmcFormat * format)
{
  jobject j_codec_name;
  JNIEnv *env = gst_jni_get_env ();

  j_codec_name = gst_jni_call_object_method (env, self->object,
      media_codec_list.find_decoder_for_format, format->object);

  if (j_codec_name == NULL) {
    return NULL;
  }
  return gst_jni_string_to_gchar (env, j_codec_name, TRUE);
}

jobjectArray
gst_jni_media_codec_list_get_codec_infos (GstJniMediaCodecList * self)
{
  JNIEnv *env = gst_jni_get_env ();
  return gst_jni_call_object_method (env, self->object,
      media_codec_list.get_codec_infos);
}
