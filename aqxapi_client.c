#include <stdio.h>
#include <curl/curl.h>
#include <string.h>
#include <json-c/json.h>
#include "aqxapi_client.h"

/* note that the SSL certificate is not quite ok */
#define SKIP_PEER_VERIFICATION

#define GOOGLE_OAUTH2_ENDPOINT_URL "https://www.googleapis.com/oauth2/v3/token"
#define GOOGLE_CLIENT_ID "75692667349-nble6luh47u4o0e3srath2nf45sv3679.apps.googleusercontent.com"
#define GOOGLE_CLIENT_SECRET "wvOoMwCJMbKTCLhCdR81VpRx"
#define GOOGLE_REDIRECT_URL "urn:ietf:wg:oauth:2.0:oob"

#define REFRESH_PARAMS "refresh_token=%s&client_id=%s&client_secret=%s&grant_type=refresh_token"

#define ACCESS_TOKEN_MAXLEN 100
#define AUTH_HEADER_MAXLEN 200
#define REFRESH_POST_PARAMS_MAXLEN 1024
#define JSON_BUFFER_SIZE 2048
#define MAX_MEASUREMENTS 10

/* Module init configuration */
static struct aqx_client_options config;
static time_t last_submission_time;

/*
  for now, we buffer the measurements up in a static buffer and
  send them to the API when the conditions are met
*/
static int num_measurements;
static struct aqx_measurement measurement_data[MAX_MEASUREMENTS];

/* Google OAuth communcation state */
static char refresh_post_params[REFRESH_POST_PARAMS_MAXLEN];
static char auth_header[AUTH_HEADER_MAXLEN];
static char access_token[ACCESS_TOKEN_MAXLEN];

/* JSON buffer*/
static int json_count = 0;
static char json_buffer[JSON_BUFFER_SIZE];

/*
 * Callback function for the access token retrieval function.
 */
size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  size_t num_bytes = size * nmemb;

  /* ensure the callback will not stomp over buffer boundaries */
  if (json_count + num_bytes >= JSON_BUFFER_SIZE) return 0;

  memcpy(&json_buffer[json_count], ptr, num_bytes);
  json_count += num_bytes;
  return num_bytes;
}

/*
  Given the refresh token, retrieve a new access token from the Google token service
  endpoint.
  Currently, this only checks the 'access_token' field of the response JSON object.
*/
const char *get_access_token(const char *refresh_token)
{
  CURL *curl;
  CURLcode result;
  const char *retval = NULL;

  curl = curl_easy_init();

  sprintf(refresh_post_params, REFRESH_PARAMS, refresh_token,
          GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET);
  if (curl) {
    memset(json_buffer, 0, sizeof(json_buffer));
    curl_easy_setopt(curl, CURLOPT_URL, GOOGLE_OAUTH2_ENDPOINT_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, refresh_post_params);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, json_buffer);
    result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
      LOG_DEBUG("curl_easy_perform() failed: %s\n", curl_easy_strerror(result));
    } else {
      json_object *obj;
      obj = json_tokener_parse(json_buffer);
      json_count = 0;

      /* evaluate data */
      if (json_object_is_type(obj, json_type_object)) {
        struct json_object *access_token_obj;

        if (json_object_object_get_ex(obj, "access_token", &access_token_obj)) {
          const char *recvd_token = json_object_get_string(access_token_obj);
          strncpy(access_token, recvd_token, ACCESS_TOKEN_MAXLEN);
          retval = access_token;
        } else {
          LOG_DEBUG("no access token found\n");
        }
      }
      json_object_put(obj);
    }
    curl_easy_cleanup(curl);
  }
  return retval;
}

int submit_measurements(const char *service_url, const char *access_token, const char *json_str)
{
  CURL *curl;
  CURLcode result;
  int retval = 0;
  static char app_url_buffer[200];

  curl = curl_easy_init();
  if (curl) {
    struct curl_slist *chunk = NULL;

    memset(json_buffer, 0, sizeof(json_buffer));
    snprintf(app_url_buffer, sizeof(app_url_buffer), service_url, config.system_uid);

    /* Verification header + Content-Type */
    sprintf(auth_header, "Authorization: Bearer %s", access_token);
    chunk = curl_slist_append(chunk, auth_header);
    chunk = curl_slist_append(chunk, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_URL, app_url_buffer);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, json_buffer);

#ifdef SKIP_PEER_VERIFICATION
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
    result = curl_easy_perform(curl);

    if (result != CURLE_OK) {
      LOG_DEBUG("curl_easy_perform() failed: %s\n", curl_easy_strerror(result));
      /* TODO: Handle submit error
         maybe retry ? After that, store in a file */
    } else {
      json_object *obj, *error_obj;
      obj = json_tokener_parse(json_buffer);
      json_count = 0;

      if (json_object_object_get_ex(obj, "error", &error_obj)) {
        const char *error_msg = json_object_get_string(error_obj);
        LOG_DEBUG("Error: '%s'\n", error_msg);
      } else {
        LOG_DEBUG("everything ok: '%s'\n", json_object_get_string(obj));
        retval = 1;
      }
      json_object_put(obj);
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(chunk);
  }
  return retval;
}

/* serialize a measurement struct into a json_object */
json_object *to_json(struct aqx_measurement *m)
{
  static char time_buffer[20];
  struct json_object *obj = json_object_new_object();
  struct tm *tstruct = localtime(&m->time);
  /* mm/dd/yyyy HH:MM:SS */
  sprintf(time_buffer, "%02d/%02d/%04d %02d:%02d:%02d",
          tstruct->tm_mon + 1, tstruct->tm_mday, 1900 + tstruct->tm_year,
          tstruct->tm_hour, tstruct->tm_min, tstruct->tm_sec);

  json_object_object_add(obj, "time", json_object_new_string(time_buffer));
  json_object_object_add(obj, "temp", json_object_new_double(m->temperature));
  json_object_object_add(obj, "ph", json_object_new_double(m->ph));
  json_object_object_add(obj, "o2", json_object_new_double(m->o2));
  json_object_object_add(obj, "light", json_object_new_double(m->light));
  json_object_object_add(obj, "ammonium", json_object_new_double(m->ammonium));
  json_object_object_add(obj, "nitrate", json_object_new_double(m->nitrate));

  return obj;
}

static struct json_object *serialize_measurements()
{
  int i;
  struct json_object *arr = json_object_new_array();

  for (i = 0; i < num_measurements; i++) {
    json_object_array_add(arr, to_json(&measurement_data[i]));
  }
  return arr;
}

/***************************************************************************
 *
 * Public Interface
 *
 ***************************************************************************/

int aqx_client_init(struct aqx_client_options *options)
{
  memcpy(&config, options, sizeof(struct aqx_client_options));
  num_measurements = 0;

  /* reset timer */
  time(&last_submission_time);

  curl_global_init(CURL_GLOBAL_ALL);
  return 1;
}

void aqx_client_cleanup()
{
  curl_global_cleanup();
}

void aqx_client_flush()
{
    struct json_object *arr;
    const char *json_str, *access_token;
    arr = serialize_measurements();
    json_str = json_object_get_string(arr);
    LOG_DEBUG("Submit: %s\n", json_str);

    access_token = get_access_token(config.oauth2_refresh_token);
    if (access_token) {
      LOG_DEBUG("received access token: '%s'\n", access_token);
      submit_measurements(config.service_url, access_token, json_str);
    }

    json_object_put(arr); /* free object */
    
    /* reset counter and timer */
    num_measurements = 0;
    time(&last_submission_time);
}

int aqx_add_measurement(struct aqx_measurement *m)
{
  time_t currtime;
  time_t elapsed;

  time(&currtime);
  elapsed = currtime - last_submission_time;
  memcpy(&measurement_data[num_measurements++], m, sizeof(struct aqx_measurement));
  if (elapsed >= config.send_interval_secs || num_measurements >= MAX_MEASUREMENTS) {
    aqx_client_flush();
  }
  return 1;
}
