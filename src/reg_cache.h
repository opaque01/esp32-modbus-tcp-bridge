#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/*
 * Register cache — four static FC03 read blocks from the Huawei inverter:
 *
 *  Block 0 (A): 30000–30080   (81 regs)  — device info / firmware / SN
 *  Block 1 (B): 32000–32115  (116 regs)  — core PV/AC values
 *  Block 2 (C): 37000–37155  (156 regs)  — battery SOC, power, grid import + meter block for 37100/0x38
 *  Block 3 (D): 38210–38233   (24 regs)  — battery pack detail (optional)
 *
 * Each block carries a `valid` flag and a `last_updated` timestamp.
 * A FreeRTOS mutex serialises all accesses between the poll task and
 * the (potentially many) Modbus server client tasks.
 */

/* Return codes for reg_cache_lookup() */
#define REG_CACHE_OK         0  /* value retrieved successfully */
#define REG_CACHE_STALE      4  /* block exists but marked invalid → Modbus Exception 0x04 */
#define REG_CACHE_NOT_FOUND  2  /* address not covered by any block → Modbus Exception 0x02 */

void   reg_cache_init(void);
void   reg_cache_invalidate_all(void);
void   reg_cache_write_block(uint8_t block_id, const uint16_t *data, uint16_t count);
int    reg_cache_lookup(uint16_t address, uint16_t *out_value);
bool   reg_cache_is_valid(void);
time_t reg_cache_last_updated(void);
