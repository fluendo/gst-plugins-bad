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

#include "gstjnibufferinfo.h"
#include "gstjniutils.h"

G_DEFINE_TYPE (GstJniJbufferInfo, gst_jni_jbuffer_info, G_TYPE_OBJECT);

static gpointer parent_class = NULL;
static void gst_jni_jbuffer_info_dispose (GObject * object);

static gboolean
_cache_java_class (GstJniJbufferInfoClass * klass)
{
  JNIEnv *env;

  gst_jni_initialize ();

  env = gst_jni_get_env ();

  klass->jklass = gst_jni_get_class (env, "android/media/MediaCodec$BufferInfo");
  if (!klass->jklass)
    return FALSE;

  klass->constructor = gst_jni_get_method (env, klass->jklass, "<init>", "()V");
  klass->size = gst_jni_get_field_id (env, klass->jklass, "size", "I");
  klass->offset = gst_jni_get_field_id (env, klass->jklass, "offset", "I");
  klass->pts =
      gst_jni_get_field_id (env, klass->jklass, "presentationTimeUs", "J");
  klass->flags = gst_jni_get_field_id (env, klass->jklass, "flags", "I");

  if (!klass->constructor ||
      !klass->size || !klass->offset || !klass->pts || !klass->flags) {
    return FALSE;
  }
  return TRUE;
}


static void
gst_jni_jbuffer_info_class_init (GstJniJbufferInfoClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->dispose = gst_jni_jbuffer_info_dispose;

  klass->java_cached = _cache_java_class (klass);
  if (!klass->java_cached) {
    g_critical ("Could not cache java class android/Media/BufferInfo");
  }
}

static void
gst_jni_jbuffer_info_init (GstJniJbufferInfo * self)
{
  /* initialize the object */
}

static void
gst_jni_jbuffer_info_dispose (GObject * object)
{
  GstJniJbufferInfo *self;

  self = GST_JNI_JBUFFER_INFO (object);

  if (self->jobject) {
    JNIEnv *env;
    env = gst_jni_get_env ();

    gst_jni_release_object (env, self->jobject);
    self->jobject = NULL;
  }
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

GstJniJbufferInfo *
gst_jni_jbuffer_info_new (void)
{
  JNIEnv *env;
  GstJniJbufferInfo *buffer_info;
  GstJniJbufferInfoClass *klass;

  buffer_info = g_object_new (GST_TYPE_JNI_JBUFFER_INFO, NULL);
  klass = GST_JNI_JBUFFER_INFO_GET_CLASS (buffer_info);
  if (!klass->java_cached)
    return NULL;

  env = gst_jni_get_env ();

  buffer_info->jobject = gst_jni_new_object (env, klass->jklass,
      klass->constructor);
  if (buffer_info->jobject == NULL) {
    g_object_unref (buffer_info);
    return NULL;
  }

  return buffer_info;
}

gint
gst_jni_jbuffer_info_get_size (GstJniJbufferInfo * self)
{
  JNIEnv *env;
  GstJniJbufferInfoClass *klass;

  g_return_val_if_fail (self != NULL, G_MININT);

  env = gst_jni_get_env ();
  klass = GST_JNI_JBUFFER_INFO_GET_CLASS (self);

  return gst_jni_get_int_field (env, self->jobject, klass->size);
}

gint
gst_jni_jbuffer_info_get_offset (GstJniJbufferInfo * self)
{
  JNIEnv *env;
  GstJniJbufferInfoClass *klass;

  g_return_val_if_fail (self != NULL, G_MININT);

  env = gst_jni_get_env ();
  klass = GST_JNI_JBUFFER_INFO_GET_CLASS (self);

  return gst_jni_get_int_field (env, self->jobject, klass->offset);
}

glong
gst_jni_jbuffer_info_get_pts (GstJniJbufferInfo * self)
{
  JNIEnv *env;
  GstJniJbufferInfoClass *klass;

  g_return_val_if_fail (self != NULL, G_MININT);

  env = gst_jni_get_env ();
  klass = GST_JNI_JBUFFER_INFO_GET_CLASS (self);

  return gst_jni_get_long_field (env, self->jobject, klass->pts);
}

gint
gst_jni_jbuffer_info_get_flags (GstJniJbufferInfo * self)
{
  JNIEnv *env;
  GstJniJbufferInfoClass *klass;

  g_return_val_if_fail (self != NULL, G_MININT);

  env = gst_jni_get_env ();
  klass = GST_JNI_JBUFFER_INFO_GET_CLASS (self);

  return gst_jni_get_int_field (env, self->jobject, klass->flags);
}

GstJniBufferInfo *
gst_jni_buffer_info_new (void)
{
  GstJniBufferInfo *info;

  info = g_slice_new (GstJniBufferInfo);

  return info;
}

void
gst_jni_buffer_info_free (GstJniBufferInfo *buffer_info)
{
  g_slice_free (GstJniBufferInfo, buffer_info);
}

void
gst_jni_buffer_info_fill (GstJniBufferInfo *buffer_info,
    GstJniJbufferInfo *jbuffer_info)
{
  g_return_if_fail (buffer_info != NULL);
  g_return_if_fail (jbuffer_info != NULL);

  buffer_info->size = gst_jni_jbuffer_info_get_size (jbuffer_info);
  buffer_info->offset = gst_jni_jbuffer_info_get_offset (jbuffer_info);
  buffer_info->flags = gst_jni_jbuffer_info_get_flags (jbuffer_info);
  buffer_info->pts = gst_jni_jbuffer_info_get_pts (jbuffer_info);
};
