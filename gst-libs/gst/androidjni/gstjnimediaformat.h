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

#ifndef __GST_JNI_media_format_H__
#define __GST_JNI_media_format_H__

#include <glib-object.h>
#include <gst/gst.h>
#include <jni.h>

G_BEGIN_DECLS
#define GST_TYPE_JNI_MEDIA_FORMAT                  (gst_jni_media_format_get_type ())
#define GST_JNI_MEDIA_FORMAT(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_JNI_MEDIA_FORMAT, GstJniMediaFormat))
#define GST_IS_JNI_MEDIA_FORMAT(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_JNI_MEDIA_FORMAT))
#define GST_JNI_MEDIA_FORMAT_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_JNI_MEDIA_FORMAT, GstJniMediaFormatClass))
#define GST_IS_JNI_MEDIA_FORMAT_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_JNI_MEDIA_FORMAT))
#define GST_JNI_MEDIA_FORMAT_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_JNI_MEDIA_FORMAT, GstJniMediaFormatClass))
typedef struct _GstJniMediaFormat GstJniMediaFormat;
typedef struct _GstJniMediaFormatClass GstJniMediaFormatClass;

struct _GstJniMediaFormat
{
  GObject parent_instance;

  /* instance members */
  jobject jobject;
};

struct _GstJniMediaFormatClass
{
  GObjectClass parent_class;

  /* class members */
  gboolean java_cached;
  jclass jklass;
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
};

GType gst_jni_media_format_get_type (void);

GstJniMediaFormat *gst_jni_media_format_new (jobject object);

GstJniMediaFormat *gst_jni_media_format_new_audio (const gchar * mime,
    gint sample_rate, gint channels);

GstJniMediaFormat *gst_jni_media_format_new_video (const gchar * mime,
    gint width, gint height);

gboolean gst_jni_media_format_contains_key (GstJniMediaFormat * self,
    const gchar * key);

gchar *gst_jni_media_format_to_string (GstJniMediaFormat * format);

gfloat gst_jni_media_format_get_float (GstJniMediaFormat * format,
    const gchar * key);

void gst_jni_media_format_set_float (GstJniMediaFormat * format,
    const gchar * key, gfloat value);

gint gst_jni_media_format_get_int (GstJniMediaFormat * format,
    const gchar * key);

void gst_jni_media_format_set_int (GstJniMediaFormat * format,
    const gchar * key, gint value);

gchar *gst_jni_media_format_get_string (GstJniMediaFormat * format,
    const gchar * key);

void gst_jni_media_format_set_string (GstJniMediaFormat * format,
    const gchar * key, const gchar * value);

GstBuffer *gst_jni_media_format_get_buffer (GstJniMediaFormat * format,
    const gchar * key);

void gst_jni_media_format_set_buffer (GstJniMediaFormat * format,
    const gchar * key, GstBuffer * value);

G_END_DECLS
#endif
