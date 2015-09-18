#include "webserver.h"
#include "aqxapi_client.h"

#include <stdio.h>

#define HTDOCS_PREFIX "htdocs"

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
  uint64_t size;
  int ret = 0, is_static = 1;
  const char *filepath;
  char *path_buffer = NULL;
  struct aqx_system_entries *entries;


  LOG_DEBUG("Method: '%s', URL: '%s', Version: '%s' upload data: '%s'\n",
            method, url, version, upload_data);
  if (!strcmp(url, "/")) {
    LOG_DEBUG("BLUBBER URL: '%s'\n", url);
    filepath = "htdocs/index.html";
    entries = aqx_get_systems();
    aqx_free_systems(entries);

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
    size = ftell(fp);
    rewind(fp);
  } else {
    LOG_DEBUG("open failed\n");
    return 0;
  }

  /* Directly serve up the response from a file */
  if (is_static) {
    LOG_DEBUG("serve static (from '%s'), size: %d\n", filepath, size);
    response = MHD_create_response_from_fd(size, fileno(fp));
  } else {
    /* dynamic template */
    LOG_DEBUG("SERVING DYNAMIC TEMPLATE (from '%s')\n", filepath);
  }
  ret = MHD_queue_response(connection, MHD_HTTP_OK, response);

  /* destroy will auto close the file, don't call fclose() !!!! */
  MHD_destroy_response(response);

  if (path_buffer) free(path_buffer);
  return ret;
}

struct MHD_Daemon *start_webserver(int port)
{
  return MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port, NULL, NULL,
                            &answer_to_connection, NULL, MHD_OPTION_END);
}
