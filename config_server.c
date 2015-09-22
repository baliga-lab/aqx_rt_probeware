/* config_server.c - Configuration server
 *
 * Description: Configuration is handled here, the user can modify the
 * ------------ settings through the embedded web interface
 * TODO: Make this part of aqxclient as its frontend, for reusability
 */
#include "config_server.h"
#include "aqxapi_client.h"
#include "simple_templates.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define HTDOCS_PREFIX "htdocs"
/* Length of the opening and closing option tags <option "..."></option> */
#define OPTION_TAG_LENGTH (11 + 9)
#define MEASUREMENTS_URL_PREFIX   "measurements_url="
#define SYSTEMS_URL_PREFIX   "systems_url="
#define REFRESH_TOKEN_PREFIX "refresh_token="
#define SYSTEM_UID_PREFIX    "system_uid="
#define SERVICE_PORT_PREFIX  "service_port="

static struct aqxclient_config client_config;

struct token_post_data {
  struct MHD_PostProcessor *pp;
  char token[OAUTH2_TOKEN_MAXLEN + 1];
};

/*
 * Handle the HTTP server requests here.
 */
/* GET method requests */
static int handle_get(struct MHD_Connection *connection, const char *url)
{
  struct MHD_Response *response;
  FILE *fp;
  uint64_t file_size;
  int ret = 0, is_static = 1, has_token = 0;
  const char *filepath;
  char *path_buffer = NULL;

  /* Check and process routes */
  if (!strcmp(url, "/")) {
    if (has_token) {
      filepath = "templates/system_settings.html";
      is_static = 0;
    } else {
      /* redirect to static page */
      url = "/enter_token.html";
    }
  }

  /* construct the path to the static file */
  if (is_static) {
    int pathlen = strlen(url) + strlen(HTDOCS_PREFIX);
    path_buffer = (char *) calloc(pathlen + 1, sizeof(char));
    snprintf(path_buffer, pathlen + 1, "%s%s", HTDOCS_PREFIX, url);
    LOG_DEBUG("PATH BUFFER: '%s' (from url '%s')\n", path_buffer, url);
    filepath = path_buffer;
  }

  /* open the file, regardless if static or template */
  fp = fopen(filepath, "r");
  if (fp) {
    fseek(fp, 0L, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);
  } else {
    LOG_DEBUG("open failed\n");
    return 0;
  }

  /* Directly serve up the response from a file */
  if (is_static) {
    /* destroy will auto close the file, don't call fclose() !!!! */
    LOG_DEBUG("serve static (from '%s'), size: %d\n", filepath, (int) file_size);
    response = MHD_create_response_from_fd(file_size, fileno(fp));
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
  } else {
    /* dynamic template, currently this is only the system setting  */
    int i;
    struct aqx_system_entries *entries = NULL;
    struct stemp_dict *dict = NULL;
    char *template_in = NULL;
    LOG_DEBUG("SERVING DYNAMIC TEMPLATE (from '%s')\n", filepath);

    /* NOTE: we need to be aware that this webserver module is dependent
       on the caller having initialized the aqx client. Otherwise this call
       will fall on its nose
     */
    if ((entries = aqx_get_systems())) {
      int buffersize = 0;
      char *option_buffer;
      dict = stemp_new_dict();
      /* Determine buffer space first */
      for (i = 0; i < entries->num_entries; i++) {
        buffersize += OPTION_TAG_LENGTH;
        buffersize += strlen(entries->entries[i].uid);
        buffersize += strlen(entries->entries[i].name);
      }

      option_buffer = calloc(buffersize + 2, sizeof(char));
      for (i = 0; i < entries->num_entries; i++) {
        strcat(option_buffer, "<option \"");
        strcat(option_buffer, entries->entries[i].uid);
        strcat(option_buffer, "\">");
        strcat(option_buffer, entries->entries[i].name);
        strcat(option_buffer, "</option>");
      }
      stemp_dict_put(dict, "system_options", option_buffer);
      aqx_free_systems(entries);
      free(option_buffer);
      template_in = (char *) calloc(file_size + 1, sizeof(char));
      if (fread(template_in, sizeof(char), file_size, fp) == file_size) {
        /* destroy will auto-free the buffer */
        char *result = stemp_apply_template(template_in, dict);
        response = MHD_create_response_from_buffer(strlen(result), result, MHD_RESPMEM_MUST_FREE);
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
      }
      fclose(fp);
      stemp_free_dict(dict);
      free(template_in);
    }
  }
  if (path_buffer) free(path_buffer);
  return ret;
}

/* POST method requests */
void request_completed (void *cls, struct MHD_Connection *connection, 
                        void **con_cls, enum MHD_RequestTerminationCode toe)
{
  /* this is a no-op */
}

static int iterate_post(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                        const char *filename, const char *content_type,
                        const char *transfer_encoding, const char *data, 
                        uint64_t off, size_t size)
{
  LOG_DEBUG("ITERATE_POST\n");
  struct token_post_data *post_data = (struct token_post_data *) coninfo_cls;

  if (post_data && !strcmp("access-token", key)) {
    strncpy(post_data->token, data, OAUTH2_TOKEN_MAXLEN);
    return MHD_NO;
  }
  return MHD_YES;
}

/*
  The simplest way to implement the post processor handling.
  We create the post processor at the beginning and
  destroy it when there is no more data.
*/
static int handle_post(struct MHD_Connection *connection,
                       const char *url,
                       const char *upload_data, size_t *upload_data_size,
                       void **con_cls)
{
  struct MHD_Response *response;
  /* user data is currently post processor */
  struct token_post_data *post_data = *con_cls;
  int ret = 0;
  if (!strcmp("/enter-token", url)) {
    if (!post_data) {
      post_data = (struct token_post_data *) calloc(1, sizeof(struct token_post_data));
      if (post_data) {
        post_data->pp = MHD_create_post_processor(connection, 1024, iterate_post, post_data);
        *con_cls = (void *) post_data;
      }
      return MHD_YES;
    } else {
      if (*upload_data_size) {
        MHD_post_process(post_data->pp, upload_data, *upload_data_size);
        /* setting upload data to 0 says "processed" */
        *upload_data_size = 0;
        return MHD_YES;
      } else {
        /* No more data */
        /* We implement the PRG pattern here (POST-Redirect-GET) */
        const char *refresh_token;

        LOG_DEBUG("ACCESS TOKEN obtained: %s\n", post_data->token);
        refresh_token = aqx_get_refresh_token(post_data->token);
        if (refresh_token) {
          LOG_DEBUG("refresh token: '%s'\n", refresh_token);
          /* copy the valid token into the configuration */
        }

        MHD_destroy_post_processor(post_data->pp);
        free(post_data);

        response = MHD_create_response_from_buffer(2, "ok", MHD_RESPMEM_PERSISTENT);      
        MHD_add_response_header(response, "Location", "/");
        ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
        MHD_destroy_response(response);
        return ret;
      }
    }
  } else {
    response = MHD_create_response_from_buffer(5, "error", MHD_RESPMEM_PERSISTENT);      
    ret = MHD_queue_response(connection, MHD_HTTP_FOUND, response);
    MHD_destroy_response(response);
    return ret;    
  }
}

static int answer_to_connection (void *cls, struct MHD_Connection *connection,
                                 const char *url,
                                 const char *method, const char *version,
                                 const char *upload_data,
                                 size_t *upload_data_size, void **con_cls)
{
  LOG_DEBUG("Method: '%s', URL: '%s', Version: '%s' upload data: '%s'\n",
            method, url, version, upload_data);
  if (!strcmp("GET", method)) {
    return handle_get(connection, url);
  } else if (!strcmp("POST", method)) {
    return handle_post(connection, url, upload_data, upload_data_size, con_cls);
  }
  return MHD_NO;
}

static void parse_config_line(struct aqxclient_config *cfg, char *line)
{
  /* remove trailing white space */
  int line_end = strlen(line);
  while (line_end > 0 && isspace(line[line_end - 1])) {
    line[line_end - 1] = 0;
    line_end--;
  }

  /* extract configuration settings */
  if (!strncmp(line, MEASUREMENTS_URL_PREFIX, strlen(MEASUREMENTS_URL_PREFIX))) {
    strncpy(cfg->measurements_url, &line[strlen(MEASUREMENTS_URL_PREFIX)],
            sizeof(cfg->measurements_url));
  } else if (!strncmp(line, SYSTEMS_URL_PREFIX, strlen(SYSTEMS_URL_PREFIX))) {
    strncpy(cfg->systems_url, &line[strlen(SYSTEMS_URL_PREFIX)],
            sizeof(cfg->systems_url));
  } else if (!strncmp(line, SYSTEM_UID_PREFIX, strlen(SYSTEM_UID_PREFIX))) {
    strncpy(cfg->system_uid, &line[strlen(SYSTEM_UID_PREFIX)], sizeof(cfg->system_uid));
  } else if (!strncmp(line, REFRESH_TOKEN_PREFIX, strlen(REFRESH_TOKEN_PREFIX))) {
    strncpy(cfg->refresh_token, &line[strlen(REFRESH_TOKEN_PREFIX)], sizeof(cfg->refresh_token));
  } else if (!strncmp(line, SERVICE_PORT_PREFIX, strlen(SERVICE_PORT_PREFIX))) {
    LOG_DEBUG("parsing int at: '%s'\n", &line[strlen(SERVICE_PORT_PREFIX)]);
    cfg->service_port = atoi(&line[strlen(SERVICE_PORT_PREFIX)]);
  }
}

/**********************************************************************
 * Public API
 **********************************************************************/

struct aqxclient_config *read_config()
{
  FILE *fp = fopen("config.ini", "r");
  static char line_buffer[200];

  if (fp) {
    LOG_DEBUG("configuration file opened\n");
    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
      parse_config_line(&client_config, line_buffer);
    }
    LOG_DEBUG("measurements url: '%s', systems url: '%s' system_uid: '%s', refresh_token: '%s'\n",
              client_config.measurements_url, client_config.systems_url,
              client_config.system_uid, client_config.refresh_token);
    fclose(fp);
  }
  return &client_config;
}

struct MHD_Daemon *start_webserver()
{
  return MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, client_config.service_port, NULL, NULL,
                          &answer_to_connection, NULL,
                          MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
                          MHD_OPTION_END);
}
