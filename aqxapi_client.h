#pragma once
#ifndef __AQXAPI_CLIENT_H__
#define __AQXAPI_CLIENT_H__

#include <time.h>

/* Maximum number of characters for an OAuth2 token */
#define OAUTH2_TOKEN_MAXLEN 100
#define SYSTEM_UID_MAXLEN 48

#define API_MEASUREMENT_TIME          "time"
#define API_MEASUREMENT_TYPE_TEMP     "temp"
#define API_MEASUREMENT_TYPE_PH       "ph"
#define API_MEASUREMENT_TYPE_DIO      "o2"
#define API_MEASUREMENT_TYPE_LIGHT    "light"
#define API_MEASUREMENT_TYPE_AMMONIUM "ammonium"
#define API_MEASUREMENT_TYPE_NITRATE  "nitrate"
#define API_MEASUREMENT_TYPE_NITRITE  "nitrite"

/*
 * These are the measurement entities that are stored
 * and submitted.
 */
struct aqx_measurement {
    time_t time;
    double temperature, ph, o2, nitrate, ammonium, light, nitrite;
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
  int service_port;
  char system_uid[SYSTEM_UID_MAXLEN + 1]; /* System's UID */
  char oauth2_refresh_token[OAUTH2_TOKEN_MAXLEN + 1]; /* OAuth2 refresh token from Google */
};

/* 
   Private API
   don't call these functions directly, these are called by the config_server
   after a user configuration update
*/
extern int aqx_client_init_api();
extern void aqx_client_update_refresh_token(const char *token);
extern void aqx_client_update_system(const char *system_uid);

extern struct aqx_system_entries *aqx_get_systems();
extern void aqx_free_systems(struct aqx_system_entries *entries);
extern const char *aqx_get_refresh_token(const char *initial_code);

/* Public API */
extern int aqx_add_measurement(struct aqx_measurement *m);
/* force send everything available */
extern void aqx_client_flush();
extern void aqx_client_cleanup();

/* read config and initialize the api client library  */
extern struct aqx_client_options *aqx_client_init();

/* start the web server for user configuration */
extern struct MHD_Daemon *start_webserver();


#ifdef DEBUG
#define LOG_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else 
#define LOG_DEBUG(...)
#endif

#endif /* __AQXAPI_CLIENT_H__ */
