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

#ifndef __GST_JNI_MEDIA_CODEC_H__
#define __GST_JNI_MEDIA_CODEC_H__

#include <glib-object.h>
#include <jni.h>
#include "gstjnimediaformat.h"
#include "gstjnibufferinfo.h"
#include "gstjnisurface.h"

G_BEGIN_DECLS
#define GST_TYPE_JNI_MEDIA_CODEC                  (gst_jni_media_codec_get_type ())
#define GST_JNI_MEDIA_CODEC(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_JNI_MEDIA_CODEC, GstJniMediaCodec))
#define GST_IS_JNI_MEDIA_CODEC(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_JNI_MEDIA_CODEC))
#define GST_JNI_MEDIA_CODEC_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_JNI_MEDIA_CODEC, GstJniMediaCodecClass))
#define GST_IS_JNI_MEDIA_CODEC_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_JNI_MEDIA_CODEC))
#define GST_JNI_MEDIA_CODEC_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_JNI_MEDIA_CODEC, GstJniMediaCodecClass))
typedef struct _GstJniMediaCodec GstJniMediaCodec;
typedef struct _GstJniMediaCodecClass GstJniMediaCodecClass;

struct _GstJniMediaCodec
{
  GObject parent_instance;

  /* instance members */
  jobject jobject;
};

struct _GstJniMediaCodecClass
{
  GObjectClass parent_class;

  /* class members */
  gboolean java_cached;
  jclass jklass;
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
  jmethodID start;
  jmethodID stop;
};

GType gst_jni_media_codec_get_type (void);

GstJniMediaCodec *gst_jni_media_codec_new (const gchar * codec_name);

gboolean gst_jni_media_codec_configure (GstJniMediaCodec * media_codec,
    GstJniMediaFormat * format, GstJniSurface * surface, gint flags);

gboolean gst_jni_media_codec_dequeue_input_buffer (GstJniMediaCodec *
    media_codec, gint64 timeout_us);

gboolean gst_jni_media_codec_dequeue_output_buffer (GstJniMediaCodec *
    media_codec, GstJniBufferInfo ** info, gint64 timeout_us);

GList *gst_jni_media_codec_get_input_buffers (GstJniMediaCodec * media_codec);

GList *gst_jni_media_codec_get_output_buffers (GstJniMediaCodec * media_codec);

GstJniMediaFormat *gst_jni_media_codec_get_output_format (GstJniMediaCodec *
    media_codec);

gboolean gst_jni_media_codec_queue_input_buffer (GstJniMediaCodec * media_codec,
    gint index, GstJniBufferInfo * info);

gboolean gst_jni_media_codec_release_output_buffer (GstJniMediaCodec *
    media_codec, gint idx, gboolean render);

gboolean gst_jni_media_codec_flush (GstJniMediaCodec * media_codec);

gboolean gst_jni_media_codec_start (GstJniMediaCodec * media_codec);

gboolean gst_jni_media_codec_stop (GstJniMediaCodec * media_codec);

gboolean gst_jni_media_codec_release (GstJniMediaCodec * media_codec);

gint gst_jni_media_codec_describe_contents (GstJniMediaCodec * media_codec);

G_END_DECLS
#endif
