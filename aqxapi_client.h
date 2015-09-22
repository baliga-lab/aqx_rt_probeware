#pragma once
#ifndef __AQXAPI_CLIENT_H__
#define __AQXAPI_CLIENT_H__

#include <time.h>

/* Maximum number of characters for an OAuth2 token */
#define OAUTH2_TOKEN_MAXLEN 100

/*
 * These are the measurement entities that are stored
 * and submitted.
 */
struct aqx_measurement {
  time_t time;
  double temperature, ph, o2, nitrate, ammonium, light;
};

struct aqx_system_info {
  char *uid;
  char *name;
};

struct aqx_system_entries {
  int num_entries;
  struct aqx_system_info *entries;
};

struct aqx_client_options {
  const char *add_measurements_url; /* the service's URL */
  const char *get_systems_url; /* the service's URL */
  const char *system_uid; /* System's UID */
  const char *oauth2_refresh_token; /* OAuth2 refresh token from Google */
  unsigned int send_interval_secs; /* send update every x seconds */
};

extern int aqx_client_init(struct aqx_client_options *options);
extern struct aqx_system_entries *aqx_get_systems();
extern void aqx_free_systems(struct aqx_system_entries *entries);
extern const char *aqx_get_refresh_token(const char *initial_code);

extern int aqx_add_measurement(struct aqx_measurement *m);
/* force send everything available */
extern void aqx_client_flush();
extern void aqx_client_cleanup();

#ifdef DEBUG
#define LOG_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else 
#define LOG_DEBUG(...)
#endif

#endif /* __AQXAPI_CLIENT_H__ */
