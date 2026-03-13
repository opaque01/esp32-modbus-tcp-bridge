#pragma once

#include <stdint.h>
#include <stdbool.h>

#define APP_FIRMWARE_VERSION "1.0.1"

/* Maximum number of register-mapping entries stored in NVS */
#define MAX_MAPPING_ENTRIES 32

typedef struct {
    /* Huawei inverter connection */
    char     huawei_ip[64];
    uint16_t huawei_port;     /* default 502 */
    uint8_t  huawei_dev;      /* Modbus unit-id, default 1 */
    uint32_t poll_interval;   /* seconds between cache refreshes, default 60 */

    /* Register mapping */
    bool     mapping_enabled;

    /* LAN / PoE network */
    bool     dhcp;
    char     hostname[32];
    char     ip_addr[16];
    char     netmask[16];
    char     gateway[16];
    char     dns1[16];
    char     dns2[16];

    /* Wi-Fi AP for configuration portal */
    char     ap_password[32]; /* default "ModBus" */

    /* Mapping table
     * Row i (0-based) → mapped Modbus address (i+1), source = register_addr */
    struct {
        char     name[32];
        uint16_t register_addr;  /* source Huawei register (e.g. 37004 = BAT SOC) */
        uint8_t  function_code;  /* always 3 = FC03 */
        uint8_t  data_type;      /* 0=uint16, 1=int16, 2=float32 */
        uint8_t  byte_order;     /* 0=Big-Endian, 1=Little-Endian */
        float    factor;         /* scale factor applied to raw value */
        char     unit[8];        /* display unit, e.g. "%" or "W" */
        uint16_t timeout_ms;     /* reserved, 0 = no timeout value */
    } mapping[MAX_MAPPING_ENTRIES];
    uint8_t mapping_count;
} app_config_t;

/* Single global config instance — defined in main.c */
extern app_config_t s_config;
