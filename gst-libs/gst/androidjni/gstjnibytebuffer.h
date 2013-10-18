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

#ifndef __GST_JNI_BYTE_BUFFER_H__
#define __GST_JNI_BYTE_BUFFER_H__

#include <glib-object.h>
#include <jni.h>

G_BEGIN_DECLS
#define GST_TYPE_JNI_BYTE_BUFFER                  (gst_jni_byte_buffer_get_type ())
#define GST_JNI_BYTE_BUFFER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_JNI_BYTE_BUFFER, GstJniByteBuffer))
#define GST_IS_JNI_BYTE_BUFFER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_JNI_BYTE_BUFFER))
#define GST_JNI_BYTE_BUFFER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_JNI_BYTE_BUFFER, GstJniByteBufferClass))
#define GST_IS_JNI_BYTE_BUFFER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_JNI_BYTE_BUFFER))
#define GST_JNI_BYTE_BUFFER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_JNI_BYTE_BUFFER, GstJniByteBufferClass))
typedef struct _GstJniByteBuffer GstJniByteBuffer;
typedef struct _GstJniByteBufferClass GstJniByteBufferClass;

struct _GstJniByteBuffer
{
  GObject parent_instance;

  /* instance members */
  guint8 *data;
  gsize size;
  jobject jobject;
};

struct _GstJniByteBufferClass
{
  GObjectClass parent_class;
};

GType gst_jni_byte_buffer_get_type (void);

GstJniByteBuffer *gst_jni_byte_buffer_new (jobject object);

gsize gst_jni_byte_buffer_get_size (GstJniByteBuffer * byte_buffer);

guint8 *gst_jni_byte_buffer_get_data (GstJniByteBuffer * byte_buffer);

G_END_DECLS
#endif
