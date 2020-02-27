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

#ifndef __GST_JNI_MEDIA_CODEC_H__
#define __GST_JNI_MEDIA_CODEC_H__

#include <glib-object.h>
#include <jni.h>

G_BEGIN_DECLS typedef struct _GstJniMediaCodec GstJniMediaCodec;

struct _GstJniMediaCodec
{
  /* < private > */
  jobject object;               /* global reference */
};

gboolean gst_jni_media_codec_init (void);

/* Functions in mapped to MedaCodecs JNI */
gboolean gst_jni_media_codec_configure (jobject codec_obj, jobject format, guint8 * surface, jobject mcrypto, gint flags);

gboolean gst_jni_media_codec_create_codec_by_name (jobject codec_obj, const gchar *name);

gboolean gst_jni_media_codec_dequeue_input_buffer (jobject codec_obj, gint64 timeoutUs, gint *ret);

gboolean gst_jni_media_codec_dequeue_output_buffer (jobject codec_obj, jobject info_obj,  
    gint64 timeout_us, gint *buf_idx);

gboolean gst_jni_media_codec_flush (jobject *codec_obj);

gboolean gst_jni_media_codec_get_input_buffers (jobject codec_obj,
    jobject input_buffers);

gboolean gst_jni_media_codec_get_output_buffers (jobject codec_obj,
    jobject output_buffers);

gboolean gst_jni_media_codec_get_output_format (jobject codec_obj, jobject object);

gboolean gst_jni_media_codec_queue_input_buffer (jobject codec_obj, gint index, gint offset,
    gint size, gint64 presentation_time_us, gint flags);

gboolean gst_jni_media_codec_release (jobject codec_obj);

gboolean gst_jni_media_codec_release_output_buffer (jobject codec_obj, gint index);

gboolean gst_jni_media_codec_release_output_buffer_ts (jobject codec_obj, gint index,
    GstClockTime ts);

gboolean gst_jni_media_codec_set_output_surface (jobject codec_obj, guint8 * surface);

gboolean gst_jni_media_codec_start (jobject codec_obj);

gboolean gst_jni_media_codec_stop (jobject codec_obj);

gboolean gst_jni_media_codec_queue_secure_input_buffer (jobject codec_obj, gint index,
    gint offset, jobject crypto_info, gint64 presentation_time_us, gint flags);

gboolean gst_jni_media_codec_get_codec_info (jobject codec_obj, jobject codec_info);

jmethodID gst_jni_media_codec_get_release_ts_method_id (void);

jmethodID gst_jni_media_codec_get_release_method_id (void);

G_END_DECLS
#endif
