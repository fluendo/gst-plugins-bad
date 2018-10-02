/*
 * Copyright (C) 2018, Fluendo S.A
 *   Author: Aleksandr Slobodeniuk <alenuke@yandex.ru>
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

#ifndef __GST_AMC_MACRO_H__
#define __GST_AMC_MACRO_H__
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

#endif /* __GST_AMC_MACRO_H__ */
