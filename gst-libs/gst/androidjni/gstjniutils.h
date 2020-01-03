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
#ifndef __GST_JNI_UTILS_H__
#define __GST_JNI_UTILS_H__

#include <jni.h>
#include <glib.h>
#include <gst/gst.h>

jclass gst_jni_get_class (JNIEnv * env, const gchar * name);

jmethodID gst_jni_get_method (JNIEnv * env, jclass klass,
    const gchar * name, const gchar * signature);

jmethodID gst_jni_get_static_method (JNIEnv * env, jclass klass,
    const gchar * name, const gchar * signature);

jfieldID gst_jni_get_field_id (JNIEnv * env, jclass klass,
    const gchar * name, const gchar * type);

jobject gst_jni_new_object (JNIEnv * env, jclass klass,
    jmethodID constructor, ...);

jobject gst_jni_new_object_from_static (JNIEnv * env, jclass klass,
    jmethodID constructor, ...);

jobject gst_jni_object_make_global (JNIEnv * env, jobject object);

jobject gst_jni_object_ref (JNIEnv * env, jobject object);

void gst_jni_object_unref (JNIEnv * env, jobject object);

void gst_jni_object_local_unref (JNIEnv * env, jobject object);

gchar *gst_jni_string_to_gchar (JNIEnv * env, jstring string, gboolean release);

jstring gst_jni_string_from_gchar (JNIEnv * env, const gchar * string);

gboolean gst_jni_initialize (JavaVM * java_vm);

gboolean gst_jni_is_vm_started (void);

JNIEnv *gst_jni_get_env (void);

#define DEF_CALL_TYPE_METHOD(_type, _name,  _jname, _retval)                                 \
_type gst_jni_call_##_name##_method (JNIEnv *env, jobject obj, jmethodID methodID, ...); \

DEF_CALL_TYPE_METHOD (gboolean, boolean, Boolean, FALSE)
DEF_CALL_TYPE_METHOD (gint8, byte, Byte, G_MININT8)
DEF_CALL_TYPE_METHOD (gshort, short, Short, G_MINSHORT)
DEF_CALL_TYPE_METHOD (gint, int, Int, G_MININT)
DEF_CALL_TYPE_METHOD (gchar, char, Char, 0)
DEF_CALL_TYPE_METHOD (glong, long, Long, G_MINLONG)
DEF_CALL_TYPE_METHOD (gfloat, float, Float, G_MINFLOAT)
DEF_CALL_TYPE_METHOD (gdouble, double, Double, G_MINDOUBLE)
DEF_CALL_TYPE_METHOD (jobject, object, Object, NULL)
gboolean gst_jni_call_void_method (JNIEnv * env, jclass klass,
    jmethodID method, ...);

#define DEF_GET_TYPE_FIELD(_type, _name, _jname, _retval)                           \
_type gst_jni_get_##_name##_field (JNIEnv *env, jobject obj, jfieldID fieldID); \

DEF_GET_TYPE_FIELD (gint, int, Int, G_MININT)
DEF_GET_TYPE_FIELD (glong, long, Long, G_MINLONG)


/* Macros have next rules:
   J_CALL_<TYPE> (), J_CALL_STATIC_<TYPE> () - first parameter is a variable
   to write to, if it's not J_CALL_VOID () or J_CALL_STATIC_VOID ().
   If exception occured, it jumps to "error" label, and the variable
   is kept untouched.
 */

/* =================== This macros are for use in the code:  */

#define J_CALL_STATIC_OBJ(ret, ...) J_CALL_RET_STATIC\
(jobject, ret, CallStaticObjectMethod, ##__VA_ARGS__)

#define J_CALL_STATIC_BOOLEAN(ret, ...) J_CALL_RET_STATIC\
(jboolean, ret, CallStaticBooleanMethod, ##__VA_ARGS__)

#define J_CALL_VOID(...) do { J_CALL (CallVoidMethod, __VA_ARGS__); } while(0)
#define J_CALL_INT(ret, ...) J_CALL_RET (jint, ret, CallIntMethod, __VA_ARGS__)
#define J_CALL_OBJ(ret, ...) J_CALL_RET (jobject, ret, CallObjectMethod, __VA_ARGS__)
#define J_CALL_BOOL(ret, ...) J_CALL_RET (jboolean, ret, CallBooleanMethod, __VA_ARGS__)
#define J_CALL_FLOAT(ret, ...) J_CALL_RET (jfloat, ret, CallFloatMethod, __VA_ARGS__)

#define AMC_CHK(statement) G_STMT_START {               \
    if (G_UNLIKELY(!(statement))) {                     \
      GST_ERROR ("check for ("#statement ") failed");   \
      J_EXCEPTION_CHECK ("(none)");                     \
      goto error;                                       \
    }                                                   \
  } G_STMT_END


#define J_EXCEPTION_CHECK(method) G_STMT_START {        \
    if (G_UNLIKELY((*env)->ExceptionCheck (env))) {     \
      GST_ERROR ("Caught exception on call " method);   \
      (*env)->ExceptionDescribe (env);                  \
      (*env)->ExceptionClear (env);                     \
      goto error;                                       \
    }                                                   \
  } G_STMT_END

#define J_DELETE_LOCAL_REF(ref) J_DELETE_REF (DeleteLocalRef, ref)
#define J_DELETE_GLOBAL_REF(ref) J_DELETE_REF (DeleteGlobalRef, ref)

#define J_INIT_METHOD_ID(class, method, name, desc)             \
  J_INIT_METHOD_ID_GEN(GetMethodID, class, method, name, desc)

#define J_INIT_STATIC_METHOD_ID(class, method, name, desc)              \
  J_INIT_METHOD_ID_GEN(GetStaticMethodID, class, method, name, desc)


/* ======================= This macros are used to generate other macros */

#define J_CALL_STATIC(envfunc, class, method, ...)                      \
  (*env)->envfunc(env, class.klass, class.method, ##__VA_ARGS__);       \
  J_EXCEPTION_CHECK (#class "." #method)

#define J_CALL(envfunc, obj, method, ...)               \
  (*env)->envfunc (env, obj, method, ##__VA_ARGS__);    \
  J_EXCEPTION_CHECK (#obj "->" #method)

/* Here we have a hack to set the return value only in non-error cases,
   this avoids unnecessary jumps/sets after error label
 */
#define J_CALL_RET(rettype, ret, ...)                   \
  G_STMT_START {                                        \
    rettype jmacro_tmp = J_CALL (__VA_ARGS__);        \
    ret = jmacro_tmp;                                   \
  } G_STMT_END

#define J_CALL_RET_STATIC(rettype, ret, ...)            \
  G_STMT_START {                                        \
    rettype jmacro_tmp = J_CALL_STATIC (__VA_ARGS__); \
    ret = jmacro_tmp;                                   \
  } G_STMT_END

#define J_DELETE_REF(delfunc, ref) G_STMT_START {        \
    if (G_LIKELY(ref)) {                        \
      (*env)->delfunc (env, ref);               \
      ref = NULL;                               \
    }                                           \
  } G_STMT_END

#define J_INIT_METHOD_ID_GEN(calltype, class, method, name, desc)       \
  G_STMT_START {                                                        \
    class.method = (*env)->calltype (env, class.klass, name, desc);     \
    AMC_CHK (class.method);                                             \
  } G_STMT_END

#endif