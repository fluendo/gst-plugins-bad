/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_EGLGLESSINK_H__
#define __GST_EGLGLESSINK_H__

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/base/gstdataqueue.h>
#ifdef HAVE_ANDROID_MEDIA
#include <gst/androidjni/gstjnisurfacetexture.h>
#endif

#include "gstegladaptation.h"

G_BEGIN_DECLS
#define GST_TYPE_EGLGLESSINK \
  (gst_eglglessink_get_type())
#define GST_EGLGLESSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EGLGLESSINK,GstEglGlesSink))
#define GST_EGLGLESSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EGLGLESSINK,GstEglGlesSinkClass))
#define GST_IS_EGLGLESSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EGLGLESSINK))
#define GST_IS_EGLGLESSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EGLGLESSINK))
typedef struct _GstEglGlesSink GstEglGlesSink;
typedef struct _GstEglGlesSinkClass GstEglGlesSinkClass;

/*
 * GstEglGlesSink:
 * @par_n: Incoming frame's aspect ratio numerator
 * @par_d: Incoming frame's aspect ratio denominator
 * @format: Caps' video format field
 * @display_region: Surface region to use as rendering canvas
 * @sinkcaps: Full set of suported caps
 * @current_caps: Current caps
 * @rendering_path: Rendering path (Slow/Fast)
 * @flow_lock: Simple concurrent access ward to the sink's runtime state
 * @have_window: Set if the sink has access to a window to hold it's canvas
 * @window_changed: Tracks window changed to upload a new texture in expose
 * @using_own_window: Set if the sink created its own window
 * @egl_started: Set if the whole EGL setup has been performed
 * @create_window: Property value holder to allow/forbid internal window creation
 * @force_rendering_slow: Property value holder to force slow rendering path
 * @force_aspect_ratio: Property value holder to consider PAR/DAR when scaling
 *
 * The #GstEglGlesSink data structure.
 */
struct _GstEglGlesSink
{
  GstVideoSink videosink;       /* Element hook */
  int par_n, par_d;             /* Aspect ratio from caps */

  GstVideoFormat format;

  /* Region of the surface that should be rendered */
  GstVideoRectangle render_region;
  gboolean render_region_changed;
  gboolean render_region_user;

  gint64 clocks_diff;

  /* orientation handling */
  gint rotation;
  GLfloat rotation_matrix[16];

  /* Region of render_region that should be filled
   * with the video frames */
  GstVideoRectangle display_region;

  gboolean size_changed;
  GstCaps *sinkcaps;
  GstCaps *current_caps, *configured_caps;
  gboolean context_changed;

  GstEglAdaptationContext *egl_context;

  /* Runtime flags */
  gboolean have_window;
  gboolean window_changed;
  gboolean using_own_window;
  gboolean egl_started;

  gpointer own_window_data;

  GThread *thread;
  gboolean thread_running;
  GstDataQueue *queue;
  GCond *render_cond;
  GMutex *render_lock;
  GstFlowReturn last_flow;

  /* Properties */
  gboolean create_window;
  gboolean force_aspect_ratio;

  gint64 render_start;

#ifdef HAVE_ANDROID_MEDIA
  GstJniSurfaceTexture *surface_texture;
#endif

  /* Needed for requesting a window while playing */
  GRecMutex window_lock;
};

struct _GstEglGlesSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_eglglessink_get_type (void);

G_END_DECLS
#endif /* __GST_EGLGLESSINK_H__ */
