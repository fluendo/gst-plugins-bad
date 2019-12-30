/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "gstamc.h"
#include "gstamc-constants.h"

#include "gstamcvideodec.h"
#include "gstamcaudiodec.h"
#include "gstamcvideosink.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/androidjni/gstjniutils.h>
#include <string.h>
#include <jni.h>

#include <curl/curl.h>

/* Macros have next rules:
   J_CALL_<TYPE> (), J_CALL_STATIC_<TYPE> () - first parameter is a variable
   to write to, if it's not J_CALL_VOID () or J_CALL_STATIC_VOID ().
   If exception occured, it jumps to "error" label, and the variable
   is kept untouched.
 */
#include <gstamcmacro.h>

GST_DEBUG_CATEGORY (gst_amc_debug);
#define GST_CAT_DEFAULT gst_amc_debug

GQuark gst_amc_codec_info_quark = 0;

static GList *codec_infos = NULL;
#ifdef GST_AMC_IGNORE_UNKNOWN_COLOR_FORMATS
static gboolean ignore_unknown_color_formats = TRUE;
#else
static gboolean ignore_unknown_color_formats = FALSE;
#endif

static gboolean accepted_color_formats (GstAmcCodecType * type,
    gboolean is_encoder);

/* Global cached references */
static struct
{
  jclass klass;
  jmethodID constructor;
} java_string;
static struct
{
  jclass klass;
  jmethodID configure;
  jmethodID create_by_codec_name;
  jmethodID dequeue_input_buffer;
  jmethodID dequeue_output_buffer;
  jmethodID flush;
  jmethodID get_input_buffers;
  jmethodID get_output_buffers;
  jmethodID get_output_format;
  jmethodID queue_input_buffer;
  jmethodID release;
  jmethodID release_output_buffer;
  jmethodID release_output_buffer_ts;
  jmethodID set_output_surface;
  jmethodID start;
  jmethodID stop;
  jint CRYPTO_MODE_AES_CTR;
  jmethodID queue_secure_input_buffer;
} media_codec;
static struct
{
  jclass klass;
  jmethodID get_error_code;
} crypto_exception;
static struct
{
  jclass klass;
  jmethodID constructor;
  jfieldID flags;
  jfieldID offset;
  jfieldID presentation_time_us;
  jfieldID size;
} media_codec_buffer_info;
static struct
{
  jclass klass;
  jmethodID create_audio_format;
  jmethodID create_video_format;
  jmethodID to_string;
  jmethodID contains_key;
  jmethodID get_float;
  jmethodID set_float;
  jmethodID get_integer;
  jmethodID set_integer;
  jmethodID get_string;
  jmethodID set_string;
  jmethodID get_byte_buffer;
  jmethodID set_byte_buffer;
} media_format;
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
static struct
{
  jclass klass;
  jmethodID from_string;
} uuid;

jbyteArray
jbyte_arr_from_data (JNIEnv * env, const guchar * data, gsize size)
{
  jbyteArray arr = (*env)->NewByteArray (env, size);
  AMC_CHK (arr);
  (*env)->SetByteArrayRegion (env, arr, 0, size, (const jbyte *) data);
  J_EXCEPTION_CHECK ("SetByteArrayRegion");

  return arr;
error:
  J_DELETE_LOCAL_REF (arr);
  return NULL;
}


static gchar *
gst_amc_get_string_utf8 (JNIEnv * env, jstring v_str)
{
  const gchar *v = NULL;
  gchar *ret = NULL;
  AMC_CHK (v_str);

  v = (*env)->GetStringUTFChars (env, v_str, NULL);
  AMC_CHK (v);

  ret = g_strdup (v);
error:
  if (v)
    (*env)->ReleaseStringUTFChars (env, v_str, v);
  return ret;
}


static jobject
gst_amc_get_crypto_info (const GstStructure * s, gsize bufsize)
{
  guint n_subsamples = 0;
  jint j_n_subsamples = 0;
  gboolean ok = FALSE;
  FlucDrmCencSencEntry *subsamples_buf_mem = NULL;
  jintArray j_n_bytes_of_clear_data = NULL, j_n_bytes_of_encrypted_data = NULL;
  jbyteArray j_kid = NULL, j_iv = NULL;
  jobject crypto_info = NULL, crypto_info_ret = NULL;
  JNIEnv *env = gst_jni_get_env ();

  ok = gst_structure_get_uint (s, "subsample_count", &n_subsamples);
  AMC_CHK (ok && n_subsamples);

  j_n_subsamples = n_subsamples;

  // Performing subsample arrays
  {
    jint *n_bytes_of_clear_data = g_new (jint, n_subsamples);
    jint *n_bytes_of_encrypted_data = g_new (jint, n_subsamples);
    const GValue *subsamples_val;
    GstBuffer *subsamples_buf;
    gint i;
    gsize entries_sumsize = 0;

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
      GST_ERROR ("### Sanity check failed: bufsize %d != entries size %d",
          bufsize, entries_sumsize);
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

    g_free (n_bytes_of_clear_data);
    g_free (n_bytes_of_encrypted_data);
  }
  // Performing key and iv
  {
    const GValue *kid_val, *iv_val;
    const GstBuffer *kid_buf, *iv_buf;

    AMC_CHK (kid_val = gst_structure_get_value (s, "kid"));
    AMC_CHK (kid_buf = gst_value_get_buffer (kid_val));
    AMC_CHK (iv_val = gst_structure_get_value (s, "iv"));
    AMC_CHK (iv_buf = gst_value_get_buffer (iv_val));

    /* There's a check in MediaCodec for kid size == 16 and iv size == 16
       So, we always create and copy 16-byte arrays.
       We manage iv size to always be 16 on android in flu-codec-sdk. */
    AMC_CHK ((GST_BUFFER_SIZE (kid_buf) >= 16)
        && (GST_BUFFER_SIZE (iv_buf) >= 16));

    j_kid = jbyte_arr_from_data (env, GST_BUFFER_DATA (kid_buf), 16);
    j_iv = jbyte_arr_from_data (env, GST_BUFFER_DATA (iv_buf), 16);
    AMC_CHK (j_kid && j_iv);
  }

  // new MediaCodec.CryptoInfo
  crypto_info = (*env)->NewObject (env, media_codec_crypto_info.klass,
      media_codec_crypto_info.constructor);
  AMC_CHK (crypto_info);

  J_CALL_VOID (crypto_info, media_codec_crypto_info.set, j_n_subsamples,        // int newNumSubSamples
      j_n_bytes_of_clear_data,  // int[] newNumBytesOfClearData
      j_n_bytes_of_encrypted_data,      // int[] newNumBytesOfEncryptedData
      j_kid,                    // byte[] newKey
      j_iv,                     // byte[] newIV
      media_codec.CRYPTO_MODE_AES_CTR   // int newMode
      );


  crypto_info_ret = crypto_info;
error:
  J_DELETE_LOCAL_REF (j_n_bytes_of_clear_data);
  J_DELETE_LOCAL_REF (j_n_bytes_of_encrypted_data);
  J_DELETE_LOCAL_REF (j_kid);
  J_DELETE_LOCAL_REF (j_iv);
  return crypto_info_ret;
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

gboolean
hack_pssh_initdata (guchar * payload, gsize payload_size,
    gsize * new_payload_size)
{
  guchar *payload_begin = payload;
  if (payload_size < 32) {
    GST_ERROR ("Invalid pssh data");
    return FALSE;
  }

  if (FALSE == (payload[4] == 'p' &&
          payload[5] == 's' && payload[6] == 's' && payload[7] == 'h')) {
    GST_ERROR ("Sanity check failed: provided payload is not pssh");
    return FALSE;
  }

  {
    guint32 version = GST_READ_UINT32_BE (payload + 8);
    version = version >> 24;
    payload += 28;

    if (version != 1)
      GST_ERROR ("Sanity check failed: pssh version (%d) != 1", version);

    if (version > 0) {
      gint i;
      guint32 kid_count = GST_READ_UINT32_BE (payload);
      GST_ERROR ("### PSSH: kid_count = %d", kid_count);
      payload += 4;
      for (i = 0; i < kid_count; i++) {
        guchar *p = payload;
        GST_ERROR ("### kid[%d] = [%02x.%02x.%02x.%02x."
            "%02x.%02x.%02x.%02x."
            "%02x.%02x.%02x.%02x."
            "%02x.%02x.%02x.%02x]", i,
            p[0], p[1], p[2], p[3],
            p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]
            );

        payload += 16;
      }
    }
  }

  {
    gsize data_field_size = GST_READ_UINT32_BE (payload);
    payload += 4;

    GST_ERROR ("### Size of data field inside pssh: %d", data_field_size);
  }

  /* Now we have to hack pssh a little because of Android libmediadrm's pitfall: 
     It requires initData (pssh) to have "data" size == 0, and if "data" size != 0,
     android will just refuse to parse in
     av/drm/mediadrm/plugins/clearkey/InitDataParcer.cpp:112
   */
  *new_payload_size = payload - payload_begin;
  if (*new_payload_size != payload_size) {
    GST_ERROR
        ("&&& Overwriting pssh header's size from %u to %u, and \"data size\" field to 0",
        payload_size, *new_payload_size);
    GST_WRITE_UINT32_BE (payload_begin, *new_payload_size);
    GST_WRITE_UINT32_BE (payload - 4, 0);
  }

  return TRUE;
}


static void
gst_amc_log_big (const gchar * pref, const gchar * text, gsize size)
{
  jsize i;
  GST_ERROR ("### start logging %s of size %d", pref, size);
  for (i = 0; i < size; i += 700) {
    gchar chunk[701];
    snprintf (chunk, 701, "%s", text + i);
    GST_ERROR ("### %s = [%s]", pref, chunk);
  }
}


gboolean
jmedia_crypto_from_drm_event (GstEvent * event, GstAmcCrypto * crypto_ctx)
{
  GstBuffer *data_buf = NULL;
  const gchar *origin = NULL, *system_id = NULL;
  jobject juuid = NULL, media_crypto_obj = NULL, media_drm_obj = NULL, request =
      NULL;
  jbyteArray jsession_id = NULL, jinit_data = NULL;
  JNIEnv *env = gst_jni_get_env ();
  static jint KEY_TYPE_STREAMING = 1;
  jstring jmime;
  guchar *complete_pssh_payload;
  gchar *def_url = NULL;
  gsize complete_pssh_payload_size;

  fluc_drm_event_parse (event, &system_id, &data_buf, &origin);
  AMC_CHK (system_id && data_buf && origin);

  complete_pssh_payload = GST_BUFFER_DATA (data_buf);
  complete_pssh_payload_size = GST_BUFFER_SIZE (data_buf);

  GST_ERROR
      ("{{{ Parsed drm event. system id = %s (%s supported by device), origin = %s, data_size = %d",
      system_id, is_protection_system_id_supported (system_id) ? "" : "not",
      origin, complete_pssh_payload_size);

  /* If source is quicktime, "data" buffer is wrapped in qt atom.
     To be compatible with qtdemux 1.0 from community, we have to skip
     this atom thing here, and not in qtdemux.
   */
  if (g_str_has_prefix (origin, "isobmff/") && sysid_is_clearkey (system_id) &&
      !hack_pssh_initdata (complete_pssh_payload, complete_pssh_payload_size,
          &complete_pssh_payload_size))
    goto error;

  jinit_data =
      jbyte_arr_from_data (env, complete_pssh_payload,
      complete_pssh_payload_size);

  AMC_CHK (jinit_data);

  juuid = juuid_from_utf8 (env, system_id);

  AMC_CHK (juuid);

  media_drm_obj = (*env)->NewObject (env, media_drm.klass,
      media_drm.constructor, juuid);
  AMC_CHK (media_drm_obj);

  J_CALL_OBJ (jsession_id /* = */ , media_drm_obj, media_drm.open_session);
  AMC_CHK (jsession_id);

  // For all other systemids it's "video/mp4" or "audio/mp4"
  jmime = (*env)->NewStringUTF (env, "cenc");
  AMC_CHK (jmime);

  J_CALL_OBJ (request /* = */ , media_drm_obj, media_drm.get_key_request,
      jsession_id, jinit_data, jmime, KEY_TYPE_STREAMING, NULL);
  AMC_CHK (request);

  {
    /* getKeyRequest */
    jbyteArray jreq_data;
    jsize req_data_len;
    gchar *req_data_utf8;       // leaks
    jstring jdef_url;
    J_CALL_OBJ (jdef_url /* = */ , request,
        media_drm_key_request.get_default_url);

    def_url = gst_amc_get_string_utf8 (env, jdef_url);
    AMC_CHK (def_url);
    GST_ERROR ("### default url is: [%s]", def_url);

    J_CALL_OBJ (jreq_data /* = */ , request, media_drm_key_request.get_data);
    AMC_CHK (jreq_data);

    req_data_len = (*env)->GetArrayLength (env, jreq_data);
    J_EXCEPTION_CHECK ("GetArrayLength");
    GST_ERROR ("### req_data_len = %d", req_data_len);

    req_data_utf8 = g_malloc0 (req_data_len + 1);
    (*env)->GetByteArrayRegion (env, jreq_data, 0, req_data_len,
        (jbyte *) req_data_utf8);
    J_EXCEPTION_CHECK ("GetByteArrayRegion");

    gst_amc_log_big ("req", req_data_utf8, req_data_len);

    {
      /* ProvideKeyResponse */
      size_t key_response_size = 0;
      jbyteArray jkey_response;
      jobject tmp;
      gchar *key_response = NULL;       // leak

      // FIXME: if clearkey --> reencode request and response base64/base64url

      if (!gst_amc_curl_post_request (def_url, req_data_utf8, req_data_len,
              &key_response, &key_response_size)) {
        GST_ERROR ("Could not post key request to url <%s>", def_url);
        goto error;
      }

      gst_amc_log_big ("resp", key_response, key_response_size);

      jkey_response =
          jbyte_arr_from_data (env, (guchar *) key_response, key_response_size);
      AMC_CHK (jkey_response);

      J_CALL_OBJ (tmp /* = */ , media_drm_obj,
          media_drm.provide_key_response, jsession_id, jkey_response);

      J_DELETE_LOCAL_REF (tmp);
      J_DELETE_LOCAL_REF (jkey_response);
      g_free (key_response);    // <--- move to errlabel
    }
  }
  /* Create MediaCrypto object */
  media_crypto_obj = (*env)->NewObject (env, media_crypto.klass,
      media_crypto.constructor, juuid, jsession_id);
  AMC_CHK (media_crypto_obj);

  /* Will be unreffed in free_format func */
  crypto_ctx->mdrm = (*env)->NewGlobalRef (env, media_drm_obj);
  crypto_ctx->mcrypto = (*env)->NewGlobalRef (env, media_crypto_obj);
  crypto_ctx->mdrm_session_id = (*env)->NewGlobalRef (env, jsession_id);

  AMC_CHK (crypto_ctx->mdrm && crypto_ctx->mcrypto
      && crypto_ctx->mdrm_session_id);
error:
  J_DELETE_LOCAL_REF (juuid);
  J_DELETE_LOCAL_REF (jinit_data);
  g_free (def_url);

  return crypto_ctx->mdrm && crypto_ctx->mcrypto && crypto_ctx->mdrm_session_id;
}


GstAmcCodec *
gst_amc_codec_new (const gchar * name)
{
  GstAmcCodec *codec = NULL;
  jstring name_str = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (name);

  name_str = (*env)->NewStringUTF (env, name);
  AMC_CHK (name_str);

  J_CALL_STATIC_OBJ (object /* = */ , media_codec, create_by_codec_name,
      name_str);

  codec = g_slice_new0 (GstAmcCodec);
  codec->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (codec->object);

done:
  J_DELETE_LOCAL_REF (object);
  J_DELETE_LOCAL_REF (name_str);
  return codec;
error:
  if (codec)
    g_slice_free (GstAmcCodec, codec);
  codec = NULL;
  goto done;
}

void
gst_amc_codec_free (GstAmcCodec * codec, GstAmcCrypto * crypto_ctx)
{
  JNIEnv *env = gst_jni_get_env ();
  g_return_if_fail (codec != NULL);

  env = gst_jni_get_env ();

  if (crypto_ctx) {
    if (crypto_ctx->mdrm) {
      // If we have mdrm, we think that the mcrypto is created by us, not the user
      J_DELETE_GLOBAL_REF (crypto_ctx->mcrypto);
      if (crypto_ctx->mdrm_session_id) {
        J_CALL_VOID (crypto_ctx->mdrm, media_drm.close_session,
            crypto_ctx->mdrm_session_id);
      }
    error:                     // <-- to resolve J_CALL_VOID
      J_DELETE_GLOBAL_REF (crypto_ctx->mdrm_session_id);
      J_DELETE_GLOBAL_REF (crypto_ctx->mdrm);
    }

    memset (crypto_ctx, 0, sizeof (GstAmcCrypto));
  }

  J_DELETE_GLOBAL_REF (codec->object);
  g_slice_free (GstAmcCodec, codec);
}

jmethodID
gst_amc_codec_get_release_ts_method_id (GstAmcCodec * codec)
{
  return media_codec.release_output_buffer_ts;
}

jmethodID
gst_amc_codec_get_release_method_id (GstAmcCodec * codec)
{
  return media_codec.release_output_buffer;
}


gboolean
gst_amc_codec_enable_adaptive_playback (GstAmcCodec * codec,
    GstAmcFormat * format)
{
  gboolean adaptivePlaybackSupported = FALSE;
  jclass codec_info_class = NULL;
  jobject capabilities = NULL;
  jmethodID get_codec_info_id;
  jmethodID get_capabilities_for_type_id;
  jmethodID is_feature_supported_id;
  char *mime = NULL;
  jclass capabilities_class = NULL;
  jstring jtmpstr = NULL;

  JNIEnv *env = gst_jni_get_env ();
  get_codec_info_id =
      (*env)->GetStaticMethodID (env, codec->object, "getCodecInfo",
      "()Landroid/media/MediaCodecInfo;");
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get get_codec_info_id method");
    goto error;
  }

  jobject codec_info =
      (*env)->CallStaticObjectMethod (env, codec->object, get_codec_info_id);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get MediaCodecInfo from codec");
    goto error;
  }

  codec_info_class = (*env)->GetObjectClass (env, codec_info);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec_info_class class");
    goto error;
  }

  if (!gst_amc_format_get_string (format, "mime", &mime)) {
    GST_ERROR ("Can't read mime from codec format");
    goto error;
  }

  get_capabilities_for_type_id =
      (*env)->GetMethodID (env, codec_info_class, "getCapabilitiesForType",
      "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");

  capabilities =
      (*env)->CallObjectMethod (env, codec_info, get_capabilities_for_type_id,
      mime);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get capabilities for %s", mime);
    goto error;
  }

  capabilities_class = (*env)->GetObjectClass (env, capabilities);
  if (!capabilities_class) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get capabilities class");
    goto error;
  }

  is_feature_supported_id =
      (*env)->GetMethodID (env, capabilities_class, "isFeatureSupported",
      "(Ljava/lang/String;)Z");
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get isFeatureSupported method");
    goto error;
  }

  jtmpstr = (*env)->NewStringUTF (env, "adaptive-playback");
  adaptivePlaybackSupported =
      (*env)->CallBooleanMethod (env, capabilities, is_feature_supported_id,
      jtmpstr);

  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Caught exception on quering if adaptive-playback is supported");
    (*env)->ExceptionClear (env);
    goto error;
  }

  GST_ERROR ("&&& Codec %s: adaptive-playback %ssupported", mime,
      adaptivePlaybackSupported ? "" : "not ");

  if (adaptivePlaybackSupported) {
    int width = 0;
    int height = 0;
    gst_amc_format_get_int (format, "width", &width);
    gst_amc_format_get_int (format, "height", &height);
    GST_ERROR ("TEST WIDTH=%d HEIGHT =%d", width, height);

    GST_ERROR ("Setting max-width = %d max-height = %d", width, height);
    gst_amc_format_set_int (format, "max-width", width);
    gst_amc_format_set_int (format, "max-height", height);
    gst_amc_format_set_int (format, "adaptive-playback", 1);
  }

error:
  if (jtmpstr != NULL)
    J_DELETE_LOCAL_REF (jtmpstr);
  g_free (mime);
  return adaptivePlaybackSupported;
}

gboolean
gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format,
    guint8 * surface, jobject mcrypto_obj, gint flags)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (codec && format);

  if (mcrypto_obj) {
    GST_ERROR ("{{{ configuring with MCrypto [%p]", mcrypto_obj);
    AMC_CHK ((*env)->IsInstanceOf (env, mcrypto_obj, media_crypto.klass));
  }
/*
  jclass tmp;
  jmethodID set_integer_id;
  jmethodID get_integer_id;
  jclass tmp = (*env)->FindClass (env, "android/media/MediaFormat");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format class");
    goto error;
  }

  get_integer_id =
      (*env)->GetMethodID (env, tmp, "getInteger",
      "(Ljava/lang/String;)I");

  set_integer_id =
      (*env)->GetMethodID (env, tmp, "setInteger",
      "(Ljava/lang/String;I)V");
      
  format.setInteger(MediaFormat.KEY_MAX_WIDTH, 1920);
  format.setInteger(MediaFormat.KEY_MAX_HEIGHT, 1080);
*/

/*

  MediaCodecInfo info = aCodec.getCodecInfo();
  MediaCodecInfo.CodecCapabilities capabilities = info.getCapabilitiesForType(aMimeType);
  return capabilities != null &&
          capabilities.isFeatureSupported(
              MediaCodecInfo.CodecCapabilities.FEATURE_AdaptivePlayback);
*/
  gst_amc_codec_enable_adaptive_playback (codec, format);
  J_CALL_VOID (codec->object, media_codec.configure,
      format->object, surface, mcrypto_obj, flags);
  ret = TRUE;
error:
  return ret;
}

GstAmcFormat *
gst_amc_codec_get_output_format (GstAmcCodec * codec)
{
  GstAmcFormat *ret = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (codec);

  J_CALL_OBJ (object /* = */ , codec->object, media_codec.get_output_format);

  ret = g_slice_new0 (GstAmcFormat);
  ret->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (ret->object);
done:
  J_DELETE_LOCAL_REF (object);
  return ret;
error:
  if (ret)
    g_slice_free (GstAmcFormat, ret);
  ret = NULL;
  goto done;

}

gboolean
gst_amc_codec_start (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec);
  J_CALL_VOID (codec->object, media_codec.start);
  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_stop (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec);
  J_CALL_VOID (codec->object, media_codec.stop);
  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_flush (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec);
  J_CALL_VOID (codec->object, media_codec.flush);
  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_release (GstAmcCodec * codec)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec);
  J_CALL_VOID (codec->object, media_codec.release);
  ret = TRUE;
error:
  return ret;
}

void
gst_amc_codec_free_buffers (GstAmcBuffer * buffers, gsize n_buffers)
{
  if (buffers) {
    JNIEnv *env = gst_jni_get_env ();
    jsize i;
    for (i = 0; i < n_buffers; i++) {
      J_DELETE_GLOBAL_REF (buffers[i].object);
    }
    g_free (buffers);
  }
}

GstAmcBuffer *
gst_amc_codec_get_output_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  jobject output_buffers = NULL;
  jsize n_output_buffers = 0;
  GstAmcBuffer *ret = NULL;
  jsize i;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (codec && n_buffers);
  *n_buffers = 0;

  J_CALL_OBJ (output_buffers /* = */ , codec->object,
      media_codec.get_output_buffers);
  AMC_CHK (output_buffers);

  n_output_buffers = (*env)->GetArrayLength (env, output_buffers);
  J_EXCEPTION_CHECK ("(get output buffers array length)");
  AMC_CHK (n_output_buffers);

  ret = g_new0 (GstAmcBuffer, n_output_buffers);

  for (i = 0; i < n_output_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, output_buffers, i);
    AMC_CHK (buffer);

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    J_DELETE_LOCAL_REF (buffer);
    AMC_CHK (ret[i].object);

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    AMC_CHK (ret[i].data);
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

  *n_buffers = n_output_buffers;
  GST_DEBUG ("Created %d", *n_buffers);
done:
  J_DELETE_LOCAL_REF (output_buffers);
  return ret;
error:
  gst_amc_codec_free_buffers (ret, n_output_buffers);
  ret = NULL;
  goto done;
}

GstAmcBuffer *
gst_amc_codec_get_input_buffers (GstAmcCodec * codec, gsize * n_buffers)
{
  jobject input_buffers = NULL;
  jsize n_input_buffers = 0;
  GstAmcBuffer *ret = NULL;
  jsize i;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (codec && n_buffers);

  *n_buffers = 0;

  J_CALL_OBJ (input_buffers /* = */ , codec->object,
      media_codec.get_input_buffers);
  AMC_CHK (input_buffers);

  n_input_buffers = (*env)->GetArrayLength (env, input_buffers);
  J_EXCEPTION_CHECK ("(get input buffers array length)");

  ret = g_new0 (GstAmcBuffer, n_input_buffers);

  for (i = 0; i < n_input_buffers; i++) {
    jobject buffer = NULL;

    buffer = (*env)->GetObjectArrayElement (env, input_buffers, i);
    AMC_CHK (buffer);

    ret[i].object = (*env)->NewGlobalRef (env, buffer);
    J_DELETE_LOCAL_REF (buffer);
    AMC_CHK (ret[i].object);

    ret[i].data = (*env)->GetDirectBufferAddress (env, ret[i].object);
    AMC_CHK (ret[i].data);
    ret[i].size = (*env)->GetDirectBufferCapacity (env, ret[i].object);
  }

  *n_buffers = n_input_buffers;
done:
  J_DELETE_LOCAL_REF (input_buffers);
  return ret;
error:
  gst_amc_codec_free_buffers (ret, n_input_buffers);
  ret = NULL;
  goto done;
}

gint
gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs)
{
  gint ret = G_MININT;
  JNIEnv *env = gst_jni_get_env ();
  g_return_val_if_fail (codec != NULL, G_MININT);

  J_CALL_INT (ret /* = */ , codec->object, media_codec.dequeue_input_buffer,
      timeoutUs);
error:
  return ret;
}

static gboolean
gst_amc_codec_fill_buffer_info (JNIEnv * env, jobject buffer_info,
    GstAmcBufferInfo * info)
{
  AMC_CHK (buffer_info);

  info->flags =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.flags);
  J_EXCEPTION_CHECK ("(get buffer info field <flags>)");

  info->offset =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.offset);
  J_EXCEPTION_CHECK ("(get buffer info field <offset>)");

  info->presentation_time_us =
      (*env)->GetLongField (env, buffer_info,
      media_codec_buffer_info.presentation_time_us);
  J_EXCEPTION_CHECK ("(get buffer info field <presentation_time_us>)");

  info->size =
      (*env)->GetIntField (env, buffer_info, media_codec_buffer_info.size);
  J_EXCEPTION_CHECK ("(get buffer info field <size>)");

  return TRUE;
error:
  return FALSE;
}

gint
gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec,
    GstAmcBufferInfo * info, gint64 timeoutUs)
{
  JNIEnv *env;
  gint ret = G_MININT, buf_idx;
  jobject info_o = NULL;

  g_return_val_if_fail (codec != NULL, G_MININT);

  env = gst_jni_get_env ();

  info_o =
      (*env)->NewObject (env, media_codec_buffer_info.klass,
      media_codec_buffer_info.constructor);
  AMC_CHK (info_o);

  J_CALL_INT (buf_idx /* = */ , codec->object,
      media_codec.dequeue_output_buffer, info_o, timeoutUs);

  AMC_CHK (gst_amc_codec_fill_buffer_info (env, info_o, info));

  ret = buf_idx;
error:
  J_DELETE_LOCAL_REF (info_o);
  return ret;
}

gboolean
gst_amc_codec_queue_secure_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info, const GstBuffer * drmbuf)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();
  jobject crypto_info = NULL;
  const GstStructure *cenc_info;

  if (!fluc_drm_is_buffer (drmbuf)) {
    GST_ERROR ("DRM Buffer not found");
    return FALSE;
  }

  cenc_info = fluc_drm_buffer_find_by_name (drmbuf, "application/x-cenc");
  if (!cenc_info) {
    GST_ERROR ("cenc structure not found in drmbuffer");
    return FALSE;
  }

  crypto_info = gst_amc_get_crypto_info (cenc_info, GST_BUFFER_SIZE (drmbuf));
  if (!crypto_info) {
    GST_ERROR
        ("Couldn't create MediaCodec.CryptoInfo object or parse cenc structure");
    return FALSE;
  }
  // queueSecureInputBuffer
  GST_ERROR (";;;; Calling queue_secure_input_buffer, bufsize = %d",
      GST_BUFFER_SIZE (drmbuf));
  (*env)->CallVoidMethod (env, codec->object,
      media_codec.queue_secure_input_buffer, index, info->offset, crypto_info,
      info->presentation_time_us, info->flags);

  if (G_UNLIKELY ((*env)->ExceptionCheck (env))) {
    jthrowable ex = (*env)->ExceptionOccurred (env);
    GST_ERROR
        ("Caught exception on call media_codec.queue_secure_input_buffer");
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
          goto printed;                                                 \
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
  printed:
    goto error;
  }

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (crypto_info);
  return ret;
}


gboolean
gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  J_CALL_VOID (codec->object, media_codec.queue_input_buffer,
      index, info->offset, info->size, info->presentation_time_us, info->flags);

  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  J_CALL_VOID (codec->object, media_codec.release_output_buffer,
      index, JNI_FALSE);

  ret = TRUE;
error:
  return ret;
}

gboolean
gst_amc_codec_render_output_buffer (GstAmcCodec * codec, gint index,
    GstClockTime ts)
{
  gboolean ret = FALSE;
  JNIEnv *env = gst_jni_get_env ();

  J_CALL_VOID (codec->object, media_codec.release_output_buffer_ts, index, ts);

  ret = TRUE;
error:
  return ret;
}

GstAmcFormat *
gst_amc_format_new_audio (const gchar * mime, gint sample_rate, gint channels)
{
  GstAmcFormat *format = NULL;
  jstring mime_str = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (mime);

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_slice_new0 (GstAmcFormat);
  J_CALL_STATIC_OBJ (object /* = */ , media_format, create_audio_format,
      mime_str, sample_rate, channels);
  AMC_CHK (object);

  format->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (format->object);

done:
  J_DELETE_LOCAL_REF (object);
  J_DELETE_LOCAL_REF (mime_str);
  return format;

error:
  GST_ERROR ("Failed to create format '%s'", mime ? mime : "NULL");
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

GstAmcFormat *
gst_amc_format_new_video (const gchar * mime, gint width, gint height)
{
  GstAmcFormat *format = NULL;
  jstring mime_str = NULL;
  jobject object = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (mime && width && height);

  mime_str = (*env)->NewStringUTF (env, mime);
  if (mime_str == NULL)
    goto error;

  format = g_slice_new0 (GstAmcFormat);

  J_CALL_STATIC_OBJ (object /* = */ , media_format, create_video_format,
      mime_str, width, height);

  AMC_CHK (object);
  format->object = (*env)->NewGlobalRef (env, object);
  AMC_CHK (format->object);

  {
    /*
       gboolean adaptivePlaybackSupported = false;
       jclass codec_info_class = NULL;
       jmethodID get_codec_info_id;
       get_codec_info_id =
       (*env)->GetStaticMethodID (env, codec.object, "getCodecInfo",
       "()Landroid/media/MediaCodecInfo;");

       jobject codec_info =
       (*env)->CallStaticObjectMethod (env, codec_list_class,
       get_codec_info_id);

       codec_info_class = (*env)->GetObjectClass (env, codec_info);

       get_capabilities_for_type_id =
       (*env)->GetMethodID (env, codec_info_class, "getCapabilitiesForType",
       "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");

       is_feature_supported_id =
       (*env)->GetMethodID (env, capabilities_class, "isFeatureSupported",
       "(Ljava/lang/String;)Z");

       if (is_feature_supported_id)
       {
       jstring jtmpstr;

       jtmpstr = (*env)->NewStringUTF (env, "adaptive-playback");
       adaptivePlaybackSupported =
       (*env)->CallBooleanMethod (env, capabilities, is_feature_supported_id,
       jtmpstr);
       if ((*env)->ExceptionCheck (env)) {
       GST_ERROR
       ("Caught exception on quering if adaptive-playback is supported");
       (*env)->ExceptionClear (env);
       }
       J_DELETE_LOCAL_REF (jtmpstr);
       GST_ERROR
       ("&&& Codec %s: adaptive-playback %ssupported",
       name_str, adaptivePlaybackSupported ? "" : "not " );
       }else
       {
       GST_ERROR ("&&& isFeatureSupported not found " );
       }

       if (adaptivePlaybackSupported)
     */
    {
      GST_ERROR ("Setting max-width = %d max-height = %d", width, height);
      gst_amc_format_set_int (format, "max-width", width);
      gst_amc_format_set_int (format, "max-height", height);
      gst_amc_format_set_int (format, "adaptive-playback", 1);
    }
  }


done:
  J_DELETE_LOCAL_REF (object);
  J_DELETE_LOCAL_REF (mime_str);
  return format;

error:
  GST_ERROR ("Failed to create format '%s',"
      " width = %d, height = %d", mime ? mime : "NULL", width, height);
  if (format)
    g_slice_free (GstAmcFormat, format);
  format = NULL;
  goto done;
}

void
gst_amc_format_free (GstAmcFormat * format)
{
  JNIEnv *env = gst_jni_get_env ();
  g_return_if_fail (format != NULL);

  J_DELETE_GLOBAL_REF (format->object);
  g_slice_free (GstAmcFormat, format);
}


gchar *
gst_amc_format_to_string (GstAmcFormat * format)
{
  jstring v_str = NULL;
  gchar *ret = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format);

  J_CALL_OBJ (v_str /* = */ , format->object, media_format.to_string);
  ret = gst_amc_get_string_utf8 (env, v_str);
error:
  J_DELETE_LOCAL_REF (v_str);
  return ret;
}

gboolean
gst_amc_format_contains_key (GstAmcFormat * format, const gchar * key)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_BOOL (ret /* = */ , format->object, media_format.contains_key,
      key_str);
error:
  J_DELETE_LOCAL_REF (key_str);
  return ret;
}

gboolean
gst_amc_format_get_float (GstAmcFormat * format, const gchar * key,
    gfloat * value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_FLOAT (*value /* = */ , format->object, media_format.get_float,
      key_str);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  return ret;
}

void
gst_amc_format_set_float (GstAmcFormat * format, const gchar * key,
    gfloat value)
{
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_VOID (format->object, media_format.set_float, key_str, value);
error:
  J_DELETE_LOCAL_REF (key_str);
}

gboolean
gst_amc_format_get_int (const GstAmcFormat * format, const gchar * key,
    gint * value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_INT (*value /* = */ , format->object, media_format.get_integer,
      key_str);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  return ret;
}

void
gst_amc_format_set_int (GstAmcFormat * format, const gchar * key, gint value)
{
  jstring key_str = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_VOID (format->object, media_format.set_integer, key_str, value);
error:
  J_DELETE_LOCAL_REF (key_str);
}

gboolean
gst_amc_format_get_string (GstAmcFormat * format, const gchar * key,
    gchar ** value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jstring v_str = NULL;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (format && key && value);
  *value = NULL;

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_OBJ (v_str /* = */ , format->object, media_format.get_string, key_str);

  *value = gst_amc_get_string_utf8 (env, v_str);
  AMC_CHK (*value);
  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  J_DELETE_LOCAL_REF (v_str);
  return ret;
}

void
gst_amc_format_set_string (GstAmcFormat * format, const gchar * key,
    const gchar * value)
{
  jstring key_str = NULL;
  jstring v_str = NULL;
  JNIEnv *env = gst_jni_get_env ();

  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  v_str = (*env)->NewStringUTF (env, value);
  AMC_CHK (v_str);

  J_CALL_VOID (format->object, media_format.set_string, key_str, v_str);
error:
  J_DELETE_LOCAL_REF (key_str);
  J_DELETE_LOCAL_REF (v_str);
}

gboolean
gst_amc_format_get_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer ** value)
{
  gboolean ret = FALSE;
  jstring key_str = NULL;
  jobject v = NULL;
  guint8 *data;
  gsize size;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  *value = NULL;

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  J_CALL_OBJ (v /* = */ , format->object, media_format.get_byte_buffer,
      key_str);

  data = (*env)->GetDirectBufferAddress (env, v);
  AMC_CHK (data);

  size = (*env)->GetDirectBufferCapacity (env, v);
  *value = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (*value), data, size);

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (key_str);
  J_DELETE_LOCAL_REF (v);
  return ret;
}

void
gst_amc_format_set_buffer (GstAmcFormat * format, const gchar * key,
    GstBuffer * value)
{
  jstring key_str = NULL;
  jobject v = NULL;
  JNIEnv *env = gst_jni_get_env ();
  AMC_CHK (format && key && value);

  key_str = (*env)->NewStringUTF (env, key);
  AMC_CHK (key_str);

  /* FIXME: The buffer must remain valid until the codec is stopped */
  v = (*env)->NewDirectByteBuffer (env, GST_BUFFER_DATA (value),
      GST_BUFFER_SIZE (value));
  AMC_CHK (v);

  J_CALL_VOID (format->object, media_format.set_byte_buffer, key_str, v);
error:
  J_DELETE_LOCAL_REF (key_str);
  J_DELETE_LOCAL_REF (v);
}


static jclass
j_find_class (JNIEnv * env, const gchar * desc)
{
  jclass ret = NULL;
  jclass tmp = (*env)->FindClass (env, desc);
  AMC_CHK (tmp);

  ret = (*env)->NewGlobalRef (env, tmp);
  AMC_CHK (ret);
error:
  J_DELETE_LOCAL_REF (tmp);
  return ret;
}


static gboolean
get_java_classes (void)
{
  gboolean ret = TRUE;
  JNIEnv *env;
  jclass tmp;

  GST_DEBUG ("Retrieving Java classes");

  env = gst_jni_get_env ();

  tmp = (*env)->FindClass (env, "java/lang/String");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get string class");
    goto done;
  }
  java_string.klass = (*env)->NewGlobalRef (env, tmp);
  if (!java_string.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get string class global reference");
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  java_string.constructor =
      (*env)->GetMethodID (env, java_string.klass, "<init>", "([C)V");
  if (!java_string.constructor) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get string methods");
    goto done;
  }

  tmp = (*env)->FindClass (env, "android/media/MediaCodec");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec class");
    goto done;
  }
  media_codec.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_codec.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec class global reference");
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_codec.CRYPTO_MODE_AES_CTR = 1;  // this constant is taken from Android docs webpage
  media_codec.queue_secure_input_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "queueSecureInputBuffer",
      "(IILandroid/media/MediaCodec$CryptoInfo;JI)V");

  media_codec.create_by_codec_name =
      (*env)->GetStaticMethodID (env, media_codec.klass, "createByCodecName",
      "(Ljava/lang/String;)Landroid/media/MediaCodec;");
  media_codec.configure =
      (*env)->GetMethodID (env, media_codec.klass, "configure",
      "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");
  media_codec.dequeue_input_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "dequeueInputBuffer",
      "(J)I");
  media_codec.dequeue_output_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "dequeueOutputBuffer",
      "(Landroid/media/MediaCodec$BufferInfo;J)I");
  media_codec.flush =
      (*env)->GetMethodID (env, media_codec.klass, "flush", "()V");
  media_codec.get_input_buffers =
      (*env)->GetMethodID (env, media_codec.klass, "getInputBuffers",
      "()[Ljava/nio/ByteBuffer;");
  media_codec.get_output_buffers =
      (*env)->GetMethodID (env, media_codec.klass, "getOutputBuffers",
      "()[Ljava/nio/ByteBuffer;");
  media_codec.get_output_format =
      (*env)->GetMethodID (env, media_codec.klass, "getOutputFormat",
      "()Landroid/media/MediaFormat;");
  media_codec.queue_input_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "queueInputBuffer",
      "(IIIJI)V");
  media_codec.release =
      (*env)->GetMethodID (env, media_codec.klass, "release", "()V");
  media_codec.release_output_buffer =
      (*env)->GetMethodID (env, media_codec.klass, "releaseOutputBuffer",
      "(IZ)V");
  media_codec.release_output_buffer_ts =
      (*env)->GetMethodID (env, media_codec.klass, "releaseOutputBuffer",
      "(IJ)V");
  media_codec.set_output_surface =
      (*env)->GetMethodID (env, media_codec.klass, "setOutputSurface",
      "(Landroid/view/Surface;)V");
  media_codec.start =
      (*env)->GetMethodID (env, media_codec.klass, "start", "()V");
  media_codec.stop =
      (*env)->GetMethodID (env, media_codec.klass, "stop", "()V");

  AMC_CHK (media_codec.queue_secure_input_buffer &&
      media_codec.configure &&
      media_codec.create_by_codec_name &&
      media_codec.dequeue_input_buffer &&
      media_codec.dequeue_output_buffer &&
      media_codec.flush &&
      media_codec.get_input_buffers &&
      media_codec.get_output_buffers &&
      media_codec.get_output_format &&
      media_codec.queue_input_buffer &&
      media_codec.release &&
      media_codec.release_output_buffer &&
      media_codec.release_output_buffer_ts &&
      media_codec.set_output_surface && media_codec.start && media_codec.stop);

  tmp = (*env)->FindClass (env, "android/media/MediaCodec$BufferInfo");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec buffer info class");
    goto done;
  }
  media_codec_buffer_info.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_codec_buffer_info.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get codec buffer info class global reference");
    goto done;
  }
  (*env)->DeleteLocalRef (env, tmp);
  tmp = NULL;

  media_codec_buffer_info.constructor =
      (*env)->GetMethodID (env, media_codec_buffer_info.klass, "<init>", "()V");
  media_codec_buffer_info.flags =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "flags", "I");
  media_codec_buffer_info.offset =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "offset", "I");
  media_codec_buffer_info.presentation_time_us =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass,
      "presentationTimeUs", "J");
  media_codec_buffer_info.size =
      (*env)->GetFieldID (env, media_codec_buffer_info.klass, "size", "I");
  if (!media_codec_buffer_info.constructor || !media_codec_buffer_info.flags
      || !media_codec_buffer_info.offset
      || !media_codec_buffer_info.presentation_time_us
      || !media_codec_buffer_info.size) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get buffer info methods and fields");
    goto done;
  }

  tmp = (*env)->FindClass (env, "android/media/MediaFormat");
  if (!tmp) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format class");
    goto done;
  }
  media_format.klass = (*env)->NewGlobalRef (env, tmp);
  if (!media_format.klass) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format class global reference");
    goto done;
  }

  media_format.create_audio_format =
      (*env)->GetStaticMethodID (env, media_format.klass, "createAudioFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  media_format.create_video_format =
      (*env)->GetStaticMethodID (env, media_format.klass, "createVideoFormat",
      "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
  media_format.to_string =
      (*env)->GetMethodID (env, media_format.klass, "toString",
      "()Ljava/lang/String;");
  media_format.contains_key =
      (*env)->GetMethodID (env, media_format.klass, "containsKey",
      "(Ljava/lang/String;)Z");
  media_format.get_float =
      (*env)->GetMethodID (env, media_format.klass, "getFloat",
      "(Ljava/lang/String;)F");
  media_format.set_float =
      (*env)->GetMethodID (env, media_format.klass, "setFloat",
      "(Ljava/lang/String;F)V");
  media_format.get_integer =
      (*env)->GetMethodID (env, media_format.klass, "getInteger",
      "(Ljava/lang/String;)I");
  media_format.set_integer =
      (*env)->GetMethodID (env, media_format.klass, "setInteger",
      "(Ljava/lang/String;I)V");
  media_format.get_string =
      (*env)->GetMethodID (env, media_format.klass, "getString",
      "(Ljava/lang/String;)Ljava/lang/String;");
  media_format.set_string =
      (*env)->GetMethodID (env, media_format.klass, "setString",
      "(Ljava/lang/String;Ljava/lang/String;)V");
  media_format.get_byte_buffer =
      (*env)->GetMethodID (env, media_format.klass, "getByteBuffer",
      "(Ljava/lang/String;)Ljava/nio/ByteBuffer;");
  media_format.set_byte_buffer =
      (*env)->GetMethodID (env, media_format.klass, "setByteBuffer",
      "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");
  if (!media_format.create_audio_format || !media_format.create_video_format
      || !media_format.contains_key || !media_format.get_float
      || !media_format.set_float || !media_format.get_integer
      || !media_format.set_integer || !media_format.get_string
      || !media_format.set_string || !media_format.get_byte_buffer
      || !media_format.set_byte_buffer) {
    ret = FALSE;
    (*env)->ExceptionClear (env);
    GST_ERROR ("Failed to get format methods");
    goto done;
  }

  /* MEDIA DRM */
  media_drm.klass = j_find_class (env, "android/media/MediaDrm");
  if (!media_drm.klass)
    goto error;
  J_INIT_METHOD_ID (media_drm, constructor, "<init>", "(Ljava/util/UUID;)V");
  J_INIT_METHOD_ID (media_drm, open_session, "openSession", "()[B");
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

  /* ==================================== MediaDrm.KeyRequest */
  media_drm_key_request.klass =
      j_find_class (env, "android/media/MediaDrm$KeyRequest");
  if (!media_drm_key_request.klass)
    goto error;
  J_INIT_METHOD_ID (media_drm_key_request, get_default_url, "getDefaultUrl",
      "()Ljava/lang/String;");
  J_INIT_METHOD_ID (media_drm_key_request, get_data, "getData", "()[B");

  /* ==================================== CryptoInfo       */

  media_codec_crypto_info.klass =
      j_find_class (env, "android/media/MediaCodec$CryptoInfo");
  if (!media_codec_crypto_info.klass)
    goto error;
  J_INIT_METHOD_ID (media_codec_crypto_info, constructor, "<init>", "()V");
  J_INIT_METHOD_ID (media_codec_crypto_info, set, "set", "(I[I[I[B[BI)V");

  /* ==================================== CryptoException   */
  crypto_exception.klass =
      j_find_class (env, "android/media/MediaCodec$CryptoException");
  if (!crypto_exception.klass)
    goto error;
  J_INIT_METHOD_ID (crypto_exception, get_error_code, "getErrorCode", "()I");

  /* ==================================== Media Crypto     */
  media_crypto.klass = j_find_class (env, "android/media/MediaCrypto");
  if (!media_crypto.klass)
    goto error;
  J_INIT_STATIC_METHOD_ID (media_crypto, is_crypto_scheme_supported,
      "isCryptoSchemeSupported", "(Ljava/util/UUID;)Z");

  J_INIT_METHOD_ID (media_crypto, set_media_drm_session, "setMediaDrmSession",
      "([B)V");
  J_INIT_METHOD_ID (media_crypto, constructor, "<init>",
      "(Ljava/util/UUID;[B)V");
  /* ====================================== UUID          */
  uuid.klass = j_find_class (env, "java/util/UUID");
  if (!uuid.klass)
    goto error;
  J_INIT_STATIC_METHOD_ID (uuid, from_string, "fromString",
      "(Ljava/lang/String;)Ljava/util/UUID;");
  /* ======================================               */

done:
  J_DELETE_LOCAL_REF (tmp);
  return ret;
error:
  ret = FALSE;
  goto done;
}

#ifdef GST_PLUGIN_BUILD_STATIC
static gchar *
get_cache_file (void)
{
  const gchar *cache_dir;
  gchar *cache_file = NULL;

  cache_dir = g_getenv ("XDG_CACHE_HOME");
  if (cache_dir) {
    cache_file = g_build_filename (cache_dir, "amccodecs.bin", NULL);
  }

  return cache_file;
}
#endif

static const GstStructure *
load_codecs (GstPlugin * plugin)
{
#ifdef GST_PLUGIN_BUILD_STATIC
  const GstStructure *cache_data = NULL;
  gchar *cache_file = get_cache_file ();
  if (cache_file) {
    gchar *cache_contents;

    g_file_get_contents (cache_file, &cache_contents, NULL, NULL);
    if (cache_contents) {
      cache_data = gst_structure_from_string (cache_contents, NULL);
      g_free (cache_contents);
    }
    g_free (cache_file);
  }
  return cache_data;
#else
  return gst_plugin_get_cache_data (plugin);
#endif
}

static void
save_codecs (GstPlugin * plugin, GstStructure * cache_data)
{
#ifdef GST_PLUGIN_BUILD_STATIC
  gchar *cache_file;

  cache_file = get_cache_file ();
  if (cache_file) {
    gchar *cache_contents;

    cache_contents = gst_structure_to_string (cache_data);
    g_file_set_contents (cache_file, cache_contents, -1, NULL);
    g_free (cache_contents);
  }
  gst_structure_free (cache_data);
#else
  gst_plugin_set_cache_data (plugin, cache_data);
#endif
}


//  "69f908af-4816-46ea-910c-cd5dcccb0a3a", "PSSH"}, {
//  "e2719d58-a985-b3c9-781a-b030af78d30e", "CENC"}, {
//  "5e629af5-38da-4063-8977-97ffbd9902d4", "MPD"}

static struct
{
  const char *uuid;
  const char *name;
  gboolean supported;
} known_cryptos[] = {
  {
    // clearkey should be the first, it's used in sysid_is_clearkey
  "1077efec-c0b2-4d02-ace3-3c1e52e2fb4b", "CLEARKEY"}, {
  "9a04f079-9840-4286-ab92-e65be0885f95", "PLAYREADY_BE"}, {
  "79f0049a-4098-8642-ab92-e65be0885f95", "PLAYREADY"}
};

static gboolean cached_supported_system_ids = FALSE;


gboolean
sysid_is_clearkey (const gchar * sysid)
{
  return !g_ascii_strcasecmp (sysid, known_cryptos[0].uuid);
}


static const gchar *
detect_known_protection_name (const gchar * uuid_utf8,
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

jobject
juuid_from_utf8 (JNIEnv * env, const gchar * uuid_utf8)
{
  jobject juuid = NULL;
  jstring juuid_string = NULL;

  juuid_string = (*env)->NewStringUTF (env, uuid_utf8);
  AMC_CHK (juuid_string);

  J_CALL_STATIC_OBJ (juuid /* = */ , uuid, from_string, juuid_string);
  AMC_CHK (juuid);
error:
  J_DELETE_LOCAL_REF (juuid_string);
  return juuid;
}

jobject
gst_amc_global_ref_jobj (jobject obj)
{
  JNIEnv *env = gst_jni_get_env ();
  return (*env)->NewGlobalRef (env, obj);
}

gboolean
gst_amc_codec_set_output_surface (GstAmcCodec * codec, guint8 * surface)
{
  JNIEnv *env = gst_jni_get_env ();
  GST_DEBUG ("Set surface %p to codec %p", surface, codec->object);
  J_CALL_VOID (codec->object, media_codec.set_output_surface, surface);
  return TRUE;
error:
  GST_ERROR ("Failed to call MediaCodec.setOutputSurface (%p)", surface);
  return FALSE;
}

void
gst_amc_handle_drm_event (GstElement * self, GstEvent * event,
    GstAmcCrypto * crypto_ctx)
{
  GstBuffer *data_buf;
  const gchar *system_id, *origin;
  fluc_drm_event_parse (event, &system_id, &data_buf, &origin);
  GST_ERROR_OBJECT (self, "{{{ Received drm event."
      "SystemId = [%s] (%ssupported by device), origin = [%s], %s data buffer,"
      "data size = %d", system_id,
      is_protection_system_id_supported (system_id) ? "" : "not ",
      origin, data_buf ? "attached" : "no",
      data_buf ? GST_BUFFER_SIZE (data_buf) : 0);

  // Hack for now to be sure we're providing pssh
  if (!data_buf || !GST_BUFFER_SIZE (data_buf))
    return;

  if (g_str_has_prefix (origin, "isobmff/") && sysid_is_clearkey (system_id)) {
    gsize new_size;
    hack_pssh_initdata (GST_BUFFER_DATA (data_buf),
        GST_BUFFER_SIZE (data_buf), &new_size);
    GST_BUFFER_SIZE (data_buf) = new_size;
  }
#if 1                           // Disabled to test in-band
  gst_element_post_message (self,
      gst_message_new_element
      (GST_OBJECT (self),
          gst_structure_new ("prepare-drm-agent-handle",
              "init_data", GST_TYPE_BUFFER, data_buf, NULL)));
#endif

  if (crypto_ctx->mcrypto) {
    GST_ERROR_OBJECT (self, "{{{ Received from user MediaCrypto [%p]",
        crypto_ctx->mcrypto);
  } else {
    GST_ERROR_OBJECT (self,
        "{{{ User didn't provide us MediaCrypto, trying In-band mode");
    if (!jmedia_crypto_from_drm_event (event, crypto_ctx))
      GST_ERROR_OBJECT (self, "{{{ In-band mode's drm event parsing failed");
  }
}

gboolean
is_protection_system_id_supported (const gchar * uuid_utf8)
{
  jobject juuid = NULL;
  jboolean jis_supported = FALSE;
  JNIEnv *env = gst_jni_get_env ();
  gboolean cached_supported = FALSE;
  gboolean found = FALSE;
  const gchar *sysid_name =
      detect_known_protection_name (uuid_utf8, &cached_supported, &found);
  if (found) {
    jis_supported = cached_supported;
    goto error;                 // <-- not an error, but same label
  }

  juuid = juuid_from_utf8 (env, uuid_utf8);
  AMC_CHK (juuid);

  J_CALL_STATIC_BOOLEAN (jis_supported /* = */ , media_crypto,
      is_crypto_scheme_supported, juuid);
error:
  J_DELETE_LOCAL_REF (juuid);
  GST_ERROR ("Protection scheme %s (%s) is%s supported by device",
      sysid_name, uuid_utf8, jis_supported ? "" : " not");
  return jis_supported;
}


static void
log_known_supported_protection_schemes (void)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (known_cryptos); i++)
    known_cryptos[i].supported =
        is_protection_system_id_supported (known_cryptos[i].uuid);

  cached_supported_system_ids = TRUE;
}

static gboolean
scan_codecs (GstPlugin * plugin)
{
  gboolean ret = FALSE;
  JNIEnv *env;
  jclass codec_list_class = NULL;
  jmethodID get_codec_count_id, get_codec_info_at_id;
  jint codec_count, i;
  const GstStructure *cache_data;

  GST_DEBUG ("Scanning codecs");

  cache_data = load_codecs (plugin);
  if (cache_data) {
    const GValue *arr = gst_structure_get_value (cache_data, "codecs");
    guint i, n;

    GST_DEBUG ("Getting codecs from cache");
    n = gst_value_array_get_size (arr);
    for (i = 0; i < n; i++) {
      const GValue *cv = gst_value_array_get_value (arr, i);
      const GstStructure *cs = gst_value_get_structure (cv);
      const gchar *name;
      gboolean is_encoder;
      const GValue *starr;
      guint j, n2;
      GstAmcCodecInfo *gst_codec_info;

      gst_codec_info = g_new0 (GstAmcCodecInfo, 1);

      name = gst_structure_get_string (cs, "name");
      gst_structure_get_boolean (cs, "is-encoder", &is_encoder);
      gst_codec_info->name = g_strdup (name);
      gst_codec_info->is_encoder = is_encoder;

      starr = gst_structure_get_value (cs, "supported-types");
      n2 = gst_value_array_get_size (starr);

      gst_codec_info->n_supported_types = n2;
      gst_codec_info->supported_types = g_new0 (GstAmcCodecType, n2);

      for (j = 0; j < n2; j++) {
        const GValue *stv = gst_value_array_get_value (starr, j);
        const GstStructure *sts = gst_value_get_structure (stv);
        const gchar *mime;
        const GValue *cfarr;
        const GValue *plarr;
        guint k, n3;
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[j];

        mime = gst_structure_get_string (sts, "mime");
        gst_codec_type->mime = g_strdup (mime);

        //GST_ERROR ("&&& Found mime: %s", mime);

        cfarr = gst_structure_get_value (sts, "color-formats");
        n3 = gst_value_array_get_size (cfarr);

        gst_codec_type->n_color_formats = n3;
        gst_codec_type->color_formats = g_new0 (gint, n3);

        for (k = 0; k < n3; k++) {
          const GValue *cfv = gst_value_array_get_value (cfarr, k);
          gint cf = g_value_get_int (cfv);

          gst_codec_type->color_formats[k] = cf;
        }

        plarr = gst_structure_get_value (sts, "profile-levels");
        n3 = gst_value_array_get_size (plarr);

        gst_codec_type->n_profile_levels = n3;
        gst_codec_type->profile_levels =
            g_malloc0 (sizeof (gst_codec_type->profile_levels[0]) * n3);

        for (k = 0; k < n3; k++) {
          const GValue *plv = gst_value_array_get_value (plarr, k);
          const GValue *p, *l;

          p = gst_value_array_get_value (plv, 0);
          l = gst_value_array_get_value (plv, 1);
          gst_codec_type->profile_levels[k].profile = g_value_get_int (p);
          gst_codec_type->profile_levels[k].level = g_value_get_int (l);

          //GST_ERROR ("&&& Found levels for %s: prof = %d, lev = %d",
          //    mime, gst_codec_type->profile_levels[k].profile,
          //    gst_codec_type->profile_levels[k].level);
        }
      }

      codec_infos = g_list_append (codec_infos, gst_codec_info);
    }

    return TRUE;
  }

  env = gst_jni_get_env ();

  codec_list_class = (*env)->FindClass (env, "android/media/MediaCodecList");
  AMC_CHK (codec_list_class);

  get_codec_count_id =
      (*env)->GetStaticMethodID (env, codec_list_class, "getCodecCount", "()I");
  get_codec_info_at_id =
      (*env)->GetStaticMethodID (env, codec_list_class, "getCodecInfoAt",
      "(I)Landroid/media/MediaCodecInfo;");
  AMC_CHK (get_codec_count_id && get_codec_info_at_id);

  // J_CALL_STATIC_INT
  codec_count =
      (*env)->CallStaticIntMethod (env, codec_list_class, get_codec_count_id);
  J_EXCEPTION_CHECK ("codec_list_class->get_codec_count_id");

  GST_LOG ("Found %d available codecs", codec_count);

  for (i = 0; i < codec_count; i++) {
    GstAmcCodecInfo *gst_codec_info;
    jobject codec_info = NULL;
    jclass codec_info_class = NULL;
    jmethodID get_capabilities_for_type_id, get_name_id;
    jmethodID get_supported_types_id, is_encoder_id, is_feature_supported_id;
    jobject name = NULL;
    const gchar *name_str = NULL;
    jboolean is_encoder;
    jarray supported_types = NULL;
    jsize n_supported_types;
    jsize j;
    gboolean valid_codec = TRUE;

    gst_codec_info = g_new0 (GstAmcCodecInfo, 1);

    codec_info =
        (*env)->CallStaticObjectMethod (env, codec_list_class,
        get_codec_info_at_id, i);
    if ((*env)->ExceptionCheck (env) || !codec_info) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info %d", i);
      valid_codec = FALSE;
      goto next_codec;
    }

    codec_info_class = (*env)->GetObjectClass (env, codec_info);
    if (!codec_list_class) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info class");
      valid_codec = FALSE;
      goto next_codec;
    }

    get_capabilities_for_type_id =
        (*env)->GetMethodID (env, codec_info_class, "getCapabilitiesForType",
        "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;");
    get_name_id =
        (*env)->GetMethodID (env, codec_info_class, "getName",
        "()Ljava/lang/String;");
    get_supported_types_id =
        (*env)->GetMethodID (env, codec_info_class, "getSupportedTypes",
        "()[Ljava/lang/String;");
    is_encoder_id =
        (*env)->GetMethodID (env, codec_info_class, "isEncoder", "()Z");

    if (!get_capabilities_for_type_id || !get_name_id
        || !get_supported_types_id || !is_encoder_id) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec info method IDs");
      valid_codec = FALSE;
      goto next_codec;
    }

    name = (*env)->CallObjectMethod (env, codec_info, get_name_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get codec name");
      valid_codec = FALSE;
      goto next_codec;
    }
    name_str = (*env)->GetStringUTFChars (env, name, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to convert codec name to UTF8");
      valid_codec = FALSE;
      goto next_codec;
    }

    GST_INFO ("Checking codec '%s'", name_str);

    /* Compatibility codec names */
    /* The MP3Decoder is found on Sony Xperia Z but it fails
     * when creating the element and triggers a SEGV
     * The OMX.SEC.avcdec Found on Samsung Galaxy S3 gives errors
     */
    if (strcmp (name_str, "AACEncoder") == 0 ||
        strcmp (name_str, "AACDecoder") == 0 ||
        strcmp (name_str, "MP3Decoder") == 0 ||
        strcmp (name_str, "OMX.SEC.avcdec") == 0 ||
        strcmp (name_str, "OMX.google.raw.decoder") == 0) {
      GST_INFO ("Skipping compatibility codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }
#if 0
    if (g_str_has_suffix (name_str, ".secure")) {
      GST_INFO ("Skipping DRM codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }
#endif

    /* FIXME: Non-Google codecs usually just don't work and hang forever
     * or crash when not used from a process that started the Java
     * VM via the non-public AndroidRuntime class. Can we somehow
     * initialize all this?
     */
    if (gst_jni_is_vm_started () && !g_str_has_prefix (name_str, "OMX.google.")) {
      GST_INFO ("Skipping non-Google codec '%s' in standalone mode", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    if (g_str_has_prefix (name_str, "OMX.ARICENT.")
        || g_str_has_prefix (name_str, "OMX.MTK.AUDIO")) {
      GST_INFO ("Skipping possible broken codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    /* FIXME:
     *   - Vorbis: Generates clicks for multi-channel streams
     *   - *Law: Generates output with too low frequencies
     */
    if (strcmp (name_str, "OMX.google.vorbis.decoder") == 0 ||
        strcmp (name_str, "OMX.google.g711.alaw.decoder") == 0 ||
        strcmp (name_str, "OMX.google.g711.mlaw.decoder") == 0) {
      GST_INFO ("Skipping known broken codec '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }
    gst_codec_info->name = g_strdup (name_str);

    is_encoder = (*env)->CallBooleanMethod (env, codec_info, is_encoder_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to detect if codec is an encoder");
      valid_codec = FALSE;
      goto next_codec;
    }

    /* We do not support encoders. There are issues with a Sony Xperia P
     * where getting the capabilities of a type for an encoder takes around 5s
     */
    if (is_encoder) {
      GST_INFO ("Skipping encoder '%s'", name_str);
      valid_codec = FALSE;
      goto next_codec;
    }

    gst_codec_info->is_encoder = is_encoder;

    supported_types =
        (*env)->CallObjectMethod (env, codec_info, get_supported_types_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types");
      valid_codec = FALSE;
      goto next_codec;
    }

    n_supported_types = (*env)->GetArrayLength (env, supported_types);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionClear (env);
      GST_ERROR ("Failed to get supported types array length");
      valid_codec = FALSE;
      goto next_codec;
    }

    GST_INFO ("Codec '%s' has %d supported types", name_str, n_supported_types);

    gst_codec_info->supported_types =
        g_new0 (GstAmcCodecType, n_supported_types);
    gst_codec_info->n_supported_types = n_supported_types;

    if (n_supported_types == 0) {
      valid_codec = FALSE;
      GST_ERROR ("Codec has no supported types");
      goto next_codec;
    }

    for (j = 0; j < n_supported_types; j++) {
      GstAmcCodecType *gst_codec_type;
      jobject supported_type = NULL;
      gchar *supported_type_str = NULL;
      jobject capabilities = NULL;
      jclass capabilities_class = NULL;
      jfieldID color_formats_id, profile_levels_id;
      jobject color_formats = NULL;
      jobject profile_levels = NULL;
      jint *color_formats_elems = NULL;
      jsize n_elems, k;

      gst_codec_type = &gst_codec_info->supported_types[j];

      supported_type = (*env)->GetObjectArrayElement (env, supported_types, j);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get %d-th supported type", j);
        valid_codec = FALSE;
        goto next_supported_type;
      }

      supported_type_str = gst_amc_get_string_utf8 (env, supported_type);
      if (!supported_type_str) {
        GST_ERROR ("Failed to convert supported type to UTF8");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      GST_INFO ("Supported type '%s'", supported_type_str);
      gst_codec_type->mime = supported_type_str;

      capabilities =
          (*env)->CallObjectMethod (env, codec_info,
          get_capabilities_for_type_id, supported_type);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities for supported type");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      capabilities_class = (*env)->GetObjectClass (env, capabilities);
      if (!capabilities_class) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities class");
        valid_codec = FALSE;
        goto next_supported_type;
      }


      is_feature_supported_id =
          (*env)->GetMethodID (env, capabilities_class, "isFeatureSupported",
          "(Ljava/lang/String;)Z");

      if (is_feature_supported_id) {
        gboolean adaptivePlaybackSupported;
        jstring jtmpstr;

        jtmpstr = (*env)->NewStringUTF (env, "adaptive-playback");
        adaptivePlaybackSupported =
            (*env)->CallBooleanMethod (env, capabilities,
            is_feature_supported_id, jtmpstr);
        if ((*env)->ExceptionCheck (env)) {
          GST_ERROR
              ("Caught exception on quering if adaptive-playback is supported");
          (*env)->ExceptionClear (env);
        }
        J_DELETE_LOCAL_REF (jtmpstr);
        GST_ERROR
            ("&&& Codec %s: adaptive-playback %ssupported",
            name_str, adaptivePlaybackSupported ? "" : "not ");
      } else {
        GST_ERROR ("&&& isFeatureSupported not found ");
      }


      color_formats_id =
          (*env)->GetFieldID (env, capabilities_class, "colorFormats", "[I");
      profile_levels_id =
          (*env)->GetFieldID (env, capabilities_class, "profileLevels",
          "[Landroid/media/MediaCodecInfo$CodecProfileLevel;");
      if (!color_formats_id || !profile_levels_id) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get capabilities field IDs");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      color_formats =
          (*env)->GetObjectField (env, capabilities, color_formats_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color formats");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, color_formats);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color formats array length");
        valid_codec = FALSE;
        goto next_supported_type;
      }
      gst_codec_type->n_color_formats = n_elems;
      gst_codec_type->color_formats = g_new0 (gint, n_elems);
      color_formats_elems =
          (*env)->GetIntArrayElements (env, color_formats, NULL);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get color format elements");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      for (k = 0; k < n_elems; k++) {
        GST_INFO ("Color format %d: %d", k, color_formats_elems[k]);
        gst_codec_type->color_formats[k] = color_formats_elems[k];
      }

      if (g_str_has_prefix (gst_codec_type->mime, "video/")) {
        if (!n_elems) {
          GST_ERROR ("No supported color formats for video codec");
          valid_codec = FALSE;
          goto next_supported_type;
        }

        if (!ignore_unknown_color_formats
            && !accepted_color_formats (gst_codec_type, is_encoder)) {
          GST_ERROR ("Codec has unknown color formats, ignoring");
          valid_codec = FALSE;
          g_assert_not_reached ();
          goto next_supported_type;
        }
      }

      profile_levels =
          (*env)->GetObjectField (env, capabilities, profile_levels_id);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get profile/levels");
        valid_codec = FALSE;
        goto next_supported_type;
      }

      n_elems = (*env)->GetArrayLength (env, profile_levels);
      if ((*env)->ExceptionCheck (env)) {
        (*env)->ExceptionClear (env);
        GST_ERROR ("Failed to get profile/levels array length");
        valid_codec = FALSE;
        goto next_supported_type;
      }
      gst_codec_type->n_profile_levels = n_elems;
      gst_codec_type->profile_levels =
          g_malloc0 (sizeof (gst_codec_type->profile_levels[0]) * n_elems);
      for (k = 0; k < n_elems; k++) {
        jobject profile_level = NULL;
        jclass profile_level_class = NULL;
        jfieldID level_id, profile_id;
        jint level, profile;

        profile_level = (*env)->GetObjectArrayElement (env, profile_levels, k);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get %d-th profile/level", k);
          valid_codec = FALSE;
          goto next_profile_level;
        }

        profile_level_class = (*env)->GetObjectClass (env, profile_level);
        if (!profile_level_class) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level class");
          valid_codec = FALSE;
          goto next_profile_level;
        }

        level_id = (*env)->GetFieldID (env, profile_level_class, "level", "I");
        profile_id =
            (*env)->GetFieldID (env, profile_level_class, "profile", "I");
        if (!level_id || !profile_id) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile/level field IDs");
          valid_codec = FALSE;
          goto next_profile_level;
        }

        level = (*env)->GetIntField (env, profile_level, level_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get level");
          valid_codec = FALSE;
          goto next_profile_level;
        }
        GST_INFO ("Level %d: 0x%08x", k, level);
        gst_codec_type->profile_levels[k].level = level;

        profile = (*env)->GetIntField (env, profile_level, profile_id);
        if ((*env)->ExceptionCheck (env)) {
          (*env)->ExceptionClear (env);
          GST_ERROR ("Failed to get profile");
          valid_codec = FALSE;
          goto next_profile_level;
        }
        GST_INFO ("Profile %d: 0x%08x", k, profile);
        gst_codec_type->profile_levels[k].profile = profile;

      next_profile_level:
        J_DELETE_LOCAL_REF (profile_level);
        J_DELETE_LOCAL_REF (profile_level_class);
        if (!valid_codec)
          break;
      }

    next_supported_type:
      if (color_formats_elems)
        (*env)->ReleaseIntArrayElements (env, color_formats,
            color_formats_elems, JNI_ABORT);
      color_formats_elems = NULL;

      J_DELETE_LOCAL_REF (profile_levels);
      J_DELETE_LOCAL_REF (color_formats);
      J_DELETE_LOCAL_REF (capabilities);
      J_DELETE_LOCAL_REF (capabilities_class);
      J_DELETE_LOCAL_REF (supported_type);

      if (!valid_codec)
        break;
    }

    /* We need at least a valid supported type */
    if (valid_codec) {
      GST_LOG ("Successfully scanned codec '%s'", name_str);
      codec_infos = g_list_append (codec_infos, gst_codec_info);
      gst_codec_info = NULL;
    }

    /* Clean up of all local references we got */
  next_codec:
    if (name_str)
      (*env)->ReleaseStringUTFChars (env, name, name_str);
    name_str = NULL;
    J_DELETE_LOCAL_REF (name);
    J_DELETE_LOCAL_REF (supported_types);
    J_DELETE_LOCAL_REF (codec_info);
    J_DELETE_LOCAL_REF (codec_info_class);
    if (gst_codec_info) {
      gint j;

      for (j = 0; j < gst_codec_info->n_supported_types; j++) {
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[j];

        g_free (gst_codec_type->mime);
        g_free (gst_codec_type->color_formats);
        g_free (gst_codec_type->profile_levels);
      }
      g_free (gst_codec_info->supported_types);
      g_free (gst_codec_info->name);
      g_free (gst_codec_info);
    }
    gst_codec_info = NULL;
    valid_codec = TRUE;
  }

  ret = codec_infos != NULL;

  /* If successful we store a cache of the codec information in
   * the registry. Otherwise we would always load all codecs during
   * plugin initialization which can take quite some time (because
   * of hardware) and also loads lots of shared libraries (which
   * number is limited by 64 in Android).
   */
  if (ret) {
    GstStructure *new_cache_data = gst_structure_empty_new ("gst-amc-cache");
    GList *l;
    GValue arr = { 0, };

    g_value_init (&arr, GST_TYPE_ARRAY);

    for (l = codec_infos; l; l = l->next) {
      GstAmcCodecInfo *gst_codec_info = l->data;
      GValue cv = { 0, };
      GstStructure *cs = gst_structure_empty_new ("gst-amc-codec");
      GValue starr = { 0, };
      gint i;

      gst_structure_set (cs, "name", G_TYPE_STRING, gst_codec_info->name,
          "is-encoder", G_TYPE_BOOLEAN, gst_codec_info->is_encoder, NULL);

      g_value_init (&starr, GST_TYPE_ARRAY);

      for (i = 0; i < gst_codec_info->n_supported_types; i++) {
        GstAmcCodecType *gst_codec_type = &gst_codec_info->supported_types[i];
        GstStructure *sts = gst_structure_empty_new ("gst-amc-supported-type");
        GValue stv = { 0, };
        GValue tmparr = { 0, };
        gint j;

        gst_structure_set (sts, "mime", G_TYPE_STRING, gst_codec_type->mime,
            NULL);

        g_value_init (&tmparr, GST_TYPE_ARRAY);
        for (j = 0; j < gst_codec_type->n_color_formats; j++) {
          GValue tmp = { 0, };

          g_value_init (&tmp, G_TYPE_INT);
          g_value_set_int (&tmp, gst_codec_type->color_formats[j]);
          gst_value_array_append_value (&tmparr, &tmp);
          g_value_unset (&tmp);
        }
        gst_structure_set_value (sts, "color-formats", &tmparr);
        g_value_unset (&tmparr);

        g_value_init (&tmparr, GST_TYPE_ARRAY);
        for (j = 0; j < gst_codec_type->n_profile_levels; j++) {
          GValue tmparr2 = { 0, };
          GValue tmp = { 0, };

          g_value_init (&tmparr2, GST_TYPE_ARRAY);
          g_value_init (&tmp, G_TYPE_INT);
          g_value_set_int (&tmp, gst_codec_type->profile_levels[j].profile);
          gst_value_array_append_value (&tmparr2, &tmp);
          g_value_set_int (&tmp, gst_codec_type->profile_levels[j].level);
          gst_value_array_append_value (&tmparr2, &tmp);
          gst_value_array_append_value (&tmparr, &tmparr2);
          g_value_unset (&tmp);
          g_value_unset (&tmparr2);
        }
        gst_structure_set_value (sts, "profile-levels", &tmparr);

        g_value_init (&stv, GST_TYPE_STRUCTURE);
        gst_value_set_structure (&stv, sts);
        gst_value_array_append_value (&starr, &stv);
        g_value_unset (&tmparr);
        gst_structure_free (sts);
      }

      gst_structure_set_value (cs, "supported-types", &starr);
      g_value_unset (&starr);

      g_value_init (&cv, GST_TYPE_STRUCTURE);
      gst_value_set_structure (&cv, cs);
      gst_value_array_append_value (&arr, &cv);
      g_value_unset (&cv);
      gst_structure_free (cs);
    }

    gst_structure_set_value (new_cache_data, "codecs", &arr);
    g_value_unset (&arr);

    save_codecs (plugin, new_cache_data);
  }

  ret = TRUE;
error:
  J_DELETE_LOCAL_REF (codec_list_class);
  return ret;
}

static const struct
{
  gint color_format;
  GstVideoFormat video_format;
} color_format_mapping_table[] = {
  {
  COLOR_FormatSurface1, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface2, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface3, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface4, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface5, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatSurface6, GST_VIDEO_FORMAT_ENCODED}, {
  HAL_PIXEL_FORMAT_YCrCb_420_SP, GST_VIDEO_FORMAT_ENCODED}, {
  COLOR_FormatYUV420Planar, GST_VIDEO_FORMAT_I420}, {
  COLOR_FormatYUV420Flexible, GST_VIDEO_FORMAT_I420}, {
  COLOR_FormatYUV420SemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_FormatYUV411PackedPlanar, GST_VIDEO_FORMAT_IYU1,}, {
  COLOR_TI_FormatYUV420PackedSemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYUV420SemiPlanar, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYUV420PackedSemiPlanar64x32Tile2m8ka, GST_VIDEO_FORMAT_NV12}, {
  COLOR_QCOM_FormatYVU420SemiPlanar32m, GST_VIDEO_FORMAT_NV12}
};

static gboolean
accepted_color_formats (GstAmcCodecType * type, gboolean is_encoder)
{
  gint i, j;
  gint accepted = 0, all = type->n_color_formats;

  for (i = 0; i < type->n_color_formats; i++) {
    gboolean found = FALSE;

    /* We ignore this one */
    if (type->color_formats[i] == COLOR_FormatAndroidOpaque)
      all--;
    if (type->color_formats[i] == COLOR_FormatAndroidUndocumented1)
      all--;

    for (j = 0; j < G_N_ELEMENTS (color_format_mapping_table); j++) {
      if (color_format_mapping_table[j].color_format == type->color_formats[i]) {
        found = TRUE;
        break;
      }
    }

    if (found)
      accepted++;
  }

  if (is_encoder)
    return accepted > 0;
  else
    return accepted == all && all > 0;
}

GstVideoFormat
gst_amc_color_format_to_video_format (gint color_format)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (color_format_mapping_table); i++) {
    if (color_format_mapping_table[i].color_format == color_format)
      return color_format_mapping_table[i].video_format;
  }

  return GST_VIDEO_FORMAT_UNKNOWN;
}


const gchar *
gst_amc_hevc_profile_to_string (gint profile)
{
  static const struct
  {
    gint id;
    const gchar *str;
  } hevc_profile_mapping_table[] = {
    {
    HEVCProfileMain, "main"}, {
    HEVCProfileMain10, "main-10"}, {
    HEVCProfileMain10HDR10, "main-10-hdr10"}    /* <-- this caps not exist in h265parse */
  };


  gint i;
  for (i = 0; i < G_N_ELEMENTS (hevc_profile_mapping_table); i++)
    if (hevc_profile_mapping_table[i].id == profile)
      return hevc_profile_mapping_table[i].str;
  return NULL;
}


gboolean
gst_amc_hevc_level_to_string (gint id, const gchar ** level,
    const gchar ** tier)
{
  static const struct
  {
    gint id;
    const gchar *tier, *level;
  } hevc_level_mapping_table[] = {
    {
    HEVCMainTierLevel1, "1", "main"}, {
    HEVCHighTierLevel1, "1", "high"}, {
    HEVCMainTierLevel2, "2", "main"}, {
    HEVCHighTierLevel2, "2", "high"}, {
    HEVCMainTierLevel21, "2.1", "main"}, {
    HEVCHighTierLevel21, "2.1", "high"}, {
    HEVCMainTierLevel3, "3", "main"}, {
    HEVCHighTierLevel3, "3", "high"}, {
    HEVCMainTierLevel31, "3.1", "main"}, {
    HEVCHighTierLevel31, "3.1", "high"}, {
    HEVCMainTierLevel4, "4", "main"}, {
    HEVCHighTierLevel4, "4", "high"}, {
    HEVCMainTierLevel41, "4.1", "main"}, {
    HEVCHighTierLevel41, "4.1", "high"}, {
    HEVCMainTierLevel5, "5", "main"}, {
    HEVCHighTierLevel5, "5", "high"}, {
    HEVCMainTierLevel51, "5.1", "main"}, {
    HEVCHighTierLevel51, "5.1", "high"}, {
    HEVCMainTierLevel52, "5.2", "main"}, {
    HEVCHighTierLevel52, "5.2", "high"}, {
    HEVCMainTierLevel6, "6", "main"}, {
    HEVCHighTierLevel6, "6", "high"}, {
    HEVCMainTierLevel61, "6.1", "main"}, {
    HEVCHighTierLevel61, "6.1", "high"}, {
    HEVCMainTierLevel62, "6.2", "main"}, {
    HEVCHighTierLevel62, "6.2", "high"}
  };

  gint i;

  for (i = 0; i < G_N_ELEMENTS (hevc_level_mapping_table); i++)
    if (hevc_level_mapping_table[i].id == id) {
      *level = hevc_level_mapping_table[i].level;
      *tier = hevc_level_mapping_table[i].tier;
      return TRUE;
    }

  return FALSE;
}


static const struct
{
  gint id;
  const gchar *str;
  const gchar *alt_str;
} avc_profile_mapping_table[] = {
  {
  AVCProfileBaseline, "baseline", "constrained-baseline"}, {
  AVCProfileMain, "main", NULL}, {
  AVCProfileExtended, "extended", NULL}, {
  AVCProfileHigh, "high"}, {
  AVCProfileHigh10, "high-10", "high-10-intra"}, {
  AVCProfileHigh422, "high-4:2:2", "high-4:2:2-intra"}, {
  AVCProfileHigh444, "high-4:4:4", "high-4:4:4-intra"}
};

const gchar *
gst_amc_avc_profile_to_string (gint profile, const gchar ** alternative)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (avc_profile_mapping_table); i++) {
    if (avc_profile_mapping_table[i].id == profile) {
      *alternative = avc_profile_mapping_table[i].alt_str;
      return avc_profile_mapping_table[i].str;
    }
  }

  return NULL;
}


static const struct
{
  gint id;
  const gchar *str;
} avc_level_mapping_table[] = {
  {
  AVCLevel1, "1"}, {
  AVCLevel1b, "1b"}, {
  AVCLevel11, "1.1"}, {
  AVCLevel12, "1.2"}, {
  AVCLevel13, "1.3"}, {
  AVCLevel2, "2"}, {
  AVCLevel21, "2.1"}, {
  AVCLevel22, "2.2"}, {
  AVCLevel3, "3"}, {
  AVCLevel31, "3.1"}, {
  AVCLevel32, "3.2"}, {
  AVCLevel4, "4"}, {
  AVCLevel41, "4.1"}, {
  AVCLevel42, "4.2"}, {
  AVCLevel5, "5"}, {
  AVCLevel51, "5.1"}
};

const gchar *
gst_amc_avc_level_to_string (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (avc_level_mapping_table); i++) {
    if (avc_level_mapping_table[i].id == level)
      return avc_level_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  gint id;
  gint gst_id;
} h263_profile_mapping_table[] = {
  {
  H263ProfileBaseline, 0}, {
  H263ProfileH320Coding, 1}, {
  H263ProfileBackwardCompatible, 2}, {
  H263ProfileISWV2, 3}, {
  H263ProfileISWV3, 4}, {
  H263ProfileHighCompression, 5}, {
  H263ProfileInternet, 6}, {
  H263ProfileInterlace, 7}, {
  H263ProfileHighLatency, 8}
};

gint
gst_amc_h263_profile_to_gst_id (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_profile_mapping_table); i++) {
    if (h263_profile_mapping_table[i].id == profile)
      return h263_profile_mapping_table[i].gst_id;
  }

  return -1;
}


static const struct
{
  gint id;
  gint gst_id;
} h263_level_mapping_table[] = {
  {
  H263Level10, 10}, {
  H263Level20, 20}, {
  H263Level30, 30}, {
  H263Level40, 40}, {
  H263Level50, 50}, {
  H263Level60, 60}, {
  H263Level70, 70}
};

gint
gst_amc_h263_level_to_gst_id (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (h263_level_mapping_table); i++) {
    if (h263_level_mapping_table[i].id == level)
      return h263_level_mapping_table[i].gst_id;
  }

  return -1;
}


static const struct
{
  gint id;
  const gchar *str;
} mpeg4_profile_mapping_table[] = {
  {
  MPEG4ProfileSimple, "simple"}, {
  MPEG4ProfileSimpleScalable, "simple-scalable"}, {
  MPEG4ProfileCore, "core"}, {
  MPEG4ProfileMain, "main"}, {
  MPEG4ProfileNbit, "n-bit"}, {
  MPEG4ProfileScalableTexture, "scalable"}, {
  MPEG4ProfileSimpleFace, "simple-face"}, {
  MPEG4ProfileSimpleFBA, "simple-fba"}, {
  MPEG4ProfileBasicAnimated, "basic-animated-texture"}, {
  MPEG4ProfileHybrid, "hybrid"}, {
  MPEG4ProfileAdvancedRealTime, "advanced-real-time"}, {
  MPEG4ProfileCoreScalable, "core-scalable"}, {
  MPEG4ProfileAdvancedCoding, "advanced-coding-efficiency"}, {
  MPEG4ProfileAdvancedCore, "advanced-core"}, {
  MPEG4ProfileAdvancedScalable, "advanced-scalable-texture"}, {
  MPEG4ProfileAdvancedSimple, "advanced-simple"}
};

const gchar *
gst_amc_mpeg4_profile_to_string (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (mpeg4_profile_mapping_table); i++) {
    if (mpeg4_profile_mapping_table[i].id == profile)
      return mpeg4_profile_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  gint id;
  const gchar *str;
} mpeg4_level_mapping_table[] = {
  {
  MPEG4Level0, "0"}, {
  MPEG4Level0b, "0b"}, {
  MPEG4Level1, "1"}, {
  MPEG4Level2, "2"}, {
  MPEG4Level3, "3"}, {
  MPEG4Level4, "4"}, {
  MPEG4Level4a, "4a"}, {
MPEG4Level5, "5"},};

const gchar *
gst_amc_mpeg4_level_to_string (gint level)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (mpeg4_level_mapping_table); i++) {
    if (mpeg4_level_mapping_table[i].id == level)
      return mpeg4_level_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  gint id;
  const gchar *str;
} aac_profile_mapping_table[] = {
  {
  AACObjectMain, "main"}, {
  AACObjectLC, "lc"}, {
  AACObjectSSR, "ssr"}, {
  AACObjectLTP, "ltp"}
};

const gchar *
gst_amc_aac_profile_to_string (gint profile)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (aac_profile_mapping_table); i++) {
    if (aac_profile_mapping_table[i].id == profile)
      return aac_profile_mapping_table[i].str;
  }

  return NULL;
}


static const struct
{
  guint32 mask;
  GstAudioChannelPosition pos;
} channel_mapping_table[] = {
  {
  CHANNEL_OUT_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  CHANNEL_OUT_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
  CHANNEL_OUT_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
  CHANNEL_OUT_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE}, {
  CHANNEL_OUT_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
  CHANNEL_OUT_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
  CHANNEL_OUT_FRONT_LEFT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
  CHANNEL_OUT_FRONT_RIGHT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
  CHANNEL_OUT_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
  CHANNEL_OUT_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
  CHANNEL_OUT_SIDE_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
  CHANNEL_OUT_TOP_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_INVALID}, {
  CHANNEL_OUT_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_INVALID}
};

GstAudioChannelPosition *
gst_amc_audio_channel_mask_to_positions (guint32 channel_mask, gint channels)
{
  GstAudioChannelPosition *pos = g_new0 (GstAudioChannelPosition, channels);
  gint i, j;

  if (channel_mask == 0) {
    if (channels == 1) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
      return pos;
    }
    if (channels == 2) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      return pos;
    }

    /* Now let the guesswork begin, these are the
     * AAC default channel assignments for these numbers
     * of channels */
    if (channels == 3) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER;
    } else if (channels == 4) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_CENTER;
    } else if (channels == 5) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT;
    } else if (channels == 6) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT | CHANNEL_OUT_LOW_FREQUENCY;
    } else if (channels == 8) {
      channel_mask =
          CHANNEL_OUT_FRONT_LEFT | CHANNEL_OUT_FRONT_RIGHT |
          CHANNEL_OUT_FRONT_CENTER | CHANNEL_OUT_BACK_LEFT |
          CHANNEL_OUT_BACK_RIGHT | CHANNEL_OUT_LOW_FREQUENCY |
          CHANNEL_OUT_FRONT_LEFT_OF_CENTER | CHANNEL_OUT_FRONT_RIGHT_OF_CENTER;
    }
  }

  for (i = 0, j = 0; i < G_N_ELEMENTS (channel_mapping_table); i++) {
    if ((channel_mask & channel_mapping_table[i].mask)) {
      pos[j++] = channel_mapping_table[i].pos;
      if (channel_mapping_table[i].pos == GST_AUDIO_CHANNEL_POSITION_INVALID) {
        g_free (pos);
        GST_ERROR ("Unable to map channel mask 0x%08x",
            channel_mapping_table[i].mask);
        return NULL;
      }
      if (j == channels)
        break;
    }
  }

  if (j != channels) {
    g_free (pos);
    GST_ERROR ("Unable to map all channel positions in mask 0x%08x",
        channel_mask);
    return NULL;
  }

  return pos;
}


static gchar *
create_type_name (const gchar * parent_name, const gchar * codec_name)
{
  gchar *typified_name;
  gint i, k;
  gint parent_name_len = strlen (parent_name);
  gint codec_name_len = strlen (codec_name);
  gboolean upper = TRUE;

  typified_name = g_new0 (gchar, parent_name_len + 1 + strlen (codec_name) + 1);
  memcpy (typified_name, parent_name, parent_name_len);
  typified_name[parent_name_len] = '-';

  for (i = 0, k = 0; i < codec_name_len; i++) {
    if (g_ascii_isalnum (codec_name[i])) {
      if (upper)
        typified_name[parent_name_len + 1 + k++] =
            g_ascii_toupper (codec_name[i]);
      else
        typified_name[parent_name_len + 1 + k++] =
            g_ascii_tolower (codec_name[i]);

      upper = FALSE;
    } else {
      /* Skip all non-alnum chars and start a new upper case word */
      upper = TRUE;
    }
  }

  return typified_name;
}

static gchar *
create_element_name (gboolean video, gboolean encoder, const gchar * codec_name)
{
#define PREFIX_LEN 10
  static const gchar *prefixes[] = {
    "amcviddec-",
    "amcauddec-",
    "amcvidenc-",
    "amcaudenc-"
  };
  gchar *element_name;
  gint i, k;
  gint codec_name_len = strlen (codec_name);
  const gchar *prefix;

  if (video && !encoder)
    prefix = prefixes[0];
  else if (!video && !encoder)
    prefix = prefixes[1];
  else if (video && encoder)
    prefix = prefixes[2];
  else
    prefix = prefixes[3];

  element_name = g_new0 (gchar, PREFIX_LEN + strlen (codec_name) + 1);
  memcpy (element_name, prefix, PREFIX_LEN);

  for (i = 0, k = 0; i < codec_name_len; i++) {
    if (g_ascii_isalnum (codec_name[i])) {
      element_name[PREFIX_LEN + k++] = g_ascii_tolower (codec_name[i]);
    }
    /* Skip all non-alnum chars */
  }

  return element_name;
}

#undef PREFIX_LEN

static gboolean
register_codecs (GstPlugin * plugin)
{
  gboolean ret = TRUE;
  GList *l;

  GST_DEBUG ("Registering plugins");

  for (l = codec_infos; l; l = l->next) {
    GstAmcCodecInfo *codec_info = l->data;
    gboolean is_audio = FALSE;
    gboolean is_video = FALSE;
    gint i;
    gint n_types;

    GST_DEBUG ("Registering codec '%s'", codec_info->name);
    for (i = 0; i < codec_info->n_supported_types; i++) {
      GstAmcCodecType *codec_type = &codec_info->supported_types[i];

      if (g_str_has_prefix (codec_type->mime, "audio/"))
        is_audio = TRUE;
      else if (g_str_has_prefix (codec_type->mime, "video/"))
        is_video = TRUE;
    }

    n_types = 0;
    if (is_audio)
      n_types++;
    if (is_video)
      n_types++;

    for (i = 0; i < n_types; i++) {
      GTypeQuery type_query;
      GTypeInfo type_info = { 0, };
      GType type, subtype;
      gchar *type_name, *element_name;
      guint rank;

      if (is_video && !codec_info->is_encoder) {
        type = gst_amc_video_dec_get_type ();
      } else if (is_audio && !codec_info->is_encoder) {
        type = gst_amc_audio_dec_get_type ();
      } else {
        GST_DEBUG ("Skipping unsupported codec type");
        continue;
      }

      g_type_query (type, &type_query);
      memset (&type_info, 0, sizeof (type_info));
      type_info.class_size = type_query.class_size;
      type_info.instance_size = type_query.instance_size;
      type_name = create_type_name (type_query.type_name, codec_info->name);

      if (g_type_from_name (type_name) != G_TYPE_INVALID) {
        GST_ERROR ("Type '%s' already exists for codec '%s'", type_name,
            codec_info->name);
        g_free (type_name);
        continue;
      }

      subtype = g_type_register_static (type, type_name, &type_info, 0);
      g_free (type_name);

      g_type_set_qdata (subtype, gst_amc_codec_info_quark, codec_info);

      element_name =
          create_element_name (is_video, codec_info->is_encoder,
          codec_info->name);

      /* Give the Google software codec a secondary rank,
       * everything else is likely a hardware codec, except
       * OMX.SEC.*.sw.dec (as seen in Galaxy S4) */
      if (g_str_has_prefix (codec_info->name, "OMX.google") ||
          g_str_has_suffix (codec_info->name, ".sw.dec"))
        rank = GST_RANK_SECONDARY;
      else
        rank = GST_RANK_PRIMARY;

      ret |= gst_element_register (plugin, element_name, rank, subtype);
      g_free (element_name);

      is_video = FALSE;
    }
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  const gchar *ignore;

  GST_DEBUG_CATEGORY_INIT (gst_amc_debug, "amc", 0, "android-media-codec");

  if (!gst_jni_initialize (NULL))
    return FALSE;

  gst_plugin_add_dependency_simple (plugin, NULL, "/etc", "media_codecs.xml",
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  if (!get_java_classes ())
    return FALSE;

  /* Set this to TRUE to allow registering decoders that have
   * any unknown color formats, or encoders that only have
   * unknown color formats
   */
  ignore = g_getenv ("GST_AMC_IGNORE_UNKNOWN_COLOR_FORMATS");
  if (ignore && strcmp (ignore, "yes") == 0)
    ignore_unknown_color_formats = TRUE;

  if (!scan_codecs (plugin))
    return FALSE;

  gst_amc_codec_info_quark = g_quark_from_static_string ("gst-amc-codec-info");

  log_known_supported_protection_schemes ();

  if (!register_codecs (plugin))
    return FALSE;

  return gst_element_register (plugin, "amcvideosink", GST_RANK_PRIMARY,
      GST_TYPE_AMC_VIDEO_SINK);

  return TRUE;
}

GstAmcDRBuffer *
gst_amc_dr_buffer_new (GstAmcCodec * codec, guint idx)
{
  GstAmcDRBuffer *buf;

  buf = g_new0 (GstAmcDRBuffer, 1);
  buf->codec = *codec;
  buf->codec.object = gst_amc_global_ref_jobj (buf->codec.object);
  buf->idx = idx;
  buf->released = FALSE;

  return buf;
}

gboolean
gst_amc_dr_buffer_render (GstAmcDRBuffer * buf, GstClockTime ts)
{
  gboolean ret = FALSE;

  if (!buf->released) {
    ret = gst_amc_codec_render_output_buffer (&buf->codec, buf->idx, ts);
    buf->released = TRUE;
  }

  return ret;
}

void
gst_amc_dr_buffer_free (GstAmcDRBuffer * buf)
{
  JNIEnv *env = gst_jni_get_env ();
  if (!buf->released) {
    gst_amc_codec_release_output_buffer (&buf->codec, buf->idx);
  }
  J_DELETE_GLOBAL_REF (buf->codec.object);
  g_free (buf);
}

GstQuery *
gst_amc_query_new_surface (void)
{
  GstQuery *query;
  GstQueryType qtype;
  GstStructure *structure;

  query = (GstQuery *) gst_mini_object_new (GST_TYPE_QUERY);

  qtype = gst_query_type_get_by_nick (GST_AMC_SURFACE);
  if (qtype == GST_QUERY_NONE) {
    qtype = gst_query_type_register (GST_AMC_SURFACE,
        "Queries for a surface to render");
  }
  query->type = qtype;
  GST_DEBUG ("creating new query %p %s", query,
      gst_query_type_get_name (qtype));

  structure = gst_structure_id_new (GST_QUARK (GST_AMC_SURFACE),
      GST_QUARK (GST_AMC_SURFACE_POINTER), G_TYPE_POINTER, NULL, NULL);
  query->structure = structure;
  gst_structure_set_parent_refcount (query->structure,
      &query->mini_object.refcount);

  return query;
}

gpointer
gst_amc_query_parse_surface (GstQuery * query)
{
  GstQueryType qtype = gst_query_type_get_by_nick (GST_AMC_SURFACE);

  if (GST_QUERY_TYPE (query) != qtype)
    return NULL;

  return g_value_get_pointer (gst_structure_id_get_value (query->structure,
          GST_QUARK (GST_AMC_SURFACE_POINTER)));
}

gboolean
gst_amc_query_set_surface (GstQuery * query, gpointer surface)
{
  GstQueryType qtype;

  qtype = gst_query_type_get_by_nick (GST_AMC_SURFACE);
  if (GST_QUERY_TYPE (query) != qtype)
    return FALSE;

  gst_structure_id_set (query->structure,
      GST_QUARK (GST_AMC_SURFACE_POINTER), G_TYPE_POINTER, surface, NULL);
  return TRUE;
}

gboolean
gst_amc_event_is_surface (GstEvent * event)
{
  return gst_event_has_name (event, GST_AMC_SURFACE);
}

GstEvent *
gst_amc_event_new_surface (gpointer surface)
{
  GstEvent *event;

  event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM,
      gst_structure_id_new (GST_QUARK (GST_AMC_SURFACE),
          GST_QUARK (GST_AMC_SURFACE_POINTER), G_TYPE_POINTER, surface, NULL));
  return event;
}

gpointer
gst_amc_event_parse_surface (GstEvent * event)
{
  return
      g_value_get_pointer (gst_structure_id_get_value (gst_event_get_structure
          (event), GST_QUARK (GST_AMC_SURFACE_POINTER)));
}


#ifdef GST_PLUGIN_DEFINE2
GST_PLUGIN_DEFINE2 (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    androidmedia,
    "Android Media plugin",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#else
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "androidmedia",
    "Android Media plugin",
    plugin_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
#endif
