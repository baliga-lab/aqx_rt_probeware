#pragma once
#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__
#include <microhttpd.h>
#include "aqxapi_client.h"

extern struct aqx_client_options *read_config();
extern struct MHD_Daemon *start_webserver();

#endif /* __WEBSERVER_H__ */
