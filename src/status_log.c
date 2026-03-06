#include "status_log.h"
#include "modbus_client.h"
#include "modbus_server.h"
#include "reg_cache.h"
#include "config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Ringpuffer                                                           */
/* ------------------------------------------------------------------ */

static status_log_entry_t s_entries[STATUS_LOG_MAX_ENTRIES];
static int                s_write_idx = 0;  /* nächste Schreibposition */
static int                s_count     = 0;  /* gespeicherte Einträge   */
static SemaphoreHandle_t  s_mutex     = NULL;
static vprintf_like_t     s_orig_vprintf = NULL;

/* ------------------------------------------------------------------ */
/* Hilfsfunktionen                                                      */
/* ------------------------------------------------------------------ */

/* Einfaches JSON-Escape: " → \", \ → \\, Steuerzeichen überspringen */
static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t di = 0;
    for (const char *p = src; *p && di + 3 < dst_size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"') {
            dst[di++] = '\\';
            dst[di++] = '"';
        } else if (c == '\\') {
            dst[di++] = '\\';
            dst[di++] = '\\';
        } else if (c >= 0x20) {
            dst[di++] = (char)c;
        }
        /* Steuerzeichen (< 0x20) werden übersprungen */
    }
    dst[di] = '\0';
}

/* Reads inverter model/name from holding registers 30000+ (block A). */
static void read_inverter_name(char *out, size_t out_size)
{
    out[0] = '\0';
    if (out_size < 2) {
        return;
    }

    uint16_t regs[16];
    for (int i = 0; i < 16; i++) {
        if (reg_cache_lookup((uint16_t)(30000 + i), &regs[i]) != REG_CACHE_OK) {
            return;
        }
    }

    size_t oi = 0;
    for (int i = 0; i < 16 && oi + 1 < out_size; i++) {
        uint8_t b1 = (uint8_t)(regs[i] >> 8);
        uint8_t b2 = (uint8_t)(regs[i] & 0xFF);
        uint8_t bytes[2] = { b1, b2 };
        for (int j = 0; j < 2 && oi + 1 < out_size; j++) {
            uint8_t c = bytes[j];
            if (c == 0x00 || c == 0xFF) {
                continue;
            }
            if (isprint(c) || c == ' ') {
                out[oi++] = (char)c;
            }
        }
    }
    while (oi > 0 && out[oi - 1] == ' ') {
        oi--;
    }
    out[oi] = '\0';
}

/*
 * Parst eine fertig formatierte ESP-IDF-Logzeile.
 * Erwartet: [ANSI-Escape] LEVEL ' (' ms ')' ' ' TAG ':' ' ' MESSAGE '\n'
 * Beispiel: "\033[0;32mI (12345) MY_TAG: hello world\033[0m\n"
 */
static void parse_log_line(const char *line, char *level_out,
                            char *tag_out,  size_t tag_size,
                            char *msg_out,  size_t msg_size)
{
    const char *p = line;

    /* ANSI-Escape am Anfang überspringen */
    if (*p == '\033') {
        while (*p && *p != 'm') p++;
        if (*p == 'm') p++;
    }

    /* Log-Level (1 Zeichen) */
    *level_out = *p ? *p : '?';
    if (*p) p++;

    /* Bis zur öffnenden Klammer '(' */
    while (*p && *p != '(') p++;
    if (*p == '(') p++;

    /* Zeitstempel überspringen */
    while (*p && *p != ')') p++;
    if (*p == ')') p++;
    while (*p == ' ') p++;

    /* Tag lesen (bis ':') */
    size_t ti = 0;
    while (*p && *p != ':' && ti + 1 < tag_size) {
        tag_out[ti++] = *p++;
    }
    tag_out[ti] = '\0';

    /* ': ' überspringen */
    if (*p == ':') p++;
    while (*p == ' ') p++;

    /* Nachricht lesen (bis '\n' oder ANSI-Reset) */
    size_t mi = 0;
    while (*p && *p != '\n' && *p != '\033' && mi + 1 < msg_size) {
        msg_out[mi++] = *p++;
    }
    msg_out[mi] = '\0';
}

/* ------------------------------------------------------------------ */
/* ESP-IDF Log-Hook                                                     */
/* ------------------------------------------------------------------ */

static int status_log_vprintf(const char *fmt, va_list args)
{
    /* Kopie erstellen, bevor die Original-Funktion args verbraucht */
    va_list args_copy;
    va_copy(args_copy, args);

    /* Original-UART-Ausgabe (wie bisher) */
    int ret = s_orig_vprintf ? s_orig_vprintf(fmt, args) : vprintf(fmt, args);

    /* In Ringpuffer schreiben */
    if (s_mutex) {
        char raw[512];
        vsnprintf(raw, sizeof(raw), fmt, args_copy);

        status_log_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        entry.ts_ms = esp_timer_get_time() / 1000;
        parse_log_line(raw, &entry.level,
                       entry.tag, sizeof(entry.tag),
                       entry.msg, sizeof(entry.msg));

        if (xSemaphoreTake(s_mutex, 0) == pdTRUE) {  /* nicht blockieren */
            s_entries[s_write_idx] = entry;
            s_write_idx = (s_write_idx + 1) % STATUS_LOG_MAX_ENTRIES;
            if (s_count < STATUS_LOG_MAX_ENTRIES) s_count++;
            xSemaphoreGive(s_mutex);
        }
    }

    va_end(args_copy);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Init                                                                 */
/* ------------------------------------------------------------------ */

void status_log_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_orig_vprintf = esp_log_set_vprintf(status_log_vprintf);
}

/* ------------------------------------------------------------------ */
/* HTTP GET /status Handler                                             */
/* ------------------------------------------------------------------ */

esp_err_t status_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    /* Snapshot des Ringpuffers */
    status_log_entry_t *snapshot = malloc(sizeof(status_log_entry_t) * STATUS_LOG_MAX_ENTRIES);
    int count     = 0;
    int write_idx = 0;
    if (snapshot && s_mutex != NULL) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            count     = s_count;
            write_idx = s_write_idx;
            memcpy(snapshot, s_entries, sizeof(s_entries));
            xSemaphoreGive(s_mutex);
        }
    } else if (snapshot) {
        free(snapshot);
        snapshot = NULL;
    }

    /* Header */
    char inverter_name[64];
    char inverter_name_esc[128];
    read_inverter_name(inverter_name, sizeof(inverter_name));
    json_escape(inverter_name, inverter_name_esc, sizeof(inverter_name_esc));

    char buf[512];
    char client_ips[16][16];
    int client_ip_count = modbus_server_get_client_ips((char *)client_ips, sizeof(client_ips[0]), 16);
    int64_t uptime_s = esp_timer_get_time() / 1000000LL;
    snprintf(buf, sizeof(buf),
        "{\"uptime_s\":%lld,"
        "\"firmware_version\":\"%s\","
        "\"inverter_connected\":%s,"
        "\"inverter_name\":\"%s\","
        "\"last_poll_ts\":%lld,"
        "\"modbus_clients\":%d,"
        "\"client_ips\":[",
        (long long)uptime_s,
        APP_FIRMWARE_VERSION,
        mb_inverter_connected ? "true" : "false",
        inverter_name_esc,
        (long long)mb_last_poll_ts,
        modbus_server_get_client_count());
    httpd_resp_sendstr_chunk(req, buf);

    for (int i = 0; i < client_ip_count; i++) {
        char ip_esc[40];
        json_escape(client_ips[i], ip_esc, sizeof(ip_esc));
        snprintf(buf, sizeof(buf), "%s\"%s\"", (i == 0) ? "" : ",", ip_esc);
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "],\"log\":[");

    /* Log-Einträge (neueste zuerst) */
    if (snapshot) {
        char escaped_tag[STATUS_LOG_TAG_LEN * 2 + 4];
        char escaped_msg[STATUS_LOG_MSG_LEN * 2 + 4];
        bool first = true;
        for (int i = 0; i < count; i++) {
            int idx = ((write_idx - 1 - i) + STATUS_LOG_MAX_ENTRIES) % STATUS_LOG_MAX_ENTRIES;
            status_log_entry_t *e = &snapshot[idx];

            json_escape(e->tag, escaped_tag, sizeof(escaped_tag));
            json_escape(e->msg, escaped_msg, sizeof(escaped_msg));

            snprintf(buf, sizeof(buf),
                "%s{\"ts\":%lld,\"level\":\"%c\",\"tag\":\"%s\",\"msg\":\"%s\"}",
                first ? "" : ",",
                (long long)(e->ts_ms / 1000),
                e->level,
                escaped_tag,
                escaped_msg);
            httpd_resp_sendstr_chunk(req, buf);
            first = false;
        }
        free(snapshot);
    }

    /* Footer — Chunked Transfer abschließen */
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}
