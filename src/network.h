#pragma once

#include "esp_err.h"
#include <stddef.h>

/*
 * network_init() — call once from app_main() before starting Modbus tasks.
 *
 * Initialises esp_netif, the default event loop, Ethernet (board-specific
 * PHY, see network.c) and the Wi-Fi AP for the configuration portal.
 *
 * Ethernet: applies DHCP or static IP from s_config.
 * Wi-Fi AP: starts with SSID "ModBus Server" and s_config.ap_password;
 *           stays open for 60 s and then shuts down automatically.
 * Captive DNS portal is stopped together with the AP timeout.
 *
 * GPIO0 factory-reset task: 10-second press erases NVS and reboots.
 */
esp_err_t network_init(void);
const char *network_get_hostname(void);
esp_err_t network_get_ip(char *buf, size_t len);
