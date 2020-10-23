/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
 * Copyright (C) 2012 Collabora Ltd.
 *	Author : Edward Hervey <edward@collabora.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:gstvideodecoder
 * @short_description: Base class for video decoders
 * @see_also: 
 *
 * This base class is for video decoders turning encoded data into raw video
 * frames.
 *
 * The GstVideoDecoder base class and derived subclasses should cooperate as follows:
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Configuration</title>
 *   <listitem><para>
 *     Initially, GstVideoDecoder calls @start when the decoder element
 *     is activated, which allows the subclass to perform any global setup.
 *   </para></listitem>
 *   <listitem><para>
 *     GstVideoDecoder calls @set_format to inform the subclass of caps
 *     describing input video data that it is about to receive, including
 *     possibly configuration data.
 *     While unlikely, it might be called more than once, if changing input
 *     parameters require reconfiguration.
 *   </para></listitem>
 *   <listitem><para>
 *     Incoming data buffers are processed as needed, described in Data Processing below.
 *   </para></listitem>
 *   <listitem><para>
 *     GstVideoDecoder calls @stop at end of all processing.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist>
 *   <title>Data processing</title>
 *     <listitem><para>
 *       The base class gathers input data, and optionally allows subclass
 *       to parse this into subsequently manageable chunks, typically
 *       corresponding to and referred to as 'frames'.
 *     </para></listitem>
 *     <listitem><para>
 *       Each input frame is provided in turn to the subclass' @handle_frame callback.
 *       The ownership of the frame is given to the @handle_frame callback.
 *     </para></listitem>
 *     <listitem><para>
 *       If codec processing results in decoded data, the subclass should call
 *       @gst_video_decoder_finish_frame to have decoded data pushed.
 *       downstream. Otherwise, the subclass must call @gst_video_decoder_drop_frame, to
 *       allow the base class to do timestamp and offset tracking, and possibly to
 *       requeue the frame for a later attempt in the case of reverse playback.
 *     </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Shutdown phase</title>
 *   <listitem><para>
 *     The GstVideoDecoder class calls @stop to inform the subclass that data
 *     parsing will be stopped.
 *   </para></listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Additional Notes</title>
 *   <listitem>
 *     <itemizedlist><title>Seeking/Flushing</title>
 *     <listitem><para>
 *   When the pipeline is seeked or otherwise flushed, the subclass is informed via a call
 *   to its @reset callback, with the hard parameter set to true. This indicates the
 *   subclass should drop any internal data queues and timestamps and prepare for a fresh
 *   set of buffers to arrive for parsing and decoding.
 *     </para></listitem>
 *     </itemizedlist>
 *   </listitem>
 *   <listitem>
 *     <itemizedlist><title>End Of Stream</title>
 *     <listitem><para>
 *   At end-of-stream, the subclass @parse function may be called some final times with the 
 *   at_eos parameter set to true, indicating that the element should not expect any more data
 *   to be arriving, and it should parse and remaining frames and call
 *   gst_video_decoder_have_frame() if possible.
 *     </para></listitem>
 *     </itemizedlist>
 *   </listitem>
 *   </itemizedlist>
 * </listitem>
 * </orderedlist>
 *
 * Subclass is responsible for providing pad template caps for
 * source and sink pads. The pads need to be named "sink" and "src". It also
 * needs to set the fixed caps on srcpad, when the format is ensured.  This
 * is typically when base class calls subclass' @set_format function, though
 * it might be delayed until calling @gst_video_decoder_finish_frame.
 *
 * The subclass is also responsible for providing (presentation) timestamps
 * (likely based on corresponding input ones).  If that is not applicable
 * or possible, the base class provides limited framerate based interpolation.
 *
 * Similarly, the base class provides some limited (legacy) seeking support
 * if specifically requested by the subclass, as full-fledged support
 * should rather be left to upstream demuxer, parser or alike.  This simple
 * approach caters for seeking and duration reporting using estimated input
 * bitrates. To enable it, a subclass should call
 * @gst_video_decoder_set_estimate_rate to enable handling of incoming byte-streams.
 *
 * The base class provides some support for reverse playback, in particular
 * in case incoming data is not packetized or upstream does not provide
 * fragments on keyframe boundaries.  However, the subclass should then be prepared
 * for the parsing and frame processing stage to occur separately (in normal
 * forward processing, the latter immediately follows the former),
 * The subclass also needs to ensure the parsing stage properly marks keyframes,
 * unless it knows the upstream elements will do so properly for incoming data.
 *
 * The bare minimum that a functional subclass needs to implement is:
 * <itemizedlist>
 *   <listitem><para>Provide pad templates</para></listitem>
 *   <listitem><para>
 *      Set source pad caps when appropriate
 *   </para></listitem>
 *   <listitem><para>
 *      Parse input data, if it is not considered packetized from upstream
 *      Data will be provided to @parse which should invoke @gst_video_decoder_add_to_frame and
 *      @gst_video_decoder_have_frame to separate the data belonging to each video frame.
 *   </para></listitem>
 *   <listitem><para>
 *      Accept data in @handle_frame and provide decoded results to
 *      @gst_video_decoder_finish_frame, or call @gst_video_decoder_drop_frame.
 *   </para></listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* TODO
 *
 * * Add a flag/boolean for I-frame-only/image decoders so we can do extra
 *   features, like applying QoS on input (as opposed to after the frame is
 *   decoded).
 * * Add a flag/boolean for decoders that require keyframes, so the base
 *   class can automatically discard non-keyframes before one has arrived
 * * Support for GstIndex (or shall we not care ?)
 * * Calculate actual latency based on input/output timestamp/frame_number
 *   and if it exceeds the recorded one, save it and emit a GST_MESSAGE_LATENCY
 * * Emit latency message when it changes
 *
 */

/* Implementation notes:
 * The Video Decoder base class operates in 2 primary processing modes, depending
 * on whether forward or reverse playback is requested.
 *
 * Forward playback:
 *   * Incoming buffer -> @parse() -> add_to_frame()/have_frame() -> handle_frame() -> 
 *     push downstream
 *
 * Reverse playback is more complicated, since it involves gathering incoming data regions
 * as we loop backwards through the upstream data. The processing concept (using incoming
 * buffers as containing one frame each to simplify things) is:
 *
 * Upstream data we want to play:
 *  Buffer encoded order:  1  2  3  4  5  6  7  8  9  EOS
 *  Keyframe flag:            K        K        
 *  Groupings:             AAAAAAA  BBBBBBB  CCCCCCC
 *
 * Input:
 *  Buffer reception order:  7  8  9  4  5  6  1  2  3  EOS
 *  Keyframe flag:                       K        K
 *  Discont flag:            D        D        D
 *
 * - Each Discont marks a discont in the decoding order.
 * - The keyframes mark where we can start decoding.
 *
 * Initially, we prepend incoming buffers to the gather queue. Whenever the
 * discont flag is set on an incoming buffer, the gather queue is flushed out
 * before the new buffer is collected.
 *
 * The above data will be accumulated in the gather queue like this:
 *
 *   gather queue:  9  8  7
 *                        D
 *
 * Whe buffer 4 is received (with a DISCONT), we flush the gather queue like
 * this:
 *
 *   while (gather)
 *     take head of queue and prepend to parse queue (this reverses the sequence,
 *     so parse queue is 7 -> 8 -> 9)
 *
 *   Next, we process the parse queue, which now contains all un-parsed packets (including
 *   any leftover ones from the previous decode section)
 *
 *   for each buffer now in the parse queue:
 *     Call the subclass parse function, prepending each resulting frame to
 *     the parse_gather queue. Buffers which precede the first one that
 *     produces a parsed frame are retained in the parse queue for re-processing on
 *     the next cycle of parsing.
 *
 *   The parse_gather queue now contains frame objects ready for decoding, in reverse order.
 *   parse_gather: 9 -> 8 -> 7
 *
 *   while (parse_gather)
 *     Take the head of the queue and prepend it to the decode queue
 *     If the frame was a keyframe, process the decode queue
 *   decode is now 7-8-9
 *
 *  Processing the decode queue results in frames with attached output buffers
 *  stored in the 'output_queue' ready for outputting in reverse order.
 *
 * After we flushed the gather queue and parsed it, we add 4 to the (now empty) gather queue.
 * We get the following situation:
 *
 *  gather queue:    4
 *  decode queue:    7  8  9
 *
 * After we received 5 (Keyframe) and 6:
 *
 *  gather queue:    6  5  4
 *  decode queue:    7  8  9
 *
 * When we receive 1 (DISCONT) which triggers a flush of the gather queue:
 *
 *   Copy head of the gather queue (6) to decode queue:
 *
 *    gather queue:    5  4
 *    decode queue:    6  7  8  9
 *
 *   Copy head of the gather queue (5) to decode queue. This is a keyframe so we
 *   can start decoding.
 *
 *    gather queue:    4
 *    decode queue:    5  6  7  8  9
 *
 *   Decode frames in decode queue, store raw decoded data in output queue, we
 *   can take the head of the decode queue and prepend the decoded result in the
 *   output queue:
 *
 *    gather queue:    4
 *    decode queue:    
 *    output queue:    9  8  7  6  5
 *
 *   Now output all the frames in the output queue, picking a frame from the
 *   head of the queue.
 *
 *   Copy head of the gather queue (4) to decode queue, we flushed the gather
 *   queue and can now store input buffer in the gather queue:
 *
 *    gather queue:    1
 *    decode queue:    4
 *
 *  When we receive EOS, the queue looks like:
 *
 *    gather queue:    3  2  1
 *    decode queue:    4
 *
 *  Fill decode queue, first keyframe we copy is 2:
 *
 *    gather queue:    1
 *    decode queue:    2  3  4
 *
 *  Decoded output:
 *
 *    gather queue:    1
 *    decode queue:    
 *    output queue:    4  3  2
 *
 *  Leftover buffer 1 cannot be decoded and must be discarded.
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstvideodecoder.h"
#include "gstvideoutils.h"

#include <string.h>

GST_DEBUG_CATEGORY (videodecoder_debug);
#define GST_CAT_DEFAULT videodecoder_debug

#define GST_VIDEO_DECODER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_VIDEO_DECODER, \
        GstVideoDecoderPrivate))

/* FIXME : I really hope we never see streams that go over this */
#define MAX_DTS_PTS_REORDER_DEPTH 36

struct _GstVideoDecoderPrivate
{
  /* FIXME introduce a context ? */

  /* parse tracking */
  /* input data */
  GstAdapter *input_adapter;
  /* assembles current frame */
  GstAdapter *output_adapter;

  /* Whether we attempt to convert newsegment from bytes to
   * time using a bitrate estimation */
  gboolean do_estimate_rate;

  /* Whether input is considered packetized or not */
  gboolean packetized;

  /* Error handling */
  gint max_errors;
  gint error_count;

  /* ... being tracked here;
   * only available during parsing */
  GstVideoCodecFrame *current_frame;
  /* events that should apply to the current frame */
  GList *current_frame_events;

  /* relative offset of input data */
  guint64 input_offset;
  /* relative offset of frame */
  guint64 frame_offset;
  /* tracking ts and offsets */
  GList *timestamps;

  /* last incoming and outgoing ts */
  GstClockTime last_timestamp_in;
  GstClockTime last_timestamp_out;

  /* last outgoing system frame number (used to detect reordering) */
  guint last_out_frame_number;

  /* TRUE if input timestamp is not monotonically increasing */
  gboolean reordered_input;

  /* TRUE if frames come out in a different order than they were inputted */
  gboolean reordered_output;

  /* reverse playback */
  /* collect input */
  GList *gather;
  /* to-be-parsed */
  GList *parse;
  /* collected parsed frames */
  GList *parse_gather;
  /* frames to be handled == decoded */
  GList *decode;
  /* collected output - of buffer objects, not frames */
  GList *output_queued;


  /* base_picture_number is the picture number of the reference picture */
  guint64 base_picture_number;
  /* combine with base_picture_number, framerate and calcs to yield (presentation) ts */
  GstClockTime base_timestamp;

  /* FIXME : reorder_depth is never set */
  int reorder_depth;
  int distance_from_sync;

  guint32 system_frame_number;
  guint32 decode_frame_number;

  GList *frames;                /* Protected with OBJECT_LOCK */
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;
  gboolean output_state_changed;

  /* QoS properties */
  gdouble proportion;           /* OBJECT_LOCK */
  GstClockTime earliest_time;   /* OBJECT_LOCK */
  GstClockTime qos_frame_duration;      /* OBJECT_LOCK */
  gboolean discont;
  /* qos messages: frames dropped/processed */
  guint dropped;
  guint processed;

  /* Outgoing byte size ? */
  gint64 bytes_out;
  gint64 time;

  gint64 min_latency;
  gint64 max_latency;

  /* Handle incoming buffers with DTS instead of PTS as timestamps */
  GstClockTime incoming_timestamps[MAX_DTS_PTS_REORDER_DEPTH];
  guint reorder_idx_in;
  guint reorder_idx_out;
};

static void gst_video_decoder_finalize (GObject * object);

static gboolean gst_video_decoder_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_video_decoder_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_video_decoder_src_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_video_decoder_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_video_decoder_sink_query (GstPad * pad, GstQuery * query);
static GstStateChangeReturn
gst_video_decoder_change_state (GstElement * element,
    GstStateChange transition);
static const GstQueryType *gst_video_decoder_get_query_types (GstPad * pad);
static gboolean gst_video_decoder_src_query (GstPad * pad, GstQuery * query);
static void gst_video_decoder_reset (GstVideoDecoder * decoder, gboolean full);

static GstFlowReturn gst_video_decoder_decode_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_video_decoder_set_src_caps (GstVideoDecoder * decoder);

static GstClockTime gst_video_decoder_get_frame_duration (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame);
static GstVideoCodecFrame *gst_video_decoder_new_frame (GstVideoDecoder *
    decoder);
static GstFlowReturn gst_video_decoder_clip_and_push_buf (GstVideoDecoder *
    decoder, GstBuffer * buf);
static GstFlowReturn gst_video_decoder_flush_parse (GstVideoDecoder * dec,
    gboolean at_eos);

static void gst_video_decoder_clear_queues (GstVideoDecoder * dec);

GST_BOILERPLATE (GstVideoDecoder, gst_video_decoder,
    GstElement, GST_TYPE_ELEMENT);

static void
gst_video_decoder_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (videodecoder_debug, "videodecoder", 0,
      "Base Video Decoder");
}

static void
gst_video_decoder_class_init (GstVideoDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstVideoDecoderPrivate));

  gobject_class->finalize = gst_video_decoder_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_video_decoder_change_state);
}

static void
gst_video_decoder_init (GstVideoDecoder * decoder, GstVideoDecoderClass * klass)
{
  GstPadTemplate *pad_template;
  GstPad *pad;

  GST_DEBUG_OBJECT (decoder, "gst_video_decoder_init");

  decoder->priv = GST_VIDEO_DECODER_GET_PRIVATE (decoder);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  decoder->sinkpad = pad = gst_pad_new_from_template (pad_template, "sink");

  gst_pad_set_chain_function (pad, GST_DEBUG_FUNCPTR (gst_video_decoder_chain));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_sink_event));
  gst_pad_set_setcaps_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_sink_setcaps));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_sink_query));
  gst_element_add_pad (GST_ELEMENT (decoder), decoder->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  decoder->srcpad = pad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_src_event));
  gst_pad_set_query_type_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_get_query_types));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_video_decoder_src_query));
  gst_pad_use_fixed_caps (pad);
  gst_element_add_pad (GST_ELEMENT (decoder), decoder->srcpad);

  gst_segment_init (&decoder->input_segment, GST_FORMAT_TIME);
  gst_segment_init (&decoder->output_segment, GST_FORMAT_TIME);

  g_static_rec_mutex_init (&decoder->stream_lock);

  decoder->priv->input_adapter = gst_adapter_new ();
  decoder->priv->output_adapter = gst_adapter_new ();
  decoder->priv->packetized = TRUE;

  gst_video_decoder_reset (decoder, TRUE);
}

static gboolean
gst_video_rawvideo_convert (GstVideoCodecState * state,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  guint vidsize;
  guint fps_n, fps_d;

  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (src_format == *dest_format || src_value == 0 || src_value == -1) {
    *dest_value = src_value;
    return TRUE;
  }

  vidsize = GST_VIDEO_INFO_SIZE (&state->info);
  fps_n = GST_VIDEO_INFO_FPS_N (&state->info);
  fps_d = GST_VIDEO_INFO_FPS_D (&state->info);

  if (src_format == GST_FORMAT_BYTES &&
      *dest_format == GST_FORMAT_DEFAULT && vidsize) {
    /* convert bytes to frames */
    *dest_value = gst_util_uint64_scale_int (src_value, 1, vidsize);
    res = TRUE;
  } else if (src_format == GST_FORMAT_DEFAULT &&
      *dest_format == GST_FORMAT_BYTES && vidsize) {
    /* convert bytes to frames */
    *dest_value = src_value * vidsize;
    res = TRUE;
  } else if (src_format == GST_FORMAT_DEFAULT &&
      *dest_format == GST_FORMAT_TIME && fps_n) {
    /* convert frames to time */
    /* FIXME add segment time? */
    *dest_value = gst_util_uint64_scale (src_value, GST_SECOND * fps_d, fps_n);
    res = TRUE;
  } else if (src_format == GST_FORMAT_TIME &&
      *dest_format == GST_FORMAT_DEFAULT && fps_d) {
    /* convert time to frames */
    /* FIXME subtract segment time? */
    *dest_value = gst_util_uint64_scale (src_value, fps_n, GST_SECOND * fps_d);
    res = TRUE;
  } else if (src_format == GST_FORMAT_TIME &&
      *dest_format == GST_FORMAT_BYTES && fps_d && vidsize) {
    /* convert time to frames */
    /* FIXME subtract segment time? */
    *dest_value = gst_util_uint64_scale (src_value,
        fps_n * vidsize, GST_SECOND * fps_d);
    res = TRUE;
  } else if (src_format == GST_FORMAT_BYTES &&
      *dest_format == GST_FORMAT_TIME && fps_n && vidsize) {
    /* convert frames to time */
    /* FIXME add segment time? */
    *dest_value = gst_util_uint64_scale (src_value,
        GST_SECOND * fps_d, fps_n * vidsize);
    res = TRUE;
  }

  return res;
}

static gboolean
gst_video_encoded_video_convert (gint64 bytes, gint64 time,
    GstFormat src_format, gint64 src_value, GstFormat * dest_format,
    gint64 * dest_value)
{
  gboolean res = FALSE;

  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (G_UNLIKELY (src_format == *dest_format || src_value == 0 ||
          src_value == -1)) {
    if (dest_value)
      *dest_value = src_value;
    return TRUE;
  }

  if (bytes <= 0 || time <= 0) {
    GST_DEBUG ("not enough metadata yet to convert");
    goto exit;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value, time, bytes);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale (src_value, bytes, time);
          res = TRUE;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      GST_DEBUG ("unhandled conversion from %d to %d", src_format,
          *dest_format);
      res = FALSE;
  }

exit:
  return res;
}

static GstVideoCodecState *
_new_input_state (GstCaps * caps)
{
  GstVideoCodecState *state;
  GstStructure *structure;
  const GValue *codec_data;

  state = g_slice_new0 (GstVideoCodecState);
  state->ref_count = 1;
  gst_video_info_init (&state->info);
  if (G_UNLIKELY (!gst_video_info_from_caps (&state->info, caps)))
    goto parse_fail;
  state->caps = gst_caps_ref (caps);

  structure = gst_caps_get_structure (caps, 0);

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER)
    state->codec_data = GST_BUFFER (gst_value_dup_mini_object (codec_data));

  return state;

parse_fail:
  {
    g_slice_free (GstVideoCodecState, state);
    return NULL;
  }
}

static GstVideoCodecState *
_new_output_state (GstVideoFormat fmt, guint width, guint height,
    GstVideoCodecState * reference)
{
  GstVideoCodecState *state;

  state = g_slice_new0 (GstVideoCodecState);
  state->ref_count = 1;
  gst_video_info_init (&state->info);
  gst_video_info_set_format (&state->info, fmt, width, height);

  if (reference) {
    GstVideoInfo *tgt, *ref;

    tgt = &state->info;
    ref = &reference->info;

    /* Copy over extra fields from reference state */
    tgt->interlace_mode = ref->interlace_mode;
    tgt->flags = ref->flags;
    tgt->chroma_site = ref->chroma_site;
    tgt->colorimetry = ref->colorimetry;
    GST_DEBUG ("reference par %d/%d fps %d/%d",
        ref->par_n, ref->par_d, ref->fps_n, ref->fps_d);
    tgt->par_n = ref->par_n;
    tgt->par_d = ref->par_d;
    tgt->fps_n = ref->fps_n;
    tgt->fps_d = ref->fps_d;
    tgt->rotation = ref->rotation;
  }

  GST_DEBUG ("reference par %d/%d fps %d/%d",
      state->info.par_n, state->info.par_d,
      state->info.fps_n, state->info.fps_d);

  return state;
}

static gboolean
gst_video_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  GstVideoCodecState *state;
  gboolean ret = TRUE;

  decoder = GST_VIDEO_DECODER (gst_pad_get_parent (pad));
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (decoder, "setcaps %" GST_PTR_FORMAT, caps);

  state = _new_input_state (caps);

  if (G_UNLIKELY (state == NULL))
    goto parse_fail;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (decoder_class->set_format)
    ret = decoder_class->set_format (decoder, state);

  if (!ret)
    goto refused_format;

  if (decoder->priv->input_state)
    gst_video_codec_state_unref (decoder->priv->input_state);
  decoder->priv->input_state = state;

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  gst_object_unref (decoder);

  return ret;

  /* ERRORS */

parse_fail:
  {
    GST_WARNING_OBJECT (decoder, "Failed to parse caps");
    gst_object_unref (decoder);
    return FALSE;
  }

refused_format:
  {
    GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    GST_WARNING_OBJECT (decoder, "Subclass refused caps");
    gst_video_codec_state_unref (state);
    gst_object_unref (decoder);
    return FALSE;
  }
}

static void
gst_video_decoder_finalize (GObject * object)
{
  GstVideoDecoder *decoder;

  decoder = GST_VIDEO_DECODER (object);

  GST_DEBUG_OBJECT (object, "finalize");

  g_static_rec_mutex_free (&decoder->stream_lock);

  if (decoder->priv->input_adapter) {
    g_object_unref (decoder->priv->input_adapter);
    decoder->priv->input_adapter = NULL;
  }
  if (decoder->priv->output_adapter) {
    g_object_unref (decoder->priv->output_adapter);
    decoder->priv->output_adapter = NULL;
  }

  if (decoder->priv->input_state)
    gst_video_codec_state_unref (decoder->priv->input_state);
  if (decoder->priv->output_state)
    gst_video_codec_state_unref (decoder->priv->output_state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* hard == FLUSH, otherwise discont */
static GstFlowReturn
gst_video_decoder_flush (GstVideoDecoder * dec, gboolean hard,
    gboolean flush_subclass)
{
  GstVideoDecoderClass *klass;
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_VIDEO_DECODER_GET_CLASS (dec);

  GST_LOG_OBJECT (dec, "flush hard %d", hard);

  /* Inform subclass */
  if (klass->reset)
    klass->reset (dec, hard, TRUE);

  /* FIXME make some more distinction between hard and soft,
   * but subclass may not be prepared for that */
  /* FIXME perhaps also clear pending frames ?,
   * but again, subclass may still come up with one of those */
  if (!hard) {
    /* TODO ? finish/drain some stuff */
  } else {
    gst_segment_init (&dec->input_segment, GST_FORMAT_UNDEFINED);
    gst_segment_init (&dec->output_segment, GST_FORMAT_UNDEFINED);
    gst_video_decoder_clear_queues (dec);
    priv->error_count = 0;
    g_list_foreach (priv->current_frame_events, (GFunc) gst_event_unref, NULL);
    g_list_free (priv->current_frame_events);
    priv->current_frame_events = NULL;
  }
  /* and get (re)set for the sequel */
  gst_video_decoder_reset (dec, FALSE);

  return ret;
}

static GstFlowReturn
gst_video_decoder_handle_eos (GstVideoDecoder * dec)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_GET_CLASS (dec);
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_VIDEO_DECODER_STREAM_LOCK (dec);

  if (dec->input_segment.rate > 0.0) {
    /* Forward mode, if unpacketized, give the child class
     * a final chance to flush out packets */
    if (!priv->packetized) {
      while (ret == GST_FLOW_OK && gst_adapter_available (priv->input_adapter)) {
        if (priv->current_frame == NULL)
          priv->current_frame = gst_video_decoder_new_frame (dec);

        ret = decoder_class->parse (dec, priv->current_frame,
            priv->input_adapter, TRUE);
      }
    }
  } else {
    /* Reverse playback mode */
    ret = gst_video_decoder_flush_parse (dec, TRUE);
  }

  ret = GST_FLOW_OK;

  if (decoder_class->finish)
    ret = decoder_class->finish (dec);

  GST_VIDEO_DECODER_STREAM_UNLOCK (dec);

  return ret;
}

static gboolean
gst_video_decoder_sink_eventfunc (GstVideoDecoder * decoder, GstEvent * event)
{
  GstVideoDecoderPrivate *priv;
  gboolean handled = FALSE;

  priv = decoder->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstFlowReturn flow_ret = GST_FLOW_OK;

      flow_ret = gst_video_decoder_handle_eos (decoder);
      handled = (flow_ret == GST_VIDEO_DECODER_FLOW_DROPPED);

      break;
    }
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      double rate, arate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 pos;
      GstSegment *segment = &decoder->input_segment;

      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      gst_event_parse_new_segment_full (event, &update, &rate,
          &arate, &format, &start, &stop, &pos);

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (decoder,
            "received TIME NEW_SEGMENT %" GST_TIME_FORMAT
            " -- %" GST_TIME_FORMAT ", pos %" GST_TIME_FORMAT
            ", rate %g, applied_rate %g",
            GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (pos),
            rate, arate);
      } else {
        GstFormat dformat = GST_FORMAT_TIME;

        GST_DEBUG_OBJECT (decoder,
            "received NEW_SEGMENT %" G_GINT64_FORMAT
            " -- %" G_GINT64_FORMAT ", time %" G_GINT64_FORMAT
            ", rate %g, applied_rate %g", start, stop, pos, rate, arate);

        /* handle newsegment as a result from our legacy simple seeking */
        /* note that initial 0 should convert to 0 in any case */
        if (priv->do_estimate_rate &&
            gst_pad_query_convert (decoder->sinkpad, GST_FORMAT_BYTES, start,
                &dformat, &start)) {
          /* best attempt convert */
          /* as these are only estimates, stop is kept open-ended to avoid
           * premature cutting */
          GST_DEBUG_OBJECT (decoder,
              "converted to TIME start %" GST_TIME_FORMAT,
              GST_TIME_ARGS (start));
          pos = start;
          stop = GST_CLOCK_TIME_NONE;
          /* replace event */
          gst_event_unref (event);
          event = gst_event_new_new_segment_full (update, rate, arate,
              GST_FORMAT_TIME, start, stop, pos);
        } else {
          GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
          goto newseg_wrong_format;
        }
      }

      if (!update) {
        gst_video_decoder_flush (decoder, FALSE, FALSE);
      }

      priv->base_timestamp = GST_CLOCK_TIME_NONE;
      priv->base_picture_number = 0;

      gst_segment_set_newsegment_full (segment,
          update, rate, arate, format, start, stop, pos);

      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      /* well, this is kind of worse than a DISCONT */
      gst_video_decoder_flush (decoder, TRUE, TRUE);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
    }
    default:
      break;
  }

  return handled;

newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (decoder, "received non TIME newsegment");
    gst_event_unref (event);
    /* SWALLOW EVENT */
    /* FIXME : Ideally we'd like to return FALSE in the event handler */
    return TRUE;
  }
}

static gboolean
gst_video_decoder_push_event (GstVideoDecoder * decoder, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      double rate;
      double applied_rate;
      GstFormat format;
      gint64 start;
      gint64 stop;
      gint64 position;

      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);

      GST_DEBUG_OBJECT (decoder, "newseg rate %g, applied rate %g, "
          "format %d, start = %" GST_TIME_FORMAT ", stop = %" GST_TIME_FORMAT
          ", pos = %" GST_TIME_FORMAT, rate, applied_rate, format,
          GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (position));

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (decoder, "received non TIME newsegment");
        GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
        break;
      }

      gst_segment_set_newsegment_full (&decoder->output_segment, update, rate,
          applied_rate, format, start, stop, position);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      break;
    }
    default:
      break;
  }

  return gst_pad_push_event (decoder->srcpad, event);
}

static gboolean
gst_video_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  gboolean ret = FALSE;
  gboolean handled = FALSE;

  decoder = GST_VIDEO_DECODER (gst_pad_get_parent (pad));
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (decoder, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (decoder_class->sink_event)
    handled = decoder_class->sink_event (decoder, event);

  if (!handled)
    handled = gst_video_decoder_sink_eventfunc (decoder, event);

  if (!handled) {
    /* Forward non-serialized events and EOS/FLUSH_STOP immediately.
     * For EOS this is required because no buffer or serialized event
     * will come after EOS and nothing could trigger another
     * _finish_frame() call.   *
     * If the subclass handles sending of EOS manually it can return
     * _DROPPED from ::finish() and all other subclasses should have
     * decoded/flushed all remaining data before this
     *
     * For FLUSH_STOP this is required because it is expected
     * to be forwarded immediately and no buffers are queued anyway.
     */
    if (!GST_EVENT_IS_SERIALIZED (event)
        || GST_EVENT_TYPE (event) == GST_EVENT_EOS
        || GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP) {
      ret = gst_video_decoder_push_event (decoder, event);
    } else {
      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      decoder->priv->current_frame_events =
          g_list_prepend (decoder->priv->current_frame_events, event);
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      ret = TRUE;
    }
  }

  gst_object_unref (decoder);
  return ret;

}

/* perform upstream byte <-> time conversion (duration, seeking)
 * if subclass allows and if enough data for moderately decent conversion */
static inline gboolean
gst_video_decoder_do_byte (GstVideoDecoder * dec)
{
  return dec->priv->do_estimate_rate && (dec->priv->bytes_out > 0)
      && (dec->priv->time > GST_SECOND);
}

static gboolean
gst_video_decoder_do_seek (GstVideoDecoder * dec, GstEvent * event)
{
  GstSeekFlags flags;
  GstSeekType start_type, end_type;
  GstFormat format;
  gdouble rate;
  gint64 start, start_time, end_time;
  GstSegment seek_segment;
  guint32 seqnum;

  gst_event_parse_seek (event, &rate, &format, &flags, &start_type,
      &start_time, &end_type, &end_time);

  /* we'll handle plain open-ended flushing seeks with the simple approach */
  if (rate != 1.0) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: rate");
    return FALSE;
  }

  if (start_type != GST_SEEK_TYPE_SET) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: start time");
    return FALSE;
  }

  if (end_type != GST_SEEK_TYPE_NONE ||
      (end_type == GST_SEEK_TYPE_SET && end_time != GST_CLOCK_TIME_NONE)) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: end time");
    return FALSE;
  }

  if (!(flags & GST_SEEK_FLAG_FLUSH)) {
    GST_DEBUG_OBJECT (dec, "unsupported seek: not flushing");
    return FALSE;
  }

  memcpy (&seek_segment, &dec->output_segment, sizeof (seek_segment));
  gst_segment_set_seek (&seek_segment, rate, format, flags, start_type,
      start_time, end_type, end_time, NULL);
  start_time = seek_segment.last_stop;

  format = GST_FORMAT_BYTES;
  if (!gst_pad_query_convert (dec->sinkpad, GST_FORMAT_TIME, start_time,
          &format, &start)) {
    GST_DEBUG_OBJECT (dec, "conversion failed");
    return FALSE;
  }

  seqnum = gst_event_get_seqnum (event);
  event = gst_event_new_seek (1.0, GST_FORMAT_BYTES, flags,
      GST_SEEK_TYPE_SET, start, GST_SEEK_TYPE_NONE, -1);
  gst_event_set_seqnum (event, seqnum);

  GST_DEBUG_OBJECT (dec, "seeking to %" GST_TIME_FORMAT " at byte offset %"
      G_GINT64_FORMAT, GST_TIME_ARGS (start_time), start);

  return gst_pad_push_event (dec->sinkpad, event);
}

static gboolean
gst_video_decoder_src_eventfunc (GstVideoDecoder * decoder, GstEvent * event)
{
  GstVideoDecoderPrivate *priv;
  gboolean res = FALSE;

  priv = decoder->priv;

  GST_DEBUG_OBJECT (decoder,
      "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format, tformat;
      gdouble rate;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;
      guint32 seqnum;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);
      seqnum = gst_event_get_seqnum (event);

      /* upstream gets a chance first */
      if ((res = gst_pad_push_event (decoder->sinkpad, event)))
        break;

      /* if upstream fails for a time seek, maybe we can help if allowed */
      if (format == GST_FORMAT_TIME) {
        if (gst_video_decoder_do_byte (decoder))
          res = gst_video_decoder_do_seek (decoder, event);
        break;
      }

      /* ... though a non-time seek can be aided as well */
      /* First bring the requested format to time */
      tformat = GST_FORMAT_TIME;
      if (!(res =
              gst_pad_query_convert (decoder->srcpad, format, cur, &tformat,
                  &tcur)))
        goto convert_error;
      if (!(res =
              gst_pad_query_convert (decoder->srcpad, format, stop, &tformat,
                  &tstop)))
        goto convert_error;

      /* then seek with time on the peer */
      event = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);
      gst_event_set_seqnum (event, seqnum);

      res = gst_pad_push_event (decoder->sinkpad, event);
      break;
    }
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      GST_OBJECT_LOCK (decoder);
      priv->proportion = proportion;
      if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (timestamp))) {
        if (G_UNLIKELY (diff > 0)) {
          priv->earliest_time = timestamp + 2 * diff + priv->qos_frame_duration;
        } else {
          priv->earliest_time = timestamp + diff;
        }
      } else {
        priv->earliest_time = GST_CLOCK_TIME_NONE;
      }
      GST_OBJECT_UNLOCK (decoder);

      GST_DEBUG_OBJECT (decoder,
          "got QoS %" GST_TIME_FORMAT ", %" G_GINT64_FORMAT ", %g",
          GST_TIME_ARGS (timestamp), diff, proportion);

      res = gst_pad_push_event (decoder->sinkpad, event);
      break;
    }
    default:
      res = gst_pad_push_event (decoder->sinkpad, event);
      break;
  }
done:
  return res;

convert_error:
  GST_DEBUG_OBJECT (decoder, "could not convert format");
  goto done;
}

static gboolean
gst_video_decoder_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  gboolean ret = TRUE;
  gboolean handled = FALSE;

  decoder = GST_VIDEO_DECODER (gst_pad_get_parent (pad));
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (decoder, "received event %d, %s", GST_EVENT_TYPE (event),
      GST_EVENT_TYPE_NAME (event));

  if (decoder_class->src_event)
    handled = decoder_class->src_event (decoder, event);

  if (!handled)
    handled = gst_video_decoder_src_eventfunc (decoder, event);

  gst_object_unref (decoder);

  return ret;
}

static const GstQueryType *
gst_video_decoder_get_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return query_types;
}

static gboolean
gst_video_decoder_src_query (GstPad * pad, GstQuery * query)
{
  GstVideoDecoder *dec;
  gboolean res = TRUE;

  dec = GST_VIDEO_DECODER (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 time, value;

      /* upstream gets a chance first */
      if ((res = gst_pad_peer_query (dec->sinkpad, query))) {
        GST_LOG_OBJECT (dec, "returning peer response");
        break;
      }

      /* we start from the last seen time */
      time = dec->priv->last_timestamp_out;
      /* correct for the segment values */
      time = gst_segment_to_stream_time (&dec->output_segment,
          GST_FORMAT_TIME, time);

      GST_LOG_OBJECT (dec,
          "query %p: our time: %" GST_TIME_FORMAT, query, GST_TIME_ARGS (time));

      /* and convert to the final format */
      gst_query_parse_position (query, &format, NULL);
      if (!(res = gst_pad_query_convert (pad, GST_FORMAT_TIME, time,
                  &format, &value)))
        break;

      gst_query_set_position (query, format, value);

      GST_LOG_OBJECT (dec,
          "query %p: we return %" G_GINT64_FORMAT " (format %u)", query, value,
          format);
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      /* upstream in any case */
      if ((res = gst_pad_query_default (pad, query)))
        break;

      gst_query_parse_duration (query, &format, NULL);
      /* try answering TIME by converting from BYTE if subclass allows  */
      if (format == GST_FORMAT_TIME && gst_video_decoder_do_byte (dec)) {
        gint64 value;

        format = GST_FORMAT_BYTES;
        if (gst_pad_query_peer_duration (dec->sinkpad, &format, &value)) {
          GST_LOG_OBJECT (dec, "upstream size %" G_GINT64_FORMAT, value);
          format = GST_FORMAT_TIME;
          if (gst_pad_query_convert (dec->sinkpad,
                  GST_FORMAT_BYTES, value, &format, &value)) {
            gst_query_set_duration (query, GST_FORMAT_TIME, value);
            res = TRUE;
          }
        }
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      GST_DEBUG_OBJECT (dec, "convert query");

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      GST_VIDEO_DECODER_STREAM_LOCK (dec);
      if (dec->priv->output_state != NULL)
        res = gst_video_rawvideo_convert (dec->priv->output_state,
            src_fmt, src_val, &dest_fmt, &dest_val);
      else
        res = FALSE;
      GST_VIDEO_DECODER_STREAM_UNLOCK (dec);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min_latency, max_latency;

      res = gst_pad_peer_query (dec->sinkpad, query);
      if (res) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);
        GST_DEBUG_OBJECT (dec, "Peer qlatency: live %d, min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT, live,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        GST_OBJECT_LOCK (dec);
        min_latency += dec->priv->min_latency;
        if (dec->priv->max_latency == GST_CLOCK_TIME_NONE) {
          max_latency = GST_CLOCK_TIME_NONE;
        } else if (max_latency != GST_CLOCK_TIME_NONE) {
          max_latency += dec->priv->max_latency;
        }
        GST_OBJECT_UNLOCK (dec);

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
    }
      break;
    default:
      res = gst_pad_query_default (pad, query);
  }
  gst_object_unref (dec);
  return res;

error:
  GST_ERROR_OBJECT (dec, "query failed");
  gst_object_unref (dec);
  return res;
}

static gboolean
gst_video_decoder_sink_query (GstPad * pad, GstQuery * query)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderPrivate *priv;
  gboolean res = FALSE;

  decoder = GST_VIDEO_DECODER (gst_pad_get_parent (pad));
  priv = decoder->priv;

  GST_LOG_OBJECT (decoder, "handling query: %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_video_encoded_video_convert (priv->bytes_out, priv->time, src_fmt,
          src_val, &dest_fmt, &dest_val);
      if (!res)
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (decoder);

  return res;
error:
  GST_DEBUG_OBJECT (decoder, "query failed");
  goto done;
}

typedef struct _Timestamp Timestamp;
struct _Timestamp
{
  guint64 offset;
  GstClockTime timestamp;
  GstClockTime duration;
};

static void
timestamp_free (Timestamp * ts)
{
  g_slice_free (Timestamp, ts);
}

static void
gst_video_decoder_add_timestamp (GstVideoDecoder * decoder, GstBuffer * buffer)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  Timestamp *ts;

  ts = g_slice_new (Timestamp);

  GST_LOG_OBJECT (decoder,
      "adding timestamp %" GST_TIME_FORMAT " (offset:%" G_GUINT64_FORMAT ")",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)), priv->input_offset);

  ts->offset = priv->input_offset;
  ts->timestamp = GST_BUFFER_TIMESTAMP (buffer);
  ts->duration = GST_BUFFER_DURATION (buffer);

  priv->timestamps = g_list_append (priv->timestamps, ts);
}

static void
gst_video_decoder_get_timestamp_at_offset (GstVideoDecoder *
    decoder, guint64 offset, GstClockTime * timestamp, GstClockTime * duration)
{
#ifndef GST_DISABLE_GST_DEBUG
  guint64 got_offset = 0;
#endif
  Timestamp *ts;
  GList *g;

  *timestamp = GST_CLOCK_TIME_NONE;
  *duration = GST_CLOCK_TIME_NONE;

  g = decoder->priv->timestamps;
  while (g) {
    ts = g->data;
    if (ts->offset <= offset) {
#ifndef GST_DISABLE_GST_DEBUG
      got_offset = ts->offset;
#endif
      *timestamp = ts->timestamp;
      *duration = ts->duration;
      timestamp_free (ts);
      g = g->next;
      decoder->priv->timestamps = g_list_remove (decoder->priv->timestamps, ts);
    } else {
      break;
    }
  }

  GST_LOG_OBJECT (decoder,
      "got timestamp %" GST_TIME_FORMAT " @ offs %" G_GUINT64_FORMAT
      " (wanted offset:%" G_GUINT64_FORMAT ")", GST_TIME_ARGS (*timestamp),
      got_offset, offset);
}

static void
gst_video_decoder_clear_queues (GstVideoDecoder * dec)
{
  GstVideoDecoderPrivate *priv = dec->priv;

  g_list_foreach (priv->output_queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->output_queued);
  priv->output_queued = NULL;

  g_list_foreach (priv->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->gather);
  priv->gather = NULL;
  g_list_foreach (priv->decode, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (priv->decode);
  priv->decode = NULL;
  g_list_foreach (priv->parse, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (priv->parse);
  priv->parse = NULL;
  g_list_foreach (priv->parse_gather, (GFunc) gst_video_codec_frame_unref,
      NULL);
  g_list_free (priv->parse_gather);
  priv->parse_gather = NULL;
  g_list_foreach (priv->frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (priv->frames);
  priv->frames = NULL;
}

static void
gst_video_decoder_reset (GstVideoDecoder * decoder, gboolean full)
{
  GstVideoDecoderPrivate *priv = decoder->priv;

  GST_DEBUG_OBJECT (decoder, "reset full %d", full);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (full) {
    gst_segment_init (&decoder->input_segment, GST_FORMAT_UNDEFINED);
    gst_segment_init (&decoder->output_segment, GST_FORMAT_UNDEFINED);
    gst_video_decoder_clear_queues (decoder);
    priv->error_count = 0;
    priv->max_errors = GST_VIDEO_DECODER_MAX_ERRORS;
    if (priv->input_state)
      gst_video_codec_state_unref (priv->input_state);
    priv->input_state = NULL;
    if (priv->output_state)
      gst_video_codec_state_unref (priv->output_state);
    priv->output_state = NULL;

    GST_OBJECT_LOCK (decoder);
    priv->qos_frame_duration = 0;
    GST_OBJECT_UNLOCK (decoder);

    priv->min_latency = 0;
    priv->max_latency = 0;
  }

  priv->discont = TRUE;

  priv->base_timestamp = GST_CLOCK_TIME_NONE;
  priv->last_timestamp_in = GST_CLOCK_TIME_NONE;
  priv->last_timestamp_out = GST_CLOCK_TIME_NONE;
  priv->last_out_frame_number = (guint) (-1);
  priv->reordered_output = FALSE;
  priv->reordered_input = FALSE;

  priv->input_offset = 0;
  priv->frame_offset = 0;
  gst_adapter_clear (priv->input_adapter);
  gst_adapter_clear (priv->output_adapter);
  g_list_foreach (priv->timestamps, (GFunc) timestamp_free, NULL);
  g_list_free (priv->timestamps);
  priv->timestamps = NULL;

  if (priv->current_frame) {
    gst_video_codec_frame_unref (priv->current_frame);
    priv->current_frame = NULL;
  }

  priv->dropped = 0;
  priv->processed = 0;

  priv->decode_frame_number = 0;
  priv->base_picture_number = 0;

  g_list_foreach (priv->frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (priv->frames);
  priv->frames = NULL;

  priv->bytes_out = 0;
  priv->time = 0;

  GST_OBJECT_LOCK (decoder);
  priv->earliest_time = GST_CLOCK_TIME_NONE;
  priv->proportion = 0.5;
  GST_OBJECT_UNLOCK (decoder);

  priv->reorder_idx_out = priv->reorder_idx_in = 0;

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
}

static GstFlowReturn
gst_video_decoder_chain_forward (GstVideoDecoder * decoder,
    GstBuffer * buf, gboolean at_eos)
{
  GstVideoDecoderPrivate *priv;
  GstVideoDecoderClass *klass;
  GstFlowReturn ret = GST_FLOW_OK;

  klass = GST_VIDEO_DECODER_GET_CLASS (decoder);
  priv = decoder->priv;

  g_return_val_if_fail (priv->packetized || klass->parse, GST_FLOW_ERROR);

  if (priv->current_frame == NULL)
    priv->current_frame = gst_video_decoder_new_frame (decoder);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf) && !priv->packetized) {
    gst_video_decoder_add_timestamp (decoder, buf);
  }
  priv->input_offset += GST_BUFFER_SIZE (buf);

  if (priv->packetized) {
    if (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT)) {
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (priv->current_frame);
    }

    priv->current_frame->input_buffer = buf;

    if (decoder->input_segment.rate < 0.0) {
      priv->parse_gather =
          g_list_prepend (priv->parse_gather, priv->current_frame);
    } else {
      ret = gst_video_decoder_decode_frame (decoder, priv->current_frame);
    }
    priv->current_frame = NULL;
  } else {

    gst_adapter_push (priv->input_adapter, buf);

    if (G_UNLIKELY (!gst_adapter_available (priv->input_adapter)))
      goto beach;

    do {
      /* current frame may have been parsed and handled,
       * so we need to set up a new one when asking subclass to parse */
      if (priv->current_frame == NULL)
        priv->current_frame = gst_video_decoder_new_frame (decoder);

      ret = klass->parse (decoder, priv->current_frame,
          priv->input_adapter, at_eos);
    } while (ret == GST_FLOW_OK && gst_adapter_available (priv->input_adapter));
  }

beach:
  if (ret == GST_VIDEO_DECODER_FLOW_NEED_DATA)
    return GST_FLOW_OK;

  return ret;
}

static gint
_sort_by_buffer_pts (gconstpointer a, gconstpointer b)
{
  GstClockTime timestamp_a;
  GstClockTime timestamp_b;
  GstVideoCodecFrame *frame_a = (GstVideoCodecFrame *) a;
  GstVideoCodecFrame *frame_b = (GstVideoCodecFrame *) b;

  timestamp_a = GST_BUFFER_TIMESTAMP (frame_a->input_buffer);
  timestamp_b = GST_BUFFER_TIMESTAMP (frame_b->input_buffer);

  return timestamp_a - timestamp_b;
}

static GstFlowReturn
gst_video_decoder_flush_decode (GstVideoDecoder * dec)
{
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn res = GST_FLOW_OK;
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_GET_CLASS (dec);
  GList *walk;

  GST_DEBUG_OBJECT (dec, "flushing buffers to decode");

  /* clear buffer and decoder state */
  gst_video_decoder_flush (dec, FALSE, FALSE);

  /* Retimestamp if input timestamps are going backwards */
  if (priv->decode) {
    GstVideoCodecFrame *frame_first;
    GstVideoCodecFrame *frame_last;

    frame_first = (GstVideoCodecFrame *) g_list_nth_data (priv->decode, 0);
    frame_last =
        (GstVideoCodecFrame *) g_list_nth_data (priv->decode,
        g_list_length (priv->decode) - 1);

    if (GST_BUFFER_TIMESTAMP (frame_last->input_buffer) <
        GST_BUFFER_TIMESTAMP (frame_first->input_buffer)) {
      /* We need to keep buffers order the same, but just change timestamps,
       * so all the reordering would also be kept the same. To do this we will
       * create a list of buffers ordered by timestamps, then reverse it, and then
       * apply new timestamps to this list */
      gint i = 0;
      GList *timestamps_list;
      GList *timestamps_list2;

      timestamps_list = g_list_copy (priv->decode);
      timestamps_list = g_list_sort (timestamps_list, _sort_by_buffer_pts);
      timestamps_list2 = g_list_copy (timestamps_list);
      timestamps_list = g_list_reverse (timestamps_list);


      for (walk = timestamps_list2; walk; walk = walk->next) {
        GstVideoCodecFrame *frame;
        GstVideoCodecFrame *frame2;

        frame = (GstVideoCodecFrame *) (walk->data);
        frame2 = (GstVideoCodecFrame *) g_list_nth_data (timestamps_list, i++);

        GST_BUFFER_TIMESTAMP (frame->input_buffer) =
            GST_BUFFER_TIMESTAMP (frame2->input_buffer);

        frame->pts = frame2->pts;
      }

      g_list_free (timestamps_list);
      g_list_free (timestamps_list2);
    }
  }

  walk = priv->decode;
  while (walk) {
    GList *next;
    GstVideoCodecFrame *frame = (GstVideoCodecFrame *) (walk->data);

    GST_DEBUG_OBJECT (dec, "decoding frame %p buffer %p, ts %" GST_TIME_FORMAT,
        frame, frame->input_buffer,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (frame->input_buffer)));

    next = walk->next;

    priv->decode = g_list_delete_link (priv->decode, walk);

    /* decode buffer, resulting data prepended to queue */
    res = gst_video_decoder_decode_frame (dec, frame);
    if (res != GST_FLOW_OK)
      break;

    walk = next;
  }

  /* Drain decoder + sync. */
  if (res == GST_FLOW_OK && decoder_class->finish)
    res = decoder_class->finish (dec);

  return res;
}

/* gst_video_decoder_flush_parse is called from the
 * chain_reverse() function when a buffer containing
 * a DISCONT - indicating that reverse playback
 * looped back to the next data block, and therefore
 * all available data should be fed through the
 * decoder and frames gathered for reversed output
 */
static GstFlowReturn
gst_video_decoder_flush_parse (GstVideoDecoder * dec, gboolean at_eos)
{
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  GST_DEBUG_OBJECT (dec, "flushing buffers to parsing");

  /* Reverse the gather list, and prepend it to the parse list,
   * then flush to parse whatever we can */
  priv->gather = g_list_reverse (priv->gather);
  priv->parse = g_list_concat (priv->gather, priv->parse);
  priv->gather = NULL;

  /* clear buffer and decoder state */
  gst_video_decoder_flush (dec, FALSE, FALSE);

  walk = priv->parse;
  while (walk) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);
    GList *next = walk->next;

    GST_DEBUG_OBJECT (dec, "parsing buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    /* parse buffer, resulting frames prepended to parse_gather queue */
    gst_buffer_ref (buf);
    res = gst_video_decoder_chain_forward (dec, buf, at_eos);

    /* if we generated output, we can discard the buffer, else we
     * keep it in the queue */
    if (priv->parse_gather) {
      GST_DEBUG_OBJECT (dec, "parsed buffer to %p", priv->parse_gather->data);
      priv->parse = g_list_delete_link (priv->parse, walk);
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (dec, "buffer did not decode, keeping");
    }
    walk = next;
  }

  /* now we can process frames. Start by moving each frame from the parse_gather
   * to the decode list, reverse the order as we go, and stopping when/if we
   * copy a keyframe. */
  GST_DEBUG_OBJECT (dec, "checking parsed frames for a keyframe to decode");
  walk = priv->parse_gather;
  while (walk) {
    GstVideoCodecFrame *frame = (GstVideoCodecFrame *) (walk->data);

    /* remove from the gather list */
    priv->parse_gather = g_list_remove_link (priv->parse_gather, walk);

    /* move it to the front of the decode queue */
    priv->decode = g_list_concat (walk, priv->decode);

    /* if we copied a keyframe, flush and decode the decode queue */
    if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame)) {
      GST_DEBUG_OBJECT (dec, "found keyframe %p with PTS %" GST_TIME_FORMAT,
          frame, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (frame->input_buffer)));
      res = gst_video_decoder_flush_decode (dec);
      if (res != GST_FLOW_OK)
        goto done;
    }

    walk = priv->parse_gather;
  }

  /* now send queued data downstream */
  walk = priv->output_queued;
  while (walk) {
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    if (G_LIKELY (res == GST_FLOW_OK)) {
      /* avoid stray DISCONT from forward processing,
       * which have no meaning in reverse pushing */
      GST_BUFFER_FLAG_UNSET (buf, GST_BUFFER_FLAG_DISCONT);

      /* Last chance to calculate a timestamp as we loop backwards
       * through the list */
      if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE)
        priv->last_timestamp_out = GST_BUFFER_TIMESTAMP (buf);
      else if (priv->last_timestamp_out != GST_CLOCK_TIME_NONE &&
          GST_BUFFER_DURATION (buf) != GST_CLOCK_TIME_NONE) {
        GST_BUFFER_TIMESTAMP (buf) =
            priv->last_timestamp_out - GST_BUFFER_DURATION (buf);
        priv->last_timestamp_out = GST_BUFFER_TIMESTAMP (buf);
        GST_LOG_OBJECT (dec,
            "Calculated TS %" GST_TIME_FORMAT " working backwards. Duration %"
            GST_TIME_FORMAT, GST_TIME_ARGS (priv->last_timestamp_out),
            GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
      }

      res = gst_video_decoder_clip_and_push_buf (dec, buf);
    } else {
      gst_buffer_unref (buf);
    }

    priv->output_queued =
        g_list_delete_link (priv->output_queued, priv->output_queued);
    walk = priv->output_queued;
  }

done:
  return res;
}

static GstFlowReturn
gst_video_decoder_chain_reverse (GstVideoDecoder * dec, GstBuffer * buf)
{
  GstVideoDecoderPrivate *priv = dec->priv;
  GstFlowReturn result = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (!buf || GST_BUFFER_IS_DISCONT (buf)) {
    GST_DEBUG_OBJECT (dec, "received discont");

    /* parse and decode stuff in the gather and parse queues */
    gst_video_decoder_flush_parse (dec, FALSE);
  }

  if (G_LIKELY (buf)) {
    GST_DEBUG_OBJECT (dec, "gathering buffer %p of size %u, "
        "time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT, buf,
        GST_BUFFER_SIZE (buf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* add buffer to gather queue */
    priv->gather = g_list_prepend (priv->gather, buf);
  }

  return result;
}

static GstFlowReturn
gst_video_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstVideoDecoder *decoder;
  GstFlowReturn ret = GST_FLOW_OK;

  decoder = GST_VIDEO_DECODER (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (decoder,
      "chain %" GST_TIME_FORMAT " duration %" GST_TIME_FORMAT " size %d",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_BUFFER_SIZE (buf));

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  /* NOTE:
   * requiring the pad to be negotiated makes it impossible to use
   * oggdemux or filesrc ! decoder */

  if (decoder->input_segment.format == GST_FORMAT_UNDEFINED) {
    GstEvent *event;

    GST_WARNING_OBJECT (decoder,
        "Received buffer without a new-segment. "
        "Assuming timestamps start from 0.");

    gst_segment_set_newsegment_full (&decoder->input_segment, FALSE, 1.0, 1.0,
        GST_FORMAT_TIME, 0, GST_CLOCK_TIME_NONE, 0);

    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0,
        GST_CLOCK_TIME_NONE, 0);

    decoder->priv->current_frame_events =
        g_list_prepend (decoder->priv->current_frame_events, event);
  }

  if (decoder->input_segment.rate > 0.0)
    ret = gst_video_decoder_chain_forward (decoder, buf, FALSE);
  else
    ret = gst_video_decoder_chain_reverse (decoder, buf);

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return ret;
}

static GstStateChangeReturn
gst_video_decoder_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoDecoder *decoder;
  GstVideoDecoderClass *decoder_class;
  GstStateChangeReturn ret;

  decoder = GST_VIDEO_DECODER (element);
  decoder_class = GST_VIDEO_DECODER_GET_CLASS (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open device/library if needed */
      if (decoder_class->open && !decoder_class->open (decoder))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Initialize device/library if needed */
      if (decoder_class->start && !decoder_class->start (decoder))
        goto start_failed;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (decoder_class->stop && !decoder_class->stop (decoder))
        goto stop_failed;

      GST_VIDEO_DECODER_STREAM_LOCK (decoder);
      gst_video_decoder_reset (decoder, TRUE);
      g_list_foreach (decoder->priv->current_frame_events,
          (GFunc) gst_event_unref, NULL);
      g_list_free (decoder->priv->current_frame_events);
      decoder->priv->current_frame_events = NULL;
      GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* close device/library if needed */
      if (decoder_class->close && !decoder_class->close (decoder))
        goto close_failed;
      break;
    default:
      break;
  }

  return ret;

  /* Errors */
open_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to open decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

start_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to start decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

stop_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to stop decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }

close_failed:
  {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Failed to close decoder"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static GstVideoCodecFrame *
gst_video_decoder_new_frame (GstVideoDecoder * decoder)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoCodecFrame *frame;

  frame = g_slice_new0 (GstVideoCodecFrame);

  frame->ref_count = 1;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  frame->system_frame_number = priv->system_frame_number;
  priv->system_frame_number++;
  frame->decode_frame_number = priv->decode_frame_number;
  priv->decode_frame_number++;

  frame->dts = GST_CLOCK_TIME_NONE;
  frame->pts = GST_CLOCK_TIME_NONE;
  frame->duration = GST_CLOCK_TIME_NONE;
  frame->events = priv->current_frame_events;
  priv->current_frame_events = NULL;
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  GST_LOG_OBJECT (decoder, "Created new frame %p (sfn:%d)",
      frame, frame->system_frame_number);

  return frame;
}


GstVideoCodecFrame *
gst_video_decoder_get_output_frame (GstVideoDecoder * decoder,
    GstClockTime reference_timestamp)
{
  GList *frames, *l;
  gint64 min_ts = G_MAXINT64;
  GstVideoCodecFrame *ret = NULL;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  if (!decoder->priv->input_state) {
    GST_ERROR_OBJECT (decoder, "No input state");
    return NULL;
  }

  /* Getting a frame with smallest pts */
  frames = gst_video_decoder_get_frames (decoder);
  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *frame = l->data;

    if (frame->pts < min_ts) {
      min_ts = frame->pts;
      ret = frame;
    }
  }

  if (ret)
    gst_video_codec_frame_ref (ret);
  else {
    ret = gst_video_decoder_new_frame (decoder);
    flow_ret = gst_video_decoder_alloc_output_frame (decoder, ret);
    if (G_UNLIKELY (flow_ret != GST_FLOW_OK)) {
      GST_ERROR_OBJECT (decoder,
          "Failed to allocate frame for pts = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (reference_timestamp));
      gst_video_codec_frame_unref (ret);
      ret = NULL;
    }
  }

  if (ret) {
    /* We trust the timestamp OMX decoder provided to us, and hack the duration,
       because no duration is ever provided. */
    ret->pts = reference_timestamp;
    if (decoder->priv->input_state->info.fps_n)
      ret->duration = GST_SECOND * decoder->priv->input_state->info.fps_d /
          decoder->priv->input_state->info.fps_n;
    else
      ret->duration = 0;
    if (decoder->priv->input_state->info.interlace_mode ==
        GST_VIDEO_INTERLACE_MODE_INTERLEAVED)
      ret->duration /= 2;
    /* Hacking the deadline to avoid dropping. Side-effect of this is
       that we'll not drop this frame even if we should.. */
    ret->deadline = decoder->priv->earliest_time + ret->duration;
    GST_LOG_OBJECT (decoder, "Providing frame with pts=%" GST_TIME_FORMAT
        ",duration=%" GST_TIME_FORMAT, GST_TIME_ARGS (ret->pts),
        GST_TIME_ARGS (ret->duration));
  }

  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (frames);
  return ret;
}


static void
gst_video_decoder_prepare_finish_frame (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame, gboolean dropping)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GList *l, *events = NULL;
  GstClockTime reorder_pts;

#ifndef GST_DISABLE_GST_DEBUG
  GST_LOG_OBJECT (decoder, "n %d in %d out %d",
      g_list_length (priv->frames),
      gst_adapter_available (priv->input_adapter),
      gst_adapter_available (priv->output_adapter));
#endif

  reorder_pts = priv->incoming_timestamps[priv->reorder_idx_out];
  priv->reorder_idx_out =
      (priv->reorder_idx_out + 1) % MAX_DTS_PTS_REORDER_DEPTH;

  if (!priv->reordered_output && frame->system_frame_number &&
      priv->last_out_frame_number != (guint) (-1) &&
      frame->system_frame_number != (priv->last_out_frame_number + 1)) {
    GST_DEBUG_OBJECT (decoder, "Detected reordered output");
    //priv->reordered_output = TRUE;
  }

  GST_LOG_OBJECT (decoder,
      "finish frame %p (#%d) sync:%d pts:%" GST_TIME_FORMAT " dts:%"
      GST_TIME_FORMAT " reorder_pts:%" GST_TIME_FORMAT,
      frame, frame->system_frame_number,
      GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame), GST_TIME_ARGS (frame->pts),
      GST_TIME_ARGS (frame->dts), GST_TIME_ARGS (reorder_pts));

  /* Push all pending events that arrived before this frame */
  for (l = priv->frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;

    if (tmp->events) {
      events = g_list_concat (events, tmp->events);
      tmp->events = NULL;
    }

    if (tmp == frame)
      break;
  }

  for (l = g_list_last (events); l; l = g_list_previous (l)) {
    GST_LOG_OBJECT (decoder, "pushing %s event", GST_EVENT_TYPE_NAME (l->data));
    gst_video_decoder_push_event (decoder, l->data);
  }
  g_list_free (events);

  /* Check if the data should not be displayed. For example altref/invisible
   * frame in vp8. In this case we should not update the timestamps. */
  if (GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (frame))
    return;

  /* If the frame is meant to be output but we don't have an output_buffer
   * we have a problem :) */
  if (G_UNLIKELY ((frame->output_buffer == NULL) && !dropping))
    goto no_output_buffer;

  if (GST_CLOCK_TIME_IS_VALID (frame->pts)) {
    if (frame->pts != priv->base_timestamp) {
      GST_DEBUG_OBJECT (decoder,
          "sync timestamp %" GST_TIME_FORMAT " diff %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->pts),
          GST_TIME_ARGS (frame->pts - decoder->output_segment.start));
      priv->base_timestamp = frame->pts;
      priv->base_picture_number = frame->decode_frame_number;
    }
  }

  if (frame->duration == GST_CLOCK_TIME_NONE) {
    frame->duration = gst_video_decoder_get_frame_duration (decoder, frame);
    GST_LOG_OBJECT (decoder,
        "Guessing duration %" GST_TIME_FORMAT " for frame...",
        GST_TIME_ARGS (frame->duration));
  }

  if (frame->pts == GST_CLOCK_TIME_NONE) {
    /* Last ditch timestamp guess: Just add the duration to the previous
     * frame */
    if (priv->last_timestamp_out != GST_CLOCK_TIME_NONE &&
        frame->duration != GST_CLOCK_TIME_NONE) {
      frame->pts = priv->last_timestamp_out + frame->duration;
      GST_LOG_OBJECT (decoder,
          "Guessing timestamp %" GST_TIME_FORMAT " for frame...",
          GST_TIME_ARGS (frame->pts));
    }
  }

  /* Fix buffers that came in with DTS and were reordered */
  if (!priv->reordered_input && priv->reordered_output
      && GST_CLOCK_TIME_IS_VALID (reorder_pts)) {
    GST_DEBUG_OBJECT (decoder,
        "Correcting PTS, input buffers had DTS on their timestamps");
    frame->pts = reorder_pts;
  }

  if (GST_CLOCK_TIME_IS_VALID (priv->last_timestamp_out)) {
    if (frame->pts < priv->last_timestamp_out) {
      GST_WARNING_OBJECT (decoder,
          "decreasing timestamp (%" GST_TIME_FORMAT " < %"
          GST_TIME_FORMAT ")",
          GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (priv->last_timestamp_out));
      frame->pts = reorder_pts;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (frame->pts)) {
    priv->last_timestamp_out = frame->pts;
    priv->last_out_frame_number = frame->system_frame_number;
  }

  return;

  /* ERRORS */
no_output_buffer:
  {
    GST_ERROR_OBJECT (decoder, "No buffer to output !");
  }
}

void
gst_video_decoder_release_frame (GstVideoDecoder * dec,
    GstVideoCodecFrame * frame)
{
  GList *link;

  /* unref once from the list */
  link = g_list_find (dec->priv->frames, frame);
  if (link) {
    gst_video_codec_frame_unref (frame);
    dec->priv->frames = g_list_delete_link (dec->priv->frames, link);
  }

  /* unref because this function takes ownership */
  gst_video_codec_frame_unref (frame);
}

/**
 * gst_video_decoder_drop_frame:
 * @dec: a #GstVideoDecoder
 * @frame: (transfer full): the #GstVideoCodecFrame to drop
 *
 * Similar to gst_video_decoder_finish_frame(), but drops @frame in any
 * case and posts a QoS message with the frame's details on the bus.
 * In any case, the frame is considered finished and released.
 *
 * Returns: a #GstFlowReturn, usually GST_FLOW_OK.
 *
 * Since: 0.10.37
 */
GstFlowReturn
gst_video_decoder_drop_frame (GstVideoDecoder * dec, GstVideoCodecFrame * frame)
{
  GstClockTime stream_time, jitter, earliest_time, qostime, timestamp;
  GstSegment *segment;
  GstMessage *qos_msg;
  gdouble proportion;

  GST_LOG_OBJECT (dec, "drop frame %p", frame);

  GST_VIDEO_DECODER_STREAM_LOCK (dec);

  gst_video_decoder_prepare_finish_frame (dec, frame, TRUE);

  GST_DEBUG_OBJECT (dec, "dropping frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->pts));

  dec->priv->dropped++;

  /* post QoS message */
  GST_OBJECT_LOCK (dec);
  proportion = dec->priv->proportion;
  earliest_time = dec->priv->earliest_time;
  GST_OBJECT_UNLOCK (dec);

  timestamp = frame->pts;
  segment = &dec->output_segment;
  stream_time =
      gst_segment_to_stream_time (segment, GST_FORMAT_TIME, timestamp);
  qostime = gst_segment_to_running_time (segment, GST_FORMAT_TIME, timestamp);
  jitter = GST_CLOCK_DIFF (qostime, earliest_time);
  qos_msg =
      gst_message_new_qos (GST_OBJECT_CAST (dec), FALSE, qostime, stream_time,
      timestamp, GST_CLOCK_TIME_NONE);
  gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
  gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
      dec->priv->processed, dec->priv->dropped);
  gst_element_post_message (GST_ELEMENT_CAST (dec), qos_msg);

  /* now free the frame */
  gst_video_decoder_release_frame (dec, frame);

  GST_VIDEO_DECODER_STREAM_UNLOCK (dec);

  return GST_FLOW_OK;
}

/**
 * gst_video_decoder_finish_frame:
 * @decoder: a #GstVideoDecoder
 * @frame: (transfer full): a decoded #GstVideoCodecFrame
 *
 * @frame should have a valid decoded data buffer, whose metadata fields
 * are then appropriately set according to frame data and pushed downstream.
 * If no output data is provided, @frame is considered skipped.
 * In any case, the frame is considered finished and released.
 *
 * Returns: a #GstFlowReturn resulting from sending data downstream
 *
 * Since: 0.10.37
 */
GstFlowReturn
gst_video_decoder_finish_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoCodecState *state = priv->output_state;
  GstBuffer *output_buffer;

  GST_LOG_OBJECT (decoder, "finish frame %p", frame);

  if (G_UNLIKELY (priv->output_state_changed))
    gst_video_decoder_set_src_caps (decoder);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  gst_video_decoder_prepare_finish_frame (decoder, frame, FALSE);
  priv->processed++;

  /* no buffer data means this frame is skipped */
  if (!frame->output_buffer || GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (frame)) {
    GST_DEBUG_OBJECT (decoder, "skipping frame %" GST_TIME_FORMAT,
        GST_TIME_ARGS (frame->pts));
    goto done;
  }

  /* A reference always needs to be owned by the frame on the buffer.
   * For that reason, we use a complete sub-buffer (zero-cost) to push
   * downstream.
   * The original buffer will be free-ed only when downstream AND the
   * current implementation are done with the frame. */
  output_buffer =
      gst_buffer_create_sub (frame->output_buffer, 0,
      GST_BUFFER_SIZE (frame->output_buffer));

  GST_BUFFER_FLAG_UNSET (output_buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  if (GST_VIDEO_INFO_IS_INTERLACED (&state->info)) {
    if (GST_VIDEO_CODEC_FRAME_FLAG_IS_SET (frame,
            GST_VIDEO_CODEC_FRAME_FLAG_TFF)) {
      GST_BUFFER_FLAG_SET (output_buffer, GST_VIDEO_BUFFER_TFF);
    } else {
      GST_BUFFER_FLAG_UNSET (output_buffer, GST_VIDEO_BUFFER_TFF);
    }
    if (GST_VIDEO_CODEC_FRAME_FLAG_IS_SET (frame,
            GST_VIDEO_CODEC_FRAME_FLAG_RFF)) {
      GST_BUFFER_FLAG_SET (output_buffer, GST_VIDEO_BUFFER_RFF);
    } else {
      GST_BUFFER_FLAG_UNSET (output_buffer, GST_VIDEO_BUFFER_RFF);
    }
    if (GST_VIDEO_CODEC_FRAME_FLAG_IS_SET (frame,
            GST_VIDEO_CODEC_FRAME_FLAG_ONEFIELD)) {
      GST_BUFFER_FLAG_SET (output_buffer, GST_VIDEO_BUFFER_ONEFIELD);
    } else {
      GST_BUFFER_FLAG_UNSET (output_buffer, GST_VIDEO_BUFFER_ONEFIELD);
    }
  }

  GST_BUFFER_TIMESTAMP (output_buffer) = frame->pts;
  GST_BUFFER_DURATION (output_buffer) = frame->duration;

  GST_BUFFER_OFFSET (output_buffer) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (output_buffer) = GST_BUFFER_OFFSET_NONE;

  if (priv->discont) {
    GST_BUFFER_FLAG_SET (output_buffer, GST_BUFFER_FLAG_DISCONT);
    priv->discont = FALSE;
  }

  if (decoder->output_segment.rate < 0.0) {
    GST_LOG_OBJECT (decoder, "queued frame");
    priv->output_queued = g_list_prepend (priv->output_queued, output_buffer);
  } else {
    ret = gst_video_decoder_clip_and_push_buf (decoder, output_buffer);
  }

done:
  gst_video_decoder_release_frame (decoder, frame);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return ret;
}


/* With stream lock, takes the frame reference */
static GstFlowReturn
gst_video_decoder_clip_and_push_buf (GstVideoDecoder * decoder, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoDecoderPrivate *priv = decoder->priv;
  gint64 start, stop;
  gint64 cstart, cstop;
  GstSegment *segment;
  GstClockTime duration;

  /* Check for clipping */
  start = GST_BUFFER_TIMESTAMP (buf);
  duration = GST_BUFFER_DURATION (buf);

  stop = GST_CLOCK_TIME_NONE;

  if (GST_CLOCK_TIME_IS_VALID (start) && GST_CLOCK_TIME_IS_VALID (duration)) {
    stop = start + duration;
  }

  segment = &decoder->output_segment;
  if (gst_segment_clip (segment, GST_FORMAT_TIME, start, stop, &cstart, &cstop)) {

    GST_BUFFER_TIMESTAMP (buf) = cstart;

    if (stop != GST_CLOCK_TIME_NONE)
      GST_BUFFER_DURATION (buf) = cstop - cstart;

    GST_LOG_OBJECT (decoder,
        "accepting buffer inside segment: %" GST_TIME_FORMAT " %"
        GST_TIME_FORMAT " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
        " time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf) +
            GST_BUFFER_DURATION (buf)),
        GST_TIME_ARGS (segment->start), GST_TIME_ARGS (segment->stop),
        GST_TIME_ARGS (segment->time));
  } else {
    GST_LOG_OBJECT (decoder,
        "dropping buffer outside segment: %" GST_TIME_FORMAT
        " %" GST_TIME_FORMAT
        " seg %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
        " time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
        GST_TIME_ARGS (segment->start),
        GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time));
    gst_buffer_unref (buf);
    goto done;
  }

  /* update rate estimate */
  priv->bytes_out += GST_BUFFER_SIZE (buf);
  if (GST_CLOCK_TIME_IS_VALID (duration)) {
    priv->time += duration;
  } else {
    /* FIXME : Use difference between current and previous outgoing
     * timestamp, and relate to difference between current and previous
     * bytes */
    /* better none than nothing valid */
    priv->time = GST_CLOCK_TIME_NONE;
  }

  gst_buffer_set_caps (buf, GST_PAD_CAPS (decoder->srcpad));

  GST_LOG_OBJECT (decoder, "pushing buffer %p of size %u ts %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT, buf, GST_BUFFER_SIZE (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  /* we got data, so note things are looking up again, reduce
   * the error count, if there is one */
  if (G_UNLIKELY (priv->error_count))
    priv->error_count = 0;

  ret = gst_pad_push (decoder->srcpad, buf);

done:
  return ret;
}

/**
 * gst_video_decoder_add_to_frame:
 * @decoder: a #GstVideoDecoder
 * @n_bytes: the number of bytes to add
 *
 * Removes next @n_bytes of input data and adds it to currently parsed frame.
 *
 * Since: 0.10.37
 */
void
gst_video_decoder_add_to_frame (GstVideoDecoder * decoder, int n_bytes)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstBuffer *buf;

  GST_LOG_OBJECT (decoder, "add %d bytes to frame", n_bytes);

  if (n_bytes == 0)
    return;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (gst_adapter_available (priv->output_adapter) == 0) {
    priv->frame_offset =
        priv->input_offset - gst_adapter_available (priv->input_adapter);
  }
  buf = gst_adapter_take_buffer (priv->input_adapter, n_bytes);

  gst_adapter_push (priv->output_adapter, buf);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
}

static guint64
gst_video_decoder_get_frame_duration (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVideoCodecState *state = decoder->priv->output_state;
  gint fields;

  /* it's possible that we don't have a state yet when we are dropping the
   * initial buffers */
  if (state == NULL)
    goto def_fps;

  if (state->info.fps_d == 0 || state->info.fps_n == 0) {
    goto def_fps;
  }

  if (GST_VIDEO_CODEC_FRAME_FLAG_IS_SET (frame, GST_VIDEO_CODEC_FRAME_FLAG_RFF))
    fields = 3;
  else if (GST_VIDEO_CODEC_FRAME_FLAG_IS_SET (frame,
          GST_VIDEO_CODEC_FRAME_FLAG_ONEFIELD))
    fields = 1;
  else
    fields = 2;

  return gst_util_uint64_scale (fields * GST_SECOND, state->info.fps_d,
      state->info.fps_n * 2);

def_fps:
  return gst_util_uint64_scale (GST_SECOND, 1, 30);
}

/**
 * gst_video_decoder_have_frame:
 * @decoder: a #GstVideoDecoder
 *
 * Gathers all data collected for currently parsed frame, gathers corresponding
 * metadata and passes it along for further processing, i.e. @handle_frame.
 *
 * Returns: a #GstFlowReturn
 *
 * Since: 0.10.37
 */
GstFlowReturn
gst_video_decoder_have_frame (GstVideoDecoder * decoder)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstBuffer *buffer;
  int n_available;
  GstClockTime timestamp;
  GstClockTime duration;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (decoder, "have_frame");

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  n_available = gst_adapter_available (priv->output_adapter);
  if (n_available) {
    buffer = gst_adapter_take_buffer (priv->output_adapter, n_available);
  } else {
    buffer = gst_buffer_new_and_alloc (0);
  }

  priv->current_frame->input_buffer = buffer;

  gst_video_decoder_get_timestamp_at_offset (decoder,
      priv->frame_offset, &timestamp, &duration);

  GST_BUFFER_TIMESTAMP (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = duration;

  GST_LOG_OBJECT (decoder, "collected frame size %d, "
      "ts %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT,
      n_available, GST_TIME_ARGS (timestamp), GST_TIME_ARGS (duration));

  /* In reverse playback, just capture and queue frames for later processing */
  if (decoder->output_segment.rate < 0.0) {
    priv->parse_gather =
        g_list_prepend (priv->parse_gather, priv->current_frame);
  } else {
    /* Otherwise, decode the frame, which gives away our ref */
    ret = gst_video_decoder_decode_frame (decoder, priv->current_frame);
  }
  /* Current frame is gone now, either way */
  priv->current_frame = NULL;

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return ret;
}

/* Pass the frame in priv->current_frame through the
 * handle_frame() callback for decoding and passing to gvd_finish_frame(), 
 * or dropping by passing to gvd_drop_frame() */
static GstFlowReturn
gst_video_decoder_decode_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoDecoderClass *decoder_class;
  GstFlowReturn ret = GST_FLOW_OK;

  decoder_class = GST_VIDEO_DECODER_GET_CLASS (decoder);

  /* FIXME : This should only have to be checked once (either the subclass has an 
   * implementation, or it doesn't) */
  g_return_val_if_fail (decoder_class->handle_frame != NULL, GST_FLOW_ERROR);

  frame->distance_from_sync = priv->distance_from_sync;
  priv->distance_from_sync++;
  frame->pts = GST_BUFFER_TIMESTAMP (frame->input_buffer);
  frame->duration = GST_BUFFER_DURATION (frame->input_buffer);

  /* For keyframes, DTS = PTS */
  if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
    frame->dts = frame->pts;

  GST_LOG_OBJECT (decoder, "pts %" GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
  GST_LOG_OBJECT (decoder, "dts %" GST_TIME_FORMAT, GST_TIME_ARGS (frame->dts));
  GST_LOG_OBJECT (decoder, "dist %d", frame->distance_from_sync);
  priv->frames = g_list_append (priv->frames, frame);
  frame->deadline =
      gst_segment_to_running_time (&decoder->input_segment, GST_FORMAT_TIME,
      frame->pts);

  /* Store pts */
  if (GST_CLOCK_TIME_IS_VALID (frame->pts)
      && GST_CLOCK_TIME_IS_VALID (priv->last_timestamp_in)
      && frame->pts < priv->last_timestamp_in) {
    GST_DEBUG_OBJECT (decoder, "Incoming timestamps are out of order");
    //priv->reordered_input = TRUE;
  }
  priv->last_timestamp_in = frame->pts;
  priv->incoming_timestamps[priv->reorder_idx_in] = frame->pts;
  priv->reorder_idx_in = (priv->reorder_idx_in + 1) % MAX_DTS_PTS_REORDER_DEPTH;

  /* do something with frame */
  gst_video_codec_frame_ref (frame);
  ret = decoder_class->handle_frame (decoder, frame);
  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (decoder, "flow error %s", gst_flow_get_name (ret));

  /* the frame has either been added to parse_gather or sent to
     handle frame so there is no need to unref it */
  return ret;
}


/**
 * gst_video_decoder_get_output_state:
 * @decoder: a #GstVideoDecoder
 *
 * Get the #GstVideoCodecState currently describing the output stream.
 *
 * Returns: (transfer full): #GstVideoCodecState describing format of video data.
 *
 * Since: 0.10.37
 */
GstVideoCodecState *
gst_video_decoder_get_output_state (GstVideoDecoder * decoder)
{
  GstVideoCodecState *state = NULL;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (decoder->priv->output_state)
    state = gst_video_codec_state_ref (decoder->priv->output_state);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return state;
}

/**
 * gst_video_decoder_set_output_state:
 * @decoder: a #GstVideoDecoder
 * @fmt: a #GstVideoFormat
 * @width: The width in pixels
 * @height: The height in pixels
 * @reference: (allow-none) (transfer none): An optional reference #GstVideoCodecState
 *
 * Creates a new #GstVideoCodecState with the specified @fmt, @width and @height
 * as the output state for the decoder.
 * Any previously set output state on @decoder will be replaced by the newly
 * created one.
 *
 * If the subclass wishes to copy over existing fields (like pixel aspec ratio,
 * or framerate) from an existing #GstVideoCodecState, it can be provided as a
 * @reference.
 *
 * If the subclass wishes to override some fields from the output state (like
 * pixel-aspect-ratio or framerate) it can do so on the returned #GstVideoCodecState.
 *
 * The new output state will only take effect (set on pads and buffers) starting
 * from the next call to #gst_video_decoder_finish_frame().
 *
 * Returns: (transfer full): the newly configured output state.
 *
 * Since: 0.10.37
 */
GstVideoCodecState *
gst_video_decoder_set_output_state (GstVideoDecoder * decoder,
    GstVideoFormat fmt, guint width, guint height,
    GstVideoCodecState * reference)
{
  GstVideoDecoderPrivate *priv = decoder->priv;
  GstVideoCodecState *state;
  GstClockTime qos_frame_duration;

  GST_DEBUG_OBJECT (decoder, "fmt:%d, width:%d, height:%d, reference:%p",
      fmt, width, height, reference);

  /* Create the new output state */
  state = _new_output_state (fmt, width, height, reference);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  /* Replace existing output state by new one */
  if (priv->output_state)
    gst_video_codec_state_unref (priv->output_state);
  priv->output_state = gst_video_codec_state_ref (state);

  if (priv->output_state != NULL && priv->output_state->info.fps_n > 0) {
    qos_frame_duration =
        gst_util_uint64_scale (GST_SECOND, priv->output_state->info.fps_d,
        priv->output_state->info.fps_n);
  } else {
    qos_frame_duration = 0;
  }
  priv->output_state_changed = TRUE;
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  GST_OBJECT_LOCK (decoder);
  priv->qos_frame_duration = qos_frame_duration;
  GST_OBJECT_UNLOCK (decoder);

  return state;
}


/**
 * gst_video_decoder_get_oldest_frame:
 * @decoder: a #GstVideoDecoder
 *
 * Get the oldest pending unfinished #GstVideoCodecFrame
 *
 * Returns: (transfer full): oldest pending unfinished #GstVideoCodecFrame.
 *
 * Since: 0.10.37
 */
GstVideoCodecFrame *
gst_video_decoder_get_oldest_frame (GstVideoDecoder * decoder)
{
  GstVideoCodecFrame *frame = NULL;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (decoder->priv->frames)
    frame = gst_video_codec_frame_ref (decoder->priv->frames->data);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return (GstVideoCodecFrame *) frame;
}

/**
 * gst_video_decoder_get_frame:
 * @decoder: a #GstVideoDecoder
 * @frame_number: system_frame_number of a frame
 *
 * Get a pending unfinished #GstVideoCodecFrame
 * 
 * Returns: (transfer full): pending unfinished #GstVideoCodecFrame identified by @frame_number.
 *
 * Since: 0.10.37
 */
GstVideoCodecFrame *
gst_video_decoder_get_frame (GstVideoDecoder * decoder, int frame_number)
{
  GList *g;
  GstVideoCodecFrame *frame = NULL;

  GST_DEBUG_OBJECT (decoder, "frame_number : %d", frame_number);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  for (g = decoder->priv->frames; g; g = g->next) {
    GstVideoCodecFrame *tmp = g->data;

    if (tmp->system_frame_number == frame_number) {
      frame = tmp;
      gst_video_codec_frame_ref (frame);
      break;
    }
  }
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return frame;
}

/**
 * gst_video_decoder_get_frames:
 * @decoder: a #GstVideoDecoder
 *
 * Get all pending unfinished #GstVideoCodecFrame
 * 
 * Returns: (transfer full) (element-type GstVideoCodecFrame): pending unfinished #GstVideoCodecFrame.
 */
GList *
gst_video_decoder_get_frames (GstVideoDecoder * decoder)
{
  GList *frames;

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  frames = g_list_copy (decoder->priv->frames);
  g_list_foreach (frames, (GFunc) gst_video_codec_frame_ref, NULL);
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return frames;
}

/**
 * gst_video_decoder_set_src_caps:
 * @decoder: a #GstVideoDecoder
 *
 * Sets src pad caps according to currently configured #GstVideoCodecState.
 *
 * Returns: #TRUE if the caps were accepted downstream, else #FALSE.
 *
 * Since: 0.10.37
 */
static gboolean
gst_video_decoder_set_src_caps (GstVideoDecoder * decoder)
{
  GstVideoCodecState *state = decoder->priv->output_state;
  gboolean ret;

  g_return_val_if_fail (GST_VIDEO_INFO_WIDTH (&state->info) != 0, FALSE);
  g_return_val_if_fail (GST_VIDEO_INFO_HEIGHT (&state->info) != 0, FALSE);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  GST_DEBUG_OBJECT (decoder, "output_state par %d/%d fps %d/%d",
      state->info.par_n, state->info.par_d,
      state->info.fps_n, state->info.fps_d);

  if (state->caps == NULL) {
    state->caps = gst_video_info_to_caps (&state->info);
    if (state->info.finfo->format == GST_VIDEO_FORMAT_ENCODED) {
      gst_structure_set_name (gst_caps_get_structure (state->caps, 0),
          "video/x-amc");
    }
  }

  GST_DEBUG_OBJECT (decoder, "setting caps %" GST_PTR_FORMAT, state->caps);

  ret = gst_pad_set_caps (decoder->srcpad, state->caps);
  decoder->priv->output_state_changed = FALSE;

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return ret;
}

/**
 * gst_video_decoder_alloc_output_buffer:
 * @decoder: a #GstVideoDecoder
 *
 * Helper function that uses @gst_pad_alloc_buffer_and_set_caps()
 * to allocate a buffer to hold a video frame for @decoder's
 * current #GstVideoCodecState.
 *
 * Returns: (transfer full): allocated buffer
 *
 * Since: 0.10.37
 */
GstBuffer *
gst_video_decoder_alloc_output_buffer (GstVideoDecoder * decoder)
{
  GstBuffer *buffer = NULL;
  GstFlowReturn flow_ret;
  GstVideoCodecState *state = decoder->priv->output_state;
  int num_bytes = GST_VIDEO_INFO_SIZE (&state->info);

  GST_DEBUG ("alloc src buffer caps=%" GST_PTR_FORMAT,
      GST_PAD_CAPS (decoder->srcpad));

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);
  if (G_UNLIKELY (decoder->priv->output_state_changed))
    gst_video_decoder_set_src_caps (decoder);

  flow_ret =
      gst_pad_alloc_buffer_and_set_caps (decoder->srcpad,
      GST_BUFFER_OFFSET_NONE, num_bytes, GST_PAD_CAPS (decoder->srcpad),
      &buffer);

  GST_DEBUG ("alloc returned %d buffer %" GST_PTR_FORMAT, flow_ret, buffer);

  /* TODO need to check that the buffer size matches and properly attempt
   * a renegotiation if the videodecoder is able to handle it */
  if (flow_ret != GST_FLOW_OK || GST_BUFFER_SIZE (buffer) != num_bytes ||
      !gst_caps_is_equal_fixed (GST_PAD_CAPS (decoder->srcpad),
          GST_BUFFER_CAPS (buffer))) {
    buffer = gst_buffer_new_and_alloc (num_bytes);
    gst_buffer_set_caps (buffer, GST_PAD_CAPS (decoder->srcpad));
  }

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return buffer;
}

/**
 * gst_video_decoder_alloc_output_frame:
 * @decoder: a #GstVideoDecoder
 * @frame: a #GstVideoCodecFrame
 *
 * Helper function that uses @gst_pad_alloc_buffer_and_set_caps()
 * to allocate a buffer to hold a video frame for @decoder's
 * current #GstVideoCodecState.  Subclass should already have configured video state
 * and set src pad caps.
 *
 * Returns: result from pad alloc call
 *
 * Since: 0.10.37
 */
GstFlowReturn
gst_video_decoder_alloc_output_frame (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstVideoCodecState *state;
  int num_bytes;

  g_return_val_if_fail (frame->output_buffer == NULL, GST_FLOW_ERROR);

  GST_VIDEO_DECODER_STREAM_LOCK (decoder);

  if (G_UNLIKELY (decoder->priv->output_state_changed))
    gst_video_decoder_set_src_caps (decoder);

  g_return_val_if_fail (GST_PAD_CAPS (decoder->srcpad) != NULL, GST_FLOW_ERROR);

  state = decoder->priv->output_state;
  if (state == NULL) {
    GST_ERROR_OBJECT (decoder,
        "Output state should be set before allocating frame");
    goto error;
  }
  num_bytes = GST_VIDEO_INFO_SIZE (&state->info);
  if (num_bytes) {
    GST_LOG_OBJECT (decoder, "alloc buffer size %d", num_bytes);
    flow_ret =
        gst_pad_alloc_buffer_and_set_caps (decoder->srcpad,
        GST_BUFFER_OFFSET_NONE, num_bytes, GST_PAD_CAPS (decoder->srcpad),
        &frame->output_buffer);

    if (flow_ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (decoder, "failed to get buffer %s",
          gst_flow_get_name (flow_ret));
    }
  }

  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);

  return flow_ret;

error:
  GST_VIDEO_DECODER_STREAM_UNLOCK (decoder);
  return GST_FLOW_ERROR;
}

/**
 * gst_video_decoder_get_max_decode_time:
 * @decoder: a #GstVideoDecoder
 * @frame: a #GstVideoCodecFrame
 *
 * Determines maximum possible decoding time for @frame that will
 * allow it to decode and arrive in time (as determined by QoS events).
 * In particular, a negative result means decoding in time is no longer possible
 * and should therefore occur as soon/skippy as possible.
 *
 * Returns: max decoding time.
 *
 * Since: 0.10.37
 */
GstClockTimeDiff
gst_video_decoder_get_max_decode_time (GstVideoDecoder *
    decoder, GstVideoCodecFrame * frame)
{
  GstClockTimeDiff deadline;
  GstClockTime earliest_time;

  GST_OBJECT_LOCK (decoder);
  earliest_time = decoder->priv->earliest_time;
  if (GST_CLOCK_TIME_IS_VALID (earliest_time)
      && GST_CLOCK_TIME_IS_VALID (frame->deadline))
    deadline = GST_CLOCK_DIFF (earliest_time, frame->deadline);
  else
    deadline = G_MAXINT64;

  GST_LOG_OBJECT (decoder, "earliest %" GST_TIME_FORMAT
      ", frame deadline %" GST_TIME_FORMAT ", deadline %" GST_TIME_FORMAT,
      GST_TIME_ARGS (earliest_time), GST_TIME_ARGS (frame->deadline),
      GST_TIME_ARGS (deadline));

  GST_OBJECT_UNLOCK (decoder);

  return deadline;
}

/**
 * gst_video_decoder_get_qos_proportion:
 * @decoder: a #GstVideoDecoder
 *     current QoS proportion, or %NULL
 *
 * Returns: The current QoS proportion.
 *
 * Since: 1.0.3
 */
gdouble
gst_video_decoder_get_qos_proportion (GstVideoDecoder * decoder)
{
  gdouble proportion;

  g_return_val_if_fail (GST_IS_VIDEO_DECODER (decoder), 1.0);

  GST_OBJECT_LOCK (decoder);
  proportion = decoder->priv->proportion;
  GST_OBJECT_UNLOCK (decoder);

  return proportion;
}

GstFlowReturn
_gst_video_decoder_error (GstVideoDecoder * dec, gint weight,
    GQuark domain, gint code, gchar * txt, gchar * dbg, const gchar * file,
    const gchar * function, gint line)
{
  if (txt)
    GST_WARNING_OBJECT (dec, "error: %s", txt);
  if (dbg)
    GST_WARNING_OBJECT (dec, "error: %s", dbg);
  dec->priv->error_count += weight;
  dec->priv->discont = TRUE;
  if (dec->priv->max_errors < dec->priv->error_count) {
    gst_element_message_full (GST_ELEMENT (dec), GST_MESSAGE_ERROR,
        domain, code, txt, dbg, file, function, line);
    return GST_FLOW_ERROR;
  } else {
    g_free (txt);
    g_free (dbg);
    return GST_FLOW_OK;
  }
}

/**
 * gst_video_decoder_set_max_errors:
 * @dec: a #GstVideoDecoder
 * @num: max tolerated errors
 *
 * Sets numbers of tolerated decoder errors, where a tolerated one is then only
 * warned about, but more than tolerated will lead to fatal error.  Default
 * is set to GST_VIDEO_DECODER_MAX_ERRORS.
 *
 * Since: 0.10.37
 */
void
gst_video_decoder_set_max_errors (GstVideoDecoder * dec, gint num)
{
  g_return_if_fail (GST_IS_VIDEO_DECODER (dec));

  dec->priv->max_errors = num;
}

/**
 * gst_video_decoder_get_max_errors:
 * @dec: a #GstVideoDecoder
 *
 * Returns: currently configured decoder tolerated error count.
 *
 * Since: 0.10.37
 */
gint
gst_video_decoder_get_max_errors (GstVideoDecoder * dec)
{
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (dec), 0);

  return dec->priv->max_errors;
}

/**
 * gst_video_decoder_set_packetized:
 * @decoder: a #GstVideoDecoder
 * @packetized: whether the input data should be considered as packetized.
 *
 * Allows baseclass to consider input data as packetized or not. If the
 * input is packetized, then the @parse method will not be called.
 *
 * Since: 0.10.37
 */
void
gst_video_decoder_set_packetized (GstVideoDecoder * decoder,
    gboolean packetized)
{
  decoder->priv->packetized = packetized;
}

/**
 * gst_video_decoder_get_packetized:
 * @decoder: a #GstVideoDecoder
 *
 * Queries whether input data is considered packetized or not by the
 * base class.
 *
 * Returns: TRUE if input data is considered packetized.
 *
 * Since: 0.10.37
 */
gboolean
gst_video_decoder_get_packetized (GstVideoDecoder * decoder)
{
  return decoder->priv->packetized;
}

/**
 * gst_video_decoder_set_estimate_rate:
 * @dec: a #GstVideoDecoder
 * @enabled: whether to enable byte to time conversion
 *
 * Allows baseclass to perform byte to time estimated conversion.
 *
 * Since: 0.10.37
 */
void
gst_video_decoder_set_estimate_rate (GstVideoDecoder * dec, gboolean enabled)
{
  g_return_if_fail (GST_IS_VIDEO_DECODER (dec));

  dec->priv->do_estimate_rate = enabled;
}

/**
 * gst_video_decoder_get_estimate_rate:
 * @dec: a #GstVideoDecoder
 *
 * Returns: currently configured byte to time conversion setting
 *
 * Since: 0.10.37
 */
gboolean
gst_video_decoder_get_estimate_rate (GstVideoDecoder * dec)
{
  g_return_val_if_fail (GST_IS_VIDEO_DECODER (dec), 0);

  return dec->priv->do_estimate_rate;
}

/**
 * gst_video_decoder_set_latency:
 * @decoder: a #GstVideoDecoder
 * @min_latency: minimum latency
 * @max_latency: maximum latency
 *
 * Lets #GstVideoDecoder sub-classes tell the baseclass what the decoder
 * latency is. Will also post a LATENCY message on the bus so the pipeline
 * can reconfigure its global latency.
 *
 * Since: 0.10.37
 */
void
gst_video_decoder_set_latency (GstVideoDecoder * decoder,
    GstClockTime min_latency, GstClockTime max_latency)
{
  g_return_if_fail (GST_CLOCK_TIME_IS_VALID (min_latency));
  g_return_if_fail (max_latency >= min_latency);

  GST_OBJECT_LOCK (decoder);
  decoder->priv->min_latency = min_latency;
  decoder->priv->max_latency = max_latency;
  GST_OBJECT_UNLOCK (decoder);

  gst_element_post_message (GST_ELEMENT_CAST (decoder),
      gst_message_new_latency (GST_OBJECT_CAST (decoder)));
}

/**
 * gst_video_decoder_get_latency:
 * @decoder: a #GstVideoDecoder
 * @min_latency: (out) (allow-none): address of variable in which to store the
 *     configured minimum latency, or %NULL
 * @max_latency: (out) (allow-none): address of variable in which to store the
 *     configured mximum latency, or %NULL
 *
 * Query the configured decoder latency. Results will be returned via
 * @min_latency and @max_latency.
 *
 * Since: 0.10.37
 */
void
gst_video_decoder_get_latency (GstVideoDecoder * decoder,
    GstClockTime * min_latency, GstClockTime * max_latency)
{
  GST_OBJECT_LOCK (decoder);
  if (min_latency)
    *min_latency = decoder->priv->min_latency;
  if (max_latency)
    *max_latency = decoder->priv->max_latency;
  GST_OBJECT_UNLOCK (decoder);
}
