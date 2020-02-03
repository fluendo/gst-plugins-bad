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

#ifndef __GST_JNI_MEDIA_FORMAT_H__
#define __GST_JNI_MEDIA_FORMAT_H__

#include <glib-object.h>
#include <jni.h>

G_BEGIN_DECLS typedef struct _GstAmcFormat GstAmcFormat;

#define GST_AMC_MEDIA_FORMAT_TUNNELED_PLAYBACK "tunneled-playback"

struct _GstAmcFormat
{
  /* < private > */
  jobject object;               /* global reference */
};

gboolean gst_amc_media_format_init (void);

GstAmcFormat *gst_amc_format_new_audio (const gchar * mime, gint sample_rate,
    gint channels);

GstAmcFormat *gst_amc_format_new_video (const gchar * mime, gint width,
    gint height);

void gst_amc_format_free (GstAmcFormat * format);

gchar *gst_amc_format_to_string (GstAmcFormat * format);

gboolean gst_amc_format_contains_key (GstAmcFormat * format, const gchar * key);

gboolean gst_amc_format_get_float (GstAmcFormat * format, const gchar * key,
    gfloat * value);

void gst_amc_format_set_float (GstAmcFormat * format, const gchar * key,
    gfloat value);

gboolean gst_amc_format_get_int (const GstAmcFormat * format, const gchar * key,
    gint * value);

void gst_amc_format_set_int (GstAmcFormat * format, const gchar * key,
    gint value);

gboolean gst_amc_format_get_string (GstAmcFormat * format, const gchar * key,
    gchar ** value);

gboolean gst_amc_format_get_jstring (GstAmcFormat * format, const gchar * key,
    jstring * value);

void gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value);

gboolean gst_amc_format_get_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer ** value);

void gst_amc_format_set_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer * value);

void gst_amc_format_set_feature_enabled (GstAmcFormat * format,
    const gchar * feature, gboolean enabled);
G_END_DECLS
#endif
