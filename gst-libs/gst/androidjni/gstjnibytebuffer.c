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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstjnibytebuffer.h"
#include "gstjniutils.h"

G_DEFINE_TYPE (GstJniByteBuffer, gst_jni_byte_buffer, G_TYPE_OBJECT);

static gpointer parent_class = NULL;
static void gst_jni_byte_buffer_dispose (GObject * object);

static void
gst_jni_byte_buffer_class_init (GstJniByteBufferClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->dispose = gst_jni_byte_buffer_dispose;
}

static void
gst_jni_byte_buffer_init (GstJniByteBuffer * self)
{
  /* initialize the object */
  self->data = NULL;
  self->size = 0;
  self->jobject = NULL;
}

static void
gst_jni_byte_buffer_dispose (GObject * object)
{
  GstJniByteBuffer *self;

  self = GST_JNI_BYTE_BUFFER (object);

  if (self->jobject) {
    JNIEnv *env;
    env = gst_jni_get_env ();

    gst_jni_release_object (env, self->jobject);
    self->jobject = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

GstJniByteBuffer *
gst_jni_byte_buffer_new (jobject object)
{
  JNIEnv *env;
  GstJniByteBuffer *byte_buffer;

  g_return_val_if_fail (object != NULL, NULL);

  byte_buffer = g_object_new (GST_TYPE_JNI_BYTE_BUFFER, NULL);

  env = gst_jni_get_env ();

  byte_buffer->jobject = gst_jni_object_make_global (env, object);
  if (!byte_buffer->jobject)
    goto error;

  byte_buffer->data = (*env)->GetDirectBufferAddress (env, byte_buffer->jobject);
  if (!byte_buffer->data) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer address");
    goto error;
  }
  byte_buffer->size = (*env)->GetDirectBufferCapacity (env, byte_buffer->jobject);

done:
  return byte_buffer;

error:
  g_object_unref (byte_buffer);
  byte_buffer = NULL;
  goto done;
}

guint
gst_jni_byte_buffer_get_size (GstJniByteBuffer * self)
{
  g_return_val_if_fail (self != NULL, G_MININT);

  return self->size;
}

guint8 *
gst_jni_byte_buffer_get_data (GstJniByteBuffer * self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->data;
}
