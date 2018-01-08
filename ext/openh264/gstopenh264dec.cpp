/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define GST_USE_UNSTABLE_API

#include "gstopenh264dec.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstbasevideodecoder.h>
#include <string.h>             /* for memcpy */
//#include "gst-compat.h"

GST_DEBUG_CATEGORY_STATIC (gst_openh264dec_debug_category);
#define GST_CAT_DEFAULT gst_openh264dec_debug_category

/* prototypes */
static gboolean gst_openh264dec_start (GstBaseVideoDecoder * decoder);
static gboolean gst_openh264dec_stop (GstBaseVideoDecoder * decoder);

static gboolean gst_openh264dec_set_format (GstBaseVideoDecoder * decoder,
    GstVideoState * state);
static gboolean gst_openh264dec_reset (GstBaseVideoDecoder * decoder);
static GstFlowReturn gst_openh264dec_finish (GstBaseVideoDecoder * decoder);
static GstFlowReturn gst_openh264dec_handle_frame (GstBaseVideoDecoder *
    decoder, GstVideoFrame * frame);
/*static gboolean gst_openh264dec_decide_allocation (GstBaseVideoDecoder * decoder,
    GstQuery * query);*/

/* pad templates */

static GstStaticPadTemplate gst_openh264dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/x-h264, stream-format=(string)byte-stream, alignment=(string)au, "
        "profile=(string){ constrained-baseline, baseline}"));

static GstStaticPadTemplate gst_openh264dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ], " "framerate = (fraction) [0/1, MAX]"));

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstOpenh264Dec, gst_openh264dec,
    GST_TYPE_BASE_VIDEO_DECODER,
    GST_DEBUG_CATEGORY_INIT (gst_openh264dec_debug_category, "openh264dec", 0,
        "debug category for openh264dec element"));

static void
gst_openh264dec_class_init (GstOpenh264DecClass * klass)
{
  GstBaseVideoDecoderClass *video_decoder_class =
      GST_BASE_VIDEO_DECODER_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_openh264dec_sink_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_openh264dec_src_template);

  gst_element_class_set_details_simple (GST_ELEMENT_CLASS (klass),
      "OpenH264 video decoder", "Decoder/Video", "OpenH264 video decoder",
      "Ericsson AB, http://www.ericsson.com, Fluendo SA, http://www.fluendo.com");

  video_decoder_class->start = gst_openh264dec_start;
  video_decoder_class->stop = gst_openh264dec_stop;
  video_decoder_class->set_format = gst_openh264dec_set_format;
  video_decoder_class->reset = gst_openh264dec_reset;
  video_decoder_class->finish = gst_openh264dec_finish;
  video_decoder_class->handle_frame = gst_openh264dec_handle_frame;

  /* video_decoder_class->decide_allocation =
     GST_DEBUG_FUNCPTR (gst_openh264dec_decide_allocation); */
}

static void
gst_openh264dec_init (GstOpenh264Dec * openh264dec)
{
  openh264dec->decoder = NULL;
  openh264dec->base_openh264dec.packetized = true;

  /*
     gst_base_video_decoder_set_packetized (GST_BASE_VIDEO_DECODER (openh264dec), TRUE);
     gst_base_video_decoder_set_needs_format (GST_BASE_VIDEO_DECODER (openh264dec), TRUE);
     * */
}

static gboolean
gst_openh264dec_start (GstBaseVideoDecoder * decoder)
{
  GstOpenh264Dec *openh264dec = GST_OPENH264DEC (decoder);
  gint ret;
  SDecodingParam dec_param = { 0 };

  if (openh264dec->decoder != NULL) {
    openh264dec->decoder->Uninitialize ();
    WelsDestroyDecoder (openh264dec->decoder);
    openh264dec->decoder = NULL;
  }
  WelsCreateDecoder (&(openh264dec->decoder));

  dec_param.uiTargetDqLayer = 255;
  dec_param.eEcActiveIdc = ERROR_CON_FRAME_COPY;
#if OPENH264_MAJOR == 1 && OPENH264_MINOR < 6
  dec_param.eOutputColorFormat = videoFormatI420;
#endif
  dec_param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

  ret = openh264dec->decoder->Initialize (&dec_param);

  GST_DEBUG_OBJECT (openh264dec,
      "openh264_dec_start called, openh264dec %sinitialized OK!",
      (ret != cmResultSuccess) ? "NOT " : "");

  return (ret == cmResultSuccess);
}

static gboolean
gst_openh264dec_stop (GstBaseVideoDecoder * decoder)
{
  GstOpenh264Dec *openh264dec = GST_OPENH264DEC (decoder);

  if (openh264dec->decoder) {
    openh264dec->decoder->Uninitialize ();
    WelsDestroyDecoder (openh264dec->decoder);
    openh264dec->decoder = NULL;
  }

  openh264dec->width = openh264dec->height = 0;

  return TRUE;
}

static gboolean
gst_openh264dec_set_format (GstBaseVideoDecoder * decoder,
    GstVideoState * state)
{
  GstOpenh264Dec *openh264dec = GST_OPENH264DEC (decoder);

  GST_DEBUG_OBJECT (openh264dec, "input caps: %" GST_PTR_FORMAT, state->caps);

  memcpy (&openh264dec->input_state, state, sizeof (GstVideoState));

  return TRUE;
}

static gboolean
gst_openh264dec_reset (GstBaseVideoDecoder * decoder)
{
  GstOpenh264Dec *openh264dec = GST_OPENH264DEC (decoder);

  GST_DEBUG_OBJECT (openh264dec, "reset");

  return TRUE;
}

static GstFlowReturn
gst_openh264dec_handle_frame (GstBaseVideoDecoder * decoder,
    GstVideoFrame * frame)
{
#if 0                           /* Not ported to 0.1 yet */

  GstOpenh264Dec *openh264dec = GST_OPENH264DEC (decoder);
  GstMapInfo map_info;
  GstVideoState *state;
  SBufferInfo dst_buf_info;
  DECODING_STATE ret;
  guint8 *yuvdata[3];
  GstFlowReturn flow_status;
  guint actual_width, actual_height;
  guint i;
  guint8 *p;
  guint row_stride, component_width, component_height, src_width, row;


  state = &decoder->base_video_codec.state;

  state->format = GST_VIDEO_FORMAT_I420;
  gst_base_video_decoder_set_src_caps (decoder);

  frame->src_buffer = gst_buffer_new_and_alloc (state->bytes_per_picture);
  frame->presentation_timestamp = frame->decode_timestamp;


  if (frame) {
    if (!gst_buffer_map (frame->sink_buffer, &map_info, GST_MAP_READ)) {
      GST_ERROR_OBJECT (openh264dec, "Cannot map input buffer!");
      //gst_video_codec_frame_unref (frame);
      return GST_FLOW_ERROR;
    }

    GST_LOG_OBJECT (openh264dec, "handle frame, %d",
        map_info.size > 4 ? map_info.data[4] & 0x1f : -1);

    memset (&dst_buf_info, 0, sizeof (SBufferInfo));
    ret =
        openh264dec->decoder->DecodeFrame2 (map_info.data, map_info.size,
        yuvdata, &dst_buf_info);
/*
    if (ret == dsNoParamSets) {
      GST_DEBUG_OBJECT (openh264dec, "Requesting a key unit");
      gst_pad_push_event (GST_BASE_VIDEO_DECODER_SINK_PAD (decoder),
          gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
              FALSE, 0));
    }

    if (ret != dsErrorFree && ret != dsNoParamSets) {
      GST_DEBUG_OBJECT (openh264dec, "Requesting a key unit");
      gst_pad_push_event (GST_VIDEO_DECODER_SINK_PAD (decoder),
          gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
              FALSE, 0));
      GST_LOG_OBJECT (openh264dec, "error decoding nal, return code: %d", ret);
    }
*/
    gst_buffer_unmap (frame->input_buffer, &map_info);
    if (ret != dsErrorFree)
      return gst_base_video_decoder_drop_frame (decoder, frame);

    // gst_video_codec_frame_unref (frame);
    frame = NULL;
  } else {
    memset (&dst_buf_info, 0, sizeof (SBufferInfo));
    ret = openh264dec->decoder->DecodeFrame2 (NULL, 0, yuvdata, &dst_buf_info);
    if (ret != dsErrorFree) {
      return GST_FLOW_EOS;
    }
  }

  /* FIXME: openh264 has no way for us to get a connection
   * between the input and output frames, we just have to
   * guess based on the input. Fortunately openh264 can
   * only do baseline profile. */
  frame = gst_base_video_decoder_get_oldest_frame (decoder);
  if (!frame) {
    /* Can only happen in finish() */
    return GST_FLOW_EOS;
  }

  /* No output available yet */
  if (dst_buf_info.iBufferStatus != 1) {
    //gst_video_codec_frame_unref (frame);
    return (frame ? GST_FLOW_OK : GST_FLOW_EOS);
  }

  actual_width = dst_buf_info.UsrData.sSystemBuffer.iWidth;
  actual_height = dst_buf_info.UsrData.sSystemBuffer.iHeight;



  if ((gst_pad_get_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (openh264dec)) == NULL)
      || actual_width != openh264dec->width
      || actual_height != openh264dec->height) {

    openh264dec->width = actual_width;
    openh264dec->height = actual_height;
    state = gst_base_video_decoder_get_state (decoder);
    state->width = actual_width;
    state->height = actual_height;
    state->bytes_per_picture = actual_width * actual_height * 3;
  }

  flow_status = gst_base_video_decoder_alloc_src_frame (decoder, frame);
  if (flow_status != GST_FLOW_OK) {

    return flow_status;
  }

  /*

     if (!gst_video_frame_map (&video_frame, &state->info, frame->output_buffer,
     GST_MAP_WRITE)) {
     GST_ERROR_OBJECT (openh264dec, "Cannot map output buffer!");
     gst_video_codec_state_unref (state);
     gst_video_codec_frame_unref (frame);
     return GST_FLOW_ERROR;
     }

     for (i = 0; i < 3; i++) {
     p = GST_VIDEO_FRAME_COMP_DATA (&video_frame, i);
     row_stride = GST_VIDEO_FRAME_COMP_STRIDE (&video_frame, i);
     component_width = GST_VIDEO_FRAME_COMP_WIDTH (&video_frame, i);
     component_height = GST_VIDEO_FRAME_COMP_HEIGHT (&video_frame, i);
     src_width =
     i <
     1 ? dst_buf_info.UsrData.sSystemBuffer.
     iStride[0] : dst_buf_info.UsrData.sSystemBuffer.iStride[1];
     for (row = 0; row < component_height; row++) {
     memcpy (p, yuvdata[i], component_width);
     p += row_stride;
     yuvdata[i] += src_width;
     }
     }
     gst_video_codec_state_unref (state);
     gst_video_frame_unmap (&video_frame);
   */
  return gst_base_video_decoder_finish_frame (decoder, frame);
#else
  return GST_FLOW_OK;
#endif
}

static GstFlowReturn
gst_openh264dec_finish (GstBaseVideoDecoder * decoder)
{
  GstOpenh264Dec *openh264dec = GST_OPENH264DEC (decoder);

  GST_DEBUG_OBJECT (openh264dec, "finish");

  /* Decoder not negotiated yet */
  if (openh264dec->width == 0)
    return GST_FLOW_OK;

  /* Drain all pending frames */
  while ((gst_openh264dec_handle_frame (decoder, NULL)) == GST_FLOW_OK);

  return GST_FLOW_OK;
}

/*
static gboolean
gst_openh264dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstVideoCodecState *state;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (gst_openh264dec_parent_class)->decide_allocation
      (decoder, query))
    return FALSE;

  state = gst_video_decoder_get_output_state (decoder);

  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  gst_buffer_pool_set_config (pool, config);

  gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);

  gst_object_unref (pool);
  gst_video_codec_state_unref (state);

  return TRUE;
}
*/
