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

#ifndef __GST_JNI_AMC_UTILS_H__
#define __GST_JNI_AMC_UTILS_H__

#include <glib-object.h>
#include <jni.h>
#include "gstjniaudiotrack.h"

G_BEGIN_DECLS const gchar *gst_jni_amc_video_caps_to_mime (GstCaps * caps);

gchar * gst_jni_amc_get_tunneled_playback_decoder_name (GstCaps * caps, gint width,
    gint height);

G_END_DECLS
#endif /* __GST_JNI_AUDIO_TRACK_H__ */
