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
#include "freertos/semphr.h"
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
#define MB_CACHE_DROP_FAILS    3

static int fc03_read(int sock, uint16_t start_reg, uint16_t count,
                     uint16_t *out_data, uint8_t unit_id);
static int connect_to_inverter(void);
static int mb_transaction(int sock, uint8_t unit_id, const uint8_t *req_pdu, uint16_t req_pdu_len,
                          uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len);
static int ensure_upstream_connected_locked(void);
static void reset_upstream_locked(void);
static int run_transaction_locked(uint8_t unit_id, const uint8_t *req_pdu, uint16_t req_pdu_len,
                                  uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len);
static void ensure_client_lock(void);

static SemaphoreHandle_t s_client_lock = NULL;
static int s_upstream_sock = -1;

static bool should_drop_cache(uint8_t fail_count)
{
    (void)fail_count;
    /* The Huawei upstream is known to close the port intermittently.
     * Keep the last valid cache instead of turning all HA entities unavailable.
     */
    return false;
}

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

static void ensure_client_lock(void)
{
    if (s_client_lock == NULL) {
        s_client_lock = xSemaphoreCreateMutex();
    }
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
    uint8_t req_pdu[5];
    uint8_t pdu[256];
    uint16_t resp_pdu_len = 0;

    req_pdu[0] = 0x03;
    req_pdu[1] = (start_reg >> 8) & 0xFF;
    req_pdu[2] = start_reg & 0xFF;
    req_pdu[3] = (count >> 8) & 0xFF;
    req_pdu[4] = count & 0xFF;

    if (mb_transaction(sock, unit_id, req_pdu, sizeof(req_pdu), pdu, sizeof(pdu), &resp_pdu_len) != 0) {
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

static int mb_transaction(int sock, uint8_t unit_id, const uint8_t *req_pdu, uint16_t req_pdu_len,
                          uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len)
{
    static uint16_t s_tid = 0;
    uint8_t req[260];

    if (!req_pdu || req_pdu_len == 0 || !resp_pdu || !resp_pdu_len || resp_cap == 0 || req_pdu_len > 253) {
        return -1;
    }

    s_tid++;
    req[0] = (s_tid >> 8) & 0xFF;
    req[1] = s_tid & 0xFF;
    req[2] = 0x00;
    req[3] = 0x00;
    req[4] = (uint8_t)(((uint16_t)(req_pdu_len + 1U) >> 8) & 0xFF);
    req[5] = (uint8_t)((req_pdu_len + 1U) & 0xFF);
    req[6] = unit_id;
    memcpy(req + 7, req_pdu, req_pdu_len);

    if (send(sock, req, 7 + req_pdu_len, 0) != (int)(7 + req_pdu_len)) {
        ESP_LOGE(TAG, "send() failed: %d", errno);
        return -1;
    }

    uint8_t mbap[7];
    if (recv_all(sock, mbap, 7) != 7) {
        ESP_LOGE(TAG, "recv MBAP failed");
        return -1;
    }

    uint16_t resp_len = ((uint16_t)mbap[4] << 8) | mbap[5];
    if (resp_len < 2) {
        ESP_LOGE(TAG, "invalid MBAP length: %u", resp_len);
        return -1;
    }

    uint16_t pdu_len = resp_len - 1;
    if (pdu_len > resp_cap) {
        ESP_LOGE(TAG, "response PDU too large: %u", pdu_len);
        return -1;
    }

    if (recv_all(sock, resp_pdu, pdu_len) != pdu_len) {
        ESP_LOGE(TAG, "recv PDU failed");
        return -1;
    }

    *resp_pdu_len = pdu_len;
    return 0;
}

static void reset_upstream_locked(void)
{
    if (s_upstream_sock >= 0) {
        close(s_upstream_sock);
        s_upstream_sock = -1;
    }
}

static int ensure_upstream_connected_locked(void)
{
    if (s_upstream_sock >= 0) {
        return s_upstream_sock;
    }

    s_upstream_sock = connect_to_inverter();
    if (s_upstream_sock < 0) {
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(MB_CONNECT_SETTLE_MS));
    return s_upstream_sock;
}

static int run_transaction_locked(uint8_t unit_id, const uint8_t *req_pdu, uint16_t req_pdu_len,
                                  uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len)
{
    if (ensure_upstream_connected_locked() < 0) {
        return -1;
    }

    int rc = mb_transaction(s_upstream_sock, unit_id, req_pdu, req_pdu_len, resp_pdu, resp_cap, resp_pdu_len);
    if (rc != 0) {
        ESP_LOGW(TAG, "Upstream transaction failed FC=0x%02X unit=%u -> reconnect next cycle",
                 req_pdu_len > 0 ? req_pdu[0] : 0x00, unit_id);
        reset_upstream_locked();
        return -1;
    }

    return 0;
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
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 37200, 1, tmp, uid)) {
        reg_cache_write_block(4, tmp, 1);
    } else {
        ESP_LOGD(TAG, "Block E (37200, 1) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 37738, 50, tmp, uid)) {
        reg_cache_write_block(5, tmp, 50);
    } else {
        ESP_LOGD(TAG, "Block F (37738, 50) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 42900, 1, tmp, uid)) {
        reg_cache_write_block(6, tmp, 1);
    } else {
        ESP_LOGD(TAG, "Block G (42900, 1) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 43006, 1, tmp, uid)) {
        reg_cache_write_block(7, tmp, 1);
    } else {
        ESP_LOGD(TAG, "Block H (43006, 1) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 47000, 1, tmp, uid)) {
        reg_cache_write_block(8, tmp, 1);
    } else {
        ESP_LOGD(TAG, "Block I (47000, 1) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 47089, 1, tmp, uid)) {
        reg_cache_write_block(9, tmp, 1);
    } else {
        ESP_LOGD(TAG, "Block J (47089, 1) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 47954, 66, tmp, uid)) {
        reg_cache_write_block(10, tmp, 66);
    } else {
        ESP_LOGD(TAG, "Block K (47954, 66) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 40120, 35, tmp, uid)) {
        reg_cache_write_block(11, tmp, 35);
    } else {
        ESP_LOGD(TAG, "Block L (40120, 35) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 47075, 28, tmp, uid)) {
        reg_cache_write_block(12, tmp, 28);
    } else {
        ESP_LOGD(TAG, "Block M (47075, 28) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 47242, 58, tmp, uid)) {
        reg_cache_write_block(13, tmp, 58);
    } else {
        ESP_LOGD(TAG, "Block N (47242, 58) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 47415, 4, tmp, uid)) {
        reg_cache_write_block(14, tmp, 4);
    } else {
        ESP_LOGD(TAG, "Block O (47415, 4) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 47589, 2, tmp, uid)) {
        reg_cache_write_block(15, tmp, 2);
    } else {
        ESP_LOGD(TAG, "Block P (47589, 2) not available — skipping");
    }
    vTaskDelay(pdMS_TO_TICKS(MB_BLOCK_DELAY_MS));

    if (read_block_chunked(sock, 42054, 2, tmp, uid)) {
        reg_cache_write_block(16, tmp, 2);
    } else {
        ESP_LOGD(TAG, "Block Q (42054, 2) not available — skipping");
    }

    return essential_ok;
}

int modbus_client_forward_write(uint8_t unit_id, const uint8_t *pdu, uint16_t pdu_len,
                                uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len)
{
    int rc;

    ensure_client_lock();
    if (s_client_lock == NULL) {
        return -1;
    }

    if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGW(TAG, "Write request lock timeout");
        return -1;
    }

    rc = run_transaction_locked(unit_id, pdu, pdu_len, resp_pdu, resp_cap, resp_pdu_len);
    xSemaphoreGive(s_client_lock);

    if (rc == 0) {
        mb_inverter_connected = true;
    } else {
        mb_inverter_connected = false;
    }
    return rc;
}

int modbus_client_forward_pdu(uint8_t unit_id, const uint8_t *pdu, uint16_t pdu_len,
                              uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len)
{
    int rc;

    ensure_client_lock();
    if (s_client_lock == NULL) {
        return -1;
    }

    if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGW(TAG, "Generic request lock timeout");
        return -1;
    }

    rc = run_transaction_locked(unit_id, pdu, pdu_len, resp_pdu, resp_cap, resp_pdu_len);
    xSemaphoreGive(s_client_lock);

    if (rc == 0) {
        mb_inverter_connected = true;
    } else {
        mb_inverter_connected = false;
    }
    return rc;
}

int modbus_client_forward_read(uint8_t unit_id, const uint8_t *pdu, uint16_t pdu_len,
                               uint8_t *resp_pdu, size_t resp_cap, uint16_t *resp_pdu_len)
{
    int rc;

    ensure_client_lock();
    if (s_client_lock == NULL) {
        return -1;
    }

    if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGW(TAG, "Read-through lock timeout");
        return -1;
    }

    rc = run_transaction_locked(unit_id, pdu, pdu_len, resp_pdu, resp_cap, resp_pdu_len);
    xSemaphoreGive(s_client_lock);

    if (rc == 0 && resp_pdu_len != NULL && *resp_pdu_len > 0 && (resp_pdu[0] & 0x80) == 0) {
        mb_inverter_connected = true;
    } else if (rc != 0) {
        mb_inverter_connected = false;
    }
    return rc;
}

static void modbus_client_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Poll task started (interval=%us)", s_config.poll_interval);
    uint8_t fail_count = 0;

    while (1) {
        uint32_t base_s = s_config.poll_interval ? s_config.poll_interval : 60;
        uint32_t wait_s = compute_backoff_s(base_s, fail_count);
        uint32_t jitter_ms = (esp_random() % 3000U);
        ensure_client_lock();
        if (s_client_lock == NULL) {
            ESP_LOGE(TAG, "Failed to create Modbus client lock");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (xSemaphoreTake(s_client_lock, pdMS_TO_TICKS(10000)) != pdTRUE) {
            ESP_LOGW(TAG, "Poll lock timeout");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (ensure_upstream_connected_locked() < 0) {
            xSemaphoreGive(s_client_lock);
            mb_inverter_connected = false;
            if (fail_count < 8) fail_count++;
            if (should_drop_cache(fail_count)) {
                reg_cache_invalidate_all();
            }
            ESP_LOGW(TAG, "Connect failed (%u) -> next try in %us", fail_count, wait_s);
            vTaskDelay(pdMS_TO_TICKS(wait_s * 1000U + jitter_ms));
            continue;
        }
        if (poll_inverter(s_upstream_sock)) {
            mb_inverter_connected = true;
            mb_last_poll_ts       = time(NULL);
            fail_count            = 0;
            ESP_LOGI(TAG, "Poll OK — cache updated");
            wait_s = base_s;
        } else {
            mb_inverter_connected = false;
            reset_upstream_locked();
            if (fail_count < 8) fail_count++;
            if (should_drop_cache(fail_count)) {
                reg_cache_invalidate_all();
            }
            wait_s = compute_backoff_s(base_s, fail_count);
            ESP_LOGE(TAG, "Poll failed (%u) — reconnecting after %us", fail_count, wait_s);
        }
        xSemaphoreGive(s_client_lock);
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000U + jitter_ms));
    }
}

void modbus_client_start(void)
{
    ensure_client_lock();
    xTaskCreate(modbus_client_task, "mb_client", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Modbus TCP client started");
}
