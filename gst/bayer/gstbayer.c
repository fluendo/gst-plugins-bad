/* GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>


GType gst_bayer2rgb_get_type (void);
GType gst_rgb2bayer_get_type (void);

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "bayer2rgb", GST_RANK_NONE,
      gst_bayer2rgb_get_type ());
  gst_element_register (plugin, "rgb2bayer", GST_RANK_NONE,
      gst_rgb2bayer_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE2 (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    bayer,
    "Elements to convert Bayer images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
