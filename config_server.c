#include "config_server.h"
#include "aqxapi_client.h"
#include "simple_templates.h"

#include <stdio.h>
#include <stdlib.h>

#define HTDOCS_PREFIX "htdocs"
/* Length of the opening and closing option tags <option "..."></option> */
#define OPTION_TAG_LENGTH (11 + 9)

/*
 * Handle the HTTP server requests here.
 */
static int answer_to_connection (void *cls, struct MHD_Connection *connection,
                                 const char *url,
                                 const char *method, const char *version,
                                 const char *upload_data,
                                 size_t *upload_data_size, void **con_cls)
{
  struct MHD_Response *response;
  FILE *fp;
  uint64_t file_size;
  int ret = 0, is_static = 1;
  const char *filepath;
  char *path_buffer = NULL;

  LOG_DEBUG("Method: '%s', URL: '%s', Version: '%s' upload data: '%s'\n",
            method, url, version, upload_data);
  if (!strcmp(url, "/")) {
    LOG_DEBUG("BLUBBER URL: '%s'\n", url);
    filepath = "templates/index.html";
    is_static = 0;
  } else {
    int pathlen = strlen(url) + strlen(HTDOCS_PREFIX);
    path_buffer = (char *) malloc(pathlen + 1);
    memset(path_buffer, 0, pathlen + 1);
    snprintf(path_buffer, pathlen + 1, "%s%s", HTDOCS_PREFIX, url);
    LOG_DEBUG("PATH BUFFER: '%s' (from url '%s')\n", path_buffer, url);

    filepath = path_buffer;
  }

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
    LOG_DEBUG("serve static (from '%s'), size: %d\n", filepath, file_size);
    response = MHD_create_response_from_fd(file_size, fileno(fp));
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
  } else {
    /* dynamic template */
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

struct MHD_Daemon *start_webserver(int port)
{
  return MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
                            &answer_to_connection, NULL, MHD_OPTION_END);
}
