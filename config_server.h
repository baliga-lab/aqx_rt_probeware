#pragma once
#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__
#include <microhttpd.h>

struct aqxclient_config {
  int service_port;
  char measurements_url[200];
  char systems_url[200];
  char system_uid[48];
  char refresh_token[100];
  int send_interval_secs;
};

extern struct aqxclient_config *read_config();
extern struct MHD_Daemon *start_webserver();

#endif /* __WEBSERVER_H__ */
