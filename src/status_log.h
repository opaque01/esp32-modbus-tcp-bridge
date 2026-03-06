#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#define STATUS_LOG_MAX_ENTRIES 50
#define STATUS_LOG_MSG_LEN     128
#define STATUS_LOG_TAG_LEN     24

typedef struct {
    int64_t ts_ms;               /* ms seit Boot (esp_timer_get_time / 1000) */
    char    level;               /* 'E','W','I','D','V' */
    char    tag[STATUS_LOG_TAG_LEN];
    char    msg[STATUS_LOG_MSG_LEN];
} status_log_entry_t;

/* Muss einmal beim Start aufgerufen werden (vor dem Webserver). */
void status_log_init(void);

/* HTTP GET /status Handler — registrierbar in esp_http_server */
esp_err_t status_get_handler(httpd_req_t *req);
