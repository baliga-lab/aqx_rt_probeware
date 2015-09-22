#pragma once
#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__
#include <microhttpd.h>
#include "aqxapi_client.h"

/* read config and initialize the api client library  */
extern struct aqx_client_options *aqx_client_init();

/* start the web server for user configuration */
extern struct MHD_Daemon *start_webserver();

#endif /* __WEBSERVER_H__ */
