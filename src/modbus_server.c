/*
 * Modbus TCP Server — multiplexes the inverter cache to multiple clients.
 *
 * Design decisions (see design.md):
 *  - One FreeRTOS task per client (no artificial connection limit).
 *  - Proxy mode (always on): serves native Huawei register addresses from cache.
 *  - Mapping mode (optional): additionally serves row-indexed mapped registers,
 *    applying user-configured factor/scaling to the source register value.
 *  - Stale cache → Modbus Exception 0x04 (Server Device Failure).
 *    Never serves old values while the inverter is disconnected.
 *  - Unknown address → Modbus Exception 0x02 (Illegal Data Address).
 *  - Only FC03 (Read Holding Registers) is implemented; all others → Exception 0x01.
 */
#include "modbus_server.h"
#include "reg_cache.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static const char *TAG = "modbus_server";

#define MB_SERVER_PORT     502
#define CLIENT_TASK_STACK  4096
#define CLIENT_TASK_PRIO   5
#define MAX_TRACKED_CLIENTS 16
#define CLIENT_IP_LEN       16

static volatile int s_client_count = 0;
static char s_client_ips[MAX_TRACKED_CLIENTS][CLIENT_IP_LEN];
static SemaphoreHandle_t s_clients_mutex = NULL;

int modbus_server_get_client_count(void)
{
    return s_client_count;
}

int modbus_server_get_client_ips(char *out, size_t out_item_len, int max_items)
{
    if (!out || out_item_len == 0 || max_items <= 0 || s_clients_mutex == NULL) {
        return 0;
    }

    int copied = 0;
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < MAX_TRACKED_CLIENTS && copied < max_items; i++) {
            if (s_client_ips[i][0] != '\0') {
                char *dst = out + ((size_t)copied * out_item_len);
                snprintf(dst, out_item_len, "%s", s_client_ips[i]);
                copied++;
            }
        }
        xSemaphoreGive(s_clients_mutex);
    }
    return copied;
}

static int register_client_ip(const char *ip)
{
    if (!ip || !*ip || s_clients_mutex == NULL) {
        return -1;
    }

    int slot = -1;
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        for (int i = 0; i < MAX_TRACKED_CLIENTS; i++) {
            if (s_client_ips[i][0] == '\0') {
                snprintf(s_client_ips[i], sizeof(s_client_ips[i]), "%s", ip);
                slot = i;
                break;
            }
        }
        xSemaphoreGive(s_clients_mutex);
    }
    return slot;
}

static void unregister_client_ip(int slot)
{
    if (slot < 0 || slot >= MAX_TRACKED_CLIENTS || s_clients_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_client_ips[slot][0] = '\0';
        xSemaphoreGive(s_clients_mutex);
    }
}

/* Receive exactly len bytes; returns len on success, -1 on peer disconnect/error */
static int recv_all(int sock, void *buf, int len)
{
    int total = 0;
    while (total < len) {
        int n = recv(sock, (char *)buf + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

static void send_exception(int sock, uint16_t tid, uint8_t unit_id,
                            uint8_t fc, uint8_t ex_code)
{
    uint8_t resp[9];
    resp[0] = (tid >> 8) & 0xFF;
    resp[1] =  tid & 0xFF;
    resp[2] = 0x00;
    resp[3] = 0x00;
    resp[4] = 0x00;
    resp[5] = 0x03;       /* Length: unit_id + error-FC + exception code */
    resp[6] = unit_id;
    resp[7] = fc | 0x80;  /* Error function code */
    resp[8] = ex_code;
    send(sock, resp, sizeof(resp), 0);
}

/*
 * Resolve a single register address to a uint16_t value.
 *
 * Lookup order:
 *  1. Proxy: native Huawei addresses (30000+, 32000+, 37000+, 38000+)
 *  2. Mapping: addresses 1..mapping_count → row (addr-1), apply factor to source reg
 *
 * Returns REG_CACHE_OK, REG_CACHE_STALE, or REG_CACHE_NOT_FOUND.
 */
static int resolve_register(uint16_t addr, uint16_t *out_value)
{
    /* 1. Proxy — native Huawei cache blocks */
    int result = reg_cache_lookup(addr, out_value);
    if (result == REG_CACHE_NOT_FOUND && addr > 0) {
        /* Compatibility: some clients use 1-based notation (e.g. 30001 for 30000). */
        result = reg_cache_lookup((uint16_t)(addr - 1), out_value);
    }
    if (result != REG_CACHE_NOT_FOUND) {
        return result;   /* REG_CACHE_OK or REG_CACHE_STALE */
    }

    /* 2. Mapping — 1-based row index */
    if (s_config.mapping_enabled && addr >= 1 && addr <= s_config.mapping_count) {
        uint16_t src = s_config.mapping[addr - 1].register_addr;
        uint16_t raw;
        result = reg_cache_lookup(src, &raw);
        if (result == REG_CACHE_OK) {
            float factor = s_config.mapping[addr - 1].factor;
            *out_value   = (uint16_t)((float)raw * factor);
            return REG_CACHE_OK;
        }
        return result;   /* REG_CACHE_STALE if source block is invalid */
    }

    return REG_CACHE_NOT_FOUND;
}

/*
 * Handle FC03 — Read Holding Registers.
 * Resolves each requested register; returns a single exception if any is stale/missing.
 */
static void handle_read_regs(int sock, uint16_t tid, uint8_t unit_id, uint8_t req_fc,
                             const uint8_t *pdu, uint16_t pdu_len)
{
    if (pdu_len < 5) {
        send_exception(sock, tid, unit_id, req_fc, 0x03); /* Illegal Data Value */
        return;
    }

    uint16_t start_reg = ((uint16_t)pdu[1] << 8) | pdu[2];
    uint16_t reg_count = ((uint16_t)pdu[3] << 8) | pdu[4];

    if (reg_count == 0 || reg_count > 125) {
        send_exception(sock, tid, unit_id, req_fc, 0x03);
        return;
    }

    uint16_t values[125];
    for (uint16_t i = 0; i < reg_count; i++) {
        int r = resolve_register(start_reg + i, &values[i]);
        if (r == REG_CACHE_STALE) {
            send_exception(sock, tid, unit_id, req_fc, 0x04); /* Server Device Failure */
            return;
        }
        if (r == REG_CACHE_NOT_FOUND) {
            send_exception(sock, tid, unit_id, req_fc, 0x02); /* Illegal Data Address */
            return;
        }
    }

    /* Build FC03 response */
    uint8_t  byte_count = (uint8_t)(reg_count * 2);
    uint16_t resp_len   = 1 + 1 + 1 + byte_count; /* unit_id + FC + byte_count + data */
    uint8_t  resp[7 + 1 + 1 + 125 * 2];

    resp[0] = (tid >> 8) & 0xFF;
    resp[1] =  tid & 0xFF;
    resp[2] = 0x00;
    resp[3] = 0x00;
    resp[4] = (resp_len >> 8) & 0xFF;
    resp[5] =  resp_len & 0xFF;
    resp[6] = unit_id;
    resp[7] = req_fc;        /* FC03/FC04 */
    resp[8] = byte_count;

    for (uint16_t i = 0; i < reg_count; i++) {
        resp[9 + i * 2]     = (values[i] >> 8) & 0xFF;
        resp[9 + i * 2 + 1] =  values[i] & 0xFF;
    }
    send(sock, resp, 9 + byte_count, 0);
}

/* Per-client task — receives Modbus TCP frames and dispatches them */
typedef struct {
    int sock;
    char ip[CLIENT_IP_LEN];
} client_arg_t;

static void client_task(void *pvParameters)
{
    client_arg_t *arg = (client_arg_t *)pvParameters;
    int sock = arg->sock;
    int slot = register_client_ip(arg->ip);
    free(arg);

    s_client_count++;
    ESP_LOGI(TAG, "Client connected (total: %d)", s_client_count);

    uint8_t buf[260]; /* MBAP (7) + PDU (max 253) */

    while (1) {
        /* Read MBAP header — blocks until data arrives or peer disconnects */
        if (recv_all(sock, buf, 7) != 7) break;

        uint16_t tid     = ((uint16_t)buf[0] << 8) | buf[1];
        /* buf[2:3] = protocol id, should be 0x0000 */
        uint16_t length  = ((uint16_t)buf[4] << 8) | buf[5]; /* unit_id + PDU */
        uint8_t  unit_id = buf[6];

        if (length < 2 || length > 253) break; /* malformed frame */

        uint16_t pdu_len = length - 1; /* strip unit_id */
        if (recv_all(sock, buf + 7, pdu_len) != pdu_len) break;

        uint8_t fc = buf[7];
        if (fc == 0x03 || fc == 0x04) {
            handle_read_regs(sock, tid, unit_id, fc, buf + 7, pdu_len);
        } else {
            /* All other function codes are unsupported */
            ESP_LOGW(TAG, "Unsupported FC 0x%02X", fc);
            send_exception(sock, tid, unit_id, fc, 0x01); /* Illegal Function */
        }
    }

    close(sock);
    unregister_client_ip(slot);
    s_client_count--;
    ESP_LOGI(TAG, "Client disconnected (total: %d)", s_client_count);
    vTaskDelete(NULL);
}

/* Accept loop — creates a client task for every incoming connection */
static void server_task(void *pvParameters)
{
    int server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(MB_SERVER_PORT),
    };
    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind() failed: %d", errno);
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }
    if (listen(server_sock, 8) != 0) {
        ESP_LOGE(TAG, "listen() failed: %d", errno);
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Listening on port %d", MB_SERVER_PORT);

    while (1) {
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int client_sock = accept(server_sock, (struct sockaddr *)&peer, &peer_len);
        if (client_sock < 0) {
            ESP_LOGW(TAG, "accept() failed: %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        client_arg_t *arg = malloc(sizeof(client_arg_t));
        if (!arg) {
            ESP_LOGE(TAG, "malloc failed — dropping client");
            close(client_sock);
            continue;
        }
        arg->sock = client_sock;
        uint32_t ip = ntohl(peer.sin_addr.s_addr);
        snprintf(arg->ip, sizeof(arg->ip), "%u.%u.%u.%u",
                 (unsigned)((ip >> 24) & 0xFF),
                 (unsigned)((ip >> 16) & 0xFF),
                 (unsigned)((ip >> 8) & 0xFF),
                 (unsigned)(ip & 0xFF));

        if (xTaskCreate(client_task, "mb_srv_cli", CLIENT_TASK_STACK,
                        arg, CLIENT_TASK_PRIO, NULL) != pdPASS) {
            ESP_LOGE(TAG, "xTaskCreate failed — dropping client");
            free(arg);
            close(client_sock);
        }
    }
}

void modbus_server_start(void)
{
    if (s_clients_mutex == NULL) {
        s_clients_mutex = xSemaphoreCreateMutex();
    }
    memset(s_client_ips, 0, sizeof(s_client_ips));
    xTaskCreate(server_task, "mb_server", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Modbus TCP server started (port %d)", MB_SERVER_PORT);
}
