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
#include <jni.h>
#include <fluc/drm/flucdrm.h>

G_BEGIN_DECLS
typedef struct _GstAmcCodecInfo GstAmcCodecInfo;
typedef struct _GstAmcCodecType GstAmcCodecType;
typedef struct _GstAmcCodec GstAmcCodec;
typedef struct _GstAmcBufferInfo GstAmcBufferInfo;
typedef struct _GstAmcFormat GstAmcFormat;
typedef struct _GstAmcCrypto GstAmcCrypto;
typedef struct _GstAmcBuffer GstAmcBuffer;
typedef struct _GstAmcDRBuffer GstAmcDRBuffer;

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
};

struct _GstAmcCodecInfo
{
  gchar *name;
  gboolean is_encoder;
  GstAmcCodecType *supported_types;
  gint n_supported_types;
};

struct _GstAmcBuffer
{
  jobject object;               /* global reference */
  guint8 *data;
  gsize size;
};

struct _GstAmcDRBuffer {
  GstAmcCodec *codec;
  guint idx;
  gboolean released;
};

struct _GstAmcFormat
{
  /* < private > */
  jobject object;               /* global reference */
};

struct _GstAmcCrypto
{
  /* < private > */
  jobject mcrypto;
  jobject mdrm;
  jbyteArray mdrm_session_id;
};

struct _GstAmcCodec
{
  /* < private > */
  jobject object;               /* global reference */
};

struct _GstAmcBufferInfo
{
  gint flags;
  gint offset;
  gint64 presentation_time_us;
  gint size;
};

extern GQuark gst_amc_codec_info_quark;

GstAmcCodec *gst_amc_codec_new (const gchar * name);
void gst_amc_codec_free (GstAmcCodec * codec, GstAmcCrypto * crypto_ctx);

jmethodID gst_amc_codec_get_release_method_id (GstAmcCodec * codec);
jmethodID gst_amc_codec_get_release_ts_method_id (GstAmcCodec * codec);
gboolean gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format,
    guint8 * surface, jobject mcrypto_obj, gint flags);
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

gboolean gst_amc_codec_queue_secure_input_buffer (GstAmcCodec * codec,
    gint index, const GstAmcBufferInfo * info, const GstBuffer * drmbuf);

gboolean gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info);
gboolean gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index);
gboolean gst_amc_codec_render_output_buffer (GstAmcCodec * codec, gint index,
    GstClockTime ts);
gboolean gst_amc_codec_set_output_surface (GstAmcCodec * codec, guint8 * surface);

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
void gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value);
gboolean gst_amc_format_get_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer ** value);
void gst_amc_format_set_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer * value);

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

gboolean is_protection_system_id_supported (const gchar * uuid_utf8);

jobject juuid_from_utf8 (JNIEnv * env, const gchar * uuid_utf8);
jbyteArray jbyte_arr_from_data (JNIEnv * env, const guchar * data, gsize size);
gboolean jmedia_crypto_from_drm_event (GstEvent * event,
    GstAmcCrypto * crypto_ctx);

gboolean hack_pssh_initdata (guchar * payload, gsize payload_size,
    gsize * new_payload_size);

gboolean sysid_is_clearkey (const gchar * sysid);
void gst_amc_handle_drm_event (GstElement * self, GstEvent * event,
    GstAmcCrypto * crypto_ctx);

jobject * gst_amc_global_ref_jobj (jobject * obj);

GstAmcDRBuffer * gst_amc_dr_buffer_new (GstAmcCodec *codec, guint idx);
void gst_amc_dr_buffer_free (GstAmcDRBuffer *buf);
gboolean gst_amc_dr_buffer_render (GstAmcDRBuffer *buf, GstClockTime ts);

GstQuery * gst_amc_query_new_surface (void);
gpointer gst_amc_query_parse_surface (GstQuery *query);
gboolean gst_amc_query_set_surface (GstQuery *query, gpointer surface);
GstEvent * gst_amc_event_new_surface (gpointer surface);
gpointer gst_amc_event_parse_surface (GstEvent *event);
gboolean gst_amc_event_is_surface (GstEvent *event);

G_END_DECLS
#endif /* __GST_AMC_H__ */
