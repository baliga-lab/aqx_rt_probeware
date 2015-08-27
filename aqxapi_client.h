#pragma once
#ifndef __AQXAPI_CLIENT_H__
#define __AQXAPI_CLIENT_H__

#include <time.h>

/*
 * These are the measurement entities that are stored
 * and submitted.
 */
struct aqx_measurement {
  time_t time;
  double temperature, ph, o2, nitrate, ammonium, light;
};

struct aqx_client_options {
  const char *system_uid; /* System's UID */
  const char *oauth2_refresh_token; /* OAuth2 refresh token from Google */
  unsigned int send_interval_secs; /* send update every x seconds */
};

extern int aqx_client_init(struct aqx_client_options *options);
extern int aqx_add_measurement(struct aqx_measurement *m);
/* force send everything available */
extern void aqx_client_flush();
extern void aqx_client_cleanup();

#define LOG_DEBUG(...) fprintf(stderr, __VA_ARGS__)

#endif /* __AQXAPI_CLIENT_H__ */
