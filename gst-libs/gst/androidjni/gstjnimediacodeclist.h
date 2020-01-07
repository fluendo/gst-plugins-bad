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

#ifndef __GST_JNI_MEDIA_CODEC_LIST_H__
#define __GST_JNI_MEDIA_CODEC_LIST_H__

#include <glib-object.h>
#include <jni.h>
#include <gst/androidjni/gstjnimediaformat.h>

G_BEGIN_DECLS typedef struct _GstJniMediaCodecList GstJniMediaCodecList;

struct _GstJniMediaCodecList
{
  /* < private > */
  jobject object;               /* global reference */
};

gboolean gst_jni_media_codec_list_init (void);

GstJniMediaCodecList *gst_jni_media_codec_list_new (void);

void gst_jni_media_codec_list_free (GstJniMediaCodecList * self);

gchar *gst_jni_media_codec_list_find_decoder_for_format (GstJniMediaCodecList *
    self, GstAmcFormat * format);

G_END_DECLS
#endif
