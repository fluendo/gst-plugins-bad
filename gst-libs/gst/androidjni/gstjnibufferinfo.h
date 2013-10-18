/*
 * Copyright (C) 2013, Fluendo S.A.
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

#ifndef __GST_JNI_JBUFFER_INFO_H__
#define __GST_JNI_JBUFFER_INFO_H__

#include <glib-object.h>
#include <jni.h>

G_BEGIN_DECLS
#define GST_TYPE_JNI_JBUFFER_INFO                  (gst_jni_jbuffer_info_get_type ())
#define GST_JNI_JBUFFER_INFO(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_JNI_JBUFFER_INFO, GstJniJbufferInfo))
#define GST_IS_JNI_JBUFFER_INFO(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_JNI_JBUFFER_INFO))
#define GST_JNI_JBUFFER_INFO_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_JNI_JBUFFER_INFO, GstJniJbufferInfoClass))
#define GST_IS_JNI_JBUFFER_INFO_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_JNI_JBUFFER_INFO))
#define GST_JNI_JBUFFER_INFO_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_JNI_JBUFFER_INFO, GstJniJbufferInfoClass))
typedef struct _GstJniJbufferInfo GstJniJbufferInfo;
typedef struct _GstJniJbufferInfoClass GstJniJbufferInfoClass;
typedef struct _GstJniBufferInfo GstJniBufferInfo;

struct _GstJniBufferInfo
{
  gint size;
  gint offset;
  gint flags;
  guint64 pts;
};

struct _GstJniJbufferInfo
{
  GObject parent_instance;

  /* instance members */
  jobject jobject;
};

struct _GstJniJbufferInfoClass
{
  GObjectClass parent_class;

  /* class members */
  gboolean java_cached;
  jclass jklass;
  jmethodID constructor;
  jfieldID flags;
  jfieldID offset;
  jfieldID pts;
  jfieldID size;
};

GType gst_jni_jbuffer_info_get_type (void);

GstJniJbufferInfo *gst_jni_jbuffer_info_new (void);

gint gst_jni_jbuffer_info_get_size (GstJniJbufferInfo * buffer_info);

gint gst_jni_jbuffer_info_get_offset (GstJniJbufferInfo * buffer_info);

glong gst_jni_jbuffer_info_get_pts (GstJniJbufferInfo * buffer_info);

gint gst_jni_jbuffer_info_get_flags (GstJniJbufferInfo * buffer_info);

GstJniBufferInfo * gst_jni_buffer_info_new (void);

void gst_jni_buffer_info_free (GstJniBufferInfo *buffer_info);

void gst_jni_buffer_info_fill (GstJniBufferInfo *buffer_info, GstJniJbufferInfo *jbuffer_info);

G_END_DECLS
#endif
