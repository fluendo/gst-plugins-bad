/*
 * DRM Context class for GStreamer AMC decoders
 * Copyright (C) 2020 Fluendo S.A.
 *   @author: Aleksandr Slobodeniuk <aslobodeniuk@fluendo.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamc.h"
#include <fluc/drm/flucdrm.h>
#include <curl/curl.h>
#include <gst/androidjni/gstjniutils.h>

GST_DEBUG_CATEGORY_EXTERN (flucdrm_debug);
#define GST_CAT_DEFAULT flucdrm_debug

typedef struct _GstAmcCrypto
{
  jobject mcrypto;
  jobject mdrm;
  jbyteArray mdrm_session_id;

  GstElement *gstelement;
  guint32 last_drm_event_hash;

  gboolean inband_drm_enabled;
  GPtrArray *playready_kids;

  GList *drm_events_pack;
  gboolean drm_reconfigured;
} GstAmcCrypto;

/* Taken from https://dashif.org/identifiers/content_protection/ */
static struct
{
  const char *uuid;
  const char *name;
  gboolean supported;
} known_cryptos[] = {
  {
    /* clearkey should be at [0], it's used in sysid_is_clearkey */
  "1077efec-c0b2-4d02-ace3-3c1e52e2fb4b", "CLEARKEY"}, {
    /* playready should be at [1], it's used in sysid_is_playready */
  "9a04f079-9840-4286-ab92-e65be0885f95", "PLAYREADY"}, {
  "5E629AF5-38DA-4063-8977-97FFBD9902D4", "MARLIN"}, {
  "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed", "WIDEVINE"}
};

static gboolean cached_supported_system_ids = FALSE;

/* JNI classes */
static struct
{
  jclass klass;
  jmethodID get_error_code;
} crypto_exception;

static struct
{
  jclass klass;
  jmethodID constructor;
  jmethodID set;
} media_codec_crypto_info;

static struct
{
  jclass klass;
  jmethodID constructor;
  jmethodID open_session;
  jmethodID get_key_request;
  jmethodID provide_key_response;
  jmethodID close_session;
  jmethodID get_security_level;
} media_drm;

static struct
{
  jclass klass;
  jmethodID get_default_url;
  jmethodID get_data;
} media_drm_key_request;

static struct
{
  jclass klass;
  jmethodID constructor;
  jmethodID is_crypto_scheme_supported;
  jmethodID set_media_drm_session;
} media_crypto;


static gboolean
gst_amc_drm_sysid_is_clearkey (const gchar * sysid)
{
  return !g_ascii_strcasecmp (sysid, known_cryptos[0].uuid);
}

static gboolean
gst_amc_drm_sysid_is_playready (const gchar * sysid)
{
  return !g_ascii_strcasecmp (sysid, known_cryptos[1].uuid);
}

gboolean
gst_amc_drm_jni_init (JNIEnv * env)
{
  gboolean ret = FALSE;

  /* MediaDrm */
  media_drm.klass = gst_jni_get_class (env, "android/media/MediaDrm");

  J_INIT_METHOD_ID (media_drm, constructor, "<init>", "(Ljava/util/UUID;)V");
  J_INIT_METHOD_ID (media_drm, open_session, "openSession", "()[B");
#if 0
  /* Fails on some boards */
  J_INIT_METHOD_ID (media_drm, get_security_level, "getSecurityLevel", "([B)I");
#endif
  J_INIT_METHOD_ID (media_drm, get_key_request, "getKeyRequest", "(" "[B"       // byte[] scope
      "[B"                      // byte[] init
      "Ljava/lang/String;"      // String mimeType
      "I"                       // int keyType
      "Ljava/util/HashMap;"     // HashMap<String, String> optionalParameters
      /* returns */
      ")Landroid/media/MediaDrm$KeyRequest;");

  J_INIT_METHOD_ID (media_drm, provide_key_response, "provideKeyResponse",
      "([B[B)[B");
  J_INIT_METHOD_ID (media_drm, close_session, "closeSession", "([B)V");

  /* MediaDrm.KeyRequest */
  media_drm_key_request.klass =
      gst_jni_get_class (env, "android/media/MediaDrm$KeyRequest");

  J_INIT_METHOD_ID (media_drm_key_request, get_default_url, "getDefaultUrl",
      "()Ljava/lang/String;");
  J_INIT_METHOD_ID (media_drm_key_request, get_data, "getData", "()[B");

  /* CryptoInfo */

  media_codec_crypto_info.klass =
      gst_jni_get_class (env, "android/media/MediaCodec$CryptoInfo");

  J_INIT_METHOD_ID (media_codec_crypto_info, constructor, "<init>", "()V");
  J_INIT_METHOD_ID (media_codec_crypto_info, set, "set", "(I[I[I[B[BI)V");

  /* CryptoException */
  crypto_exception.klass =
      gst_jni_get_class (env, "android/media/MediaCodec$CryptoException");

  J_INIT_METHOD_ID (crypto_exception, get_error_code, "getErrorCode", "()I");

  /* Media Crypto */
  media_crypto.klass = gst_jni_get_class (env, "android/media/MediaCrypto");

  J_INIT_STATIC_METHOD_ID (media_crypto, is_crypto_scheme_supported,
      "isCryptoSchemeSupported", "(Ljava/util/UUID;)Z");

  J_INIT_METHOD_ID (media_crypto, set_media_drm_session, "setMediaDrmSession",
      "([B)V");
  J_INIT_METHOD_ID (media_crypto, constructor, "<init>",
      "(Ljava/util/UUID;[B)V");

  ret = TRUE;
error:
  return ret;
}


static const gchar *
gst_amc_drm_detect_known_protection_name (const gchar * uuid_utf8,
    gboolean * cached_supported, gboolean * found)
{
  int i;
  for (i = 0; i < G_N_ELEMENTS (known_cryptos); i++)
    if (!g_ascii_strcasecmp (uuid_utf8, known_cryptos[i].uuid)) {
      if (cached_supported)
        *cached_supported = known_cryptos[i].supported;
      if (found)
        *found = cached_supported_system_ids;
      return known_cryptos[i].name;
    }
  return "(unknown)";
}


typedef struct _GstAmcCurlWriteData
{
  char *data;
  size_t size;
} GstAmcCurlWriteData;

static size_t
gst_amc_curl_write_memory_callback (void *contents, size_t size, size_t nmemb,
    void *data)
{
  const size_t realsize = size * nmemb;
  GstAmcCurlWriteData *write_data = (GstAmcCurlWriteData *) data;
  write_data->data =
      g_realloc (write_data->data, write_data->size + realsize + 1);
  memcpy (&(write_data->data[write_data->size]), contents, realsize);
  write_data->size += realsize;
  write_data->data[write_data->size] = 0;
  return realsize;
}

static gboolean
gst_amc_curl_post_request (const char *url, const char *post,
    size_t post_size, char **response_data, size_t * response_size)
{
  CURL *curl;
  struct curl_slist *slist = NULL;
  GstAmcCurlWriteData chunk;
  CURLcode res;

  /* Check parameters */
  if (!url || !post)
    return FALSE;

  /* Create a new curl instance */
  curl = curl_easy_init ();
  if (!curl)
    return FALSE;

  /* Set the basic options */
  curl_easy_setopt (curl, CURLOPT_HEADER, G_GINT64_CONSTANT (0));
  curl_easy_setopt (curl, CURLOPT_USERAGENT, "Gstreamer Android decoder");
  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, G_GINT64_CONSTANT (30));
  curl_easy_setopt (curl, CURLOPT_POSTFIELDS, post);
  curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, (gint64) post_size);
  /* This is a hack to avoid ca sertificate error on android: */
  curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, G_GINT64_CONSTANT (0));

  /* Set the header options */
  slist = curl_slist_append (slist, "Content-Type: text/xml");
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, slist);

  /* Set the data options */
  chunk.data = g_new0 (char, 1);        // Will grow with realloc
  chunk.size = 0;
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, (void *) &chunk);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION,
      gst_amc_curl_write_memory_callback);

  /* process the request */
  res = curl_easy_perform (curl);

  /* Clean up */
  curl_easy_cleanup (curl);
  curl_slist_free_all (slist);

  if (res != CURLE_OK) {
    GST_ERROR ("HTTP POST failed (%d): %s", res, curl_easy_strerror (res));
    g_free (chunk.data);
    return FALSE;
  }

  *response_data = chunk.data;
  *response_size = chunk.size;
  return TRUE;
}


static gboolean
gst_amc_drm_is_protection_system_id_supported (const gchar * uuid_utf8)
{
  jobject juuid = NULL;
  jboolean jis_supported = FALSE;
  JNIEnv *env = gst_jni_get_env ();
  gboolean cached_supported = FALSE;
  gboolean found = FALSE;
  const gchar *sysid_name =
      gst_amc_drm_detect_known_protection_name (uuid_utf8, &cached_supported,
      &found);
  if (found) {
    jis_supported = cached_supported;
    goto error;                 /* <-- not an error, but same label */
  }

  juuid = juuid_from_utf8 (env, uuid_utf8);
  AMC_CHK (juuid);

  J_CALL_STATIC_BOOLEAN (jis_supported /* = */ , media_crypto,
      is_crypto_scheme_supported, juuid);
error:
  J_DELETE_LOCAL_REF (juuid);
  GST_INFO ("Protection scheme %s (%s) is%s supported by device",
      sysid_name, uuid_utf8, jis_supported ? "" : " not");
  return jis_supported;
}


static gboolean
gst_amc_drm_hack_pssh_initdata (GstElement * el, guchar * payload,
    gsize payload_size, gsize * new_payload_size)
{
  guint data_offset, data_size;
  if (!fluc_drm_cenc_validate_pssh (payload, payload_size, &data_offset,
          &data_size))
    return FALSE;

  /* Now we have to hack pssh a little because of Android libmediadrm's pitfall:
     It requires initData (pssh) to have "data" size == 0, and if "data" size != 0,
     android will just refuse to parse in
     av/drm/mediadrm/plugins/clearkey/InitDataParcer.cpp:112
   */
  *new_payload_size = data_offset;
  if (*new_payload_size != payload_size) {
    GST_DEBUG_OBJECT (el, "Overwriting pssh header's size "
        "from %" G_GSIZE_FORMAT " to %" G_GSIZE_FORMAT
        ", and \"data size\" field to 0", payload_size, *new_payload_size);
    GST_WRITE_UINT32_BE (payload, *new_payload_size);
    GST_WRITE_UINT32_BE (payload + data_offset - 4, 0);
  }

  return TRUE;
}


static void
gst_amc_drm_log_big (GstElement * el, const gchar * pref, const gchar * text,
    gsize size)
{
  jsize i;
  GST_DEBUG_OBJECT (el, "start logging %s of size %" G_GSIZE_FORMAT, pref,
      size);
  for (i = 0; i < size; i += 700) {
    gchar chunk[701];
    snprintf (chunk, 701, "%s", text + i);
    GST_DEBUG_OBJECT (el, "%s = [%s]", pref, chunk);
  }
}


static gboolean
gst_amc_drm_proccess_key_request (GstElement * el, JNIEnv * env,
    jobject request, jobject media_drm_obj, jbyteArray jsession_id)
{
  gboolean ret = FALSE;
  jbyteArray jreq_data = NULL;
  jsize req_data_len = 0;
  gchar *req_data_utf8 = NULL;
  jstring jdef_url = NULL;
  gchar *def_url = NULL;
  size_t key_response_size = 0;
  jbyteArray jkey_response = NULL;
  jobject tmp_keysetid = NULL;
  gchar *key_response = NULL;

  J_CALL_OBJ (jdef_url /* = */ , request,
      media_drm_key_request.get_default_url);

  def_url = gst_amc_get_string_utf8 (env, jdef_url);
  AMC_CHK (def_url);
  GST_DEBUG_OBJECT (el, "default url is: [%s]", def_url);

  J_CALL_OBJ (jreq_data /* = */ , request, media_drm_key_request.get_data);
  AMC_CHK (jreq_data);

  req_data_len = (*env)->GetArrayLength (env, jreq_data);
  J_EXCEPTION_CHECK ("GetArrayLength");
  GST_DEBUG_OBJECT (el, "req_data_len = %d", req_data_len);

  req_data_utf8 = g_malloc0 (req_data_len + 1);
  (*env)->GetByteArrayRegion (env, jreq_data, 0, req_data_len,
      (jbyte *) req_data_utf8);
  J_EXCEPTION_CHECK ("GetByteArrayRegion");

  gst_amc_drm_log_big (el, "req", req_data_utf8, req_data_len);

  /* FIXME: if clearkey --> reencode request and response base64/base64url */

  if (!gst_amc_curl_post_request (def_url, req_data_utf8, req_data_len,
          &key_response, &key_response_size)) {
    GST_ERROR_OBJECT (el, "Could not post key request to url <%s>", def_url);
    goto error;
  }

  gst_amc_drm_log_big (el, "resp", key_response, key_response_size);

  jkey_response =
      jbyte_arr_from_data (env, (guchar *) key_response, key_response_size);
  AMC_CHK (jkey_response);

  J_CALL_OBJ (tmp_keysetid /* = */ , media_drm_obj,
      media_drm.provide_key_response, jsession_id, jkey_response);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (tmp_keysetid);
  J_DELETE_LOCAL_REF (jkey_response);
  J_DELETE_LOCAL_REF (jreq_data);
  J_DELETE_LOCAL_REF (jdef_url);
  g_free (key_response);
  g_free (def_url);
  g_free (req_data_utf8);
  return ret;
}


static void
gst_amc_ctx_clear (GstAmcCrypto * ctx)
{
  JNIEnv *env = gst_jni_get_env ();

  g_ptr_array_free (ctx->playready_kids, TRUE);
  ctx->playready_kids = NULL;

  if (ctx->mdrm) {
    /* If we (not user) were the one who opened drmsession - we must close it */
    if (ctx->mdrm_session_id) {
      J_CALL_VOID (ctx->mdrm, media_drm.close_session, ctx->mdrm_session_id);
    }
    /* To resolve J_CALL_VOID */
  error:
    J_DELETE_GLOBAL_REF (ctx->mdrm_session_id);
    J_DELETE_GLOBAL_REF (ctx->mdrm);
  }

  J_DELETE_GLOBAL_REF (ctx->mcrypto);
}


static gboolean
gst_amc_drm_jmedia_crypto_from_pssh (GstAmcCrypto * ctx, const guchar * data,
    guint32 data_size, const gchar * system_id)
{
  jobject juuid = NULL;
  jobject media_crypto_obj = NULL;
  jobject media_drm_obj = NULL;
  jobject request = NULL;
  jbyteArray jsession_id = NULL;
  jbyteArray jinit_data = NULL;
  const jint KEY_TYPE_STREAMING = 1;
  jstring jmime = NULL;

  GstElement *el = ctx->gstelement;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (system_id && data && data_size);

  /* Reset mdrm, mcrypto, mdrm_session_id to NULLs,
   * it will be safer in case of failure */
  if (ctx->mdrm || ctx->mcrypto || ctx->mdrm_session_id) {
    gst_amc_ctx_clear (ctx);
  }

  jinit_data = jbyte_arr_from_data (env, data, data_size);
  AMC_CHK (jinit_data);

  juuid = juuid_from_utf8 (env, system_id);
  AMC_CHK (juuid);

  media_drm_obj = (*env)->NewObject (env, media_drm.klass,
      media_drm.constructor, juuid);
  AMC_CHK (media_drm_obj);

  J_CALL_OBJ (jsession_id /* = */ , media_drm_obj, media_drm.open_session);
  AMC_CHK (jsession_id);

  /* Throws exception on some boards with message "Failed to get security level" */
#if 0
  /* Log native security level of the device, obtained by MediaDrm session */
  {
    guint sec_level;
    /* Enums from MediaDrm's documentation, from 0 to 5 */
    const gchar *android_sec_levels[] = {
      "SECURITY_LEVEL_UNKNOWN",
      "SECURITY_LEVEL_SW_SECURE_CRYPTO",
      "SECURITY_LEVEL_SW_SECURE_DECODE",
      "SECURITY_LEVEL_HW_SECURE_CRYPTO",
      "SECURITY_LEVEL_HW_SECURE_DECODE",
      "SECURITY_LEVEL_HW_SECURE_ALL"
    };

    J_CALL_INT (sec_level /* = */ , media_drm_obj, media_drm.get_security_level,
        jsession_id);

    GST_DEBUG_OBJECT (el, "MediaDrm session opened with security level %s (%d)",
        android_sec_levels
        [sec_level < G_N_ELEMENTS (android_sec_levels) ?
            sec_level : 0], sec_level);
  }
#endif

  /* Other known valid mime type is "webm", but we're not sure about it, that's why
   * we always wrap initData to pssh currently */
  jmime = (*env)->NewStringUTF (env, "cenc");
  AMC_CHK (jmime);

  J_CALL_OBJ (request /* = */ , media_drm_obj, media_drm.get_key_request,
      jsession_id, jinit_data, jmime, KEY_TYPE_STREAMING, NULL);
  AMC_CHK (request);

  AMC_CHK (gst_amc_drm_proccess_key_request (el, env, request, media_drm_obj,
          jsession_id));

  /* Create MediaCrypto object */
  media_crypto_obj = (*env)->NewObject (env, media_crypto.klass,
      media_crypto.constructor, juuid, jsession_id);
  AMC_CHK (media_crypto_obj);

  /* Will be unreffed in free_format func */
  ctx->mdrm = (*env)->NewGlobalRef (env, media_drm_obj);
  ctx->mcrypto = (*env)->NewGlobalRef (env, media_crypto_obj);
  ctx->mdrm_session_id = (*env)->NewGlobalRef (env, jsession_id);

  AMC_CHK (ctx->mdrm && ctx->mcrypto && ctx->mdrm_session_id);
error:
  J_DELETE_LOCAL_REF (request);
  J_DELETE_LOCAL_REF (jmime);
  J_DELETE_LOCAL_REF (juuid);
  J_DELETE_LOCAL_REF (jinit_data);
  J_DELETE_LOCAL_REF (media_drm_obj);
  J_DELETE_LOCAL_REF (media_crypto_obj);
  J_DELETE_LOCAL_REF (jsession_id);

  return ctx->mdrm && ctx->mcrypto && ctx->mdrm_session_id;
}

GstAmcCrypto *
gst_amc_drm_ctx_new (GstElement * element)
{
  GstAmcCrypto *ctx = g_new0 (GstAmcCrypto, 1);
  ctx->gstelement = element;
  ctx->inband_drm_enabled = GST_AMC_DRM_DEFAULT_INBAND_DRM_ENABLED;
  return ctx;
}

void
gst_amc_drm_enable_inband (GstAmcCrypto * ctx, gboolean enabled)
{
  ctx->inband_drm_enabled = enabled;
}

void
gst_amc_drm_ctx_free (GstAmcCrypto * ctx)
{
  if (!ctx)
    return;
  gst_amc_ctx_clear (ctx);

  g_list_free_full (ctx->drm_events_pack, (GDestroyNotify) gst_event_unref);
  g_free (ctx);
}

static jobject
gst_amc_drm_cenc_get_crypto_info (GstAmcCrypto * ctx, const GstStructure * s,
    gsize bufsize)
{
  guint alg_id;
  guint n_subsamples = 0;
  jint j_n_subsamples = 0;
  gboolean ok = FALSE;
  FlucDrmCencSencEntry *subsamples_buf_mem = NULL;
  jintArray j_n_bytes_of_clear_data = NULL, j_n_bytes_of_encrypted_data = NULL;
  jbyteArray j_kid = NULL, j_iv = NULL;
  jobject crypto_info = NULL, crypto_info_ret = NULL;
  jint *n_bytes_of_clear_data = NULL;
  jint *n_bytes_of_encrypted_data = NULL;
  JNIEnv *env = gst_jni_get_env ();
  GstElement *el = ctx->gstelement;

  ok = gst_structure_get_uint (s, "subsample_count", &n_subsamples);
  AMC_CHK (ok && n_subsamples);

  j_n_subsamples = n_subsamples;

  /* Performing subsample arrays */
  {
    const GValue *subsamples_val;
    GstBuffer *subsamples_buf;
    gint i;
    gsize entries_sumsize = 0;

    n_bytes_of_clear_data = g_new (jint, n_subsamples);
    n_bytes_of_encrypted_data = g_new (jint, n_subsamples);

    AMC_CHK (subsamples_val = gst_structure_get_value (s, "subsamples"));
    AMC_CHK (subsamples_buf = gst_value_get_buffer (subsamples_val));

    subsamples_buf_mem =
        (FlucDrmCencSencEntry *) GST_BUFFER_DATA (subsamples_buf);
    for (i = 0; i < n_subsamples; i++) {
      n_bytes_of_clear_data[i] = subsamples_buf_mem[i].clear;
      n_bytes_of_encrypted_data[i] = subsamples_buf_mem[i].encrypted;
      entries_sumsize +=
          subsamples_buf_mem[i].clear + subsamples_buf_mem[i].encrypted;
    }

    if (G_UNLIKELY (entries_sumsize != bufsize)) {
      GST_ERROR ("Sanity check failed: bufsize %" G_GSIZE_FORMAT
          " != entries size %" G_GSIZE_FORMAT, bufsize, entries_sumsize);
      AMC_CHK (0);
    }

    j_n_bytes_of_clear_data = (*env)->NewIntArray (env, j_n_subsamples);
    j_n_bytes_of_encrypted_data = (*env)->NewIntArray (env, j_n_subsamples);
    AMC_CHK (j_n_bytes_of_clear_data && j_n_bytes_of_encrypted_data);

    (*env)->SetIntArrayRegion (env, j_n_bytes_of_clear_data, 0,
        n_subsamples, n_bytes_of_clear_data);
    (*env)->SetIntArrayRegion (env, j_n_bytes_of_encrypted_data, 0,
        n_subsamples, n_bytes_of_encrypted_data);
    J_EXCEPTION_CHECK ("SetIntArrayRegion");
  }
  // Performing key and iv
  {
    const GValue *kid_val, *iv_val;
    const GstBuffer *kid_buf, *iv_buf;
    const guchar *kid;

    AMC_CHK (kid_val = gst_structure_get_value (s, "kid"));
    AMC_CHK (kid_buf = gst_value_get_buffer (kid_val));
    AMC_CHK (iv_val = gst_structure_get_value (s, "iv"));
    AMC_CHK (iv_buf = gst_value_get_buffer (iv_val));

    /* There's a check in MediaCodec for kid size == 16 and iv size == 16
       So, we always create and copy 16-byte arrays.
       We manage iv size to always be 16 on android in flu-codec-sdk. */
    AMC_CHK ((GST_BUFFER_SIZE (kid_buf) >= 16)
        && (GST_BUFFER_SIZE (iv_buf) >= 16));

    kid = GST_BUFFER_DATA (kid_buf);

    /* We have to check each buffer to maybe detect kid mismatch. In this
     * case sometimes we can override kid, if we detect it differs only with
     * byte order.*/
    if (ctx->playready_kids) {
      const guchar *kid_override =
          flucdrm_KID_validate_or_override (kid, ctx->playready_kids);

      if (kid_override) {
        /* Log override if logging is debug */
        if (G_UNLIKELY (__gst_debug_min >= GST_LEVEL_DEBUG
                && memcmp (kid, kid_override, 16) != 0)) {
          GST_DEBUG_OBJECT (el,
              "overriding kid " FLUC_16BYTE_FORMAT " with " FLUC_16BYTE_FORMAT,
              FLUC_16BYTE_ARGS (kid), FLUC_16BYTE_ARGS (kid_override));
        }

        kid = kid_override;
      }
    }

    j_kid = jbyte_arr_from_data (env, kid, 16);
    j_iv = jbyte_arr_from_data (env, GST_BUFFER_DATA (iv_buf), 16);
    AMC_CHK (j_kid && j_iv);
  }

  /* We support only AES_CTR or AES_CBC.
   * In Android's constants they're the same as in tenc: CTR = 1, CBC = 2 */
  AMC_CHK (gst_structure_get_uint (s, "algorithm_id", &alg_id));
  AMC_CHK (alg_id == 1 || alg_id == 2);

  // new MediaCodec.CryptoInfo
  crypto_info = (*env)->NewObject (env, media_codec_crypto_info.klass,
      media_codec_crypto_info.constructor);
  AMC_CHK (crypto_info);

  J_CALL_VOID (crypto_info, media_codec_crypto_info.set, j_n_subsamples,        // int newNumSubSamples
      j_n_bytes_of_clear_data,  // int[] newNumBytesOfClearData
      j_n_bytes_of_encrypted_data,      // int[] newNumBytesOfEncryptedData
      j_kid,                    // byte[] newKey
      j_iv,                     // byte[] newIV
      alg_id                    // int newMode
      );

  crypto_info_ret = crypto_info;
error:
  J_DELETE_LOCAL_REF (j_n_bytes_of_clear_data);
  J_DELETE_LOCAL_REF (j_n_bytes_of_encrypted_data);
  J_DELETE_LOCAL_REF (j_kid);
  J_DELETE_LOCAL_REF (j_iv);
  g_free (n_bytes_of_clear_data);
  g_free (n_bytes_of_encrypted_data);
  return crypto_info_ret;
}


static gboolean
gst_amc_drm_try_drm_event (GstAmcCrypto * ctx, GstEvent * event)
{
  GstElement *el = ctx->gstelement;
  GstBuffer *data_buf = NULL;
  GstBuffer *buf_to_unref = NULL;
  const gchar *system_id = NULL, *origin = NULL;
  gboolean origin_is_iso;
  guchar *init_data;
  guint init_data_size;
  GPtrArray *playready_kids = NULL;
  gboolean system_supported;

  fluc_drm_event_parse (event, &system_id, &data_buf, &origin);

  if (!data_buf || !GST_BUFFER_SIZE (data_buf) || !GST_BUFFER_DATA (data_buf)
      || !system_id) {
    GST_ERROR_OBJECT (el, "Invalid drm event %p", event);
    return FALSE;
  }

  init_data = GST_BUFFER_DATA (data_buf);
  init_data_size = GST_BUFFER_SIZE (data_buf);

  system_supported = gst_amc_drm_is_protection_system_id_supported (system_id);

  GST_DEBUG_OBJECT (el, "Received drm event."
      "SystemId = [%s] (%ssupported by device), origin = [%s], "
      "data size = %d", system_id, system_supported ? "" : "not ",
      origin, init_data_size);

  if (!system_supported) {
    GST_INFO_OBJECT (el, "Skipping drm event: device doesn't support [%s]",
        system_id);
    return FALSE;
  }

  origin_is_iso = g_str_has_prefix (origin, "isobmff/");

  /* For case of clearkey we have to hack pssh data because of a bug in
   * av/drm/mediadrm/plugins/clearkey/InitDataParcer.cpp */
  if (origin_is_iso && gst_amc_drm_sysid_is_clearkey (system_id)) {
    gsize new_size;
    gst_amc_drm_hack_pssh_initdata (el, init_data, init_data_size, &new_size);
    init_data_size = GST_BUFFER_SIZE (data_buf) = new_size;
  }

  /* If systemid is playready - dump PO and KID */
  if (gst_amc_drm_sysid_is_playready (system_id)) {
    guint data_offset = 0;
    guint data_size = init_data_size;
    int i;
    /* Extract from pssh if needed */
    if (origin_is_iso) {
      if (!fluc_drm_cenc_validate_pssh (init_data, init_data_size,
              &data_offset, &data_size))
        return FALSE;
    }

    playready_kids =
        flucdrm_playready_OBJ_get_KIDs (init_data + data_offset, data_size);

    if (playready_kids) {
      for (i = 0; i < playready_kids->len; i++) {
        gpointer kid = g_ptr_array_index (playready_kids, i);

        GST_DEBUG_OBJECT (el, "kid [%d] from POBJ = " FLUC_16BYTE_FORMAT,
            i, FLUC_16BYTE_ARGS (kid));
      }
    }
  }

  /* If origin is not an iso we prefer to wrap data to pssh v0 */
  if (!origin_is_iso) {
    guchar *new_pssh;
    guint32 new_pssh_size;

    new_pssh =
        fluc_drm_cenc_wrap_data_to_pssh_v0 (system_id, init_data,
        init_data_size, &new_pssh_size);

    if (!new_pssh)
      return FALSE;

    /* Replace data_buf with one wrapped to pssh.
     * There's no leak here, because event is who owns data_buf, but
     * if we have created our new one - we have to unref it after usage. */
    buf_to_unref = data_buf = gst_buffer_new ();
    init_data = GST_BUFFER_DATA (data_buf) = new_pssh;
    init_data_size = GST_BUFFER_SIZE (data_buf) = new_pssh_size;

    /* Dump result pssh if we're debugging */
    if (__gst_debug_min >= GST_LEVEL_DEBUG &&
        !fluc_drm_cenc_validate_pssh (init_data, init_data_size, NULL, NULL))
      GST_ERROR_OBJECT (el, "Internal error: generated invalid pssh");
  }

  gst_element_post_message (el,
      gst_message_new_element (GST_OBJECT (el),
          gst_structure_new ("prepare-drm-agent-handle",
              "init_data", GST_TYPE_BUFFER, data_buf, NULL)));

  if (ctx->mcrypto) {
    GST_DEBUG_OBJECT (el, "Received from user MediaCrypto [%p]", ctx->mcrypto);
  } else if (ctx->inband_drm_enabled) {
    GST_DEBUG_OBJECT (el,
        "User didn't provide us MediaCrypto, trying In-band mode");
    if (!gst_amc_drm_jmedia_crypto_from_pssh (ctx, init_data, init_data_size,
            system_id)) {
      GST_INFO_OBJECT (el, "In-band mode's drm event proccessing failed");
    }
  }

  if (ctx->mcrypto) {
    if (ctx->playready_kids)
      g_ptr_array_free (ctx->playready_kids, TRUE);
    ctx->playready_kids = playready_kids;
    playready_kids = NULL;
  }

  if (playready_kids)
    g_ptr_array_free (playready_kids, TRUE);

  if (buf_to_unref)
    gst_buffer_unref (buf_to_unref);

  return ctx->mcrypto ? TRUE : FALSE;
}


gboolean
gst_amc_drm_mcrypto_update (GstAmcCrypto * ctx, gboolean * need_configure)
{
  GList *l;
  GstElement *el = ctx->gstelement;

  if (need_configure)
    *need_configure = FALSE;

  /* First check if we can keep current MCrypto: it is so
   * if one of events in the pack has the same hash as the current
   * one. */
  for (l = ctx->drm_events_pack; l; l = l->next) {
    GstEvent *e = (GstEvent *) l->data;
    guint32 h = fluc_drm_event_compile_hash (e);

    GST_ERROR ("### comparing hash %d to %d", h, ctx->last_drm_event_hash);

    if (ctx->last_drm_event_hash == h) {
      GST_ERROR_OBJECT (el,
          "### Found drm event that same hash as one already in use. "
          "will keep using previous MediaCrypto");
      goto beach;
    }
  }

  /* If no drm event "can be reused", try to create new MCrypto */
  for (l = ctx->drm_events_pack; l; l = l->next) {
    GstEvent *e = (GstEvent *) l->data;

    if (gst_amc_drm_try_drm_event (ctx, e)) {
      if (need_configure)
        *need_configure = TRUE;

      ctx->last_drm_event_hash = fluc_drm_event_compile_hash (e);
      GST_ERROR ("### storing hash %d", ctx->last_drm_event_hash);
      break;
    }
  }

beach:

  if (G_LIKELY (ctx->mcrypto)) {
    ctx->drm_reconfigured = TRUE;
    GST_ERROR ("### mcrypto ready");
    return TRUE;
  }

  GST_ELEMENT_ERROR (el, STREAM, DECRYPT_NOKEY, (NULL),
      ("Decryption isn't possible: no MediaCrypto"));

  return FALSE;
}

jobject
gst_amc_drm_get_crypto_info (GstAmcCrypto * ctx, const GstBuffer * drmbuf)
{
  const GstStructure *cenc_info;

  if (G_UNLIKELY (!fluc_drm_is_buffer (drmbuf))) {
    GST_ERROR ("DRM Buffer not found");
    return NULL;
  }

  cenc_info = fluc_drm_buffer_find_by_name (drmbuf, "application/x-cenc");
  if (!cenc_info) {
    GST_ERROR ("cenc structure not found in drmbuffer");
    return NULL;
  }

  return gst_amc_drm_cenc_get_crypto_info (ctx, cenc_info,
      GST_BUFFER_SIZE (drmbuf));
}


void
gst_amc_drm_handle_drm_event (GstAmcCrypto * ctx, GstEvent * event)
{
  if (ctx->drm_reconfigured) {
    /* If we receive the drm event after the last configuration, first
     * free the previous event list */
    g_list_free_full (ctx->drm_events_pack, (GDestroyNotify) gst_event_unref);
    ctx->drm_events_pack = NULL;
    ctx->drm_reconfigured = FALSE;
    GST_ERROR ("### clearing list of size %d",
        g_list_length (ctx->drm_events_pack));
  }

  ctx->drm_events_pack =
      g_list_append (ctx->drm_events_pack, gst_event_ref (event));
}


void
gst_amc_drm_log_known_supported_protection_schemes (void)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (known_cryptos); i++)
    known_cryptos[i].supported =
        gst_amc_drm_is_protection_system_id_supported (known_cryptos[i].uuid);

  cached_supported_system_ids = TRUE;
}

gboolean
gst_amc_drm_is_drm_event (GstEvent * event)
{
  return fluc_drm_is_event (event);
}

jobject
gst_amc_drm_mcrypto_get (GstAmcCrypto * ctx)
{
  return ctx ? ctx->mcrypto : NULL;
}

gboolean
gst_amc_drm_mcrypto_set (GstAmcCrypto * ctx, gpointer mcrypto)
{
  GstElement *el;
  JNIEnv *env;

  if (!ctx)
    goto error;

  el = ctx->gstelement;
  env = gst_jni_get_env ();
  GST_DEBUG_OBJECT (el, "setting mcrypto from user [%p]", mcrypto);

  if (!mcrypto)
    goto error;

  /* Validate if user have passed us a good object */
  AMC_CHK ((*env)->IsInstanceOf (env, mcrypto, media_crypto.klass));

  ctx->mcrypto = (*env)->NewGlobalRef (env, mcrypto);
  GST_DEBUG_OBJECT (el, "after global ref mcrypto is [%p]", ctx->mcrypto);

  return TRUE;
error:
  return FALSE;
}

gboolean
gst_amc_drm_crypto_exception_check (JNIEnv * env, const gchar * call)
{
  jthrowable ex;

  if (G_LIKELY (!(*env)->ExceptionCheck (env)))
    return FALSE;

  ex = (*env)->ExceptionOccurred (env);
  GST_ERROR ("Caught exception on call %s", call);
  (*env)->ExceptionDescribe (env);
  (*env)->ExceptionClear (env);
  if (ex && (*env)->IsInstanceOf (env, ex, crypto_exception.klass)) {
    static const jint
        ERROR_INSUFFICIENT_OUTPUT_PROTECTION = 4,
        ERROR_KEY_EXPIRED = 2,
        ERROR_NO_KEY = 1,
        ERROR_RESOURCE_BUSY = 3,
        ERROR_SESSION_NOT_OPENED = 5, ERROR_UNSUPPORTED_OPERATION = 6;

#define CHK_CRYPTO_ERR_CODE(code) do {                                  \
        if (error_code == code) {                                       \
          GST_ERROR ("Error code from crypto exception is " #code);     \
          return TRUE;                                                  \
        } } while (0)

    jint error_code =
        (*env)->CallIntMethod (env, ex, crypto_exception.get_error_code);
    CHK_CRYPTO_ERR_CODE (ERROR_INSUFFICIENT_OUTPUT_PROTECTION);
    CHK_CRYPTO_ERR_CODE (ERROR_KEY_EXPIRED);
    CHK_CRYPTO_ERR_CODE (ERROR_NO_KEY);
    CHK_CRYPTO_ERR_CODE (ERROR_RESOURCE_BUSY);
    CHK_CRYPTO_ERR_CODE (ERROR_SESSION_NOT_OPENED);
    CHK_CRYPTO_ERR_CODE (ERROR_UNSUPPORTED_OPERATION);
    GST_ERROR ("Unknown error code from CryptoException: %d", error_code);
  }

  return TRUE;
}
