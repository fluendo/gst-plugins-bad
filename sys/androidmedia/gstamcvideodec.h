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

#ifndef __GST_AMC_VIDEO_DEC_H__
#define __GST_AMC_VIDEO_DEC_H__

#include <gst/gst.h>
#include <gst/androidjni/gstjnisurface.h>

#include "video/gstvideodecoder.h"

#include "gstamc.h"

G_BEGIN_DECLS
/* Switch between expected sink: amcvideosink or eglglessink */
#define USE_AMCVIDEOSINK 1
#define GST_TYPE_AMC_VIDEO_DEC \
  (gst_amc_video_dec_get_type())
#define GST_AMC_VIDEO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMC_VIDEO_DEC,GstAmcVideoDec))
#define GST_AMC_VIDEO_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMC_VIDEO_DEC,GstAmcVideoDecClass))
#define GST_AMC_VIDEO_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMC_VIDEO_DEC,GstAmcVideoDecClass))
#define GST_IS_AMC_VIDEO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMC_VIDEO_DEC))
#define GST_IS_AMC_VIDEO_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMC_VIDEO_DEC))
typedef struct _GstAmcVideoDec GstAmcVideoDec;
typedef struct _GstAmcVideoDecClass GstAmcVideoDecClass;

struct _GstAmcVideoDec
{
  GstVideoDecoder parent;

  /* Properties */
  gint audio_session_id;

  /* < private > */
  GstAmcCodec *codec;
  GstAmcBuffer *input_buffers, *output_buffers;
  gsize n_input_buffers, n_output_buffers;

  GstVideoCodecState *input_state;
  gboolean input_state_changed;

  /* Output format of the codec */
  GstVideoFormat format;
  gint color_format;
  gint width, height, stride, slice_height;
  gint crop_left, crop_right;
  gint crop_top, crop_bottom;

  GstBuffer *codec_data;
  /* TRUE if the component is configured and saw
   * the first buffer */
  gboolean started;
  /* TRUE if the component has configured the
   * output format
   */
  gboolean output_configured;

  GstClockTime last_upstream_ts;

  /* Draining state */
  GMutex *drain_lock;
  GCond *drain_cond;
  gboolean drain_cond_signalling;

  /* TRUE if upstream is EOS */
  gboolean eos;

#if USE_AMCVIDEOSINK
  guint8 *surface;
#else
  GstJniSurface *surface;
#endif

  GstFlowReturn downstream_flow_ret;
  gboolean stop_loop;
  GstAmcCrypto *drm_ctx;
  gboolean inband_drm_enabled;
  gboolean srcpad_loop_started;
  gint cached_input_buffer;

  GstCaps *x_amc_empty_caps;
};

struct _GstAmcVideoDecClass
{
  GstVideoDecoderClass parent_class;

  const GstAmcRegisteredCodec *registered_codec;

  gboolean direct_rendering;
};

GType gst_amc_video_dec_get_type (void);

/* Allows types derived from AMCVideoDec to have specific Android feature props*/
void gst_amc_video_dec_dynamic_class_init (gpointer klass, gpointer class_data);

G_END_DECLS
#endif /* __GST_AMC_VIDEO_DEC_H__ */
