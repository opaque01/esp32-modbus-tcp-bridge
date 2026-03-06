/*
 * Modbus TCP Client — reads register blocks from the Huawei Sun 2000 inverter.
 *
 * Design decisions (see design.md):
 *  - Raw FC03 over lwIP socket, no external library.
 *  - 4 fixed cache blocks (A static once, B+C+D every poll_interval seconds).
 *  - On connection failure: invalidate cache → server returns Exception 0x04.
 *  - No exponential back-off; retry interval equals poll_interval (≥60 s).
 */
#include "modbus_client.h"
#include "reg_cache.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>
#include <errno.h>
#include <time.h>
#include "esp_system.h"

static const char *TAG = "modbus_client";

volatile time_t mb_last_poll_ts       = 0;
volatile bool   mb_inverter_connected  = false;

#define MB_RECV_TIMEOUT_S      8
#define MB_CONNECT_SETTLE_MS 800
#define MB_BLOCK_DELAY_MS    300
#define MB_BACKOFF_MAX_S     900
#define MB_CHUNK_REGS_MAX     72

static int fc03_read(int sock, uint16_t start_reg, uint16_t count,
                     uint16_t *out_data, uint8_t unit_id);

static uint32_t compute_backoff_s(uint32_t base_s, uint8_t fail_count)
{
    uint32_t backoff = (base_s < 60) ? 60 : base_s;
    for (uint8_t i = 0; i < fail_count && backoff < MB_BACKOFF_MAX_S; i++) {
        backoff *= 2;
        if (backoff > MB_BACKOFF_MAX_S) {
            backoff = MB_BACKOFF_MAX_S;
            break;
        }
    }
    return backoff;
}

static int fc03_read_retry(int sock, uint16_t start_reg, uint16_t count,
                           uint16_t *out_data, uint8_t unit_id, int tries)
{
    for (int i = 0; i < tries; i++) {
        int r = fc03_read(sock, start_reg, count, out_data, unit_id);
        if (r >= (int)count) {
            return r;
        }
        if (i + 1 < tries) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
    return -1;
}

static bool read_block_chunked(int sock, uint16_t start_reg, uint16_t count,
                               uint16_t *out_data, uint8_t unit_id)
{
    uint16_t done = 0;
    while (done < count) {
        uint16_t chunk = count - done;
        if (chunk > MB_CHUNK_REGS_MAX) {
            chunk = MB_CHUNK_REGS_MAX;
        }
        int r = fc03_read_retry(sock, start_reg + done, chunk, out_data + done, unit_id, 3);
        if (r < (int)chunk) {
            return false;
        }
        done += chunk;
        if (done < count) {
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
    return true;
}

/* Receive exactly len bytes; returns len on success, -1 on error/disconnect */
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

/*
 * Send an FC03 "Read Holding Registers" request and receive the response.
 * Returns count (number of registers read) on success, -1 on error.
 *
 * MBAP (7 bytes) + PDU request (5 bytes) = 12 bytes total.
 * Response: MBAP (7) + FC (1) + byte_count (1) + data (count×2).
 */
static int fc03_read(int sock, uint16_t start_reg, uint16_t count,
                     uint16_t *out_data, uint8_t unit_id)
{
    /* Transaction ID — monotonically incremented, wraps on overflow */
    static uint16_t s_tid = 0;
    s_tid++;

    uint8_t req[12];
    req[0]  = (s_tid >> 8) & 0xFF;
    req[1]  =  s_tid & 0xFF;
    req[2]  = 0x00;  /* Protocol ID high */
    req[3]  = 0x00;  /* Protocol ID low  */
    req[4]  = 0x00;  /* Length high (PDU = 6 bytes) */
    req[5]  = 0x06;  /* Length low                  */
    req[6]  = unit_id;
    req[7]  = 0x03;  /* FC03 */
    req[8]  = (start_reg >> 8) & 0xFF;
    req[9]  =  start_reg & 0xFF;
    req[10] = (count >> 8) & 0xFF;
    req[11] =  count & 0xFF;

    if (send(sock, req, sizeof(req), 0) != (int)sizeof(req)) {
        ESP_LOGE(TAG, "send() failed: %d", errno);
        return -1;
    }

    /* Read MBAP header (7 bytes) */
    uint8_t mbap[7];
    if (recv_all(sock, mbap, 7) != 7) {
        ESP_LOGE(TAG, "recv MBAP failed");
        return -1;
    }

    uint16_t resp_pdu_len = (((uint16_t)mbap[4] << 8) | mbap[5]) - 1; /* -1 for unit_id */
    if (resp_pdu_len < 2 || resp_pdu_len > 255) {
        ESP_LOGE(TAG, "invalid PDU length: %u", resp_pdu_len);
        return -1;
    }

    uint8_t pdu[256];
    if (recv_all(sock, pdu, resp_pdu_len) != resp_pdu_len) {
        ESP_LOGE(TAG, "recv PDU failed");
        return -1;
    }

    /* Modbus exception response: error FC = request FC | 0x80 */
    if (pdu[0] & 0x80) {
        ESP_LOGW(TAG, "Modbus exception 0x%02X (FC 0x%02X)", pdu[1], pdu[0] & 0x7F);
        return -1;
    }

    if (pdu[0] != 0x03) {
        ESP_LOGE(TAG, "unexpected function code 0x%02X", pdu[0]);
        return -1;
    }
    uint8_t byte_count = pdu[1];
    if (byte_count < count * 2) {
        ESP_LOGE(TAG, "short byte_count %u (expected >= %u)", byte_count, count * 2);
        return -1;
    }
    if (byte_count > count * 2) {
        ESP_LOGW(TAG, "byte_count %u > requested %u, trimming", byte_count, count * 2);
    }

    /* Big-Endian → host uint16_t */
    for (uint16_t i = 0; i < count; i++) {
        out_data[i] = ((uint16_t)pdu[2 + i * 2] << 8) | pdu[2 + i * 2 + 1];
    }
    return count;
}

static int connect_to_inverter(void)
{
    if (s_config.huawei_ip[0] == '\0') {
        ESP_LOGW(TAG, "Huawei IP not configured");
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: %d", errno);
        return -1;
    }

    /* Apply receive timeout so fc03_read() never blocks indefinitely */
    struct timeval tv = { .tv_sec = MB_RECV_TIMEOUT_S, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(s_config.huawei_port ? s_config.huawei_port : 502),
    };
    if (inet_pton(AF_INET, s_config.huawei_ip, &addr.sin_addr) != 1) {
        ESP_LOGE(TAG, "Invalid IP address: %s", s_config.huawei_ip);
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGI(TAG, "Cannot connect to %s:%u (%d)", s_config.huawei_ip,
                 s_config.huawei_port, errno);
        close(sock);
        return -1;
    }

    ESP_LOGI(TAG, "Connected to Huawei inverter %s:%u unit=%u",
             s_config.huawei_ip, s_config.huawei_port, s_config.huawei_dev);
    return sock;
}

/*
 * Read all four register blocks in sequence.
 * Block A (static device info) is read every cycle to detect reconnection.
 * Block D is optional — failure does not cause reconnect.
 * Returns true if blocks B+C (the essential ones) were read successfully.
 */
static bool poll_inverter(int sock)
{
    uint8_t uid = s_config.huawei_dev ? s_config.huawei_dev : 1;
    uint16_t tmp[160]; /* largest cached block (37000..37155 => 156 regs) */
    bool essential_ok = true;

    /* Match proven Huawei LAN sequence (huawei_LAN.php) */
    if (read_block_chunked(sock, 30000, 81, tmp, uid)) {
        reg_cache_write_block(0, tmp, 81);
    } else {
        ESP_LOGW(TAG, "Block A (30000, 81) read failed");
        essential_ok = false;
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 32000, 116, tmp, uid)) {
        reg_cache_write_block(1, tmp, 116);
    } else {
        ESP_LOGW(TAG, "Block B (32000, 116) read failed");
        essential_ok = false;
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 37000, 156, tmp, uid)) {
        reg_cache_write_block(2, tmp, 156);
    } else {
        ESP_LOGW(TAG, "Block C (37000, 156) read failed");
        essential_ok = false;
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    /* Block D: 38210–38233 (24 regs) — battery pack detail, optional */
    if (read_block_chunked(sock, 38210, 24, tmp, uid)) {
        reg_cache_write_block(3, tmp, 24);
    } else {
        ESP_LOGD(TAG, "Block D (38210, 24) not available — skipping");
    }

    return essential_ok;
}

static void modbus_client_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Poll task started (interval=%us)", s_config.poll_interval);
    uint8_t fail_count = 0;

    while (1) {
        uint32_t base_s = s_config.poll_interval ? s_config.poll_interval : 60;
        uint32_t wait_s = compute_backoff_s(base_s, fail_count);
        uint32_t jitter_ms = (esp_random() % 3000U);

        int sock = connect_to_inverter();
        if (sock < 0) {
            mb_inverter_connected = false;
            reg_cache_invalidate_all();
            if (fail_count < 8) fail_count++;
            ESP_LOGW(TAG, "Connect failed (%u) -> next try in %us", fail_count, wait_s);
            vTaskDelay(pdMS_TO_TICKS(wait_s * 1000U + jitter_ms));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(MB_CONNECT_SETTLE_MS));

        /* Poll loop — stay connected, retry on failure */
        while (1) {
            if (poll_inverter(sock)) {
                mb_inverter_connected = true;
                mb_last_poll_ts       = time(NULL);
                fail_count            = 0;
                ESP_LOGI(TAG, "Poll OK — cache updated");
                wait_s = base_s;
            } else {
                mb_inverter_connected = false;
                reg_cache_invalidate_all();
                if (fail_count < 8) fail_count++;
                wait_s = compute_backoff_s(base_s, fail_count);
                ESP_LOGE(TAG, "Poll failed (%u) — reconnecting after %us", fail_count, wait_s);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(wait_s * 1000U + jitter_ms));
        }

        close(sock);
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000U + jitter_ms));
    }
}

void modbus_client_start(void)
{
    xTaskCreate(modbus_client_task, "mb_client", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Modbus TCP client started");
}
