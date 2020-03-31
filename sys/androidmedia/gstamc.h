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

#ifndef __GST_AMC_H__
#define __GST_AMC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/multichannel.h>
#include <gst/androidjni/gstjnimediaformat.h>
#include <jni.h>
#include "gstamcdrm.h"

G_BEGIN_DECLS

typedef struct _GstAmcCodecInfo GstAmcCodecInfo;
typedef struct _GstAmcCodecType GstAmcCodecType;
typedef struct _GstAmcCodecFeature GstAmcCodecFeature;
typedef struct _GstAmcCodec GstAmcCodec;
typedef struct _GstAmcBufferInfo GstAmcBufferInfo;
typedef struct _GstAmcBuffer GstAmcBuffer;
typedef struct _GstAmcDRBuffer GstAmcDRBuffer;
typedef struct _GstAmcRegisteredCodec GstAmcRegisteredCodec;

struct _GstAmcCodecType
{
  gchar *mime;

  gint *color_formats;
  gint n_color_formats;

  struct
  {
    gint profile;
    gint level;
  } *profile_levels;
  gint n_profile_levels;

  GstAmcCodecFeature *features;
  gint n_features;
};

struct _GstAmcCodecInfo
{
  gchar *name;
  gboolean is_encoder;
  GstAmcCodecType *supported_types;
  gint n_supported_types;
};

struct _GstAmcCodecFeature
{
  const gchar *name;
  gboolean supported;
  gboolean required;
};

struct _GstAmcBuffer
{
  jobject object;               /* global reference */
  guint8 *data;
  gsize size;
};

struct _GstAmcCodec
{
  guint flush_id;
  GMutex buffers_lock;
  gboolean tunneled_playback_enabled;
  gboolean adaptive_enabled;
  /* < private > */
  jobject object;               /* global reference */
  gint ref_count;
};

struct _GstAmcDRBuffer {
  GstAmcCodec *codec;
  guint idx;
  gboolean released;
  guint flush_id;
};

struct _GstAmcBufferInfo
{
  gint flags;
  gint offset;
  gint64 presentation_time_us;
  gint size;
};

struct _GstAmcRegisteredCodec
{
  GstAmcCodecInfo *codec_info;
  GstAmcCodecType *codec_type;
};


extern GQuark gst_amc_codec_info_quark;

GstAmcCodec *gst_amc_codec_new (const gchar * name);
GstAmcCodec *gst_amc_codec_ref (GstAmcCodec * codec);
void gst_amc_codec_unref (GstAmcCodec * codec);

jmethodID gst_amc_codec_get_release_method_id (GstAmcCodec * codec);
jmethodID gst_amc_codec_get_release_ts_method_id (GstAmcCodec * codec);
gboolean gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format,
    guint8 * surface, GstAmcCrypto * drm_ctx, gint flags, gint audio_session_id);
GstAmcFormat *gst_amc_codec_get_output_format (GstAmcCodec * codec);

gboolean gst_amc_codec_start (GstAmcCodec * codec);
gboolean gst_amc_codec_stop (GstAmcCodec * codec);
gboolean gst_amc_codec_flush (GstAmcCodec * codec);
gboolean gst_amc_codec_release (GstAmcCodec * codec);

GstAmcBuffer *gst_amc_codec_get_output_buffers (GstAmcCodec * codec,
    gsize * n_buffers);
GstAmcBuffer *gst_amc_codec_get_input_buffers (GstAmcCodec * codec,
    gsize * n_buffers);
void gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers);

gint gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs);
gint gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec,
    GstAmcBufferInfo * info, gint64 timeoutUs);

gboolean gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info, const GstBuffer * drmbuf,
    GstAmcCrypto * drmctx);
gboolean gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index);
gboolean gst_amc_codec_render_output_buffer (GstAmcCodec * codec, gint index,
    GstClockTime ts);
gboolean gst_amc_codec_set_output_surface (GstAmcCodec * codec, guint8 * surface);

gchar *
gst_amc_get_string_utf8 (JNIEnv * env, jstring v_str);

GstVideoFormat gst_amc_color_format_to_video_format (gint color_format);

const gchar *gst_amc_avc_profile_to_string (gint profile,
    const gchar ** alternative);
const gchar *gst_amc_avc_level_to_string (gint level);

const gchar *gst_amc_hevc_profile_to_string (gint profile);

gboolean
gst_amc_hevc_level_to_string (gint id, const gchar ** level,
    const gchar ** tier);

gint gst_amc_h263_profile_to_gst_id (gint profile);
gint gst_amc_h263_level_to_gst_id (gint level);
const gchar *gst_amc_mpeg4_profile_to_string (gint profile);
const gchar *gst_amc_mpeg4_level_to_string (gint level);
const gchar *gst_amc_aac_profile_to_string (gint profile);

GstAudioChannelPosition *gst_amc_audio_channel_mask_to_positions (guint32
    channel_mask, gint channels);

GstQuery *gst_amc_query_new_surface (void);
gpointer gst_amc_query_parse_surface (GstQuery * query);
gboolean gst_amc_query_set_surface (GstQuery * query, gpointer surface);
GstEvent *gst_amc_event_new_surface (gpointer surface);
gpointer gst_amc_event_parse_surface (GstEvent * event);
gboolean gst_amc_event_is_surface (GstEvent * event);

jobject juuid_from_utf8 (JNIEnv * env, const gchar * uuid_utf8);
jbyteArray jbyte_arr_from_data (JNIEnv * env, const guchar * data, gsize size);

GstAmcDRBuffer * gst_amc_dr_buffer_new (GstAmcCodec *codec, guint idx);
void gst_amc_dr_buffer_free (GstAmcDRBuffer *buf);
gboolean gst_amc_dr_buffer_render (GstAmcDRBuffer *buf, GstClockTime ts);

GstQuery * gst_amc_query_new_surface (void);
gpointer gst_amc_query_parse_surface (GstQuery *query);
gboolean gst_amc_query_set_surface (GstQuery *query, gpointer surface);
GstEvent * gst_amc_event_new_surface (gpointer surface);
gpointer gst_amc_event_parse_surface (GstEvent *event);
gboolean gst_amc_event_is_surface (GstEvent *event);

gboolean gst_amc_codec_is_feature_supported (GstAmcCodec * codec, GstAmcFormat * format, const gchar * feature);
gboolean gst_amc_codec_enable_adaptive_playback (GstAmcCodec * codec, GstAmcFormat * format);
gboolean gst_amc_codec_enable_tunneled_video_playback (GstAmcCodec * codec, GstAmcFormat * format, gint audio_session_id);

G_END_DECLS
#endif /* __GST_AMC_H__ */
