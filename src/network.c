/*
 * network.c — Ethernet + Wi-Fi AP initialisation for ESP32-S3 ETH boards.
 *
 * BOARD-SPECIFIC SECTION (see "Configure for your board" below):
 *   This file is written for boards using a W5500 SPI Ethernet chip,
 *   which is common on Waveshare ESP32-S3-ETH and similar boards.
 *   For boards with RMII Ethernet (e.g. WT32-ETH01 with LAN8720), replace
 *   the W5500 MAC/PHY init block with the RMII variant (commented at the
 *   bottom of this file).
 */
#include "network.h"
#include "config.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_eth_phy.h"
#include "esp_eth_mac_spi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "network";
static esp_netif_t *s_eth_netif = NULL;
static volatile bool s_captive_dns_running = false;
static int s_captive_dns_sock = -1;

static bool mac_is_invalid(const uint8_t mac[6])
{
    bool all_zero = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            all_zero = false;
            break;
        }
    }
    return all_zero || ((mac[0] & 0x01) != 0); /* multicast/broadcast not allowed */
}

typedef struct {
    const char *name;
    spi_host_device_t host;
    int clock_mhz;
    int pin_mosi;
    int pin_miso;
    int pin_sclk;
    int pin_cs;
    int pin_int;
    int pin_rst;
} eth_profile_t;

/* Common W5500 pinouts for ESP32-S3 ETH boards */
static const eth_profile_t s_eth_profiles[] = {
    /* Waveshare ESP32-S3-ETH wiki: MOSI=11, MISO=12, SCLK=13, CS=14, RST=9, INT=10 */
    { "waveshare-official",   SPI2_HOST, 10, 11, 12, 13, 14, -1, 9 },
    /* Fallbacks for variant boards / custom wiring */
    { "waveshare-int10",      SPI2_HOST, 10, 11, 12, 13, 14, 10, 9 },
    { "waveshare-sclk12",     SPI2_HOST, 10, 11, 13, 12, 14, 10, 9 },
    { "waveshare-cs10-poll",  SPI2_HOST, 10, 11, 12, 13, 10, -1, 9 },
};

/* Wi-Fi AP */
#define AP_SSID           "ModBus Server"
#define AP_MAX_STA        4
#define AP_CHANNEL        1
#define AP_TIMEOUT_SECS   60
#define AP_IP_ADDR        "192.168.4.1"
#define HOSTNAME          "modbusserver"

/* GPIO0 is the BOOT button (active LOW) */
#define FACTORY_RESET_GPIO GPIO_NUM_0
#define FACTORY_RESET_SECS 10

/* -----------------------------------------------------------------------
 * AP timeout task
 * ----------------------------------------------------------------------- */
static void ap_timer_task(void *pvParameters)
{
    int remaining = AP_TIMEOUT_SECS;
    ESP_LOGI(TAG, "AP timer started — %d s", remaining);

    while (remaining > 0) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        remaining--;
    }

    ESP_LOGI(TAG, "AP timeout — shutting down Wi-Fi AP");
    s_captive_dns_running = false;
    if (s_captive_dns_sock >= 0) {
        shutdown(s_captive_dns_sock, SHUT_RDWR);
        close(s_captive_dns_sock);
        s_captive_dns_sock = -1;
    }
    esp_wifi_stop();
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * Captive DNS task (answers every query with AP IP)
 * ----------------------------------------------------------------------- */
static int build_dns_a_response(const uint8_t *req, int req_len, uint8_t *resp, int resp_cap)
{
    if (req_len < 12 || resp_cap < 64) {
        return -1;
    }

    int qdcount = ((int)req[4] << 8) | req[5];
    if (qdcount < 1) {
        qdcount = 1;
    }

    int q_end = 12;
    while (q_end < req_len && req[q_end] != 0) {
        uint8_t label_len = req[q_end];
        if ((label_len & 0xC0) != 0 || q_end + 1 + label_len >= req_len) {
            return -1;
        }
        q_end += 1 + label_len;
    }
    if (q_end + 5 > req_len) {
        return -1;
    }
    q_end += 5; /* zero label + qtype + qclass */

    int question_len = q_end - 12;
    int total_len = 12 + question_len + 16;
    if (total_len > resp_cap) {
        return -1;
    }

    memset(resp, 0, total_len);
    resp[0] = req[0];
    resp[1] = req[1];
    resp[2] = 0x81; /* response + recursion available */
    resp[3] = 0x80; /* no error */
    resp[4] = req[4];
    resp[5] = req[5];
    resp[6] = 0x00;
    resp[7] = 0x01; /* one answer */

    memcpy(&resp[12], &req[12], question_len);

    int a = 12 + question_len;
    resp[a + 0] = 0xC0; resp[a + 1] = 0x0C; /* name pointer to query */
    resp[a + 2] = 0x00; resp[a + 3] = 0x01; /* type A */
    resp[a + 4] = 0x00; resp[a + 5] = 0x01; /* class IN */
    resp[a + 6] = 0x00; resp[a + 7] = 0x00; resp[a + 8] = 0x00; resp[a + 9] = 0x3C; /* TTL 60s */
    resp[a +10] = 0x00; resp[a +11] = 0x04; /* IPv4 length */
    resp[a +12] = 192;  resp[a +13] = 168;  resp[a +14] = 4;    resp[a +15] = 1;
    return total_len;
}

static void captive_dns_task(void *pvParameters)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Captive DNS socket create failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Captive DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    s_captive_dns_sock = sock;

    struct timeval tv = {
        .tv_sec = 1,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "Captive DNS started on %s:53", AP_IP_ADDR);

    uint8_t req[512];
    uint8_t resp[512];
    while (s_captive_dns_running) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&from, &from_len);
        if (len <= 0) {
            continue;
        }
        int out_len = build_dns_a_response(req, len, resp, sizeof(resp));
        if (out_len > 0) {
            sendto(sock, resp, out_len, 0, (struct sockaddr *)&from, from_len);
        }
    }
    close(sock);
    if (s_captive_dns_sock == sock) {
        s_captive_dns_sock = -1;
    }
    ESP_LOGI(TAG, "Captive DNS stopped");
    vTaskDelete(NULL);
}

/* -----------------------------------------------------------------------
 * GPIO0 factory-reset task (10 s long-press)
 * ----------------------------------------------------------------------- */
static void gpio0_task(void *pvParameters)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << FACTORY_RESET_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    int press_secs = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (gpio_get_level(FACTORY_RESET_GPIO) == 0) {
            press_secs++;
            if (press_secs == 3) {
                ESP_LOGW(TAG, "BOOT held — release within %d s to cancel reset",
                         FACTORY_RESET_SECS - press_secs);
            }
            if (press_secs >= FACTORY_RESET_SECS) {
                ESP_LOGW(TAG, "Factory reset — erasing NVS and rebooting");
                nvs_flash_erase();
                esp_restart();
            }
        } else {
            press_secs = 0;
        }
    }
}

/* -----------------------------------------------------------------------
 * Ethernet event handler
 * ----------------------------------------------------------------------- */
static void eth_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    if (id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        if (s_eth_netif) {
            esp_netif_set_default_netif(s_eth_netif);
            ESP_LOGI(TAG, "Ethernet set as default netif");
        }
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

/* -----------------------------------------------------------------------
 * Wi-Fi AP initialisation
 * ----------------------------------------------------------------------- */
static void wifi_ap_init(void)
{
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif) {
        esp_netif_set_hostname(ap_netif, HOSTNAME);
    }

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid            = AP_SSID,
            .ssid_len        = sizeof(AP_SSID) - 1,
            .channel         = AP_CHANNEL,
            .max_connection  = AP_MAX_STA,
            .authmode        = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)ap_config.ap.password, s_config.ap_password,
            sizeof(ap_config.ap.password) - 1);

    /* Open AP if password is too short for WPA2 (< 8 chars) */
    if (strlen(s_config.ap_password) < 8) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ap_config.ap.password[0] = '\0';
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started — SSID: %s", AP_SSID);

    /* Captive portal DNS (wildcard -> 192.168.4.1) */
    s_captive_dns_running = true;
    xTaskCreate(captive_dns_task, "captive_dns", 4096, NULL, 4, NULL);

    /* AP auto-shutdown task */
    xTaskCreate(ap_timer_task, "ap_timer", 4096, NULL, 3, NULL);
}

/* -----------------------------------------------------------------------
 * Ethernet initialisation — W5500 SPI variant
 * ----------------------------------------------------------------------- */
static esp_err_t ethernet_try_profile(const eth_profile_t *profile)
{
    if (profile->pin_int >= 0) {
        esp_err_t isr_ret = gpio_install_isr_service(0);
        if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(isr_ret));
            return isr_ret;
        }
    }

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num   = profile->pin_mosi,
        .miso_io_num   = profile->pin_miso,
        .sclk_io_num   = profile->pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t err = spi_bus_initialize(profile->host, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[%s] spi_bus_initialize failed: %s", profile->name, esp_err_to_name(err));
        return err;
    }

    /* W5500 SPI device config */
    spi_device_interface_config_t devcfg = {
        .command_bits  = 16,
        .address_bits  = 8,
        .mode          = 0,
        .clock_speed_hz = profile->clock_mhz * 1000 * 1000,
        .spics_io_num  = profile->pin_cs,
        .queue_size    = 20,
    };
    /* MAC + PHY */
    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num   = profile->pin_rst;

    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(profile->host, &devcfg);
    w5500_cfg.int_gpio_num        = profile->pin_int;
    w5500_cfg.poll_period_ms      = (profile->pin_int >= 0) ? 0 : 100;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (mac == NULL || phy == NULL) {
        ESP_LOGW(TAG, "[%s] W5500 MAC/PHY allocation failed", profile->name);
        if (mac != NULL) {
            mac->del(mac);
        }
        if (phy != NULL) {
            phy->del(phy);
        }
        return ESP_FAIL;
    }

    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_hdl;
    err = esp_eth_driver_install(&eth_cfg, &eth_hdl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[%s] esp_eth_driver_install failed: %s", profile->name, esp_err_to_name(err));
        mac->del(mac);
        phy->del(phy);
        return err;
    }

    /* Ensure Ethernet interface has a valid unicast MAC address */
    uint8_t cur_mac[6] = {0};
    err = esp_eth_ioctl(eth_hdl, ETH_CMD_G_MAC_ADDR, cur_mac);
    if (err == ESP_OK && mac_is_invalid(cur_mac)) {
        uint8_t new_mac[6] = {0};
        if (esp_read_mac(new_mac, ESP_MAC_ETH) != ESP_OK || mac_is_invalid(new_mac)) {
            ESP_ERROR_CHECK(esp_read_mac(new_mac, ESP_MAC_WIFI_STA));
            new_mac[0] = (new_mac[0] | 0x02) & 0xFE; /* locally administered unicast */
            new_mac[5] ^= 0x01;                      /* avoid Wi-Fi MAC collision */
        }
        ESP_LOGW(TAG, "[%s] invalid ETH MAC " MACSTR " -> setting " MACSTR,
                 profile->name, MAC2STR(cur_mac), MAC2STR(new_mac));
        ESP_ERROR_CHECK(esp_eth_ioctl(eth_hdl, ETH_CMD_S_MAC_ADDR, new_mac));
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "[%s] ETH MAC " MACSTR, profile->name, MAC2STR(cur_mac));
    }

    /* Attach to netif */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_hdl));
    esp_netif_set_hostname(eth_netif, HOSTNAME);
    s_eth_netif = eth_netif;

    /* Register event handlers */
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL);
    esp_event_handler_register(IP_EVENT,  IP_EVENT_ETH_GOT_IP, ip_event_handler, NULL);

    /* DHCP or static IP */
    if (!s_config.dhcp && s_config.ip_addr[0] != '\0') {
        esp_netif_dhcpc_stop(eth_netif);
        esp_netif_ip_info_t ip_info;
        ip_info.ip.addr      = inet_addr(s_config.ip_addr);
        ip_info.netmask.addr = inet_addr(s_config.netmask[0] ? s_config.netmask : "255.255.255.0");
        ip_info.gw.addr      = inet_addr(s_config.gateway);
        esp_netif_set_ip_info(eth_netif, &ip_info);
        ESP_LOGI(TAG, "Static IP: %s", s_config.ip_addr);
    } else {
        ESP_LOGI(TAG, "DHCP enabled");
    }

    err = esp_eth_start(eth_hdl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[%s] esp_eth_start failed: %s", profile->name, esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

static esp_err_t ethernet_init(void)
{
    esp_err_t last_err = ESP_FAIL;

    for (size_t i = 0; i < sizeof(s_eth_profiles) / sizeof(s_eth_profiles[0]); i++) {
        const eth_profile_t *p = &s_eth_profiles[i];
        ESP_LOGI(TAG,
                 "Trying W5500 profile '%s' (host=%d mosi=%d miso=%d sclk=%d cs=%d int=%d rst=%d)",
                 p->name, p->host, p->pin_mosi, p->pin_miso, p->pin_sclk, p->pin_cs, p->pin_int, p->pin_rst);
        esp_err_t err = ethernet_try_profile(p);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Ethernet init succeeded with profile '%s'", p->name);
            return ESP_OK;
        }
        last_err = err;
        spi_bus_free(p->host);
    }

    return last_err;
}

/* -----------------------------------------------------------------------
 * Public entry point
 * ----------------------------------------------------------------------- */
esp_err_t network_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_err_t err = ethernet_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet init failed: %s", esp_err_to_name(err));
        return err;
    }

    wifi_ap_init();

    /* Factory reset via GPIO0 long-press */
    xTaskCreate(gpio0_task, "gpio0_reset", 2048, NULL, 3, NULL);

    return ESP_OK;
}

/*
 * -----------------------------------------------------------------------
 * RMII alternative (e.g. WT32-ETH01 with LAN8720)
 * -----------------------------------------------------------------------
 * Replace ethernet_init() with:
 *
 *   eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
 *   eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
 *   esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);
 *
 *   eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
 *   phy_cfg.phy_addr        = 1;     // adjust to board
 *   phy_cfg.reset_gpio_num  = 5;     // adjust to board
 *   esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_cfg);
 *
 *   ... rest stays the same (driver install, netif attach, DHCP/static)
 * -----------------------------------------------------------------------
 */
