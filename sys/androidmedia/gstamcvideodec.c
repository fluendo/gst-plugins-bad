/*
 * Initially based on gst-omx/omx/gstomxvideodec.c
 *
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * Copyright (C) 2012, Rafaël Carré <funman@videolanorg>
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

#include <gst/gst.h>
#include <gst/androidjni/gstjniamcdirectbuffer.h>
#include <gst/androidjni/gstjniamcutils.h>
#include <string.h>

#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif

#include "gstamcvideodec.h"
#include "gstamc-constants.h"


GST_DEBUG_CATEGORY_STATIC (gst_amc_video_dec_debug_category);
#define GST_CAT_DEFAULT gst_amc_video_dec_debug_category
#define DEFAULT_DIRECT_RENDERING TRUE

/* prototypes */
static void gst_amc_video_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_amc_video_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_amc_video_dec_open (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_close (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_start (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_amc_video_dec_reset (GstVideoDecoder * decoder,
    gboolean hard);
static GstFlowReturn gst_amc_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_amc_video_dec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_amc_video_dec_eos (GstVideoDecoder * decoder);

static gboolean gst_amc_video_dec_src_event (GstVideoDecoder * decoder,
    GstEvent * event);

enum
{
  PROP_0,
  PROP_DRM_AGENT_HANDLE,
  PROP_AUDIO_SESSION_ID,
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_amc_video_dec_debug_category, "amcvideodec", 0, \
      "Android MediaCodec video decoder");

GST_BOILERPLATE_FULL (GstAmcVideoDec, gst_amc_video_dec, GstVideoDecoder,
    GST_TYPE_VIDEO_DECODER, DEBUG_INIT);

static GstCaps *
create_sink_caps (const GstAmcCodecInfo * codec_info)
{
  GstCaps *ret;
  gint i;

  ret = gst_caps_new_empty ();

  for (i = 0; i < codec_info->n_supported_types; i++) {
    const GstAmcCodecType *type = &codec_info->supported_types[i];

    if (strcmp (type->mime, "video/mp4v-es") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile_level = FALSE;

      tmp = gst_structure_new ("video/mpeg",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

      if (type->n_profile_levels) {
        for (j = type->n_profile_levels - 1; j >= 0; j--) {
          const gchar *profile, *level;
          gint k;
          GValue va = { 0, };
          GValue v = { 0, };

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&v, G_TYPE_STRING);

          profile =
              gst_amc_mpeg4_profile_to_string (type->profile_levels[j].profile);
          if (!profile) {
            GST_ERROR ("Unable to map MPEG4 profile 0x%08x",
                type->profile_levels[j].profile);
            continue;
          }

          for (k = 1; k <= type->profile_levels[j].level && k != 0; k <<= 1) {
            level = gst_amc_mpeg4_level_to_string (k);
            if (!level)
              continue;

            g_value_set_string (&v, level);
            gst_value_list_append_value (&va, &v);
            g_value_reset (&v);
          }

          tmp2 = gst_structure_copy (tmp);
          gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);
          gst_structure_set_value (tmp2, "level", &va);
          g_value_unset (&va);
          g_value_unset (&v);
          gst_caps_merge_structure (ret, tmp2);
          have_profile_level = TRUE;
        }
      }

      if (!have_profile_level) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }

      tmp = gst_structure_new ("video/x-divx",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "divxversion", GST_TYPE_INT_RANGE, 4, 5,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_caps_merge_structure (ret, tmp);

      tmp = gst_structure_new ("video/x-xvid",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1, "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_caps_merge_structure (ret, tmp);

      tmp = gst_structure_new ("video/x-3ivx",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1, "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "video/3gpp") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile_level = FALSE;

      tmp = gst_structure_new ("video/x-h263",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "parsed", G_TYPE_BOOLEAN, TRUE,
          "variant", G_TYPE_STRING, "itu", NULL);

      if (type->n_profile_levels) {
        for (j = type->n_profile_levels - 1; j >= 0; j--) {
          gint profile, level;
          gint k;
          GValue va = { 0, };
          GValue v = { 0, };

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&v, G_TYPE_UINT);

          profile =
              gst_amc_h263_profile_to_gst_id (type->profile_levels[j].profile);

          if (profile == -1) {
            GST_ERROR ("Unable to map h263 profile 0x%08x",
                type->profile_levels[j].profile);
            continue;
          }

          for (k = 1; k <= type->profile_levels[j].level && k != 0; k <<= 1) {
            level = gst_amc_h263_level_to_gst_id (k);
            if (level == -1)
              continue;

            g_value_set_uint (&v, level);
            gst_value_list_append_value (&va, &v);
            g_value_reset (&v);
          }
          tmp2 = gst_structure_copy (tmp);
          gst_structure_set (tmp2, "profile", G_TYPE_UINT, profile, NULL);
          gst_structure_set_value (tmp2, "level", &va);
          g_value_unset (&va);
          g_value_unset (&v);
          gst_caps_merge_structure (ret, tmp2);
          have_profile_level = TRUE;
        }
      }

      if (!have_profile_level) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }
    } else if (strcmp (type->mime, "video/avc") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile_level = FALSE;

      tmp = gst_structure_new ("video/x-h264",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "parsed", G_TYPE_BOOLEAN, TRUE,
          "stream-format", G_TYPE_STRING, "byte-stream",
          "alignment", G_TYPE_STRING, "au", NULL);

      if (type->n_profile_levels) {
        for (j = type->n_profile_levels - 1; j >= 0; j--) {
          const gchar *profile, *alternative = NULL, *level;
          gint k;
          GValue va = { 0, };
          GValue v = { 0, };

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&v, G_TYPE_STRING);

          profile =
              gst_amc_avc_profile_to_string (type->profile_levels[j].profile,
              &alternative);

          if (!profile) {
            GST_ERROR ("Unable to map H264 profile 0x%08x",
                type->profile_levels[j].profile);
            continue;
          }

          for (k = 1; k <= type->profile_levels[j].level && k != 0; k <<= 1) {
            level = gst_amc_avc_level_to_string (k);
            if (!level)
              continue;

            g_value_set_string (&v, level);
            gst_value_list_append_value (&va, &v);
            g_value_reset (&v);
          }
          tmp2 = gst_structure_copy (tmp);
          gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);
          gst_structure_set_value (tmp2, "level", &va);
          if (!alternative)
            g_value_unset (&va);
          g_value_unset (&v);
          gst_caps_merge_structure (ret, tmp2);

          if (alternative) {
            tmp2 = gst_structure_copy (tmp);
            gst_structure_set (tmp2, "profile", G_TYPE_STRING, alternative,
                NULL);
            gst_structure_set_value (tmp2, "level", &va);
            g_value_unset (&va);
            gst_caps_merge_structure (ret, tmp2);
          }
          have_profile_level = TRUE;
        }
      }

      if (!have_profile_level) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }
    } else if (strcmp (type->mime, "video/x-vnd.on2.vp8") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("video/x-vp8",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "video/hevc") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile_level = FALSE;

      tmp = gst_structure_new ("video/x-h265",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1,
          "stream-format", G_TYPE_STRING, "byte-stream",
          "alignment", G_TYPE_STRING, "au",
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

      if (type->n_profile_levels) {
        for (j = type->n_profile_levels - 1; j >= 0; j--) {
          const gchar *profile;
          gint k;
          GValue va = { 0 };
          GValue v = { 0 };
          GValue ta = { 0 };
          GValue t = { 0 };

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&v, G_TYPE_STRING);
          g_value_init (&ta, GST_TYPE_LIST);
          g_value_init (&t, G_TYPE_STRING);

          profile =
              gst_amc_hevc_profile_to_string (type->profile_levels[j].profile);

          if (!profile) {
            GST_ERROR ("Unable to map HEVC profile 0x%08x",
                type->profile_levels[j].profile);
            continue;
          }

          for (k = 1; k <= type->profile_levels[j].level && k != 0; k <<= 1) {
            const gchar *level, *tier;
            if (!gst_amc_hevc_level_to_string (k, &tier, &level))
              continue;

            g_value_set_string (&t, tier);
            gst_value_list_append_value (&ta, &t);
            g_value_reset (&t);

            g_value_set_string (&v, level);
            gst_value_list_append_value (&va, &v);
            g_value_reset (&v);
          }

          tmp2 = gst_structure_copy (tmp);
          gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);
          gst_structure_set_value (tmp2, "level", &va);
          gst_structure_set_value (tmp2, "tier", &ta);
          g_value_unset (&ta);
          g_value_unset (&va);
          g_value_unset (&v);
          g_value_unset (&t);
          gst_caps_merge_structure (ret, tmp2);
          have_profile_level = TRUE;
        }
      }

      if (!have_profile_level) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }

    } else if (strcmp (type->mime, "video/mpeg2") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("video/mpeg",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

      gst_caps_merge_structure (ret, tmp);
    } else {
      GST_WARNING ("Unsupported mimetype '%s'", type->mime);
    }
  }

  // Append the x-cenc caps
  if (ret) {
    GstCaps *cenc_caps = gst_caps_new_empty ();
    guint i;
    for (i = 0; i < gst_caps_get_size (ret); ++i) {
      GstStructure *str = gst_structure_copy (gst_caps_get_structure (ret, i));
      const gchar *real_media_type = g_strdup (gst_structure_get_name (str));
      gst_structure_set_name (str, "application/x-cenc");
      gst_structure_set (str, "real-caps", G_TYPE_STRING, real_media_type,
          NULL);
      gst_caps_append_structure (cenc_caps, str);
    }
    gst_caps_append (ret, cenc_caps);
  }

  return ret;
}

static GstCaps *
create_src_caps (const GstAmcCodecInfo * codec_info, gboolean direct_rendering)
{
  GstCaps *ret, *amc;
  gint i;

  ret = gst_caps_new_empty ();

  if (direct_rendering) {
    amc = gst_caps_new_simple ("video/x-amc", NULL);
    gst_caps_merge (ret, amc);
    return ret;
  }

  for (i = 0; i < codec_info->n_supported_types; i++) {
    const GstAmcCodecType *type = &codec_info->supported_types[i];
    gint j;

    for (j = 0; j < type->n_color_formats; j++) {
      GstVideoFormat format;
      GstCaps *tmp;

      format = gst_amc_color_format_to_video_format (type->color_formats[j]);
      if (format == GST_VIDEO_FORMAT_UNKNOWN) {
        GST_WARNING ("Unknown color format 0x%08x", type->color_formats[j]);
        continue;
      }
      tmp = gst_video_format_new_template_caps (format);
      gst_caps_merge (ret, tmp);
    }
  }

  return ret;
}

static GstFlowReturn
gst_amc_video_dec_push_dummy (GstAmcVideoDec * self, gboolean set_caps)
{
  GstBuffer *buf = gst_buffer_new ();
  GstCaps *caps;

  if (G_UNLIKELY (!self->x_amc_empty_caps)) {
    self->x_amc_empty_caps = gst_caps_new_simple ("video/x-amc", NULL);
  }

  caps = self->x_amc_empty_caps;

  if (set_caps)
    gst_pad_set_caps (GST_VIDEO_DECODER (self)->srcpad, caps);
  gst_buffer_set_caps (buf, caps);
  GST_BUFFER_DATA (buf) = NULL;
  return gst_pad_push (GST_VIDEO_DECODER (self)->srcpad, buf);
}

static void
gst_amc_video_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstAmcVideoDecClass *videodec_class = GST_AMC_VIDEO_DEC_CLASS (g_class);
  const GstAmcCodecInfo *codec_info;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar *longname;

  codec_info =
      g_type_get_qdata (G_TYPE_FROM_CLASS (g_class), gst_amc_codec_info_quark);
  /* This happens for the base class and abstract subclasses */
  if (!codec_info)
    return;

  videodec_class->codec_info = codec_info;
  videodec_class->direct_rendering = DEFAULT_DIRECT_RENDERING;

  /* Add pad templates */
  caps = create_sink_caps (codec_info);
  templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  caps = create_src_caps (codec_info, videodec_class->direct_rendering);
  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  longname = g_strdup_printf ("Android MediaCodec %s", codec_info->name);
  gst_element_class_set_details_simple (element_class,
      codec_info->name,
      "Codec/Decoder/Video",
      longname, "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
  g_free (longname);
}


static void
gst_amc_video_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAmcVideoDec *thiz = GST_AMC_VIDEO_DEC (object);
  switch (prop_id) {
    case PROP_DRM_AGENT_HANDLE:
      g_value_set_pointer (value,
          (gpointer) gst_amc_drm_mcrypto_get (thiz->drm_ctx));
      break;
    case PROP_AUDIO_SESSION_ID:
      /* If not zero enables tunneled if supported */
      g_value_set_int (value, thiz->audio_session_id);
      /* FIXME: This is not an error, but we want to force this log
       * and for now we can only achieve it in android using GST_ERROR */
      GST_ERROR_OBJECT (object, "audio_session_id=%d", thiz->audio_session_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_amc_video_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmcVideoDec *thiz = GST_AMC_VIDEO_DEC (object);
  switch (prop_id) {
    case PROP_DRM_AGENT_HANDLE:
      gst_amc_drm_mcrypto_set (thiz->drm_ctx, g_value_get_pointer (value));
      break;
    case PROP_AUDIO_SESSION_ID:
      thiz->audio_session_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
gst_amc_video_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  gboolean handled = FALSE;
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      self->downstream_flow_ret = GST_FLOW_WRONG_STATE;
      /* We don't set handled to TRUE because we want this
       * event to also be pushed downstream (just in case). */
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      /* We need to handle the protection event. On such events we receive
       * the payload required to initialize the protection system.
       * We can receive as many events but before the flow, otherwise
       * it is an error
       */
      if (gst_amc_drm_is_drm_event (event)) {
        if (!self->drm_ctx)
          self->drm_ctx = gst_amc_drm_ctx_new (GST_ELEMENT (self));
        gst_amc_drm_handle_drm_event (self->drm_ctx, event);
        handled = TRUE;
      }
      break;
    default:
      break;
  }

  if (handled)
    gst_event_unref (event);
  return handled;
}

static void
gst_amc_video_dec_class_init (GstAmcVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *videodec_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_amc_video_dec_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_amc_video_dec_change_state);

  videodec_class->start = GST_DEBUG_FUNCPTR (gst_amc_video_dec_start);
  videodec_class->stop = GST_DEBUG_FUNCPTR (gst_amc_video_dec_stop);
  videodec_class->open = GST_DEBUG_FUNCPTR (gst_amc_video_dec_open);
  videodec_class->close = GST_DEBUG_FUNCPTR (gst_amc_video_dec_close);
  videodec_class->reset = GST_DEBUG_FUNCPTR (gst_amc_video_dec_reset);
  videodec_class->set_format = GST_DEBUG_FUNCPTR (gst_amc_video_dec_set_format);
  videodec_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_amc_video_dec_handle_frame);
  videodec_class->finish = GST_DEBUG_FUNCPTR (gst_amc_video_dec_finish);

  videodec_class->src_event = GST_DEBUG_FUNCPTR (gst_amc_video_dec_src_event);
  videodec_class->sink_event = GST_DEBUG_FUNCPTR (gst_amc_video_dec_sink_event);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_amc_video_dec_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_amc_video_dec_get_property);

  g_object_class_install_property (gobject_class, PROP_DRM_AGENT_HANDLE,
      g_param_spec_pointer ("drm-agent-handle", "DRM Agent handle",
          "The DRM Agent handle to use for decrypting",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUDIO_SESSION_ID,
      g_param_spec_int ("audio-session-id", "Audio Session ID",
          "Audio Session ID for tunneled video playback",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_amc_video_dec_init (GstAmcVideoDec * self, GstAmcVideoDecClass * klass)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->drain_lock = g_mutex_new ();
  self->drain_cond = g_cond_new ();
}

static gboolean
gst_amc_video_dec_open (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);
  GstAmcVideoDecClass *klass = GST_AMC_VIDEO_DEC_GET_CLASS (self);

  GST_ERROR_OBJECT (self, "Occupying video decoder");

  self->codec = gst_amc_codec_new (klass->codec_info->name);
  if (!self->codec)
    return FALSE;
  self->started = FALSE;
  GST_DEBUG_OBJECT (self, "Opened decoder");

  return TRUE;
}

static gboolean
gst_amc_video_dec_close (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (self->codec) {
    gst_amc_codec_release (self->codec);
    gst_amc_codec_unref (self->codec);
    self->codec = NULL;
    GST_ERROR_OBJECT (self, "Video decoder have been released");
  }
#if !USE_AMCVIDEOSINK
  if (self->surface)
    g_object_unref (self->surface);
#endif
  self->surface = NULL;

  self->started = FALSE;
  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_amc_video_dec_finalize (GObject * object)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (object);
  if (self->x_amc_empty_caps) {
    gst_caps_unref (self->x_amc_empty_caps);
    self->x_amc_empty_caps = NULL;
  }

  gst_amc_drm_ctx_free (self->drm_ctx);
  self->drm_ctx = NULL;

  g_mutex_free (self->drain_lock);
  g_cond_free (self->drain_cond);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_amc_video_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->output_configured = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* This is done to avoid a deadlocking in case when we're
         destroying element, when dec_loop is infinitely getting
         a timeout on dequeue_output_buffer, while GstElementClass
         is waiting dec_loop to quit */
      self->stop_loop = TRUE;
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_amc_video_dec_set_src_caps (GstAmcVideoDec * self,
    const GstAmcFormat * format)
{
  GstVideoCodecState *output_state;
  GstAmcVideoDecClass *klass;
  gint color_format, width, height;
  gint stride, slice_height;
  gint crop_left, crop_right;
  gint crop_top, crop_bottom;
  GstVideoFormat gst_format;

  if (!gst_amc_format_get_int (format, "color-format", &color_format) ||
      !gst_amc_format_get_int (format, "width", &width) ||
      !gst_amc_format_get_int (format, "height", &height)) {
    GST_ERROR_OBJECT (self, "Failed to get output format metadata");
    return FALSE;
  }

  if (!gst_amc_format_get_int (format, "stride", &stride) ||
      !gst_amc_format_get_int (format, "slice-height", &slice_height)) {
    GST_ERROR_OBJECT (self, "Failed to get stride and slice-height");
    return FALSE;
  }

  if (!gst_amc_format_get_int (format, "crop-left", &crop_left) ||
      !gst_amc_format_get_int (format, "crop-right", &crop_right) ||
      !gst_amc_format_get_int (format, "crop-top", &crop_top) ||
      !gst_amc_format_get_int (format, "crop-bottom", &crop_bottom)) {
    GST_ERROR_OBJECT (self, "Failed to get crop rectangle");
    return FALSE;
  }

  if (width == 0 || height == 0) {
    GST_ERROR_OBJECT (self, "Height or width not set");
    return FALSE;
  }

  if (crop_bottom)
    height = height - (height - crop_bottom - 1);
  if (crop_top)
    height = height - crop_top;

  if (crop_right)
    width = width - (width - crop_right - 1);
  if (crop_left)
    width = width - crop_left;

  gst_format = gst_amc_color_format_to_video_format (color_format);
  if (gst_format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Unknown color format 0x%08x", color_format);
    return FALSE;
  }

  klass = GST_AMC_VIDEO_DEC_GET_CLASS (self);
  if (klass->direct_rendering) {
    gst_format = GST_VIDEO_FORMAT_ENCODED;
    color_format = COLOR_FormatSurface1;
  }

  output_state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      gst_format, width, height, self->input_state);

  self->format = gst_format;
  self->color_format = color_format;
  self->height = height;
  self->width = width;
  self->stride = stride;
  self->slice_height = slice_height;
  self->crop_left = crop_left;
  self->crop_right = crop_right;
  self->crop_top = crop_top;
  self->crop_bottom = crop_bottom;

  gst_video_codec_state_unref (output_state);
  self->input_state_changed = FALSE;

  return TRUE;
}

/*
 * The format is called QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka.
 * Which is actually NV12 (interleaved U&V).
 */
#define TILE_WIDTH 64
#define TILE_HEIGHT 32
#define TILE_SIZE (TILE_WIDTH * TILE_HEIGHT)
#define TILE_GROUP_SIZE (4 * TILE_SIZE)

/* get frame tile coordinate. XXX: nothing to be understood here, don't try. */
static size_t
tile_pos (size_t x, size_t y, size_t w, size_t h)
{
  size_t flim = x + (y & ~1) * w;

  if (y & 1) {
    flim += (x & ~3) + 2;
  } else if ((h & 1) == 0 || y != (h - 1)) {
    flim += (x + 2) & ~3;
  }

  return flim;
}

/* The weird handling of cropping, alignment and everything is taken from
 * platform/frameworks/media/libstagefright/colorconversion/ColorConversion.cpp
 */
static gboolean
gst_amc_video_dec_fill_buffer (GstAmcVideoDec * self, gint idx,
    const GstAmcBufferInfo * buffer_info, GstBuffer * outbuf)
{
  GstAmcBuffer *buf;
  GstAmcVideoDecClass *klass = GST_AMC_VIDEO_DEC_GET_CLASS (self);
  GstVideoCodecState *state =
      gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));
  GstVideoInfo *info = &state->info;
  gboolean ret = FALSE;

  if (idx >= self->n_output_buffers) {
    GST_ERROR_OBJECT (self, "Invalid output buffer index %d of %d",
        idx, self->n_output_buffers);
    goto done;
  }

  buf = &self->output_buffers[idx];

  /* Same video format */
  if (buffer_info->size == GST_BUFFER_SIZE (outbuf)) {
    gsize copysize = buffer_info->size;

    if (buf->size <= buffer_info->offset) {
      GST_ERROR_OBJECT (self,
          "Sanity check failed: buf->size (%d) <= buf_info->offset (%d)",
          buf->size, buffer_info->offset);
      goto done;
    }

    if (buf->size < copysize + buffer_info->offset) {
      GST_WARNING_OBJECT (self, "Buffer info from android's decoder"
          " doesn't match the buffer: buf->size = %d"
          "buf_info->offset = %d, buf_info->size = %d."
          "We'll copy only the buf->size.",
          buf->size, buffer_info->offset, buffer_info->size);
      copysize = buf->size - buffer_info->offset;
    }

    GST_DEBUG_OBJECT (self, "Buffer sizes equal, doing fast copy");
    orc_memcpy (GST_BUFFER_DATA (outbuf), buf->data + buffer_info->offset,
        copysize);
    ret = TRUE;
    goto done;
  }

  GST_DEBUG_OBJECT (self,
      "Sizes not equal (%d vs %d), doing slow line-by-line copying",
      buffer_info->size, GST_BUFFER_SIZE (outbuf));

  /* Different video format, try to convert */
  switch (self->color_format) {
    case COLOR_FormatYUV420Planar:{
      gint i, j, height;
      guint8 *src, *dest;
      gint stride, slice_height;
      gint src_stride, dest_stride;
      gint row_length;

      stride = self->stride;
      if (stride == 0) {
        GST_ERROR_OBJECT (self, "Stride not set");
        goto done;
      }

      slice_height = self->slice_height;
      if (slice_height == 0) {
        /* NVidia Tegra 3 on Nexus 7 does not set this */
        if (g_str_has_prefix (klass->codec_info->name, "OMX.Nvidia.")) {
          slice_height = GST_ROUND_UP_32 (self->height);
        } else {
          GST_ERROR_OBJECT (self, "Slice height not set");
          goto done;
        }
      }

      for (i = 0; i < 3; i++) {
        if (i == 0) {
          src_stride = stride;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        } else {
          src_stride = (stride + 1) / 2;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        }

        src = buf->data + buffer_info->offset;

        if (i == 0) {
          src += self->crop_top * stride;
          src += self->crop_left;
          row_length = self->width;
        } else if (i > 0) {
          src += slice_height * stride;
          src += self->crop_top * src_stride;
          src += self->crop_left / 2;
          row_length = (self->width + 1) / 2;
        }
        if (i == 2)
          src += ((slice_height + 1) / 2) * ((stride + 1) / 2);

        dest = GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, i);
        height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

        for (j = 0; j < height; j++) {
          orc_memcpy (dest, src, row_length);
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    case COLOR_TI_FormatYUV420PackedSemiPlanar:
    case COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;
      gint row_length;

      /* This should always be set */
      if (self->stride == 0 || self->slice_height == 0) {
        GST_ERROR_OBJECT (self, "Stride or slice height not set");
        goto done;
      }

      /* FIXME: This does not work for odd widths or heights
       * but might as well be a bug in the codec */
      for (i = 0; i < 2; i++) {
        if (i == 0) {
          src_stride = self->stride;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        } else {
          src_stride = GST_ROUND_UP_2 (self->stride);
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        }

        src = buf->data + buffer_info->offset;
        if (i == 0) {
          row_length = self->width;
        } else if (i == 1) {
          src += (self->slice_height - self->crop_top / 2) * self->stride;
          row_length = GST_ROUND_UP_2 (self->width);
        }

        dest = GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, i);
        height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

        for (j = 0; j < height; j++) {
          orc_memcpy (dest, src, row_length);
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    case COLOR_QCOM_FormatYUV420SemiPlanar:
    case COLOR_FormatYUV420SemiPlanar:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride, fixed_stride;
      gint row_length;

      /* This should always be set */
      if (self->stride == 0 || self->slice_height == 0) {
        GST_ERROR_OBJECT (self, "Stride or slice height not set");
        goto done;
      }

      /* Samsung Galaxy S3 seems to report wrong strides.
         I.e. BigBuckBunny 854x480 H264 reports a stride of 864 when it is
         actually 854, so we use width instead of stride here.
         This is obviously bound to break in the future. */
      if (g_str_has_prefix (klass->codec_info->name, "OMX.SEC.")) {
        fixed_stride = self->width;
      } else {
        fixed_stride = self->stride;
      }

      for (i = 0; i < 2; i++) {
        src_stride = fixed_stride;
        dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);

        src = buf->data + buffer_info->offset;
        if (i == 0) {
          src += self->crop_top * fixed_stride;
          src += self->crop_left;
          row_length = self->width;
        } else if (i == 1) {
          src += self->slice_height * fixed_stride;
          src += self->crop_top * fixed_stride;
          src += self->crop_left;
          row_length = self->width;
        }

        dest = GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, i);
        height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

        for (j = 0; j < height; j++) {
          orc_memcpy (dest, src, row_length);
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    case COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka:{
      gint width = self->width;
      gint height = self->height;
      gint dest_luma_stride = GST_VIDEO_INFO_COMP_STRIDE (info, 0);
      gint dest_chroma_stride = GST_VIDEO_INFO_COMP_STRIDE (info, 1);
      guint8 *src = buf->data + buffer_info->offset;
      guint8 *dest_luma =
          GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, 0);
      guint8 *dest_chroma =
          GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, 1);
      gint y;

      const size_t tile_w = (width - 1) / TILE_WIDTH + 1;
      const size_t tile_w_align = (tile_w + 1) & ~1;

      const size_t tile_h_luma = (height - 1) / TILE_HEIGHT + 1;
      const size_t tile_h_chroma = (height / 2 - 1) / TILE_HEIGHT + 1;

      size_t luma_size = tile_w_align * tile_h_luma * TILE_SIZE;

      if ((luma_size % TILE_GROUP_SIZE) != 0)
        luma_size = (((luma_size - 1) / TILE_GROUP_SIZE) + 1) * TILE_GROUP_SIZE;

      for (y = 0; y < tile_h_luma; y++) {
        size_t row_width = width;
        gint x;

        for (x = 0; x < tile_w; x++) {
          size_t tile_width = row_width;
          size_t tile_height = height;
          gint luma_idx;
          gint chroma_idx;
          /* luma source pointer for this tile */
          const uint8_t *src_luma = src
              + tile_pos (x, y, tile_w_align, tile_h_luma) * TILE_SIZE;

          /* chroma source pointer for this tile */
          const uint8_t *src_chroma = src + luma_size
              + tile_pos (x, y / 2, tile_w_align, tile_h_chroma) * TILE_SIZE;
          if (y & 1)
            src_chroma += TILE_SIZE / 2;

          /* account for right columns */
          if (tile_width > TILE_WIDTH)
            tile_width = TILE_WIDTH;

          /* account for bottom rows */
          if (tile_height > TILE_HEIGHT)
            tile_height = TILE_HEIGHT;

          /* dest luma memory index for this tile */
          luma_idx = y * TILE_HEIGHT * dest_luma_stride + x * TILE_WIDTH;

          /* dest chroma memory index for this tile */
          /* XXX: remove divisions */
          chroma_idx =
              y * TILE_HEIGHT / 2 * dest_chroma_stride + x * TILE_WIDTH;

          tile_height /= 2;     // we copy 2 luma lines at once
          while (tile_height--) {
            memcpy (dest_luma + luma_idx, src_luma, tile_width);
            src_luma += TILE_WIDTH;
            luma_idx += dest_luma_stride;

            memcpy (dest_luma + luma_idx, src_luma, tile_width);
            src_luma += TILE_WIDTH;
            luma_idx += dest_luma_stride;

            memcpy (dest_chroma + chroma_idx, src_chroma, tile_width);
            src_chroma += TILE_WIDTH;
            chroma_idx += dest_chroma_stride;
          }
          row_width -= TILE_WIDTH;
        }
        height -= TILE_HEIGHT;
      }
      ret = TRUE;
      break;

    }
    default:
      GST_ERROR_OBJECT (self, "Unsupported color format %d",
          self->color_format);
      goto done;
      break;
  }

done:
  gst_video_codec_state_unref (state);
  return ret;
}


static void
gst_amc_video_dec_stop_srcpad_loop (GstAmcVideoDec * self)
{
  if (!self->srcpad_loop_started)
    return;

  self->stop_loop = TRUE;
  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (self));
  self->stop_loop = FALSE;
  /* tell chain func to start task after input buffer again */
  self->srcpad_loop_started = FALSE;
}



#define CHK(statement) do {                     \
    if (G_UNLIKELY (!(statement))) {            \
      error_msg = #statement;                   \
      goto error;                               \
    }                                           \
  } while (0)


static void
gst_amc_video_dec_loop (GstAmcVideoDec * self)
{
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstAmcBufferInfo buffer_info;
  gint idx = -1;
  const gchar *error_msg = "Unknown error";
  GstAmcVideoDecClass *klass = GST_AMC_VIDEO_DEC_GET_CLASS (self);
  gboolean pushed_to_be_rendered_directly = FALSE;

  GST_VIDEO_DECODER_STREAM_LOCK (self);

  for (;;) {
    GST_DEBUG_OBJECT (self, "Waiting for available output buffer");

    if (self->stop_loop) {
      /* To avoid infinite loop here */
      GST_VIDEO_DECODER_STREAM_UNLOCK (self);
      flow_ret = GST_FLOW_WRONG_STATE;
      goto finish;
    }

    GST_VIDEO_DECODER_STREAM_UNLOCK (self);

    /* Wait at most 100ms here, some codecs don't fail dequeueing if
     * the codec is flushing, causing deadlocks during shutdown */
    idx =
        gst_amc_codec_dequeue_output_buffer (self->codec, &buffer_info, 100000);
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    /* We have a buffer from MediaCodec, let's process it... */
    if (idx >= 0)
      break;

    switch (idx) {
      case INFO_OUTPUT_FORMAT_CHANGED:
      {
        gchar *format_string;
        GstAmcFormat *format = gst_amc_codec_get_output_format (self->codec);
        GST_DEBUG_OBJECT (self, "Output format has changed");
        CHK (format);

        format_string = gst_amc_format_to_string (format);
        GST_DEBUG_OBJECT (self, "Format changed, new output format: %s",
            format_string);
        g_free (format_string);

        /* The only way we configure srcpad */
        self->output_configured = gst_amc_video_dec_set_src_caps (self, format);
        gst_amc_format_free (format);
        CHK (self->output_configured);
        /* Pass down to buffers change: */
      }
      case INFO_OUTPUT_BUFFERS_CHANGED:
        GST_DEBUG_OBJECT (self, "Output buffers have changed");
        {
          if (!klass->direct_rendering) {
            if (self->output_buffers)
              gst_amc_codec_free_buffers (self->output_buffers,
                  self->n_output_buffers);
            self->output_buffers =
                gst_amc_codec_get_output_buffers (self->codec,
                &self->n_output_buffers);
            CHK (self->output_buffers);
          }
        }
        break;
      case INFO_TRY_AGAIN_LATER:
        GST_DEBUG_OBJECT (self, "Dequeueing output buffer timed out");
        continue;
      case G_MININT:
        CHK (!"Failure dequeueing input buffer");
      default:
        g_assert_not_reached ();
    }
  }

  /* Be sure to have the source pad configured. On Kindle Fire HDX
   * we do not receive a FORMAT_CHANGED error and thus the caps are not
   * set
   */
  if (!self->output_configured && klass->direct_rendering) {
    GstVideoCodecState *output_state;

    GST_DEBUG_OBJECT (self,
        "Received a buffer without output configuration."
        " Have to make manual setup");

    output_state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
        GST_VIDEO_FORMAT_ENCODED, self->input_state->info.width,
        self->input_state->info.height, self->input_state);
    gst_video_codec_state_unref (output_state);
  }

  GST_INFO_OBJECT (self,
      "Got output buffer at index %d: size %d time %" G_GINT64_FORMAT
      " flags 0x%08x", idx, buffer_info.size, buffer_info.presentation_time_us,
      buffer_info.flags);

  frame = gst_video_decoder_get_output_frame (GST_VIDEO_DECODER (self),
      gst_util_uint64_scale (buffer_info.presentation_time_us, GST_USECOND, 1));

  if (frame) {
    /* Check if we're in time with the frame */
    GstClockTimeDiff deadline =
        gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (self),
        frame);
    if (G_UNLIKELY (deadline < 0)) {
      /* Decoder is late */
      GST_DEBUG_OBJECT (self,
          "Frame is too late, dropping (deadline %" GST_TIME_FORMAT ")",
          GST_TIME_ARGS (-deadline));
      flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
      goto finish;
    } else if (klass->direct_rendering) {
#if USE_AMCVIDEOSINK
      /* Code for running with amcvideosink */
      GstAmcDRBuffer *b;

      b = gst_amc_dr_buffer_new (self->codec, idx);
      frame->output_buffer = gst_buffer_new ();
      GST_BUFFER_DATA (frame->output_buffer) = (guint8 *) b;
      GST_BUFFER_SIZE (frame->output_buffer) = sizeof (GstAmcDRBuffer *);
      GST_BUFFER_MALLOCDATA (frame->output_buffer) = (guint8 *) b;
      GST_BUFFER_FREE_FUNC (frame->output_buffer) =
          (GFreeFunc) gst_amc_dr_buffer_free;
      flow_ret =
          gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
      /* Direct rendering sucess.
         Don't release jni buffer, sink needs it. */
      pushed_to_be_rendered_directly = TRUE;
#else
      /* This code is for iterating with eglglessink, it's disabled currently,
       * because we use another code for amcvideosink */
      GstJniAmcDirectBuffer *b = gst_jni_amc_direct_buffer_new
          (self->surface->texture,
          self->codec->object,
          gst_amc_codec_get_release_method_id (self->codec),
          gst_amc_codec_get_release_ts_method_id (self->codec),
          idx);
      frame->output_buffer = gst_jni_amc_direct_buffer_get_gst_buffer (b);

      /* Because decoder has it's own surface, we render to it immediatelly. */
      if (G_LIKELY (gst_jni_amc_direct_buffer_render (b))) {
        flow_ret =
            gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
        /* Direct rendering sucess.
           Don't release jni buffer, sink needs it. */
        pushed_to_be_rendered_directly = TRUE;
      } else {
        GST_ERROR_OBJECT (self, "Failed rendering frame to surface, dropping");
        flow_ret =
            gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
      }
#endif
      goto finish;
    } else if (buffer_info.size > 0) {
      flow_ret = gst_video_decoder_alloc_output_frame (GST_VIDEO_DECODER
          (self), frame);
      /* seeking case */
      if (G_UNLIKELY (flow_ret == GST_FLOW_WRONG_STATE))
        goto finish;
      CHK (flow_ret == GST_FLOW_OK && "alloc output frame");

      if (!gst_amc_video_dec_fill_buffer (self, idx, &buffer_info,
              frame->output_buffer)) {
        gst_buffer_replace (&frame->output_buffer, NULL);
        gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
        CHK (!"gst_amc_video_dec_fill_buffer");
      }
      flow_ret =
          gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
      goto finish;
    }
  } else {
    /* frame == NULL */
    if (klass->direct_rendering) {
      /* Pushing this last frame produces a black frame and transitions
       * are not smooth so we just skip it */
      flow_ret = GST_FLOW_OK;
      goto finish;
    } else if (buffer_info.size > 0) {
      GstBuffer *outbuf;

      /* This sometimes happens at EOS or if the input is not properly framed,
       * let's handle it gracefully by allocating a new buffer for the current
       * caps and filling it
       */
      GST_DEBUG_OBJECT (self, "No corresponding frame found");

      outbuf = gst_video_decoder_alloc_output_buffer (GST_VIDEO_DECODER (self));

      if (!gst_amc_video_dec_fill_buffer (self, idx, &buffer_info, outbuf)) {
        gst_buffer_unref (outbuf);
        CHK (!"gst_amc_video_dec_fill_buffer");
      }

      GST_BUFFER_TIMESTAMP (outbuf) =
          gst_util_uint64_scale (buffer_info.presentation_time_us, GST_USECOND,
          1);

      flow_ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);
      goto finish;
    }
  }


  /* Some unexpected case, let's drop */
  if (frame) {
    GST_DEBUG_OBJECT (self, "Dropping frame (unexpected case)..");
    flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
  }


finish:
  self->downstream_flow_ret = flow_ret;

  /* Seeking. */
  if (flow_ret == GST_FLOW_WRONG_STATE) {
    /* Pause task until we'll start receiving buffers again */
    GST_DEBUG_OBJECT (self, "Flushing: stopping task");
    error_msg = NULL;
    goto error;
  }

  /* It was the last frame... */
  if ((buffer_info.flags & BUFFER_FLAG_END_OF_STREAM) ||
      flow_ret == GST_FLOW_UNEXPECTED) {
    error_msg = NULL;
    self->downstream_flow_ret = GST_FLOW_OK;
    GST_DEBUG_OBJECT (self, "Finished eos frame");
    goto error;
  }

  GST_LOG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));

  CHK (flow_ret == GST_FLOW_OK);

  /* Sucess */
done:
  if (idx >= 0 && !pushed_to_be_rendered_directly)
    gst_amc_codec_release_output_buffer (self->codec, idx);

  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  return;

error:
  /* We're going to stop srcpad's loop until new buffers on sinkpad */

  if (error_msg) {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL), ("%s", error_msg));
    self->downstream_flow_ret = GST_FLOW_ERROR;
  }

  /* In any case before pausing the thread we're signalling the draining */
  g_mutex_lock (self->drain_lock);
  self->drain_cond_signalling = TRUE;
  g_cond_broadcast (self->drain_cond);
  g_mutex_unlock (self->drain_lock);

  GST_DEBUG_OBJECT (self, "Pausing srcpad's loop task");
  gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
  self->srcpad_loop_started = FALSE;
  goto done;
}

static gboolean
gst_amc_video_dec_src_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);

  if (gst_amc_event_is_surface (event)) {
    self->surface = gst_amc_event_parse_surface (event);
    gst_event_unref (event);

    /* If codec is already decoding at this moment,
     * we call MediaCodec.setOutputSurface */
    if (self->started && self->surface) {
      GST_DEBUG_OBJECT (self, "Setting new surface %p", self->surface);
      /* FIXME: This potentially can be racy */
      if (!gst_amc_codec_set_output_surface (self->codec, self->surface)) {
        GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
            ("Couldn't set new surface to video decoder"));
        self->downstream_flow_ret = GST_FLOW_ERROR;
      }
    }

    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_amc_video_dec_start (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self;
  self = GST_AMC_VIDEO_DEC (decoder);
  GST_DEBUG_OBJECT (self, "Starting decoder");
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->started = FALSE;
  self->cached_input_buffer = -1;

  return TRUE;
}

static gboolean
gst_amc_video_dec_stop (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  if (G_LIKELY (self->started)) {
    /* Stop srcpad loop until we'll receive a buffer on sinkpad again */
    gst_amc_video_dec_stop_srcpad_loop (self);

    gst_amc_codec_flush (self->codec);
    gst_amc_codec_stop (self->codec);
    self->started = FALSE;
    if (self->input_buffers)
      gst_amc_codec_free_buffers (self->input_buffers, self->n_input_buffers);
    self->input_buffers = NULL;
    if (self->output_buffers)
      gst_amc_codec_free_buffers (self->output_buffers, self->n_output_buffers);
    self->output_buffers = NULL;
  }

  gst_buffer_replace (&self->codec_data, NULL);
  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  GST_DEBUG_OBJECT (self, "Stopped decoder");
  return TRUE;
}

static gboolean
gst_amc_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstAmcVideoDec *self;
  GstAmcVideoDecClass *klass;
  GstAmcFormat *format;
  const gchar *mime;
  gboolean is_format_change = FALSE;
  gboolean is_size_change = FALSE;
  gboolean needs_disable = FALSE;
  gboolean needs_config = FALSE;
  gboolean adaptive;
  gchar *format_string;
  jobject jsurface = NULL;

  self = GST_AMC_VIDEO_DEC (decoder);
  klass = GST_AMC_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  is_size_change |= self->width != state->info.width;
  is_size_change |= self->height != state->info.height;
  is_format_change |= (self->codec_data != state->codec_data);

  adaptive = self->codec->adaptive_enabled;
  needs_disable = self->started &&
      (is_format_change || (is_size_change && !adaptive));
  needs_config = !self->started || needs_disable;

  /* FIXME: This is not an error, but we want to force this log
   * and for now we can only achieve it in android using GST_ERROR */
  GST_ERROR_OBJECT (self, "needs_disable=%d needs_config=%d", needs_disable,
      needs_config);

  if (needs_disable) {
    /* Completely reinit decoder */
    GST_INFO_OBJECT (self, "reinitializing decoder");
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    gst_amc_video_dec_stop (GST_VIDEO_DECODER (self));
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    gst_amc_video_dec_close (GST_VIDEO_DECODER (self));
    if (!gst_amc_video_dec_open (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to open codec again");
      return FALSE;
    }
    if (!gst_amc_video_dec_start (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to start codec again");
    }
  }

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  if (needs_config) {
    gst_buffer_replace (&self->codec_data, state->codec_data);

#if USE_AMCVIDEOSINK
    if (klass->direct_rendering && self->surface == NULL) {
      /* Exposes pads with decodebin with a dummy buffer to link with the sink
       * and get the surface */
      GST_INFO_OBJECT (self, "Sending a dummy buffer");
      gst_amc_video_dec_push_dummy (self, TRUE);

      if (self->surface == NULL) {
        GstQuery *query = gst_amc_query_new_surface ();

        if (gst_pad_peer_query (decoder->srcpad, query)) {
          jsurface = self->surface = gst_amc_query_parse_surface (query);
          if (G_UNLIKELY (!self->surface)) {
            GST_WARNING_OBJECT (self, "Quering a surface from the sink failed");
          }
        }
        gst_query_unref (query);
      }
    }
#else /* Use eglglessink */
    if (klass->direct_rendering && self->surface == NULL) {
      self->surface = gst_jni_surface_new (gst_jni_surface_texture_new ());
      jsurface = self->surface->jobject;
    }
#endif

    mime = gst_jni_amc_video_caps_to_mime (state->caps);
    if (!mime) {
      GST_ERROR_OBJECT (self, "Failed to convert caps to mime");
      return FALSE;
    }

    format = gst_amc_format_new_video (mime, state->info.width,
        state->info.height);
    if (!format) {
      GST_ERROR_OBJECT (self, "Failed to create video format");
      return FALSE;
    }

    /* FIXME: This buffer needs to be valid until the codec is stopped again */
    if (self->codec_data)
      gst_amc_format_set_buffer (format, "csd-0", self->codec_data);
    format_string = gst_amc_format_to_string (format);
    GST_DEBUG_OBJECT (self, "Configuring codec with format: %s surface: %p "
        "audio session id:%d", format_string, jsurface, self->audio_session_id);
    g_free (format_string);
    if (!gst_amc_codec_configure (self->codec, format, jsurface,
            self->drm_ctx, 0, self->audio_session_id)) {
      GST_ERROR_OBJECT (self, "Failed to configure codec");
      gst_amc_format_free (format);
      return FALSE;
    }

    gst_amc_format_free (format);
    if (!gst_amc_codec_start (self->codec)) {
      GST_ERROR_OBJECT (self, "Failed to start codec");
      return FALSE;
    }

    gst_amc_codec_free_buffers (self->input_buffers, self->n_input_buffers);
    self->input_buffers =
        gst_amc_codec_get_input_buffers (self->codec, &self->n_input_buffers);
    if (!self->input_buffers) {
      GST_ERROR_OBJECT (self, "Failed to get input buffers");
      return FALSE;
    }
  }

  self->input_state = gst_video_codec_state_ref (state);
  self->input_state_changed = TRUE;
  self->started = TRUE;

  return TRUE;
}

static gboolean
gst_amc_video_dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstAmcVideoDec *self;
  (void) hard;

  self = GST_AMC_VIDEO_DEC (decoder);
  GST_DEBUG_OBJECT (self, "Resetting decoder");
  if (G_UNLIKELY (!self->started)) {
    GST_DEBUG_OBJECT (self, "Codec not started yet");
    return TRUE;
  }

  /* Stop srcpad loop until we'll receive a buffer on sinkpad again */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  gst_amc_video_dec_stop_srcpad_loop (self);
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  /* Flush the decoder */
  gst_amc_codec_flush (self->codec);

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->cached_input_buffer = -1;

  GST_DEBUG_OBJECT (self, "Reset decoder done");
  return TRUE;
}

static GstFlowReturn
gst_amc_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstAmcVideoDec *self;
  gint idx = -1;
  GstAmcBuffer *buf;
  GstAmcBufferInfo buffer_info;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;
  gboolean queued_input_buffer = FALSE;
  const gchar *error_msg = "Unknown error";

  self = GST_AMC_VIDEO_DEC (decoder);

  GST_LOG_OBJECT (self, "Handling frame");

  if (!self->started) {
    GST_ERROR_OBJECT (self, "Codec not started yet");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (self->eos) {
    GST_ERROR_OBJECT (self, "Got frame after EOS");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_UNEXPECTED;
  }

  if (self->codec->tunneled_playback_enabled) {
    self->downstream_flow_ret = gst_amc_video_dec_push_dummy (self, FALSE);
    gst_video_decoder_release_frame (GST_VIDEO_DECODER (self),
        gst_video_codec_frame_ref (frame));
  }

  timestamp = frame->pts;
  duration = frame->duration;

  while (offset < GST_BUFFER_SIZE (frame->input_buffer)) {

    if (G_UNLIKELY (self->cached_input_buffer != -1)) {
      /* Sometimes we have an "inputBuffer" that was dequeued, but not queued back. */
      idx = self->cached_input_buffer;
      self->cached_input_buffer = -1;
    } else {
      GST_VIDEO_DECODER_STREAM_UNLOCK (self);
      idx = gst_amc_codec_dequeue_input_buffer (self->codec, 100000);
      GST_VIDEO_DECODER_STREAM_LOCK (self);
    }

    /* If pushing loop has to be stopped (which is case of PAUSED-> READY) -
     * then we don't enqueue anything and just drop the buffer.
     * Otherwise we can deadlock in infinite loop. */
    if (self->stop_loop) {
      error_msg = NULL;
      goto error;
    }

    /* First let's analyse the state of srcpad's loop
       and codec's state (it may be flushing) */
    if (self->downstream_flow_ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Received from srcloop: %s",
          gst_flow_get_name (self->downstream_flow_ret));
      if (self->downstream_flow_ret == GST_FLOW_WRONG_STATE)
        error_msg = NULL;
      goto error;
    }

    if (idx < 0)
      switch (idx) {
        case INFO_TRY_AGAIN_LATER:
          GST_DEBUG_OBJECT (self, "Dequeueing input buffer timed out");
          continue;             /* next try */
          break;
        case G_MININT:
          CHK (!"Failed to dequeue input buffer");
        default:
          g_assert_not_reached ();
      }

    CHK (idx < self->n_input_buffers);
    /* Now handle the frame */
    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf = &self->input_buffers[idx];
    memset (&buffer_info, 0, sizeof (buffer_info));
    buffer_info.offset = 0;
    buffer_info.size =
        MIN (GST_BUFFER_SIZE (frame->input_buffer) - offset, buf->size);
    if (self->drm_ctx) {
      /* Feeding decoder with drm buffer by parts is not implemented yet. */
      CHK (GST_BUFFER_SIZE (frame->input_buffer) <= buf->size);
    }

    orc_memcpy (buf->data, GST_BUFFER_DATA (frame->input_buffer) + offset,
        buffer_info.size);
    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset =
          gst_util_uint64_scale (offset, duration,
          GST_BUFFER_SIZE (frame->input_buffer));
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buffer_info.presentation_time_us =
          gst_util_uint64_scale (timestamp + timestamp_offset, 1, GST_USECOND);
      self->last_upstream_ts = timestamp + timestamp_offset;
    }
    if (duration != GST_CLOCK_TIME_NONE)
      self->last_upstream_ts += duration;

    if (offset == 0) {
      if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
        buffer_info.flags |= BUFFER_FLAG_SYNC_FRAME;
    }

    offset += buffer_info.size;
    GST_LOG_OBJECT (self,
        "Queueing buffer %d: size %d time %" G_GINT64_FORMAT
        " flags 0x%08x", idx, buffer_info.size,
        buffer_info.presentation_time_us, buffer_info.flags);

    queued_input_buffer = self->drm_ctx ?
        gst_amc_codec_queue_secure_input_buffer (self->codec, idx,
        &buffer_info, frame->input_buffer)
        : gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info);

    CHK (queued_input_buffer);

    /* We've send some jni buffer to decoder, now let's start the thread, that
       fetches decoded frames and pushes them to srcpad: */
    if (G_UNLIKELY (!self->srcpad_loop_started)
        && !self->codec->tunneled_playback_enabled) {
      /* We do it once after each flush */
      gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
          (GstTaskFunction) gst_amc_video_dec_loop, decoder);
      self->srcpad_loop_started = TRUE;
    }
  }

  /* Sucess */
  error_msg = NULL;

error:
  if (G_UNLIKELY (!queued_input_buffer) && idx >= 0) {
    /* cache input buffer for next time. It will be dropped on flush/reconfigure */
    self->cached_input_buffer = idx;
  }

  if (G_UNLIKELY (error_msg)) {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL), (">>> %s", error_msg));
  }

  gst_video_codec_frame_unref (frame);
  return self->downstream_flow_ret;
}

static GstFlowReturn
gst_amc_video_dec_finish (GstVideoDecoder * decoder)
{
  /* There's a naming confusion in a base class: "finish" function is called on eos */
  return gst_amc_video_dec_eos (decoder);
}

static GstFlowReturn
gst_amc_video_dec_eos (GstVideoDecoder * decoder)
{
  gint idx;
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);
  GST_DEBUG_OBJECT (self, "Sending EOS to the component");

  /* Don't send EOS buffer twice, this doesn't work */
  if (G_UNLIKELY (self->eos)) {
    GST_DEBUG_OBJECT (self, "Component is already EOS");
    return GST_VIDEO_DECODER_FLOW_DROPPED;
  }
  self->eos = TRUE;

  /* Now we need to drain decoder to show the last frame.
     But only if the srcpad's loop is running */
  if (G_UNLIKELY (self->downstream_flow_ret != GST_FLOW_OK ||
          !self->srcpad_loop_started))
    return self->downstream_flow_ret;

  GST_DEBUG_OBJECT (self, "Draining codec");

  /* Unlock loop because we need it to correctly finish processing
     and signal us about received "eos" jni buffer */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  idx = gst_amc_codec_dequeue_input_buffer (self->codec, 500000);

  /* Now we're ready to send "EOS" buffer to the MediaCodec */
  if (G_LIKELY (idx >= 0 && idx < self->n_input_buffers)) {
    GstAmcBufferInfo buffer_info = {
      .presentation_time_us =
          gst_util_uint64_scale (self->last_upstream_ts, 1, GST_USECOND),
      .flags = BUFFER_FLAG_END_OF_STREAM
    };

    /* Now we're queuing the eos buffer, and start
       waiting until codec_loop will get last frame and signal us.
       It'll signal eather if it will receive the eos buffer, eather
       if for some reason the task is going to stop..
     */
    g_mutex_lock (self->drain_lock);
    if (gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info)) {
      GST_ERROR_OBJECT (self, "Waiting until codec is drained");

      self->drain_cond_signalling = FALSE;
      while (!self->drain_cond_signalling)
        g_cond_wait (self->drain_cond, self->drain_lock);

      GST_ERROR_OBJECT (self, "Drained codec");
      /* Sucess */
      ret = GST_FLOW_OK;
    } else
      GST_ERROR_OBJECT (self, "Failed to queue input buffer during draining");
    g_mutex_unlock (self->drain_lock);

  } else
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for EOS: %d/%d", idx,
        self->n_input_buffers);

  GST_VIDEO_DECODER_STREAM_LOCK (self);
  return ret;
}
