#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/*
 * Register cache — static FC03 read blocks from the Huawei inverter:
 *
 *  Block 0 (A): 30000–30080   (81 regs)  — device info / firmware / SN
 *  Block 1 (B): 32000–32115  (116 regs)  — core PV/AC values
 *  Block 2 (C): 37000–37155  (156 regs)  — battery SOC, power, grid import + meter block for 37100/0x38
 *  Block 3 (D): 38210–38233   (24 regs)  — battery pack detail (optional)
 *  Block 4 (E): 37200         (1 reg)    — optimizer count
 *  Block 5 (F): 37738–37787   (50 regs)  — battery detail used by HA
 *  Block 6 (G): 42900         (1 reg)    — daylight saving time
 *  Block 7 (H): 43006         (1 reg)    — time zone
 *  Block 8 (I): 47000         (1 reg)    — storage feature flag
 *  Block 9 (J): 47089         (1 reg)    — storage control mode
 *  Block 10 (K): 47954–48019  (66 regs)  — capacity control / TOU periods / related battery config
 *  Block 11 (L): 40120–40154  (35 regs)  — inverter configuration / writable control values
 *  Block 12 (M): 47075–47102  (28 regs)  — battery control values used by HA config
 *  Block 13 (N): 47242–47299  (58 regs)  — grid charge / TOU battery control values
 *  Block 14 (O): 47415–47418  (4 regs)   — active power / feed-in control values
 *  Block 15 (P): 47589–47590  (2 regs)   — remote charge/discharge control values
 *  Block 16 (Q): 42054–42055  (2 regs)   — MPPT scan config used by HA config
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
void   reg_cache_invalidate_range(uint16_t start_address, uint16_t count);
void   reg_cache_write_block(uint8_t block_id, const uint16_t *data, uint16_t count);
void   reg_cache_write_registers(uint16_t start_address, const uint16_t *data, uint16_t count);
int    reg_cache_lookup(uint16_t address, uint16_t *out_value);
int    reg_cache_lookup_with_meta(uint16_t address, uint16_t *out_value, time_t *out_last_updated);
bool   reg_cache_is_valid(void);
time_t reg_cache_last_updated(void);
