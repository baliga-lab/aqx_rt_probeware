#pragma once
#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__
#include <microhttpd.h>

extern struct MHD_Daemon *start_webserver(int port);

#endif /* __WEBSERVER_H__ */
