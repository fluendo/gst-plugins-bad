/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@entropywave.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstavcsrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{

  gst_element_register (plugin, "avcsrc", GST_RANK_NONE,
      gst_avc_src_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE2 (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    avcsrc,
    "AVC Video Services plugin",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
