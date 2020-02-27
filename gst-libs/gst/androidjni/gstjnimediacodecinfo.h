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

#ifndef __GST_JNI_MEDIA_CODEC_INFO_H__
#define __GST_JNI_MEDIA_CODEC_INFO_H__

#include <glib-object.h>
#include <jni.h>

G_BEGIN_DECLS typedef struct _GstJniMediaCodecInfo GstJniMediaCodecInfo;

struct _GstJniMediaCodecInfo
{
  /* < private > */
  jobject object;               /* global reference */
};

gboolean gst_jni_media_codec_info_init (void);

gboolean gst_jni_media_codec_info_get_capabilities_for_type (
    jobject capabilities,
    jobject codec_info,
    GstAmcFormat * format
);

gboolean gst_jni_media_codec_info_is_size_supported (
    jboolean *supported, jobject video_caps,
    jint max_height,jint max_width);

gboolean gst_jni_media_codec_info_get_supported_heights (
    jobject heights, jobject video_caps);


gboolean gst_jni_media_codec_info_get_supported_widths_for (
    jobject widths,
    jobject video_caps,
    jint max_height);

gboolean gst_jni_media_codec_info_video_caps_supported(void);

G_END_DECLS
#endif