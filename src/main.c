#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"       /* app_config_t definition + extern s_config */
#include "reg_cache.h"
#include "network.h"
#include "modbus_client.h"
#include "modbus_server.h"
#include "status_log.h"

static const char *TAG = "modbus_bridge";

/* Single global configuration instance — extern-declared in config.h */
app_config_t s_config;

extern const char _binary_ui_html_start[] asm("_binary_ui_html_start");
extern const char _binary_ui_html_end[] asm("_binary_ui_html_end");
extern const char _binary_bootstrap_min_css_start[] asm("_binary_bootstrap_min_css_start");
extern const char _binary_bootstrap_min_css_end[] asm("_binary_bootstrap_min_css_end");
extern const char _binary_bootstrap_bundle_min_js_start[] asm("_binary_bootstrap_bundle_min_js_start");
extern const char _binary_bootstrap_bundle_min_js_end[] asm("_binary_bootstrap_bundle_min_js_end");
extern const char _binary_favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const char _binary_favicon_ico_end[] asm("_binary_favicon_ico_end");

static size_t embedded_text_len(const char *start, const char *end)
{
    size_t len = (size_t)(end - start);
    while (len > 0 && start[len - 1] == '\0') {
        len--;
    }
    return len;
}

/* -----------------------------------------------------------------------
 * NVS persistence
 * ----------------------------------------------------------------------- */

static void load_config(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (%s) — using defaults", esp_err_to_name(err));
        s_config.huawei_port    = 502;
        s_config.huawei_dev     = 1;
        s_config.poll_interval  = 60;
        s_config.mapping_enabled = true;
        s_config.dhcp           = true;
        strcpy(s_config.ap_password, "ModBus");
        return;
    }

    size_t len;

    len = sizeof(s_config.huawei_ip);
    if (nvs_get_str(h, "huawei_ip", s_config.huawei_ip, &len) != ESP_OK)
        s_config.huawei_ip[0] = '\0';

    nvs_get_u16(h, "huawei_port", &s_config.huawei_port);
    nvs_get_u8 (h, "huawei_dev",  &s_config.huawei_dev);
    nvs_get_u32(h, "poll_intv",   &s_config.poll_interval);

    uint8_t flag = 1;
    if (nvs_get_u8(h, "mapping", &flag) == ESP_OK) s_config.mapping_enabled = flag;
    if (nvs_get_u8(h, "dhcp",    &flag) == ESP_OK) s_config.dhcp            = flag;

    len = sizeof(s_config.ip_addr);  nvs_get_str(h, "ip_addr",  s_config.ip_addr,  &len);
    len = sizeof(s_config.netmask);  nvs_get_str(h, "netmask",  s_config.netmask,  &len);
    len = sizeof(s_config.gateway);  nvs_get_str(h, "gateway",  s_config.gateway,  &len);
    len = sizeof(s_config.dns1);     nvs_get_str(h, "dns1",     s_config.dns1,     &len);
    len = sizeof(s_config.dns2);     nvs_get_str(h, "dns2",     s_config.dns2,     &len);
    len = sizeof(s_config.ap_password); nvs_get_str(h, "ap_pw", s_config.ap_password, &len);

    uint8_t count = 0;
    nvs_get_u8(h, "map_count", &count);
    if (count > MAX_MAPPING_ENTRIES) count = MAX_MAPPING_ENTRIES;
    s_config.mapping_count = count;
    for (int i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "map_%d", i);
        size_t sz = sizeof(s_config.mapping[i]);
        nvs_get_blob(h, key, &s_config.mapping[i], &sz);
    }

    /* Default example entry: BAT SOC via mapping row 1 */
    if (s_config.mapping_enabled && s_config.mapping_count == 0) {
        s_config.mapping_count = 1;
        strcpy(s_config.mapping[0].name, "BAT SOC");
        s_config.mapping[0].register_addr = 37004;
        s_config.mapping[0].function_code = 3;
        s_config.mapping[0].data_type     = 0;   /* uint16 */
        s_config.mapping[0].byte_order    = 0;   /* Big-Endian */
        s_config.mapping[0].factor        = 0.1f;
        strcpy(s_config.mapping[0].unit, "%");
    }

    nvs_close(h);
}

static void save_config(void)
{
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS write open failed");
        return;
    }

    nvs_set_str(h, "huawei_ip",  s_config.huawei_ip);
    nvs_set_u16(h, "huawei_port", s_config.huawei_port);
    nvs_set_u8 (h, "huawei_dev",  s_config.huawei_dev);
    nvs_set_u32(h, "poll_intv",   s_config.poll_interval);
    nvs_set_u8 (h, "mapping",    (uint8_t)s_config.mapping_enabled);
    nvs_set_u8 (h, "dhcp",       (uint8_t)s_config.dhcp);
    nvs_set_str(h, "ip_addr",   s_config.ip_addr);
    nvs_set_str(h, "netmask",   s_config.netmask);
    nvs_set_str(h, "gateway",   s_config.gateway);
    nvs_set_str(h, "dns1",      s_config.dns1);
    nvs_set_str(h, "dns2",      s_config.dns2);
    nvs_set_str(h, "ap_pw",     s_config.ap_password);

    nvs_set_u8(h, "map_count", s_config.mapping_count);
    for (int i = 0; i < s_config.mapping_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "map_%d", i);
        nvs_set_blob(h, key, &s_config.mapping[i], sizeof(s_config.mapping[i]));
    }

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Config saved to NVS");
}

/* -----------------------------------------------------------------------
 * HTTP handlers
 * ----------------------------------------------------------------------- */

static esp_err_t config_get_handler(httpd_req_t *req)
{
    uint8_t eth_mac[6] = {0};
    char eth_mac_str[18] = "";
    if (esp_read_mac(eth_mac, ESP_MAC_ETH) == ESP_OK) {
        snprintf(eth_mac_str, sizeof(eth_mac_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 eth_mac[0], eth_mac[1], eth_mac[2],
                 eth_mac[3], eth_mac[4], eth_mac[5]);
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(root, "huaweiIp", s_config.huawei_ip);
    cJSON_AddNumberToObject(root, "huaweiPort", s_config.huawei_port);
    cJSON_AddNumberToObject(root, "huaweiDev", s_config.huawei_dev);
    cJSON_AddNumberToObject(root, "pollInterval", (double)s_config.poll_interval);
    cJSON_AddBoolToObject(root, "mappingEnabled", s_config.mapping_enabled);
    cJSON_AddBoolToObject(root, "dhcp", s_config.dhcp);
    cJSON_AddStringToObject(root, "ipAddr", s_config.ip_addr);
    cJSON_AddStringToObject(root, "netmask", s_config.netmask);
    cJSON_AddStringToObject(root, "gateway", s_config.gateway);
    cJSON_AddStringToObject(root, "dns1", s_config.dns1);
    cJSON_AddStringToObject(root, "dns2", s_config.dns2);
    cJSON_AddStringToObject(root, "apPassword", s_config.ap_password);
    cJSON_AddStringToObject(root, "ethMac", eth_mac_str);
    cJSON_AddStringToObject(root, "firmwareVersion", APP_FIRMWARE_VERSION);

    cJSON *mapping = cJSON_AddArrayToObject(root, "mapping");
    for (int i = 0; i < s_config.mapping_count; i++) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry) continue;
        cJSON_AddStringToObject(entry, "name", s_config.mapping[i].name);
        cJSON_AddNumberToObject(entry, "register", s_config.mapping[i].register_addr);
        cJSON_AddNumberToObject(entry, "factor", (double)s_config.mapping[i].factor);
        cJSON_AddStringToObject(entry, "unit", s_config.mapping[i].unit);
        cJSON_AddItemToArray(mapping, entry);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 16384) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "Invalid request body size");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc((size_t)req->content_len + 1);
    if (!buf) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        return ESP_FAIL;
    }

    int offset = 0;
    while (offset < req->content_len) {
        int ret = httpd_req_recv(req, buf + offset, req->content_len - offset);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            free(buf);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "Invalid request body");
            return ESP_FAIL;
        }
        offset += ret;
    }
    buf[offset] = '\0';

    cJSON *root = cJSON_ParseWithLength(buf, offset);
    free(buf);
    if (root == NULL) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *item = NULL;
    item = cJSON_GetObjectItemCaseSensitive(root, "huaweiIp");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.huawei_ip, item->valuestring, sizeof(s_config.huawei_ip) - 1);
        s_config.huawei_ip[sizeof(s_config.huawei_ip) - 1] = '\0';
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "huaweiPort");
    if (cJSON_IsNumber(item)) s_config.huawei_port = (uint16_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(root, "huaweiDev");
    if (cJSON_IsNumber(item)) s_config.huawei_dev = (uint8_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(root, "pollInterval");
    if (cJSON_IsNumber(item)) s_config.poll_interval = (uint32_t)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(root, "mappingEnabled");
    if (cJSON_IsBool(item)) s_config.mapping_enabled = cJSON_IsTrue(item);
    item = cJSON_GetObjectItemCaseSensitive(root, "dhcp");
    if (cJSON_IsBool(item)) s_config.dhcp = cJSON_IsTrue(item);
    if (cJSON_IsNumber(item)) s_config.dhcp = (item->valuedouble != 0);

    item = cJSON_GetObjectItemCaseSensitive(root, "ipAddr");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.ip_addr, item->valuestring, sizeof(s_config.ip_addr) - 1);
        s_config.ip_addr[sizeof(s_config.ip_addr) - 1] = '\0';
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "netmask");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.netmask, item->valuestring, sizeof(s_config.netmask) - 1);
        s_config.netmask[sizeof(s_config.netmask) - 1] = '\0';
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "gateway");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.gateway, item->valuestring, sizeof(s_config.gateway) - 1);
        s_config.gateway[sizeof(s_config.gateway) - 1] = '\0';
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "dns1");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.dns1, item->valuestring, sizeof(s_config.dns1) - 1);
        s_config.dns1[sizeof(s_config.dns1) - 1] = '\0';
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "dns2");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.dns2, item->valuestring, sizeof(s_config.dns2) - 1);
        s_config.dns2[sizeof(s_config.dns2) - 1] = '\0';
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "apPassword");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(s_config.ap_password, item->valuestring, sizeof(s_config.ap_password) - 1);
        s_config.ap_password[sizeof(s_config.ap_password) - 1] = '\0';
    }

    cJSON *mapping = cJSON_GetObjectItemCaseSensitive(root, "mapping");
    if (cJSON_IsArray(mapping)) {
        int idx = 0;
        cJSON *row = NULL;
        cJSON_ArrayForEach(row, mapping) {
            if (idx >= MAX_MAPPING_ENTRIES) break;
            cJSON *name = cJSON_GetObjectItemCaseSensitive(row, "name");
            cJSON *reg  = cJSON_GetObjectItemCaseSensitive(row, "register");
            cJSON *factor = cJSON_GetObjectItemCaseSensitive(row, "factor");
            cJSON *unit = cJSON_GetObjectItemCaseSensitive(row, "unit");
            uint16_t reg_addr = 0;
            if (cJSON_IsNumber(reg)) {
                reg_addr = (uint16_t)reg->valuedouble;
            } else if (cJSON_IsString(reg) && reg->valuestring && reg->valuestring[0] != '\0') {
                reg_addr = (uint16_t)strtoul(reg->valuestring, NULL, 10);
            } else {
                continue;
            }

            memset(&s_config.mapping[idx], 0, sizeof(s_config.mapping[idx]));
            if (cJSON_IsString(name) && name->valuestring) {
                strncpy(s_config.mapping[idx].name, name->valuestring, sizeof(s_config.mapping[idx].name) - 1);
            }
            s_config.mapping[idx].register_addr = reg_addr;
            s_config.mapping[idx].function_code = 3;
            s_config.mapping[idx].data_type = 0;
            s_config.mapping[idx].byte_order = 0;
            s_config.mapping[idx].factor = cJSON_IsNumber(factor) ? (float)factor->valuedouble : 1.0f;
            if (cJSON_IsString(unit) && unit->valuestring) {
                strncpy(s_config.mapping[idx].unit, unit->valuestring, sizeof(s_config.mapping[idx].unit) - 1);
            }
            idx++;
        }
        s_config.mapping_count = (uint8_t)idx;
    }

    cJSON_Delete(root);

    save_config();
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

typedef struct {
    const char *device;
    const char *name;
    uint16_t register_addr;
    float factor;
    const char *unit;
    const char *type;
} register_option_t;

/* Registers from specs/huawei_registers.md, specs/huawei_inverter_registers_full.md
 * and specs/huawei_wallbox_registers.md */
static esp_err_t registers_get_handler(httpd_req_t *req)
{
    static const register_option_t options[] = {
        { "Inverter", "Firmware Name", 30000, 1.0f, "", "string" },
        { "Inverter", "Seriennummer", 30015, 1.0f, "", "string" },
        { "Inverter", "Modell-ID", 30070, 1.0f, "", "16u" },
        { "Inverter", "Anzahl PV-Strings", 30071, 1.0f, "", "16u" },
        { "Inverter", "Anzahl MPP-Tracker", 30072, 1.0f, "", "16u" },
        { "Inverter", "Nennleistung", 30073, 1.0f, "W", "16u" },
        { "Inverter", "Status1", 32000, 1.0f, "", "16u" },
        { "Inverter", "Status2", 32002, 1.0f, "", "16u" },
        { "Inverter", "Status3", 32003, 1.0f, "", "32u" },
        { "Inverter", "Alarm1", 32008, 1.0f, "", "16u" },
        { "Inverter", "Alarm2", 32009, 1.0f, "", "16u" },
        { "Inverter", "Alarm3", 32010, 1.0f, "", "16u" },
        { "Inverter", "PV Spannung 1", 32014, 0.1f, "V", "16u" },
        { "Inverter", "PV Strom 1", 32015, 0.01f, "A", "16s" },
        { "Inverter", "PV Spannung 2", 32016, 0.1f, "V", "16u" },
        { "Inverter", "PV Strom 2", 32017, 0.01f, "A", "16s" },
        { "Inverter", "AC Eingangsleistung", 32064, 1.0f, "W", "32u" },
        { "Inverter", "AC Spannung R", 32066, 0.1f, "V", "16u" },
        { "Inverter", "AC Spannung R (3-ph)", 32069, 0.1f, "V", "16u" },
        { "Inverter", "AC Spannung S", 32070, 0.1f, "V", "16u" },
        { "Inverter", "AC Spannung T", 32071, 0.1f, "V", "16u" },
        { "Inverter", "AC Strom R", 32072, 0.001f, "A", "16u" },
        { "Inverter", "AC Wirkleistung", 32080, 1.0f, "W", "16s" },
        { "Inverter", "AC Frequenz", 32085, 0.01f, "Hz", "16u" },
        { "Inverter", "Effizienz", 32086, 0.01f, "%", "16u" },
        { "Inverter", "Innentemperatur", 32087, 0.1f, "C", "16s" },
        { "Inverter", "Isolation", 32088, 1.0f, "", "16u" },
        { "Inverter", "DeviceStatus", 32089, 1.0f, "", "16u" },
        { "Inverter", "Fehlercode", 32090, 1.0f, "", "16u" },
        { "Inverter", "Wh Gesamt", 32106, 10.0f, "Wh", "32u" },
        { "Inverter", "Wh Heute", 32114, 10.0f, "Wh", "32u" },
        { "Batterie", "Batterie-Status", 37000, 1.0f, "", "16u" },
        { "Batterie", "Batterie-Leistung", 37001, 1.0f, "W", "32s" },
        { "Batterie", "BAT SOC", 37004, 0.1f, "%", "16u" },
        { "Batterie", "Einspeisung/Bezug", 37113, 0.01f, "kWh", "32s" },
        { "Batterie", "Wh Export Gesamt", 37119, 10.0f, "Wh", "32u" },
        { "Batterie", "Wh Import Gesamt", 37121, 10.0f, "Wh", "32u" },
        { "Batterie", "Batterie-Datenblock", 38210, 1.0f, "", "block" },
        { "Wallbox", "Inverter total absorbed energy", 30322, 0.01f, "kWh", "64u" },
        { "Wallbox", "Energy charged today", 30324, 0.01f, "kWh", "32u" },
        { "Wallbox", "Total charged energy", 30326, 0.01f, "kWh", "64u" },
        { "Wallbox", "Energy discharged today", 30328, 0.01f, "kWh", "32u" },
        { "Wallbox", "Total discharged energy", 30330, 0.01f, "kWh", "64u" },
        { "Wallbox", "ESS chargeable energy", 30332, 0.01f, "kWh", "32u" },
        { "Wallbox", "ESS dischargeable energy", 30334, 0.01f, "kWh", "32u" },
        { "Wallbox", "Rated ESS capacity", 30336, 0.01f, "kWh", "32u" },
        { "Wallbox", "Consumption today", 30338, 0.01f, "kWh", "32u" },
        { "Wallbox", "Total energy consumption", 30340, 0.01f, "kWh", "64u" },
        { "Wallbox", "Feed-in to grid today", 30342, 0.01f, "kWh", "32u" }
    };

    cJSON *root = cJSON_CreateArray();
    if (!root) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        return ESP_FAIL;
    }

    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); i++) {
        cJSON *entry = cJSON_CreateObject();
        if (!entry) continue;
        cJSON_AddStringToObject(entry, "device", options[i].device);
        cJSON_AddStringToObject(entry, "name", options[i].name);
        cJSON_AddNumberToObject(entry, "register", options[i].register_addr);
        cJSON_AddNumberToObject(entry, "factor", options[i].factor);
        cJSON_AddStringToObject(entry, "unit", options[i].unit);
        cJSON_AddNumberToObject(entry, "func", 3);
        cJSON_AddStringToObject(entry, "type", options[i].type);
        cJSON_AddStringToObject(entry, "order", "BE");
        cJSON_AddItemToArray(root, entry);
    }

    char *regs_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!regs_json) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Out of memory");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, regs_json, HTTPD_RESP_USE_STRLEN);
    free(regs_json);
    return ESP_OK;
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "OTA begin failed");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    char buf[1024];
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf,
                                      remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining);
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (recv_len <= 0) {
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "OTA upload interrupted");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "OTA write failed");
            return ESP_FAIL;
        }
        remaining -= recv_len;
    }

    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "OTA set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful, rebooting");
    httpd_resp_sendstr(req, "OK");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return ESP_OK;
}

static void delayed_reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGI(TAG, "Manual reboot requested via web UI");
    esp_restart();
}

static esp_err_t reboot_post_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate(delayed_reboot_task, "delayed_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, _binary_ui_html_start,
                    (ssize_t)embedded_text_len(_binary_ui_html_start, _binary_ui_html_end));
    return ESP_OK;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t bootstrap_css_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    httpd_resp_send(req, _binary_bootstrap_min_css_start,
                    (ssize_t)embedded_text_len(_binary_bootstrap_min_css_start, _binary_bootstrap_min_css_end));
    return ESP_OK;
}

static esp_err_t bootstrap_js_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript; charset=utf-8");
    httpd_resp_send(req, _binary_bootstrap_bundle_min_js_start,
                    (ssize_t)embedded_text_len(_binary_bootstrap_bundle_min_js_start, _binary_bootstrap_bundle_min_js_end));
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, _binary_favicon_ico_start,
                    _binary_favicon_ico_end - _binary_favicon_ico_start);
    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",          .method = HTTP_GET,  .handler = root_get_handler      },
        { .uri = "/index.html",.method = HTTP_GET,  .handler = root_get_handler      },
        { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/gen_204", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_redirect_handler },
        { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler },
        { .uri = "/bootstrap.min.css", .method = HTTP_GET, .handler = bootstrap_css_get_handler },
        { .uri = "/bootstrap.bundle.min.js", .method = HTTP_GET, .handler = bootstrap_js_get_handler },
        { .uri = "/config",    .method = HTTP_GET,  .handler = config_get_handler    },
        { .uri = "/config",    .method = HTTP_POST, .handler = config_post_handler   },
        { .uri = "/registers", .method = HTTP_GET,  .handler = registers_get_handler },
        { .uri = "/status",    .method = HTTP_GET,  .handler = status_get_handler    },
        { .uri = "/ota",       .method = HTTP_POST, .handler = ota_post_handler      },
        { .uri = "/reboot",    .method = HTTP_POST, .handler = reboot_post_handler   },
    };
    for (int i = 0; i < (int)(sizeof(uris) / sizeof(uris[0])); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "Web server started");
}

/* -----------------------------------------------------------------------
 * Entry point
 * ----------------------------------------------------------------------- */

void app_main(void)
{
    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Load persisted configuration */
    load_config();

    /* TEMP: status_log hook disabled due watchdog in esp_event task */
    /* status_log_init(); */

    /* Register cache (mutex + static arrays) */
    reg_cache_init();

    /* Ethernet + Wi-Fi AP + GPIO0 factory-reset task */
    ret = network_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Network init degraded (Ethernet unavailable): %s", esp_err_to_name(ret));
    }

    /* HTTP configuration server (available on AP and on LAN port 80) */
    start_webserver();

    /* Modbus: poll Huawei inverter → cache → serve clients */
    modbus_client_start();
    modbus_server_start();

    ESP_LOGI(TAG, "Startup complete");
}
