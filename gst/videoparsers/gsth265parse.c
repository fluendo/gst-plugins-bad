/* GStreamer H.265 Parser
 * Copyright (C) 2013 Intel Corporation
 *  Contact:Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 *               2018 Fluendo S.A <support@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include <gst/base/gstadapter.h>
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include "gsth265parse.h"

#include <string.h>

GST_DEBUG_CATEGORY (h265_parse_debug);
#define GST_CAT_DEFAULT h265_parse_debug

#define DEFAULT_CONFIG_INTERVAL      (0)

enum
{
  PROP_0,
  PROP_CONFIG_INTERVAL
};

enum
{
  GST_H265_PARSE_FORMAT_NONE,
  GST_H265_PARSE_FORMAT_HVC1,
  GST_H265_PARSE_FORMAT_HEV1,
  GST_H265_PARSE_FORMAT_BYTE
};

enum
{
  GST_H265_PARSE_ALIGN_NONE = 0,
  GST_H265_PARSE_ALIGN_NAL,
  GST_H265_PARSE_ALIGN_AU
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, parsed = (boolean) true, "
        "stream-format=(string) { hvc1, hev1, byte-stream }, "
        "alignment=(string) { au, nal }"));

GST_BOILERPLATE (GstH265Parse, gst_h265_parse, GstBaseParse,
    GST_TYPE_BASE_PARSE);


static void gst_h265_parse_finalize (GObject * object);

static gboolean gst_h265_parse_start (GstBaseParse * parse);
static gboolean gst_h265_parse_stop (GstBaseParse * parse);
static GstFlowReturn gst_h265_parse_parse_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);
static GstFlowReturn gst_h265_parse_pre_push_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame);

static void gst_h265_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_h265_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_h265_parse_set_caps (GstBaseParse * parse, GstCaps * caps);
static gboolean gst_h265_parse_event (GstBaseParse * parse, GstEvent * event);
static gboolean gst_h265_parse_src_event (GstBaseParse * parse,
    GstEvent * event);
static gboolean
gst_h265_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize);
static GstFlowReturn gst_h265_parse_chain (GstPad * pad, GstBuffer * buffer);
static GstCaps *gst_h265_parse_get_caps (GstBaseParse * parse);

static void
gst_h265_parse_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gst_element_class_set_details_simple (gstelement_class, "H.265 parser",
      "Codec/Parser/Converter/Video",
      "Parses H.265 streams", "Fluendo S.A <support@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (h265_parse_debug, "h265parse", 0, "h265 parser");
}


static void
gst_h265_parse_class_init (GstH265ParseClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (h265_parse_debug, "h265parse", 0, "h265 parser");

  gobject_class->finalize = gst_h265_parse_finalize;
  gobject_class->set_property = gst_h265_parse_set_property;
  gobject_class->get_property = gst_h265_parse_get_property;

  g_object_class_install_property (gobject_class, PROP_CONFIG_INTERVAL,
      g_param_spec_uint ("config-interval",
          "VPS SPS PPS Send Interval",
          "Send VPS, SPS and PPS Insertion Interval in seconds (sprop parameter sets "
          "will be multiplexed in the data stream when detected.) (0 = disabled)",
          0, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  /* Override BaseParse vfuncs */
  parse_class->start = GST_DEBUG_FUNCPTR (gst_h265_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_h265_parse_stop);
  parse_class->check_valid_frame =
      GST_DEBUG_FUNCPTR (gst_h265_parse_check_valid_frame);
  parse_class->parse_frame = GST_DEBUG_FUNCPTR (gst_h265_parse_parse_frame);
  parse_class->pre_push_frame =
      GST_DEBUG_FUNCPTR (gst_h265_parse_pre_push_frame);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_h265_parse_set_caps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_h265_parse_get_caps);
  parse_class->event = GST_DEBUG_FUNCPTR (gst_h265_parse_event);
  parse_class->src_event = GST_DEBUG_FUNCPTR (gst_h265_parse_src_event);
}

static void
gst_h265_parse_init (GstH265Parse * h265parse, GstH265ParseClass * g_class)
{
  h265parse->frame_out = gst_adapter_new ();

  h265parse->parse_chain =
      GST_PAD_CHAINFUNC (GST_BASE_PARSE_SINK_PAD (h265parse));
  gst_pad_set_chain_function (GST_BASE_PARSE_SINK_PAD (h265parse),
      gst_h265_parse_chain);
}


static void
gst_h265_parse_finalize (GObject * object)
{
  GstH265Parse *h265parse = GST_H265_PARSE (object);

  g_object_unref (h265parse->frame_out);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_h265_parse_reset_frame (GstH265Parse * h265parse)
{
  GST_DEBUG_OBJECT (h265parse, "reset frame");

  /* done parsing; reset state */
  h265parse->current_off = -1;

  h265parse->picture_start = FALSE;
  h265parse->update_caps = FALSE;
  h265parse->idr_pos = -1;
  h265parse->sei_pos = -1;
  h265parse->keyframe = FALSE;
  h265parse->header = FALSE;
  gst_adapter_clear (h265parse->frame_out);
}

static void
gst_h265_parse_reset (GstH265Parse * h265parse)
{
  h265parse->width = 0;
  h265parse->height = 0;
  h265parse->fps_num = 0;
  h265parse->fps_den = 0;
  h265parse->upstream_par_n = -1;
  h265parse->upstream_par_d = -1;
  h265parse->parsed_par_n = 0;
  h265parse->parsed_par_n = 0;
  gst_buffer_replace (&h265parse->codec_data, NULL);
  gst_buffer_replace (&h265parse->codec_data_in, NULL);
  h265parse->nal_length_size = 4;
  h265parse->packetized = FALSE;
  h265parse->transform = FALSE;

  h265parse->align = GST_H265_PARSE_ALIGN_NONE;
  h265parse->format = GST_H265_PARSE_FORMAT_NONE;

  h265parse->last_report = GST_CLOCK_TIME_NONE;
  h265parse->push_codec = FALSE;
  h265parse->have_pps = FALSE;
  h265parse->have_sps = FALSE;
  h265parse->have_vps = FALSE;

  h265parse->sent_codec_tag = FALSE;

  h265parse->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  h265parse->force_key_unit_event = NULL;

  gst_h265_parse_reset_frame (h265parse);
}

static gboolean
gst_h265_parse_start (GstBaseParse * parse)
{
  GstH265Parse *h265parse = GST_H265_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "start");
  gst_h265_parse_reset (h265parse);

  h265parse->nalparser = gst_h265_parser_new ();

  gst_base_parse_set_min_frame_size (parse, 7);

  return TRUE;
}

static gboolean
gst_h265_parse_stop (GstBaseParse * parse)
{
  guint i;
  GstH265Parse *h265parse = GST_H265_PARSE (parse);

  GST_DEBUG_OBJECT (parse, "stop");
  gst_h265_parse_reset (h265parse);

  for (i = 0; i < GST_H265_MAX_VPS_COUNT; i++)
    gst_buffer_replace (&h265parse->vps_nals[i], NULL);
  for (i = 0; i < GST_H265_MAX_SPS_COUNT; i++)
    gst_buffer_replace (&h265parse->sps_nals[i], NULL);
  for (i = 0; i < GST_H265_MAX_PPS_COUNT; i++)
    gst_buffer_replace (&h265parse->pps_nals[i], NULL);

  gst_h265_parser_free (h265parse->nalparser);

  return TRUE;
}

static const gchar *
gst_h265_parse_get_string (GstH265Parse * parse, gboolean format, gint code)
{
  if (format) {
    switch (code) {
      case GST_H265_PARSE_FORMAT_HVC1:
        return "hvc1";
      case GST_H265_PARSE_FORMAT_HEV1:
        return "hev1";
      case GST_H265_PARSE_FORMAT_BYTE:
        return "byte-stream";
      default:
        return "none";
    }
  } else {
    switch (code) {
      case GST_H265_PARSE_ALIGN_NAL:
        return "nal";
      case GST_H265_PARSE_ALIGN_AU:
        return "au";
      default:
        return "none";
    }
  }
}

static void
gst_h265_parse_format_from_caps (GstCaps * caps, guint * format, guint * align)
{
  g_return_if_fail (gst_caps_is_fixed (caps));

  GST_DEBUG ("parsing caps: %" GST_PTR_FORMAT, caps);

  if (format)
    *format = GST_H265_PARSE_FORMAT_NONE;

  if (align)
    *align = GST_H265_PARSE_ALIGN_NONE;

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;

    if (format) {
      if ((str = gst_structure_get_string (s, "stream-format"))) {
        if (strcmp (str, "hvc1") == 0)
          *format = GST_H265_PARSE_FORMAT_HVC1;
        else if (strcmp (str, "hev1") == 0)
          *format = GST_H265_PARSE_FORMAT_HEV1;
        else if (strcmp (str, "byte-stream") == 0)
          *format = GST_H265_PARSE_FORMAT_BYTE;
      }
    }

    if (align) {
      if ((str = gst_structure_get_string (s, "alignment"))) {
        if (strcmp (str, "au") == 0)
          *align = GST_H265_PARSE_ALIGN_AU;
        else if (strcmp (str, "nal") == 0)
          *align = GST_H265_PARSE_ALIGN_NAL;
      }
    }
  }
}

/* check downstream caps to configure format and alignment */
static void
gst_h265_parse_negotiate (GstH265Parse * h265parse, gint in_format,
    GstCaps * in_caps)
{
  GstCaps *caps;
  guint format = GST_H265_PARSE_FORMAT_NONE;
  guint align = GST_H265_PARSE_ALIGN_NONE;

  g_return_if_fail ((in_caps == NULL) || gst_caps_is_fixed (in_caps));

  caps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (h265parse));
  GST_DEBUG_OBJECT (h265parse, "allowed caps: %" GST_PTR_FORMAT, caps);

  /* concentrate on leading structure, since decodebin parser
   * capsfilter always includes parser template caps */
  if (caps) {
    caps = gst_caps_make_writable (caps);
    gst_caps_truncate (caps);
    GST_DEBUG_OBJECT (h265parse, "negotiating with caps: %" GST_PTR_FORMAT,
        caps);
  }

  if (in_caps && caps) {
    if (gst_caps_can_intersect (in_caps, caps)) {
      GST_DEBUG_OBJECT (h265parse, "downstream accepts upstream caps");
      gst_h265_parse_format_from_caps (in_caps, &format, &align);
      gst_caps_unref (caps);
      caps = NULL;
    }
  }

  /* FIXME We could fail the negotiation immediatly if caps are empty */
  if (caps && !gst_caps_is_empty (caps)) {
    /* fixate to avoid ambiguity with lists when parsing */
    gst_pad_fixate_caps (GST_BASE_PARSE_SRC_PAD (h265parse), caps);

    gst_h265_parse_format_from_caps (caps, &format, &align);
    gst_caps_unref (caps);
  }

  /* default */
  if (!format)
    format = GST_H265_PARSE_FORMAT_BYTE;
  if (!align)
    align = GST_H265_PARSE_ALIGN_AU;

  GST_DEBUG_OBJECT (h265parse, "selected format %s, alignment %s",
      gst_h265_parse_get_string (h265parse, TRUE, format),
      gst_h265_parse_get_string (h265parse, FALSE, align));

  h265parse->format = format;
  h265parse->align = align;

  h265parse->transform = (in_format != h265parse->format);
}

static GstBuffer *
gst_h265_parse_wrap_nal (GstH265Parse * h265parse, guint format, guint8 * data,
    guint size)
{
  GstBuffer *buf;
  guint nl = h265parse->nal_length_size;

  GST_DEBUG_OBJECT (h265parse, "nal length %d", size);

  buf = gst_buffer_new_and_alloc (size + 4 + nl);
  if (format == GST_H265_PARSE_FORMAT_HVC1
      || format == GST_H265_PARSE_FORMAT_HEV1) {

    GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), size << (32 - 8 * nl));
  } else {
    /* HACK: nl should always be 4 here, otherwise this won't work.
     * There are legit cases where nl in hevc stream is 2, but byte-stream
     * SC is still always 4 bytes. */
    nl = 4;

    GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), 1);
  }

  GST_BUFFER_SIZE (buf) = size + nl;
  memcpy (GST_BUFFER_DATA (buf) + nl, data, size);

  return buf;
}

static void
gst_h265_parser_store_nal (GstH265Parse * h265parse, guint id,
    GstH265NalUnitType naltype, GstH265NalUnit * nalu)
{
  GstBuffer *buf, **store;
  guint size = nalu->size, store_size;

  if (naltype == GST_H265_NAL_VPS) {
    store_size = GST_H265_MAX_VPS_COUNT;
    store = h265parse->vps_nals;
    GST_DEBUG_OBJECT (h265parse, "storing vps %u", id);
  } else if (naltype == GST_H265_NAL_SPS) {
    store_size = GST_H265_MAX_SPS_COUNT;
    store = h265parse->sps_nals;
    GST_DEBUG_OBJECT (h265parse, "storing sps %u", id);
  } else if (naltype == GST_H265_NAL_PPS) {
    store_size = GST_H265_MAX_PPS_COUNT;
    store = h265parse->pps_nals;
    GST_DEBUG_OBJECT (h265parse, "storing pps %u", id);
  } else
    return;

  if (id >= store_size) {
    GST_DEBUG_OBJECT (h265parse, "unable to store nal, id out-of-range %d", id);
    return;
  }

  buf = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buf), nalu->data + nalu->offset, size);

  if (store[id])
    gst_buffer_unref (store[id]);

  store[id] = buf;
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *nal_names[] = {
  "Slice_TRAIL_N",
  "Slice_TRAIL_R",
  "Slice_TSA_N",
  "Slice_TSA_R",
  "Slice_STSA_N",
  "Slice_STSA_R",
  "Slice_RADL_N",
  "Slice_RADL_R",
  "SLICE_RASL_N",
  "SLICE_RASL_R",
  "Invalid (10)",
  "Invalid (11)",
  "Invalid (12)",
  "Invalid (13)",
  "Invalid (14)",
  "Invalid (15)",
  "SLICE_BLA_W_LP",
  "SLICE_BLA_W_RADL",
  "SLICE_BLA_N_LP",
  "SLICE_IDR_W_RADL",
  "SLICE_IDR_N_LP",
  "SLICE_CRA_NUT",
  "Invalid (22)",
  "Invalid (23)",
  "Invalid (24)",
  "Invalid (25)",
  "Invalid (26)",
  "Invalid (27)",
  "Invalid (28)",
  "Invalid (29)",
  "Invalid (30)",
  "Invalid (31)",
  "VPS",
  "SPS",
  "PPS",
  "AUD",
  "EOS",
  "EOB",
  "FD",
  "PREFIX_SEI",
  "SUFFIX_SEI"
};

static const gchar *
_nal_name (GstH265NalUnitType nal_type)
{
  if (nal_type <= GST_H265_NAL_SUFFIX_SEI)
    return nal_names[nal_type];
  return "Invalid";
}
#endif

/* caller guarantees 2 bytes of nal payload */
static void
gst_h265_parse_process_nal (GstH265Parse * h265parse, GstH265NalUnit * nalu)
{
  GstH265PPS pps = { 0, };
  GstH265SPS sps = { 0, };
  GstH265VPS vps = { 0, };
  gboolean is_irap;
  guint nal_type;
  GstH265Parser *nalparser = h265parse->nalparser;
  GstH265ParserResult pres = GST_H265_PARSER_ERROR;

  /* nothing to do for broken input */
  if (G_UNLIKELY (nalu->size < 2)) {
    GST_DEBUG_OBJECT (h265parse, "not processing nal size %u", nalu->size);
    return;
  }

  /* we have a peek as well */
  nal_type = nalu->type;

  GST_DEBUG_OBJECT (h265parse, "processing nal of type %u %s, size %u",
      nal_type, _nal_name (nal_type), nalu->size);
  switch (nal_type) {
    case GST_H265_NAL_VPS:
      /* It is not mandatory to have VPS in the stream. But it might
       * be needed for other extensions like svc */
      pres = gst_h265_parser_parse_vps (nalparser, nalu, &vps);
      if (pres != GST_H265_PARSER_OK)
        GST_WARNING_OBJECT (h265parse, "failed to parse VPS");

      GST_DEBUG_OBJECT (h265parse, "triggering src caps check");
      h265parse->update_caps = TRUE;
      h265parse->have_vps = TRUE;
      if (h265parse->push_codec && h265parse->have_pps) {
        /* VPS/SPS/PPS found in stream before the first pre_push_frame, no need
         * to forcibly push at start */
        GST_INFO_OBJECT (h265parse, "have VPS/SPS/PPS in stream");
        h265parse->push_codec = FALSE;
        h265parse->have_vps = FALSE;
        h265parse->have_sps = FALSE;
        h265parse->have_pps = FALSE;
      }

      gst_h265_parser_store_nal (h265parse, vps.id, nal_type, nalu);
      h265parse->header |= TRUE;
      break;
    case GST_H265_NAL_SPS:
      pres = gst_h265_parser_parse_sps (nalparser, nalu, &sps, TRUE);


      /* arranged for a fallback sps.id, so use that one and only warn */
      if (pres != GST_H265_PARSER_OK)
        GST_WARNING_OBJECT (h265parse, "failed to parse SPS:");

      GST_DEBUG_OBJECT (h265parse, "triggering src caps check");
      h265parse->update_caps = TRUE;
      h265parse->have_sps = TRUE;
      if (h265parse->push_codec && h265parse->have_pps) {
        /* SPS and PPS found in stream before the first pre_push_frame, no need
         * to forcibly push at start */
        GST_INFO_OBJECT (h265parse, "have SPS/PPS in stream");
        h265parse->push_codec = FALSE;
        h265parse->have_sps = FALSE;
        h265parse->have_pps = FALSE;
      }

      gst_h265_parser_store_nal (h265parse, sps.id, nal_type, nalu);
      h265parse->header |= TRUE;
      break;
    case GST_H265_NAL_PPS:
      pres = gst_h265_parser_parse_pps (nalparser, nalu, &pps);


      /* arranged for a fallback pps.id, so use that one and only warn */
      if (pres != GST_H265_PARSER_OK)
        GST_WARNING_OBJECT (h265parse, "failed to parse PPS:");

      /* parameters might have changed, force caps check */
      if (!h265parse->have_pps) {
        GST_DEBUG_OBJECT (h265parse, "triggering src caps check");
        h265parse->update_caps = TRUE;
      }
      h265parse->have_pps = TRUE;
      if (h265parse->push_codec && h265parse->have_sps) {
        /* SPS and PPS found in stream before the first pre_push_frame, no need
         * to forcibly push at start */
        GST_INFO_OBJECT (h265parse, "have SPS/PPS in stream");
        h265parse->push_codec = FALSE;
        h265parse->have_sps = FALSE;
        h265parse->have_pps = FALSE;
      }

      gst_h265_parser_store_nal (h265parse, pps.id, nal_type, nalu);
      h265parse->header |= TRUE;
      break;
    case GST_H265_NAL_PREFIX_SEI:
    case GST_H265_NAL_SUFFIX_SEI:
      /*Fixme: parse sei messages */
      /* mark SEI pos */
      if (h265parse->sei_pos == -1) {
        if (h265parse->transform)
          h265parse->sei_pos = gst_adapter_available (h265parse->frame_out);
        else
          h265parse->sei_pos = nalu->sc_offset;
        GST_DEBUG_OBJECT (h265parse, "marking SEI in frame at offset %d",
            h265parse->sei_pos);
      }
      break;

    case GST_H265_NAL_SLICE_TRAIL_N:
    case GST_H265_NAL_SLICE_TRAIL_R:
    case GST_H265_NAL_SLICE_TSA_N:
    case GST_H265_NAL_SLICE_TSA_R:
    case GST_H265_NAL_SLICE_STSA_N:
    case GST_H265_NAL_SLICE_STSA_R:
    case GST_H265_NAL_SLICE_RADL_N:
    case GST_H265_NAL_SLICE_RADL_R:
    case GST_H265_NAL_SLICE_RASL_N:
    case GST_H265_NAL_SLICE_RASL_R:
    case GST_H265_NAL_SLICE_BLA_W_LP:
    case GST_H265_NAL_SLICE_BLA_W_RADL:
    case GST_H265_NAL_SLICE_BLA_N_LP:
    case GST_H265_NAL_SLICE_IDR_W_RADL:
    case GST_H265_NAL_SLICE_IDR_N_LP:
    case GST_H265_NAL_SLICE_CRA_NUT:
    {
      GstH265SliceHdr slice;

      pres = gst_h265_parser_parse_slice_hdr (nalparser, nalu, &slice);

      if (pres == GST_H265_PARSER_OK) {
        if (GST_H265_IS_I_SLICE (&slice))
          h265parse->keyframe |= TRUE;
      }
      if (slice.first_slice_segment_in_pic_flag == 1)
        GST_DEBUG_OBJECT (h265parse,
            "frame start, first_slice_segment_in_pic_flag = 1");

      GST_DEBUG_OBJECT (h265parse,
          "parse result %d, first slice_segment: %u, slice type: %u",
          pres, slice.first_slice_segment_in_pic_flag, slice.type);

      gst_h265_slice_hdr_free (&slice);
    }

      is_irap = ((nal_type >= GST_H265_NAL_SLICE_BLA_W_LP)
          && (nal_type <= GST_H265_NAL_SLICE_CRA_NUT)) ? TRUE : FALSE;
      if (G_LIKELY (!is_irap && !h265parse->push_codec))
        break;

      /* if we need to sneak codec NALs into the stream,
       * this is a good place, so fake it as IDR
       * (which should be at start anyway) */
      /* mark where config needs to go if interval expired */
      /* mind replacement buffer if applicable */
      if (h265parse->idr_pos == -1) {
        if (h265parse->transform)
          h265parse->idr_pos = gst_adapter_available (h265parse->frame_out);
        else
          h265parse->idr_pos = nalu->sc_offset;
        GST_DEBUG_OBJECT (h265parse, "marking IDR in frame at offset %d",
            h265parse->idr_pos);
      }
      /* if SEI preceeds (faked) IDR, then we have to insert config there */
      if (h265parse->sei_pos >= 0 && h265parse->idr_pos > h265parse->sei_pos) {
        h265parse->idr_pos = h265parse->sei_pos;
        GST_DEBUG_OBJECT (h265parse, "moved IDR mark to SEI position %d",
            h265parse->idr_pos);
      }
      break;
    default:
      gst_h265_parser_parse_nal (nalparser, nalu);
  }

  /* if HEVC output needed, collect properly prefixed nal in adapter,
   * and use that to replace outgoing buffer data later on */
  if (h265parse->transform) {
    GstBuffer *buf;

    GST_LOG_OBJECT (h265parse, "collecting NAL in HEVC frame");
    buf = gst_h265_parse_wrap_nal (h265parse, h265parse->format,
        nalu->data + nalu->offset, nalu->size);
    gst_adapter_push (h265parse->frame_out, buf);
  }
}

/* caller guarantees at least 3 bytes of nal payload for each nal
 * returns TRUE if next_nal indicates that nal terminates an AU */
static inline gboolean
gst_h265_parse_collect_nal (GstH265Parse * h265parse, const guint8 * data,
    guint size, GstH265NalUnit * nalu)
{
  gboolean complete;
  GstH265ParserResult parse_res;
  GstH265NalUnitType nal_type = nalu->type;
  GstH265NalUnit nnalu;

  GST_DEBUG_OBJECT (h265parse, "parsing collected nal");
  parse_res = gst_h265_parser_identify_nalu (h265parse->nalparser, data,
      nalu->offset + nalu->size, size, &nnalu);

  if (parse_res == GST_H265_PARSER_ERROR)
    return FALSE;

  /* determine if AU complete */
  GST_LOG_OBJECT (h265parse, "nal type: %d %s", nal_type, _nal_name (nal_type));
  /* coded slice NAL starts a picture,
   * i.e. other types become aggregated in front of it */
  h265parse->picture_start |= ((nal_type >= GST_H265_NAL_SLICE_TRAIL_N
          && nal_type <= GST_H265_NAL_SLICE_RASL_R)
      || (nal_type >= GST_H265_NAL_SLICE_BLA_W_LP
          && nal_type <= RESERVED_IRAP_NAL_TYPE_MAX));

  /* consider a coded slices (IRAP or not) to start a picture,
   * (so ending the previous one) if first_slice_segment_in_pic_flag == 1*/
  nal_type = nnalu.type;
  complete = h265parse->picture_start && ((nal_type >= GST_H265_NAL_VPS
          && nal_type <= GST_H265_NAL_AUD)
      || nal_type == GST_H265_NAL_PREFIX_SEI || (nal_type >= 41
          && nal_type <= 44) || (nal_type >= 48 && nal_type <= 55));

  GST_LOG_OBJECT (h265parse, "next nal type: %d %s", nal_type,
      _nal_name (nal_type));

  /* Any VCL Nal unit with first_slice_segment_in_pic_flag == 1 considered start of frame */
  complete |= h265parse->picture_start
      && (((nal_type >= GST_H265_NAL_SLICE_TRAIL_N
              && nal_type <= GST_H265_NAL_SLICE_RASL_R)
          || (nal_type >= GST_H265_NAL_SLICE_BLA_W_LP
              && nal_type <= RESERVED_IRAP_NAL_TYPE_MAX))
      && (nnalu.data[nnalu.offset + 2] & 0x80));

  GST_LOG_OBJECT (h265parse, "au complete: %d", complete);
  return complete;
}


static GstFlowReturn
gst_h265_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstH265Parse *h265parse = GST_H265_PARSE (GST_PAD_PARENT (pad));

  if (h265parse->packetized && buffer) {
    GstBuffer *sub;
    GstFlowReturn ret = GST_FLOW_OK;
    GstH265ParserResult parse_res;
    GstH265NalUnit nalu;
    const guint nl = h265parse->nal_length_size;

    if (nl < 1 || nl > 4) {
      GST_DEBUG_OBJECT (h265parse, "insufficient data to split input");
      gst_buffer_unref (buffer);
      return GST_FLOW_NOT_NEGOTIATED;
    }

    GST_LOG_OBJECT (h265parse,
        "processing packet buffer of size %" G_GSIZE_FORMAT,
        GST_BUFFER_SIZE (buffer));

    parse_res = gst_h265_parser_identify_nalu_hevc (h265parse->nalparser,
        GST_BUFFER_DATA (buffer), 0, GST_BUFFER_SIZE (buffer), nl, &nalu);

    while (parse_res == GST_H265_PARSER_OK) {
      GST_DEBUG_OBJECT (h265parse, "HEVC nal offset %d",
          nalu.offset + nalu.size);

      /* dispatch per NALU if needed */
      if (h265parse->split_packetized) {
        /* convert to NAL aligned byte stream input */
        sub = gst_h265_parse_wrap_nal (h265parse, GST_H265_PARSE_FORMAT_BYTE,
            nalu.data + nalu.offset, nalu.size);
        /* at least this should make sense */
        GST_BUFFER_TIMESTAMP (sub) = GST_BUFFER_TIMESTAMP (buffer);
        /* transfer flags (e.g. DISCONT) for first fragment */
        if (nalu.offset <= nl)
          gst_buffer_copy_metadata (sub, buffer, GST_BUFFER_COPY_FLAGS);

        ret = h265parse->parse_chain (pad, sub);
      } else {
        /* pass-through: no looking for frames (and nal processing),
         * so need to parse to collect data here */
        /* NOTE: so if it is really configured to do so,
         * pre_push can/will still insert codec-data at intervals,
         * which is not really pure pass-through, but anyway ... */
        gst_h265_parse_process_nal (h265parse, &nalu);
      }

      parse_res = gst_h265_parser_identify_nalu_hevc
          (h265parse->nalparser,
          GST_BUFFER_DATA (buffer),
          nalu.offset + nalu.size, GST_BUFFER_SIZE (buffer), nl, &nalu);
    }

    if (h265parse->split_packetized) {
      gst_buffer_unref (buffer);
      return ret;
    } else {
      /* nal processing in pass-through might have collected stuff;
       * ensure nothing happens with this later on */
      gst_adapter_clear (h265parse->frame_out);
    }

    if (parse_res == GST_H265_PARSER_NO_NAL_END ||
        parse_res == GST_H265_PARSER_BROKEN_DATA) {

      if (h265parse->split_packetized) {
        GST_ELEMENT_ERROR (h265parse, STREAM, FAILED, (NULL),
            ("invalid HEVC input data"));
        gst_buffer_unref (buffer);

        return GST_FLOW_ERROR;
      } else {
        /* do not meddle to much in this case */
        GST_DEBUG_OBJECT (h265parse, "parsing packet failed");
      }
    }
  }
  return h265parse->parse_chain (pad, buffer);
}


#define GST_BASE_PARSE_FRAME_FLAG_PARSING   0x10000

static gboolean
gst_h265_parse_check_valid_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, guint * framesize, gint * skipsize)
{
  GstH265Parse *h265parse = GST_H265_PARSE (parse);
  GstBuffer *buffer = frame->buffer;
  guint8 *data;
  gsize size;
  gint current_off = 0;
  gboolean drain, nonext;
  GstH265Parser *nalparser = h265parse->nalparser;
  GstH265NalUnit nalu;
  GstH265ParserResult pres;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  /* expect at least 3 bytes startcode == sc, and 3 bytes NALU payload */
  if (G_UNLIKELY (size < 6)) {
    *skipsize = 1;
    return GST_FLOW_OK;
  }

  /* need to configure aggregation */
  if (G_UNLIKELY (h265parse->format == GST_H265_PARSE_FORMAT_NONE))
    gst_h265_parse_negotiate (h265parse, GST_H265_PARSE_FORMAT_BYTE, NULL);

  /* avoid stale cached parsing state */
  if (!(frame->flags & GST_BASE_PARSE_FRAME_FLAG_PARSING)) {
    GST_LOG_OBJECT (h265parse, "parsing new frame");
    gst_h265_parse_reset_frame (h265parse);
    frame->flags |= GST_BASE_PARSE_FRAME_FLAG_PARSING;
  } else {
    GST_LOG_OBJECT (h265parse, "resuming frame parsing");
  }

  drain = GST_BASE_PARSE_DRAINING (parse);
  nonext = FALSE;

  current_off = h265parse->current_off;
  if (current_off < 0)
    current_off = 0;
  g_assert (current_off < size);
  GST_DEBUG_OBJECT (h265parse, "last parse position %d", current_off);

  /* check for initial skip */
  if (h265parse->current_off == -1) {
    pres =
        gst_h265_parser_identify_nalu_unchecked (nalparser, data, current_off,
        size, &nalu);
    switch (pres) {
      case GST_H265_PARSER_OK:
        if (nalu.sc_offset > 0) {
          *skipsize = nalu.sc_offset;
          goto skip;
        }
        break;
      case GST_H265_PARSER_NO_NAL:
        *skipsize = size - 3;
        goto skip;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  while (TRUE) {
    pres =
        gst_h265_parser_identify_nalu (nalparser, data, current_off, size,
        &nalu);

    switch (pres) {
      case GST_H265_PARSER_OK:
        GST_DEBUG_OBJECT (h265parse, "complete nal (offset, size): (%u, %u) ",
            nalu.offset, nalu.size);
        break;
      case GST_H265_PARSER_NO_NAL_END:
        GST_DEBUG_OBJECT (h265parse, "not a complete nal found at offset %u",
            nalu.offset);
        /* if draining, accept it as complete nal */
        if (drain) {
          nonext = TRUE;
          nalu.size = size - nalu.offset;
          GST_DEBUG_OBJECT (h265parse, "draining, accepting with size %u",
              nalu.size);
          /* if it's not too short at least */
          if (nalu.size < 3)
            goto broken;
          break;
        }
        /* otherwise need more */
        goto more;
      case GST_H265_PARSER_BROKEN_LINK:
        GST_ELEMENT_ERROR (h265parse, STREAM, FORMAT,
            ("Error parsing H.265 stream"),
            ("The link to structure needed for the parsing couldn't be found"));
        goto invalid_stream;
      case GST_H265_PARSER_ERROR:
        /* should not really occur either */
        GST_ELEMENT_ERROR (h265parse, STREAM, FORMAT,
            ("Error parsing H.265 stream"), ("Invalid H.265 stream"));
        goto invalid_stream;
      case GST_H265_PARSER_NO_NAL:
        GST_ELEMENT_ERROR (h265parse, STREAM, FORMAT,
            ("Error parsing H.265 stream"), ("No H.265 NAL unit found"));
        goto invalid_stream;
      case GST_H265_PARSER_BROKEN_DATA:
        GST_WARNING_OBJECT (h265parse, "input stream is corrupt; "
            "it contains a NAL unit of length %u", nalu.size);
      broken:
        /* broken nal at start -> arrange to skip it,
         * otherwise have it terminate current au
         * (and so it will be skipped on next frame round) */
        if (current_off == 0) {
          GST_DEBUG_OBJECT (h265parse, "skipping broken nal");
          *skipsize = nalu.offset;
          goto skip;
        } else {
          GST_DEBUG_OBJECT (h265parse, "terminating au");
          nalu.size = 0;
          nalu.offset = nalu.sc_offset;
          goto end;
        }
      default:
        g_assert_not_reached ();
        break;
    }

    GST_DEBUG_OBJECT (h265parse, "%p complete nal found. Off: %u, Size: %u",
        data, nalu.offset, nalu.size);

    /* simulate no next nal if none needed */
    nonext = nonext || (h265parse->align == GST_H265_PARSE_ALIGN_NAL);

    if (!nonext) {
      if (nalu.offset + nalu.size + 5 + 2 > size) {
        GST_DEBUG_OBJECT (h265parse, "not enough data for next NALU");
        if (drain) {
          GST_DEBUG_OBJECT (h265parse, "but draining anyway");
          nonext = TRUE;
        } else {
          goto more;
        }
      }
    }

    gst_h265_parse_process_nal (h265parse, &nalu);

    if (nonext)
      break;

    /* if no next nal, we know it's complete here */
    if (gst_h265_parse_collect_nal (h265parse, data, size, &nalu))
      break;

    GST_DEBUG_OBJECT (h265parse, "Looking for more");
    current_off = nalu.offset + nalu.size;
  }

end:
  *framesize = nalu.offset + nalu.size;

  return TRUE;

more:
  *framesize = G_MAXUINT;
  *skipsize = 0;

  /* Restart parsing from here next time */
  if (current_off > 0)
    h265parse->current_off = current_off;

  /* Fall-through. */
out:
  return GST_FLOW_OK;

skip:
  GST_DEBUG_OBJECT (h265parse, "skipping %d", *skipsize);
  gst_h265_parse_reset_frame (h265parse);
  goto out;

invalid_stream:
  return GST_FLOW_ERROR;
}

/* byte together hevc codec data based on collected pps and sps so far */
static GstBuffer *
gst_h265_parse_make_codec_data (GstH265Parse * h265parse)
{
  GstBuffer *buf, *nal;
  gint i, j, k = 0;
  guint vps_size = 0, sps_size = 0, pps_size = 0;
  guint num_vps = 0, num_sps = 0, num_pps = 0;
  gboolean found = FALSE;
  guint8 *data;
  gint nl;
  guint8 num_arrays = 0;
  GstH265SPS *sps = NULL;
  guint16 min_spatial_segmentation_idc = 0;
  GstH265ProfileTierLevel *pft;

  /* only nal payload in stored nals */
  /* Fixme: Current implementation is not embedding SEI in codec_data */
  for (i = 0; i < GST_H265_MAX_VPS_COUNT; i++) {
    if ((nal = h265parse->vps_nals[i])) {
      num_vps++;
      /* size bytes also count */
      vps_size += GST_BUFFER_SIZE (nal) + 2;
    }
  }
  if (num_vps > 0)
    num_arrays++;

  for (i = 0; i < GST_H265_MAX_SPS_COUNT; i++) {
    if ((nal = h265parse->sps_nals[i])) {
      num_sps++;
      /* size bytes also count */
      sps_size += GST_BUFFER_SIZE (nal) + 2;
      found = TRUE;
    }
  }
  if (num_sps > 0)
    num_arrays++;

  for (i = 0; i < GST_H265_MAX_PPS_COUNT; i++) {
    if ((nal = h265parse->pps_nals[i])) {
      num_pps++;
      /* size bytes also count */
      pps_size += GST_BUFFER_SIZE (nal) + 2;
    }
  }
  if (num_pps > 0)
    num_arrays++;

  GST_DEBUG_OBJECT (h265parse,
      "constructing codec_data: num_vps =%d num_sps=%d, num_pps=%d", num_vps,
      num_sps, num_pps);

  if (!found)
    return NULL;

  sps = h265parse->nalparser->last_sps;
  if (!sps)
    return NULL;

  buf =
      gst_buffer_new_and_alloc
      (23 + num_arrays + (3 * num_arrays) + vps_size + sps_size + pps_size);
  data = GST_BUFFER_DATA (buf);
  memset (data, 0, GST_BUFFER_SIZE (buf));
  nl = h265parse->nal_length_size;

  pft = &sps->profile_tier_level;
  if (sps->vui_parameters_present_flag)
    min_spatial_segmentation_idc = sps->vui_params.min_spatial_segmentation_idc;

  /* HEVCDecoderConfigurationVersion = 1
   * profile_space | tier_flat | profile_idc |
   * profile_compatibility_flags | constraint_indicator_flags |
   * level_idc */
  data[0] = 1;
  data[1] =
      (pft->profile_space << 5) | (pft->tier_flag << 5) | pft->profile_idc;
  for (i = 2; i < 6; i++) {
    for (j = 7; j >= 0; j--) {
      data[i] |= (pft->profile_compatibility_flag[k] << j);
      k++;
    }
  }
  data[6] |=
      (pft->progressive_source_flag << 7) | (pft->interlaced_source_flag << 6) |
      (pft->
      non_packed_constraint_flag << 5) | (pft->frame_only_constraint_flag << 4);
  data[12] = pft->level_idc;
  /* min_spatial_segmentation_idc */
  GST_WRITE_UINT16_BE (data + 13, min_spatial_segmentation_idc);
  data[13] |= 0xf0;
  data[15] = 0xfc;              /* keeping parrallelismType as zero (unknown) */
  data[16] = 0xfc | sps->chroma_format_idc;
  data[17] = 0xf8 | sps->bit_depth_luma_minus8;
  data[18] = 0xf8 | sps->bit_depth_chroma_minus8;
  data[19] = 0x00;              /* keep avgFrameRate as unspecified */
  data[20] = 0x00;              /* keep avgFrameRate as unspecified */
  /* constFrameRate(2 bits): 0, stream may or may not be of constant framerate
   * numTemporalLayers (3 bits): number of temporal layers, value from SPS
   * TemporalIdNested (1 bit): sps_temporal_id_nesting_flag from SPS
   * lengthSizeMinusOne (2 bits): plus 1 indicates the length of the NALUnitLength */
  data[21] =
      0x00 | ((sps->max_sub_layers_minus1 +
          1) << 3) | (sps->temporal_id_nesting_flag << 2) | (nl - 1);
  GST_WRITE_UINT8 (data + 22, num_arrays);      /* numOfArrays */

  data += 23;

  /* VPS */
  if (num_vps > 0) {
    /* array_completeness | reserved_zero bit | nal_unit_type */
    data[0] = 0x00 | 0x20;
    data++;

    GST_WRITE_UINT16_BE (data, num_vps);
    data += 2;

    for (i = 0; i < num_vps; i++) {
      if ((nal = h265parse->vps_nals[i])) {
        gsize nal_size = GST_BUFFER_SIZE (nal);
        GST_WRITE_UINT16_BE (data, nal_size);
        memcpy (data + 2, GST_BUFFER_DATA (nal), nal_size);

        data += 2 + nal_size;
      }
    }
  }

  /* SPS */
  if (num_sps > 0) {
    /* array_completeness | reserved_zero bit | nal_unit_type */
    data[0] = 0x00 | 0x21;
    data++;

    GST_WRITE_UINT16_BE (data, num_sps);
    data += 2;

    for (i = 0; i < num_sps; i++) {
      if ((nal = h265parse->sps_nals[i])) {
        gsize nal_size = GST_BUFFER_SIZE (nal);
        GST_WRITE_UINT16_BE (data, nal_size);
        memcpy (data + 2, GST_BUFFER_DATA (nal), nal_size);

        data += 2 + nal_size;
      }
    }
  }

  /* PPS */
  if (num_pps > 0) {
    /* array_completeness | reserved_zero bit | nal_unit_type */
    data[0] = 0x00 | 0x22;
    data++;

    GST_WRITE_UINT16_BE (data, num_pps);
    data += 2;

    for (i = 0; i < num_pps; i++) {
      if ((nal = h265parse->pps_nals[i])) {
        gsize nal_size = GST_BUFFER_SIZE (nal);
        GST_WRITE_UINT16_BE (data, nal_size);
        memcpy (data + 2, GST_BUFFER_DATA (nal), nal_size);

        data += 2 + nal_size;
      }
    }
  }

  return buf;
}

static void
gst_h265_parse_get_par (GstH265Parse * h265parse, gint * num, gint * den)
{
  if (h265parse->upstream_par_n != -1 && h265parse->upstream_par_d != -1) {
    *num = h265parse->upstream_par_n;
    *den = h265parse->upstream_par_d;
  } else {
    *num = h265parse->parsed_par_n;
    *den = h265parse->parsed_par_d;
  }
}

static const gchar *
digit_to_string (guint digit)
{
  static const char itoa[][2] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
  };

  if (G_LIKELY (digit < 10))
    return itoa[digit];
  else
    return NULL;
}

static const gchar *
get_profile_string (guint8 profile_idc)
{
  const gchar *profile = NULL;

  if (profile_idc == 1)
    profile = "main";
  else if (profile_idc == 2)
    profile = "main-10";
  else if (profile_idc == 3)
    profile = "main-still-picture";

  return profile;
}

static const gchar *
get_tier_string (guint8 tier_flag)
{
  const gchar *tier = NULL;

  if (tier_flag)
    tier = "high";
  else
    tier = "main";

  return tier;
}

static const gchar *
get_level_string (guint8 level_idc)
{
  if (level_idc == 0)
    return NULL;
  else if (level_idc % 30 == 0)
    return digit_to_string (level_idc / 30);
  else {
    switch (level_idc) {
      case 63:
        return "2.1";
        break;
      case 93:
        return "3.1";
        break;
      case 123:
        return "4.1";
        break;
      case 153:
        return "5.1";
        break;
      case 156:
        return "5.2";
        break;
      case 183:
        return "6.1";
        break;
      case 186:
        return "6.2";
        break;
      default:
        return NULL;
    }
  }
}

static GstCaps *
get_compatible_profile_caps (GstH265SPS * sps)
{
  GstCaps *caps = NULL;
  const gchar **profiles = NULL;
  gint i;
  GValue compat_profiles = G_VALUE_INIT;
  g_value_init (&compat_profiles, GST_TYPE_LIST);

  switch (sps->profile_tier_level.profile_idc) {
    case GST_H265_PROFILE_MAIN_10:
      if (sps->profile_tier_level.profile_compatibility_flag[1]) {
        if (sps->profile_tier_level.profile_compatibility_flag[3]) {
          static const gchar *profile_array[] =
              { "main", "main-still-picture", NULL };
          profiles = profile_array;
        } else {
          static const gchar *profile_array[] = { "main", NULL };
          profiles = profile_array;
        }
      }
      break;
    case GST_H265_PROFILE_MAIN:
      if (sps->profile_tier_level.profile_compatibility_flag[3]) {
        static const gchar *profile_array[] =
            { "main-still-picture", "main-10", NULL
        };
        profiles = profile_array;
      } else {
        static const gchar *profile_array[] = { "main-10", NULL };
        profiles = profile_array;
      }
      break;
    case GST_H265_PROFILE_MAIN_STILL_PICTURE:
    {
      static const gchar *profile_array[] = { "main", "main-10", NULL
      };
      profiles = profile_array;
    }
      break;
    default:
      break;
  }

  if (profiles) {
    GValue value = G_VALUE_INIT;
    caps = gst_caps_new_simple ("video/x-h265", NULL);

    for (i = 0; profiles[i]; i++) {
      g_value_init (&value, G_TYPE_STRING);
      g_value_set_string (&value, profiles[i]);
      gst_value_list_append_value (&compat_profiles, &value);
      g_value_unset (&value);
    }
    gst_caps_set_value (caps, "profile", &compat_profiles);
    g_value_unset (&compat_profiles);
  }

  return caps;
}

/* if downstream didn't support the exact profile indicated in sps header,
 * check for the compatible profiles also */
static void
ensure_caps_profile (GstH265Parse * h265parse, GstCaps * caps, GstH265SPS * sps)
{
  GstCaps *peer_caps, *compat_caps;

  peer_caps = gst_pad_get_caps (GST_BASE_PARSE_SRC_PAD (h265parse));

  if (peer_caps && !gst_caps_can_intersect (caps, peer_caps)) {
    GstStructure *structure;

    compat_caps = get_compatible_profile_caps (sps);
    if (compat_caps != NULL) {
      GstCaps *res_caps = NULL;

      res_caps = gst_caps_intersect (peer_caps, compat_caps);

      if (res_caps && !gst_caps_is_empty (res_caps)) {
        const gchar *profile_str = NULL;

        gst_pad_fixate_caps (GST_BASE_PARSE_SRC_PAD (h265parse), res_caps);

        structure = gst_caps_get_structure (res_caps, 0);
        profile_str = gst_structure_get_string (structure, "profile");
        if (profile_str) {
          gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile_str,
              NULL);
          GST_DEBUG_OBJECT (h265parse,
              "Setting compatible profile %s to the caps", profile_str);
        }
      }
      if (res_caps)
        gst_caps_unref (res_caps);
      gst_caps_unref (compat_caps);
    }
  }
  if (peer_caps)
    gst_caps_unref (peer_caps);
}

static void
gst_h265_parse_update_src_caps (GstH265Parse * h265parse, GstCaps * caps)
{
  GstH265SPS *sps;
  GstCaps *sink_caps = NULL, *src_caps;
  gboolean modified = FALSE;
  GstBuffer *buf = NULL;
  GstStructure *s = NULL;

  if (G_UNLIKELY (!GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (h265parse))))
    modified = TRUE;
  else if (G_UNLIKELY (!h265parse->update_caps))
    return;

  /* if this is being called from the first _setcaps call, caps on the sinkpad
   * aren't set yet and so they need to be passed as an argument */
  if (caps)
    sink_caps = gst_caps_ref (caps);
  else if (G_UNLIKELY (!GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (h265parse))))
    sink_caps = gst_pad_get_caps (GST_BASE_PARSE_SINK_PAD (h265parse));

  /* carry over input caps as much as possible; override with our own stuff */
  if (!sink_caps)
    sink_caps = gst_caps_new_simple ("video/x-h265", NULL);
  else
    s = gst_caps_get_structure (sink_caps, 0);

  sps = h265parse->nalparser->last_sps;
  GST_DEBUG_OBJECT (h265parse, "sps: %p", sps);

  /* only codec-data for nice-and-clean au aligned packetized hevc format */
  if ((h265parse->format == GST_H265_PARSE_FORMAT_HVC1
          || h265parse->format == GST_H265_PARSE_FORMAT_HEV1)
      && h265parse->align == GST_H265_PARSE_ALIGN_AU) {
    buf = gst_h265_parse_make_codec_data (h265parse);
    if (buf && h265parse->codec_data) {
      if (GST_BUFFER_SIZE (buf) != GST_BUFFER_SIZE (h265parse->codec_data)
          || memcmp (GST_BUFFER_DATA (h265parse->codec_data),
              GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf)))
        modified = TRUE;

    } else {
      if (!buf && h265parse->codec_data_in)
        buf = gst_buffer_ref (h265parse->codec_data_in);
      modified = TRUE;
    }
  }

  caps = NULL;
  if (G_UNLIKELY (!sps)) {
    caps = gst_caps_copy (sink_caps);
  } else {
    gint crop_width, crop_height;

    if (sps->conformance_window_flag) {
      crop_width = sps->crop_rect_width;
      crop_height = sps->crop_rect_height;
    } else {
      crop_width = sps->width;
      crop_height = sps->height;
    }

    if (G_UNLIKELY (h265parse->width != crop_width ||
            h265parse->height != crop_height)) {
      GST_INFO_OBJECT (h265parse, "resolution changed %dx%d",
          crop_width, crop_height);
      h265parse->width = crop_width;
      h265parse->height = crop_height;
      modified = TRUE;
    }

    /* 0/1 is set as the default in the codec parser */
    if (sps->vui_params.timing_info_present_flag &&
        !(sps->fps_num == 0 && sps->fps_den == 1)) {
      if (G_UNLIKELY (h265parse->fps_num != sps->fps_num
              || h265parse->fps_den != sps->fps_den)) {
        GST_INFO_OBJECT (h265parse, "framerate changed %d/%d",
            sps->fps_num, sps->fps_den);
        h265parse->fps_num = sps->fps_num;
        h265parse->fps_den = sps->fps_den;
        modified = TRUE;
      }
    }

    if (sps->vui_params.aspect_ratio_info_present_flag) {
      if (G_UNLIKELY ((h265parse->parsed_par_n != sps->vui_params.par_n)
              && (h265parse->parsed_par_d != sps->vui_params.par_d))) {
        h265parse->parsed_par_n = sps->vui_params.par_n;
        h265parse->parsed_par_d = sps->vui_params.par_d;
        GST_INFO_OBJECT (h265parse,
            "pixel aspect ratio has been changed %d/%d",
            h265parse->parsed_par_n, h265parse->parsed_par_d);
        modified = TRUE;
      }

    }

    if (G_UNLIKELY (modified || h265parse->update_caps)) {
      gint fps_num = h265parse->fps_num;
      gint fps_den = h265parse->fps_den;
      gint width, height;
      GstClockTime latency;

      caps = gst_caps_copy (sink_caps);

      /* sps should give this but upstream overrides */
#if 0
      /* FIXME: structure "s" sometimes has range instead of int, requires additional debugging */
      if (s && gst_structure_has_field (s, "width"))
        gst_structure_get_int (s, "width", &width);
      else
#endif
        width = h265parse->width;

#if 0
      if (s && gst_structure_has_field (s, "height"))
        gst_structure_get_int (s, "height", &height);
      else
#endif
        height = h265parse->height;

      gst_caps_set_simple (caps, "width", G_TYPE_INT, width,
          "height", G_TYPE_INT, height, NULL);

      /* upstream overrides */
      if (s && gst_structure_has_field (s, "framerate"))
        gst_structure_get_fraction (s, "framerate", &fps_num, &fps_den);

      /* but not necessarily or reliably this */
      if (fps_num > 0 && fps_den > 0) {
        GST_INFO_OBJECT (h265parse, "setting framerate in caps");
        gst_caps_set_simple (caps, "framerate",
            GST_TYPE_FRACTION, fps_num, fps_den, NULL);
        gst_base_parse_set_frame_rate (GST_BASE_PARSE (h265parse),
            fps_num, fps_den, 0, 0);
        latency = gst_util_uint64_scale (GST_SECOND, fps_den, fps_num);
        gst_base_parse_set_latency (GST_BASE_PARSE (h265parse), latency,
            latency);
      }
    }
  }

  if (caps) {
    gint par_n, par_d;

    gst_caps_set_simple (caps, "parsed", G_TYPE_BOOLEAN, TRUE,
        "stream-format", G_TYPE_STRING,
        gst_h265_parse_get_string (h265parse, TRUE, h265parse->format),
        "alignment", G_TYPE_STRING,
        gst_h265_parse_get_string (h265parse, FALSE, h265parse->align), NULL);

    gst_h265_parse_get_par (h265parse, &par_n, &par_d);
    if (par_n != 0 && par_d != 0 &&
        (!s || !gst_structure_has_field (s, "pixel-aspect-ratio"))) {
      GST_INFO_OBJECT (h265parse, "PAR %d/%d", par_n, par_d);
      gst_caps_set_simple (caps, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          par_n, par_d, NULL);
    }

    /* set profile and level in caps */
    if (sps) {
      const gchar *profile, *tier, *level;

      profile = get_profile_string (sps->profile_tier_level.profile_idc);
      if (profile != NULL)
        gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile, NULL);

      tier = get_tier_string (sps->profile_tier_level.tier_flag);
      if (tier != NULL)
        gst_caps_set_simple (caps, "tier", G_TYPE_STRING, tier, NULL);

      level = get_level_string (sps->profile_tier_level.level_idc);
      if (level != NULL)
        gst_caps_set_simple (caps, "level", G_TYPE_STRING, level, NULL);

      /* relax the profile constraint to find a suitable decoder */
      ensure_caps_profile (h265parse, caps, sps);
    }
    src_caps = gst_pad_get_caps (GST_BASE_PARSE_SRC_PAD (h265parse));

    if (src_caps) {
      /* use codec data from old caps for comparison; we don't want to resend caps
         if everything is same except codec data; */
      if (gst_structure_has_field (gst_caps_get_structure (src_caps, 0),
              "codec_data")) {
        gst_caps_set_value (caps, "codec_data",
            gst_structure_get_value (gst_caps_get_structure (src_caps, 0),
                "codec_data"));
      } else if (!buf) {
        GstStructure *s;
        /* remove any left-over codec-data hanging around */
        s = gst_caps_get_structure (caps, 0);
        gst_structure_remove_field (s, "codec_data");
      }
    }

    if (!(src_caps && gst_caps_is_strictly_equal (src_caps, caps))) {
      /* update codec data to new value */
      if (buf) {
        gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, buf, NULL);
        gst_buffer_replace (&h265parse->codec_data, buf);
        gst_buffer_unref (buf);
        buf = NULL;
      } else {
        GstStructure *s;
        /* remove any left-over codec-data hanging around */
        s = gst_caps_get_structure (caps, 0);
        gst_structure_remove_field (s, "codec_data");
        gst_buffer_replace (&h265parse->codec_data, NULL);
      }

      if (!gst_caps_is_fixed (caps))
        gst_caps_truncate (caps);

      if (!gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (h265parse), caps))
        GST_ERROR_OBJECT (h265parse, "Failed to set src caps %" GST_PTR_FORMAT,
            caps);
    }

    if (src_caps)
      gst_caps_unref (src_caps);
    gst_caps_unref (caps);
  }

  gst_caps_unref (sink_caps);
  if (buf)
    gst_buffer_unref (buf);

}

static GstFlowReturn
gst_h265_parse_parse_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstH265Parse *h265parse;
  GstBuffer *buffer;
  guint av;

  h265parse = GST_H265_PARSE (parse);
  buffer = frame->buffer;

  gst_h265_parse_update_src_caps (h265parse, NULL);

  /* Fixme: Implement timestamp interpolation based on SEI Messagses */
  GST_FIXME_OBJECT (h265parse,
      "Implement timestamp/duration interpolation based on SEI message");

  if (h265parse->keyframe)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  /* replace with transformed HEVC output if applicable */
  av = gst_adapter_available (h265parse->frame_out);
  if (av) {
    GstBuffer *buf;

    buf = gst_adapter_take_buffer (h265parse->frame_out, av);
    gst_buffer_copy_metadata (buf, buffer, GST_BUFFER_COPY_ALL);
    gst_buffer_replace (&frame->buffer, buf);
    gst_buffer_unref (buf);
  }

  return GST_FLOW_OK;
}

/* sends a codec NAL downstream, decorating and transforming as needed.
 * No ownership is taken of @nal */
static GstFlowReturn
gst_h265_parse_push_codec_buffer (GstH265Parse * h265parse,
    GstBuffer * nal, GstClockTime ts)
{
  nal = gst_h265_parse_wrap_nal (h265parse, h265parse->format,
      GST_BUFFER_DATA (nal), GST_BUFFER_SIZE (nal));

  GST_BUFFER_TIMESTAMP (nal) = ts;
  GST_BUFFER_DURATION (nal) = 0;

  gst_buffer_set_caps (nal, GST_PAD_CAPS (GST_BASE_PARSE_SRC_PAD (h265parse)));

  return gst_pad_push (GST_BASE_PARSE_SRC_PAD (h265parse), nal);
}

static GstEvent *
check_pending_key_unit_event (GstEvent * pending_event,
    GstSegment * segment, GstClockTime timestamp, guint flags,
    GstClockTime pending_key_unit_ts)
{
  GstClockTime running_time, stream_time;
  gboolean all_headers;
  guint count;
  GstEvent *event = NULL;

  g_return_val_if_fail (segment != NULL, NULL);

  if (pending_event == NULL)
    goto out;

  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      timestamp == GST_CLOCK_TIME_NONE)
    goto out;

  running_time = gst_segment_to_running_time (segment,
      GST_FORMAT_TIME, timestamp);

  GST_INFO ("now %" GST_TIME_FORMAT " wanted %" GST_TIME_FORMAT,
      GST_TIME_ARGS (running_time), GST_TIME_ARGS (pending_key_unit_ts));
  if (GST_CLOCK_TIME_IS_VALID (pending_key_unit_ts) &&
      running_time < pending_key_unit_ts)
    goto out;

  if (flags & GST_BUFFER_FLAG_DELTA_UNIT) {
    GST_DEBUG ("pending force key unit, waiting for keyframe");
    goto out;
  }

  stream_time = gst_segment_to_stream_time (segment,
      GST_FORMAT_TIME, timestamp);

  if (!gst_video_event_parse_upstream_force_key_unit (pending_event,
          NULL, &all_headers, &count)) {
    gst_video_event_parse_downstream_force_key_unit (pending_event, NULL,
        NULL, NULL, &all_headers, &count);
  }

  event =
      gst_video_event_new_downstream_force_key_unit (timestamp, stream_time,
      running_time, all_headers, count);
  gst_event_set_seqnum (event, gst_event_get_seqnum (pending_event));

out:
  return event;
}

static void
gst_h265_parse_prepare_key_unit (GstH265Parse * parse, GstEvent * event)
{
  GstClockTime running_time;
  guint count;
#ifndef GST_DISABLE_GST_DEBUG
  gboolean have_vps, have_sps, have_pps;
  gint i;
#endif

  parse->pending_key_unit_ts = GST_CLOCK_TIME_NONE;
  gst_event_replace (&parse->force_key_unit_event, NULL);

  gst_video_event_parse_downstream_force_key_unit (event,
      NULL, NULL, &running_time, NULL, &count);

  GST_INFO_OBJECT (parse, "pushing downstream force-key-unit event %d "
      "%" GST_TIME_FORMAT " count %d", gst_event_get_seqnum (event),
      GST_TIME_ARGS (running_time), count);
  gst_pad_push_event (GST_BASE_PARSE_SRC_PAD (parse), event);

#ifndef GST_DISABLE_GST_DEBUG
  have_vps = have_sps = have_pps = FALSE;
  for (i = 0; i < GST_H265_MAX_VPS_COUNT; i++) {
    if (parse->vps_nals[i] != NULL) {
      have_vps = TRUE;
      break;
    }
  }
  for (i = 0; i < GST_H265_MAX_SPS_COUNT; i++) {
    if (parse->sps_nals[i] != NULL) {
      have_sps = TRUE;
      break;
    }
  }
  for (i = 0; i < GST_H265_MAX_PPS_COUNT; i++) {
    if (parse->pps_nals[i] != NULL) {
      have_pps = TRUE;
      break;
    }
  }

  GST_INFO_OBJECT (parse,
      "preparing key unit, have vps %d have sps %d have pps %d", have_vps,
      have_sps, have_pps);
#endif

  /* set push_codec to TRUE so that pre_push_frame sends VPS/SPS/PPS again */
  parse->push_codec = TRUE;
}

static GstFlowReturn
gst_h265_parse_pre_push_frame (GstBaseParse * parse, GstBaseParseFrame * frame)
{
  GstH265Parse *h265parse;
  GstBuffer *buffer;
  GstEvent *event;

  h265parse = GST_H265_PARSE (parse);

  buffer = frame->buffer;

  if ((event = check_pending_key_unit_event (h265parse->force_key_unit_event,
              &parse->segment, GST_BUFFER_TIMESTAMP (buffer),
              GST_BUFFER_FLAGS (buffer), h265parse->pending_key_unit_ts))) {
    gst_h265_parse_prepare_key_unit (h265parse, event);
  }

  /* periodic VPS/SPS/PPS sending */
  if (h265parse->interval > 0 || h265parse->push_codec) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);
    guint64 diff;

    /* init */
    if (!GST_CLOCK_TIME_IS_VALID (h265parse->last_report)) {
      h265parse->last_report = timestamp;
    }

    if (h265parse->idr_pos >= 0) {
      GST_LOG_OBJECT (h265parse, "IDR nal at offset %d", h265parse->idr_pos);

      if (timestamp > h265parse->last_report)
        diff = timestamp - h265parse->last_report;
      else
        diff = 0;

      GST_LOG_OBJECT (h265parse,
          "now %" GST_TIME_FORMAT ", last VPS/SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp), GST_TIME_ARGS (h265parse->last_report));

      GST_DEBUG_OBJECT (h265parse,
          "interval since last VPS/SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (diff));

      if (GST_TIME_AS_SECONDS (diff) >= h265parse->interval ||
          h265parse->push_codec) {
        GstBuffer *codec_nal;
        gint i;
        GstClockTime new_ts;

        /* avoid overwriting a perfectly fine timestamp */
        new_ts = GST_CLOCK_TIME_IS_VALID (timestamp) ? timestamp :
            h265parse->last_report;

        if (h265parse->align == GST_H265_PARSE_ALIGN_NAL) {
          /* send separate config NAL buffers */
          GST_DEBUG_OBJECT (h265parse, "- sending VPS/SPS/PPS");
          for (i = 0; i < GST_H265_MAX_VPS_COUNT; i++) {
            if ((codec_nal = h265parse->vps_nals[i])) {
              GST_DEBUG_OBJECT (h265parse, "sending VPS nal");
              gst_h265_parse_push_codec_buffer (h265parse, codec_nal,
                  timestamp);
              h265parse->last_report = new_ts;
            }
          }
          for (i = 0; i < GST_H265_MAX_SPS_COUNT; i++) {
            if ((codec_nal = h265parse->sps_nals[i])) {
              GST_DEBUG_OBJECT (h265parse, "sending SPS nal");
              gst_h265_parse_push_codec_buffer (h265parse, codec_nal,
                  timestamp);
              h265parse->last_report = new_ts;
            }
          }
          for (i = 0; i < GST_H265_MAX_PPS_COUNT; i++) {
            if ((codec_nal = h265parse->pps_nals[i])) {
              GST_DEBUG_OBJECT (h265parse, "sending PPS nal");
              gst_h265_parse_push_codec_buffer (h265parse, codec_nal,
                  timestamp);
              h265parse->last_report = new_ts;
            }
          }
        } else {
          /* insert config NALs into AU */
          GstByteWriter bw;
          GstBuffer *new_buf;
          const gboolean bs = h265parse->format == GST_H265_PARSE_FORMAT_BYTE;
          const gint nls = 4 - h265parse->nal_length_size;
          gboolean ok;

          gst_byte_writer_init_with_size (&bw, GST_BUFFER_SIZE (buffer), FALSE);
          ok = gst_byte_writer_put_data (&bw, GST_BUFFER_DATA (buffer),
              h265parse->idr_pos);

          GST_DEBUG_OBJECT (h265parse, "- inserting VPS/SPS/PPS");
          for (i = 0; i < GST_H265_MAX_VPS_COUNT; i++) {
            if ((codec_nal = h265parse->vps_nals[i])) {
              gsize nal_size = GST_BUFFER_SIZE (codec_nal);
              GST_DEBUG_OBJECT (h265parse, "inserting VPS nal");
              if (bs) {
                ok &= gst_byte_writer_put_uint32_be (&bw, 1);
              } else {
                ok &= gst_byte_writer_put_uint32_be (&bw,
                    (nal_size << (nls * 8)));
                ok &= gst_byte_writer_set_pos (&bw,
                    gst_byte_writer_get_pos (&bw) - nls);
              }

              ok &= gst_byte_writer_put_data (&bw,
                  GST_BUFFER_DATA (codec_nal), GST_BUFFER_SIZE (codec_nal));
              h265parse->last_report = new_ts;
            }
          }
          for (i = 0; i < GST_H265_MAX_SPS_COUNT; i++) {
            if ((codec_nal = h265parse->sps_nals[i])) {
              gsize nal_size = GST_BUFFER_SIZE (codec_nal);
              GST_DEBUG_OBJECT (h265parse, "inserting SPS nal");
              if (bs) {
                ok &= gst_byte_writer_put_uint32_be (&bw, 1);
              } else {
                ok &= gst_byte_writer_put_uint32_be (&bw,
                    (nal_size << (nls * 8)));
                ok &= gst_byte_writer_set_pos (&bw,
                    gst_byte_writer_get_pos (&bw) - nls);
              }

              ok &= gst_byte_writer_put_data (&bw,
                  GST_BUFFER_DATA (codec_nal), GST_BUFFER_SIZE (codec_nal));
              h265parse->last_report = new_ts;
            }
          }
          for (i = 0; i < GST_H265_MAX_PPS_COUNT; i++) {
            if ((codec_nal = h265parse->pps_nals[i])) {
              gsize nal_size = GST_BUFFER_SIZE (codec_nal);
              GST_DEBUG_OBJECT (h265parse, "inserting PPS nal");
              if (bs) {
                ok &= gst_byte_writer_put_uint32_be (&bw, 1);
              } else {
                ok &= gst_byte_writer_put_uint32_be (&bw,
                    (nal_size << (nls * 8)));
                ok &= gst_byte_writer_set_pos (&bw,
                    gst_byte_writer_get_pos (&bw) - nls);
              }

              ok &= gst_byte_writer_put_data (&bw,
                  GST_BUFFER_DATA (codec_nal), GST_BUFFER_SIZE (codec_nal));
              h265parse->last_report = new_ts;
            }
          }

          ok &= gst_byte_writer_put_data (&bw,
              GST_BUFFER_DATA (buffer) + h265parse->idr_pos,
              GST_BUFFER_SIZE (buffer) - h265parse->idr_pos);

          /* collect result and push */
          new_buf = gst_byte_writer_reset_and_get_buffer (&bw);
          gst_buffer_copy_metadata (new_buf, buffer, GST_BUFFER_COPY_ALL);

          /* should already be keyframe/IDR, but it may not have been,
           * so mark it as such to avoid being discarded by picky decoder */
          GST_BUFFER_FLAG_UNSET (new_buf, GST_BUFFER_FLAG_DELTA_UNIT);
          gst_buffer_replace (&frame->buffer, new_buf);
          gst_buffer_unref (new_buf);
          /* some result checking seems to make some compilers happy */
          if (G_UNLIKELY (!ok)) {
            GST_ERROR_OBJECT (h265parse, "failed to insert SPS/PPS");
          }
        }
      }
      /* we pushed whatever we had */
      h265parse->push_codec = FALSE;
      h265parse->have_vps = FALSE;
      h265parse->have_sps = FALSE;
      h265parse->have_pps = FALSE;
    }
  }

  gst_h265_parse_reset_frame (h265parse);

  return GST_FLOW_OK;
}

static gboolean
gst_h265_parse_set_caps (GstBaseParse * parse, GstCaps * caps)
{
  GstH265Parse *h265parse;
  GstStructure *str;
  const GValue *value;
  GstBuffer *codec_data = NULL;
  gsize off, size;
  guint format, align;
  guint num_nals, i, j;
  GstH265NalUnit nalu;
  GstH265ParserResult parseres;

  h265parse = GST_H265_PARSE (parse);

  /* reset */
  h265parse->push_codec = FALSE;

  str = gst_caps_get_structure (caps, 0);

  /* accept upstream info if provided */
  gst_structure_get_int (str, "width", &h265parse->width);
  gst_structure_get_int (str, "height", &h265parse->height);
  gst_structure_get_fraction (str, "framerate", &h265parse->fps_num,
      &h265parse->fps_den);
  gst_structure_get_fraction (str, "pixel-aspect-ratio",
      &h265parse->upstream_par_n, &h265parse->upstream_par_d);

  /* get upstream format and align from caps */
  gst_h265_parse_format_from_caps (caps, &format, &align);

  /* packetized video has a codec_data */
  if (format != GST_H265_PARSE_FORMAT_BYTE &&
      (value = gst_structure_get_value (str, "codec_data"))) {
    guint8 *data;

    GST_DEBUG_OBJECT (h265parse, "have packetized h265");
    /* make note for optional split processing */
    h265parse->packetized = TRUE;

    codec_data = gst_value_get_buffer (value);
    if (!codec_data)
      goto wrong_type;
    data = GST_BUFFER_DATA (codec_data);
    size = GST_BUFFER_SIZE (codec_data);

    /* parse the hvcC data */
    if (size < 23)
      goto hvcc_too_small;

    /* parse the version, this must be one but
     * is zero until the spec is finalized */
    if (data[0] != 0 && data[0] != 1)
      goto wrong_version;

    h265parse->nal_length_size = (data[21] & 0x03) + 1;
    GST_DEBUG_OBJECT (h265parse, "nal length size %u",
        h265parse->nal_length_size);

    off = 23;
    for (i = 0; i < data[22]; i++) {
      num_nals = GST_READ_UINT16_BE (data + off + 1);
      off += 3;
      for (j = 0; j < num_nals; j++) {
        parseres = gst_h265_parser_identify_nalu_hevc (h265parse->nalparser,
            data, off, size, 2, &nalu);

        if (parseres != GST_H265_PARSER_OK)
          goto hvcc_too_small;

        gst_h265_parse_process_nal (h265parse, &nalu);
        off = nalu.offset + nalu.size;
      }
    }

    gst_buffer_replace (&h265parse->codec_data_in, codec_data);

  } else {
    GST_DEBUG_OBJECT (h265parse, "have bytestream h265");
    /* nothing to pre-process */
    h265parse->packetized = FALSE;
    /* we have 4 sync bytes */
    h265parse->nal_length_size = 4;

    if (format == GST_H265_PARSE_FORMAT_NONE) {
      format = GST_H265_PARSE_FORMAT_BYTE;
      align = GST_H265_PARSE_ALIGN_AU;
    }
  }

  {
    GstCaps *in_caps;

    /* prefer input type determined above */
    in_caps = gst_caps_new_simple ("video/x-h265",
        "parsed", G_TYPE_BOOLEAN, TRUE,
        "stream-format", G_TYPE_STRING,
        gst_h265_parse_get_string (h265parse, TRUE, format),
        "alignment", G_TYPE_STRING,
        gst_h265_parse_get_string (h265parse, FALSE, align), NULL);
    /* negotiate with downstream, sets ->format and ->align */
    gst_h265_parse_negotiate (h265parse, format, in_caps);
    gst_caps_unref (in_caps);
  }

  if (format == h265parse->format && align == h265parse->align) {
    /* do not set CAPS and passthrough mode if SPS/PPS have not been parsed */
    if (h265parse->have_sps && h265parse->have_pps) {
      gst_base_parse_set_passthrough (parse, TRUE);

      /* we did parse codec-data and might supplement src caps */
      gst_h265_parse_update_src_caps (h265parse, caps);
    }
  } else if (format == GST_H265_PARSE_FORMAT_HVC1
      || format == GST_H265_PARSE_FORMAT_HEV1) {
    /* if input != output, and input is hevc, must split before anything else */
    /* arrange to insert codec-data in-stream if needed.
     * src caps are only arranged for later on */
    h265parse->push_codec = TRUE;
    h265parse->have_vps = FALSE;
    h265parse->have_sps = FALSE;
    h265parse->have_pps = FALSE;
    h265parse->split_packetized = TRUE;
    h265parse->packetized = TRUE;
  }

  return TRUE;

  /* ERRORS */
hvcc_too_small:
  {
    GST_DEBUG_OBJECT (h265parse, "hvcC size %" G_GSIZE_FORMAT " < 23", size);
    goto refuse_caps;
  }
wrong_version:
  {
    GST_DEBUG_OBJECT (h265parse, "wrong hvcC version");
    goto refuse_caps;
  }
wrong_type:
  {
    GST_DEBUG_OBJECT (h265parse, "wrong codec-data type");
    goto refuse_caps;
  }
refuse_caps:
  {
    GST_WARNING_OBJECT (h265parse, "refused caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}


static GstCaps *
gst_h265_parse_get_caps (GstBaseParse * parse)
{
  GstCaps *peercaps;
  GstCaps *res;

  peercaps = gst_pad_get_allowed_caps (GST_BASE_PARSE_SRC_PAD (parse));
  if (peercaps) {
    guint i, n;

    peercaps = gst_caps_make_writable (peercaps);
    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);
      gst_structure_remove_field (s, "alignment");
      gst_structure_remove_field (s, "stream-format");
      gst_structure_remove_field (s, "parsed");
    }

    res =
        gst_caps_intersect_full (peercaps,
        gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse)),
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
  } else {
    res =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD
            (parse)));
  }

  return res;
}


static gboolean
gst_h265_parse_event (GstBaseParse * parse, GstEvent * event)
{
  gboolean res;
  GstH265Parse *h265parse = GST_H265_PARSE (parse);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      GstClockTime timestamp, stream_time, running_time;
      gboolean all_headers;
      guint count;

      if (gst_video_event_is_force_key_unit (event)) {
        gst_video_event_parse_downstream_force_key_unit (event,
            &timestamp, &stream_time, &running_time, &all_headers, &count);

        GST_INFO_OBJECT (h265parse,
            "received downstream force key unit event, "
            "seqnum %d running_time %" GST_TIME_FORMAT
            " all_headers %d count %d", gst_event_get_seqnum (event),
            GST_TIME_ARGS (running_time), all_headers, count);
        if (h265parse->force_key_unit_event) {
          GST_INFO_OBJECT (h265parse, "ignoring force key unit event "
              "as one is already queued");
        } else {
          h265parse->pending_key_unit_ts = running_time;
          gst_event_replace (&h265parse->force_key_unit_event, event);
        }
        gst_event_unref (event);
        res = TRUE;
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      h265parse->push_codec = TRUE;
      break;
    default:
      break;
  }
  return res;
}

static gboolean
gst_h265_parse_src_event (GstBaseParse * parse, GstEvent * event)
{
  gboolean res;
  GstH265Parse *h265parse = GST_H265_PARSE (parse);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      GstClockTime running_time;
      gboolean all_headers;
      guint count;

      if (gst_video_event_is_force_key_unit (event)) {
        gst_video_event_parse_upstream_force_key_unit (event,
            &running_time, &all_headers, &count);

        GST_INFO_OBJECT (h265parse, "received upstream force-key-unit event, "
            "seqnum %d running_time %" GST_TIME_FORMAT
            " all_headers %d count %d", gst_event_get_seqnum (event),
            GST_TIME_ARGS (running_time), all_headers, count);

        if (all_headers) {
          h265parse->pending_key_unit_ts = running_time;
          gst_event_replace (&h265parse->force_key_unit_event, event);
        }
      }
      res = GST_BASE_PARSE_CLASS (parent_class)->src_event (parse, event);
      break;
    }
    default:
      res = GST_BASE_PARSE_CLASS (parent_class)->src_event (parse, event);
      break;
  }

  return res;
}

static void
gst_h265_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstH265Parse *parse;
  parse = GST_H265_PARSE (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      parse->interval = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_h265_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstH265Parse *parse;
  parse = GST_H265_PARSE (object);

  switch (prop_id) {
    case PROP_CONFIG_INTERVAL:
      g_value_set_uint (value, parse->interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
