/*
 * DRM Context class for GStreamer AMC decoders
 * Copyright (C) 2020 Fluendo S.A.
 *   @author: Aleksandr Slobodeniuk <aslobodeniuk@fluendo.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#ifndef __GST_AMC_DRM_H__
#define __GST_AMC_DRM_H__

#include <jni.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/* Opaque class */
typedef struct _GstAmcCrypto GstAmcCrypto;

/* Log supported drm schemes */
void gst_amc_drm_log_known_supported_protection_schemes (void);

/* Convert drm metadata in the buffer to CryptoInfo object */
jobject gst_amc_drm_get_crypto_info (const GstBuffer * drmbuf);

/* Create Drm context */
GstAmcCrypto *gst_amc_drm_ctx_new (GstElement * element);

/* Free Drm context */
void gst_amc_drm_ctx_free (GstAmcCrypto * crypto_ctx);

/* Proccess drm event */
void gst_amc_drm_handle_drm_event (GstAmcCrypto * ctx, GstEvent * event);

/* get/set MCrypto object */
jobject gst_amc_drm_mcrypto_get (GstAmcCrypto * ctx);
gboolean gst_amc_drm_mcrypto_set (GstAmcCrypto * ctx, gpointer mcrypto);

/* Check GstEvent if it's a "drm event" */
gboolean gst_amc_drm_is_drm_event (GstEvent *event);

/* Check for exception occured, and verbose it if it's a CryptoException */
gboolean
gst_amc_drm_crypto_exception_check (JNIEnv * env, const gchar * call);

/* Init related Java classes */
gboolean gst_amc_drm_jni_init (JNIEnv * env);

G_END_DECLS
#endif /* __GST_AMC_DRM_H__ */
