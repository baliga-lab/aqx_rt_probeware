#pragma once
#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__
#include <microhttpd.h>

struct aqxclient_config {
  int service_port;
  char system_uid[48];
  char refresh_token[100];
};

extern struct aqxclient_config *read_config();
extern struct MHD_Daemon *start_webserver();

#endif /* __WEBSERVER_H__ */
