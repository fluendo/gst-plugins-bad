/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oravnas@cisco.com>
 * Copyright (C) 2018 Fluendo SA <ngarcia@fluendo.com>
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

#ifndef __GST_OSX_SCREEN_CAPTURE_H__
#define __GST_OSX_SCREEN_CAPTURE_H__

#import <AVFoundation/AVFoundation.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS
#define GST_TYPE_OSX_SCREEN_CAP_SRC \
  (gst_osx_screen_cap_src_get_type ())
#define GST_OSX_SCREEN_CAPTURE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_OSX_SCREEN_CAP_SRC, \
  GstOSXScreenCapSrc))
#define GST_OSX_SCREEN_CAPTURE_CAST(obj) \
  ((GstOSXScreenCapSrc *) (obj))
#define GST_OSX_SCREEN_CAPTURE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_OSX_SCREEN_CAP_SRC, \
  GstOSXScreenCapSrcClass))
#define GST_OUTPUT_BUFFER(obj) \
  ((GstOutputBuffer *) GST_OSX_SCREEN_CAPTURE_CAST (obj)->outputBuffer)
#define GST_IS_OSX_SCREEN_CAPTURE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OSX_SCREEN_CAP_SRC))

typedef struct _GstOSXScreenCapSrc GstOSXScreenCapSrc;
typedef struct _GstOSXScreenCapSrcClass GstOSXScreenCapSrcClass;

#define MAX_DISPLAYS          16

struct _GstOSXScreenCapSrc
{
  GstPushSrc push_src;
  CGDirectDisplayID displayId;
  gchar * displayName;
  BOOL doStats;
  BOOL captureScreenCursor;
  BOOL captureScreenMouseClicks;
  gint fps;
  CGDisplayCount m_displayCount;
  CGDirectDisplayID m_dispArray[MAX_DISPLAYS];
  NSString *m_dispArrayNames[MAX_DISPLAYS];
  gpointer outputBuffer;
};

struct _GstOSXScreenCapSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_osx_screen_cap_src_get_type (void);

G_END_DECLS
#endif /* __GST_OSX_SCREEN_CAPTURE_H__ */
