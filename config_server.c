/* config_server.c - Configuration server
 *
 * Description: Configuration is handled here, the user can modify the
 * ------------ settings through the embedded web interface
 * Note: the latest binary build of the libmicrohttpd library (0.9.35-w32)
 * has issues with MHD_create_response_from_fd(), furthermore it has issues
 * when using MHD_create_response_from_buffer() in combination with
 * MHD_RESPMEM_MUST_FREE. We work around the issue by using
 * MHD_create_response_from_buffer() only with the MHD_RESPMEM_MUST_COPY
 * and MHD_RESPMEM_PERSISTENT flags.
 */
#include "aqxapi_client.h"
#include "simple_templates.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <microhttpd.h>

#define HTDOCS_PREFIX "htdocs"
/* Length of the opening and closing option tags <option "..."></option> */
#define OPTION_TAG_LENGTH (17 + 9)
#define REFRESH_TOKEN_PREFIX "refresh_token="
#define SYSTEM_UID_PREFIX    "system_uid="
#define SERVICE_PORT_PREFIX  "service_port="
#define DEFAULT_SERVICE_PORT 8080

#define INI_PATH "config.ini"

static struct aqx_client_options client_config;

struct token_post_data {
    struct MHD_PostProcessor *pp;
    char token[OAUTH2_TOKEN_MAXLEN + 1];
};

struct system_post_data {
    struct MHD_PostProcessor *pp;
    char system_uid[SYSTEM_UID_MAXLEN + 1];
};

/* Store updated configuration */
static int save_configuration()
{
    FILE *fp = fopen(INI_PATH, "w");
    if (fp) {
        fprintf(fp, "refresh_token=%s\nsystem_uid=%s\nservice_port=%d\n",
                client_config.oauth2_refresh_token,
                client_config.system_uid,
                client_config.service_port);
        fclose(fp);
        return 1;
    }
    return 0;
}

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

    LOG_DEBUG("token: %s\n", client_config.oauth2_refresh_token);
    has_token = strlen(client_config.oauth2_refresh_token);

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
        int pathlen = strlen(url) + strlen(HTDOCS_PREFIX) + 1;
        path_buffer = (char *) calloc(pathlen, sizeof(char));
        snprintf(path_buffer, pathlen, "%s%s", HTDOCS_PREFIX, url);
        LOG_DEBUG("PATH BUFFER: '%s' (from url '%s')\n", path_buffer, url);
        filepath = path_buffer;
    }

    /* open the file, regardless if static or template */
    fp = fopen(filepath, "rb");
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
        /* Note: previously I used MHD_create_response_from_fd(). It turns out
           that this function does not properly work on MS Windows, so instead,
           we are now using MHD_create_response_from_buffer() instead. */
        char *data = (char *) malloc(file_size);
        LOG_DEBUG("serve static (from '%s'), size: %d\n", filepath, (int) file_size);
        if (fread(data, sizeof(char), file_size, fp) == file_size) {
            response = MHD_create_response_from_buffer(file_size, data, MHD_RESPMEM_MUST_COPY);
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            MHD_destroy_response(response);
        } else {
            /* error (TODO handle) */
        }
        free(data);
        fclose(fp);
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

            /* option buffer is the space used by the tags, and possibly
               a selected attribute */
            option_buffer = calloc(buffersize + 12, sizeof(char));
            stemp_dict_put(dict, "current_system", "none");
            for (i = 0; i < entries->num_entries; i++) {
                strcat(option_buffer, "<option value=\"");
                strcat(option_buffer, entries->entries[i].uid);
                strcat(option_buffer, "\"");
                if (!strcmp(client_config.system_uid, entries->entries[i].uid)) {
                    stemp_dict_put(dict, "current_system", entries->entries[i].name);
                    strcat(option_buffer, " selected");
                }
                strcat(option_buffer, ">");
                strcat(option_buffer, entries->entries[i].name);
                strcat(option_buffer, "</option>");
            }
            stemp_dict_put(dict, "system_options", option_buffer);
            aqx_free_systems(entries);
            free(option_buffer);
            template_in = (char *) calloc(file_size + 1, sizeof(char));
            if (fread(template_in, sizeof(char), file_size, fp) == file_size) {
                char *result = stemp_apply_template(template_in, dict);
                response = MHD_create_response_from_buffer(strlen(result), result, MHD_RESPMEM_MUST_COPY);
                free(result);
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
    struct token_post_data *post_data = (struct token_post_data *) coninfo_cls;
    if (post_data && !strcmp("access-token", key)) {
        strncpy(post_data->token, data, OAUTH2_TOKEN_MAXLEN);
        return MHD_NO;
    }
    return MHD_YES;
}

static int iterate_system_post(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                               const char *filename, const char *content_type,
                               const char *transfer_encoding, const char *data, 
                               uint64_t off, size_t size)
{
    struct system_post_data *post_data = (struct system_post_data *) coninfo_cls;
    if (post_data && !strcmp("system", key)) {
        strncpy(post_data->system_uid, data, SYSTEM_UID_MAXLEN);
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
    int ret = 0;
    if (!strcmp("/enter-token", url)) {
        struct token_post_data *post_data = (struct token_post_data *) *con_cls;
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
                    /* copy the valid token into the configuration and save */
                    LOG_DEBUG("new refresh token: '%s'\n", refresh_token);
                    strncpy(client_config.oauth2_refresh_token, refresh_token, OAUTH2_TOKEN_MAXLEN);
                    if (save_configuration()) {
                        aqx_client_update_refresh_token(client_config.oauth2_refresh_token);
                    }
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
    } else if (!strcmp("/set-system", url)) {
        struct system_post_data *post_data = (struct system_post_data *) *con_cls;
        if (!post_data) {
            post_data = (struct system_post_data *) calloc(1, sizeof(struct system_post_data));
            if (post_data) {
                post_data->pp = MHD_create_post_processor(connection, 1024, iterate_system_post,
                                                          post_data);
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
                LOG_DEBUG("System UID obtained: %s\n", post_data->system_uid);
                strncpy(client_config.system_uid, post_data->system_uid, SYSTEM_UID_MAXLEN);
                if (save_configuration()) {
                    aqx_client_update_system(client_config.system_uid);
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

static void parse_config_line(struct aqx_client_options *cfg, char *line)
{
    /* remove trailing white space */
    int line_end = strlen(line);
    while (line_end > 0 && isspace(line[line_end - 1])) {
        line[line_end - 1] = 0;
        line_end--;
    }

    /* extract configuration settings */
    if (!strncmp(line, SYSTEM_UID_PREFIX, strlen(SYSTEM_UID_PREFIX))) {
        strncpy(cfg->system_uid, &line[strlen(SYSTEM_UID_PREFIX)], sizeof(cfg->system_uid));
    } else if (!strncmp(line, REFRESH_TOKEN_PREFIX, strlen(REFRESH_TOKEN_PREFIX))) {
        strncpy(cfg->oauth2_refresh_token, &line[strlen(REFRESH_TOKEN_PREFIX)],
                sizeof(cfg->oauth2_refresh_token));
    } else if (!strncmp(line, SERVICE_PORT_PREFIX, strlen(SERVICE_PORT_PREFIX))) {
        LOG_DEBUG("parsing int at: '%s'\n", &line[strlen(SERVICE_PORT_PREFIX)]);
        cfg->service_port = atoi(&line[strlen(SERVICE_PORT_PREFIX)]);
    }
}

/**********************************************************************
 * Public API
 **********************************************************************/

/*
 * This is the entry point of the API client library, reading the
 * configuration.
 */
struct aqx_client_options *aqx_client_init()
{
    FILE *fp = fopen(INI_PATH, "r");
    static char line_buffer[200];

    if (fp) {
        LOG_DEBUG("configuration file opened\n");
        while (fgets(line_buffer, sizeof(line_buffer), fp)) {
            parse_config_line(&client_config, line_buffer);
        }
        LOG_DEBUG("system_uid: '%s', refresh_token: '%s'\n",
                  client_config.system_uid, client_config.oauth2_refresh_token);
        fclose(fp);
    }
    if (!client_config.service_port) {
        client_config.service_port = DEFAULT_SERVICE_PORT;
    }
    aqx_client_update_refresh_token(client_config.oauth2_refresh_token);
    aqx_client_update_system(client_config.system_uid);
    aqx_client_init_api();
    return &client_config;
}

struct MHD_Daemon *start_webserver()
{
    LOG_DEBUG("starting server on port: %d\n", client_config.service_port);
    return MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, client_config.service_port, NULL, NULL,
                            &answer_to_connection, NULL,
                            MHD_OPTION_NOTIFY_COMPLETED, &request_completed, NULL,
                            MHD_OPTION_END);
}
