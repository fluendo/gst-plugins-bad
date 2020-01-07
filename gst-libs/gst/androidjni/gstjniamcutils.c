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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/androidjni/gstjniutils.h>
#include <gst/androidjni/gstjnimediaformat.h>
#include <gst/androidjni/gstjnimediacodeclist.h>
#include "gstjniamcutils.h"

const gchar *
gst_jni_amc_video_caps_to_mime (GstCaps * caps)
{
  GstStructure *s;
  const gchar *name;

  s = gst_caps_get_structure (caps, 0);
  if (!s)
    return NULL;

  name = gst_structure_get_name (s);

  if (strcmp (name, "video/mpeg") == 0) {
    gint mpegversion;

    if (!gst_structure_get_int (s, "mpegversion", &mpegversion))
      return NULL;

    if (mpegversion == 4)
      return "video/mp4v-es";
    else if (mpegversion == 1 || mpegversion == 2)
      return "video/mpeg2";
  } else if (strcmp (name, "video/x-h263") == 0) {
    return "video/3gpp";
  } else if (strcmp (name, "video/x-h264") == 0) {
    return "video/avc";
  } else if (strcmp (name, "video/x-h265") == 0) {
    return "video/hevc";
  } else if (strcmp (name, "video/x-vp8") == 0) {
    return "video/x-vnd.on2.vp8";
  } else if (strcmp (name, "video/x-divx") == 0) {
    return "video/mp4v-es";
  } else if (strcmp (name, "video/x-xvid") == 0) {
    return "video/mp4v-es";
  } else if (strcmp (name, "video/x-3ivx") == 0) {
    return "video/mp4v-es";
  }

  return NULL;
}

gchar *
gst_jni_amc_get_tunneled_playback_decoder_name (GstCaps * caps, gint width,
    gint height)
{
  GstAmcFormat *format = NULL;
  GstJniMediaCodecList *codec_list = NULL;
  gchar *codec_name = NULL;

  format = gst_amc_format_new_video (gst_jni_amc_video_caps_to_mime (caps),
      width, height);
  if (format == NULL) {
    GST_ERROR ("Could not create format");
    goto done;
  }
  gst_amc_format_set_feature_enabled (format,
      GST_AMC_MEDIA_FORMAT_TUNNELED_PLAYBACK, TRUE);

  codec_list = gst_jni_media_codec_list_new ();
  if (codec_list == NULL) {
    GST_ERROR ("Could not get codec list");
    goto done;
  }
  codec_name =
      gst_jni_media_codec_list_find_decoder_for_format (codec_list, format);

done:
  if (format != NULL) {
    gst_amc_format_free (format);
  }
  if (codec_list != NULL) {
    gst_jni_media_codec_list_free (codec_list);
  }
  return codec_name;
}
