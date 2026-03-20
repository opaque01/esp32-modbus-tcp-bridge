#include "reg_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

/* Block address ranges */
#define BLOCK_A_START   30000u
#define BLOCK_A_COUNT      81u
#define BLOCK_B_START   32000u
#define BLOCK_B_COUNT     116u
#define BLOCK_C_START   37000u
#define BLOCK_C_COUNT     156u
#define BLOCK_D_START   38210u
#define BLOCK_D_COUNT      24u
#define BLOCK_E_START   37200u
#define BLOCK_E_COUNT       1u
#define BLOCK_F_START   37738u
#define BLOCK_F_COUNT      50u
#define BLOCK_G_START   42900u
#define BLOCK_G_COUNT       1u
#define BLOCK_H_START   43006u
#define BLOCK_H_COUNT       1u
#define BLOCK_I_START   47000u
#define BLOCK_I_COUNT       1u
#define BLOCK_J_START   47089u
#define BLOCK_J_COUNT       1u
#define BLOCK_K_START   47954u
#define BLOCK_K_COUNT      66u
#define BLOCK_L_START   40120u
#define BLOCK_L_COUNT      35u
#define BLOCK_M_START   47075u
#define BLOCK_M_COUNT      28u
#define BLOCK_N_START   47242u
#define BLOCK_N_COUNT      58u
#define BLOCK_O_START   47415u
#define BLOCK_O_COUNT       4u
#define BLOCK_P_START   47589u
#define BLOCK_P_COUNT       2u
#define BLOCK_Q_START   42054u
#define BLOCK_Q_COUNT       2u
#define CACHE_BLOCK_COUNT   17u

/* Static data arrays — total 627 × 2 = 1254 bytes */
static uint16_t s_data_a[BLOCK_A_COUNT];
static uint16_t s_data_b[BLOCK_B_COUNT];
static uint16_t s_data_c[BLOCK_C_COUNT];
static uint16_t s_data_d[BLOCK_D_COUNT];
static uint16_t s_data_e[BLOCK_E_COUNT];
static uint16_t s_data_f[BLOCK_F_COUNT];
static uint16_t s_data_g[BLOCK_G_COUNT];
static uint16_t s_data_h[BLOCK_H_COUNT];
static uint16_t s_data_i[BLOCK_I_COUNT];
static uint16_t s_data_j[BLOCK_J_COUNT];
static uint16_t s_data_k[BLOCK_K_COUNT];
static uint16_t s_data_l[BLOCK_L_COUNT];
static uint16_t s_data_m[BLOCK_M_COUNT];
static uint16_t s_data_n[BLOCK_N_COUNT];
static uint16_t s_data_o[BLOCK_O_COUNT];
static uint16_t s_data_p[BLOCK_P_COUNT];
static uint16_t s_data_q[BLOCK_Q_COUNT];

typedef struct {
    uint16_t  start;
    uint16_t  count;
    bool      valid;
    time_t    last_updated;
    uint16_t *data;
} cache_block_t;

static cache_block_t s_blocks[CACHE_BLOCK_COUNT] = {
    { BLOCK_A_START, BLOCK_A_COUNT, false, 0, s_data_a },
    { BLOCK_B_START, BLOCK_B_COUNT, false, 0, s_data_b },
    { BLOCK_C_START, BLOCK_C_COUNT, false, 0, s_data_c },
    { BLOCK_D_START, BLOCK_D_COUNT, false, 0, s_data_d },
    { BLOCK_E_START, BLOCK_E_COUNT, false, 0, s_data_e },
    { BLOCK_F_START, BLOCK_F_COUNT, false, 0, s_data_f },
    { BLOCK_G_START, BLOCK_G_COUNT, false, 0, s_data_g },
    { BLOCK_H_START, BLOCK_H_COUNT, false, 0, s_data_h },
    { BLOCK_I_START, BLOCK_I_COUNT, false, 0, s_data_i },
    { BLOCK_J_START, BLOCK_J_COUNT, false, 0, s_data_j },
    { BLOCK_K_START, BLOCK_K_COUNT, false, 0, s_data_k },
    { BLOCK_L_START, BLOCK_L_COUNT, false, 0, s_data_l },
    { BLOCK_M_START, BLOCK_M_COUNT, false, 0, s_data_m },
    { BLOCK_N_START, BLOCK_N_COUNT, false, 0, s_data_n },
    { BLOCK_O_START, BLOCK_O_COUNT, false, 0, s_data_o },
    { BLOCK_P_START, BLOCK_P_COUNT, false, 0, s_data_p },
    { BLOCK_Q_START, BLOCK_Q_COUNT, false, 0, s_data_q },
};

static SemaphoreHandle_t s_mutex;

void reg_cache_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
}

void reg_cache_invalidate_all(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < CACHE_BLOCK_COUNT; i++) {
        s_blocks[i].valid = false;
    }
    xSemaphoreGive(s_mutex);
}

void reg_cache_invalidate_range(uint16_t start_address, uint16_t count)
{
    if (count == 0) {
        return;
    }

    uint32_t end_address = (uint32_t)start_address + (uint32_t)count;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < CACHE_BLOCK_COUNT; i++) {
        cache_block_t *b = &s_blocks[i];
        uint32_t block_start = b->start;
        uint32_t block_end = block_start + b->count;
        if (start_address < block_end && end_address > block_start) {
            b->valid = false;
        }
    }
    xSemaphoreGive(s_mutex);
}

void reg_cache_write_block(uint8_t block_id, const uint16_t *data, uint16_t count)
{
    if (block_id >= CACHE_BLOCK_COUNT) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cache_block_t *b = &s_blocks[block_id];
    if (count > b->count) count = b->count;
    memcpy(b->data, data, count * sizeof(uint16_t));
    b->valid        = true;
    b->last_updated = time(NULL);
    xSemaphoreGive(s_mutex);
}

void reg_cache_write_registers(uint16_t start_address, const uint16_t *data, uint16_t count)
{
    if (!data || count == 0) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    time_t now = time(NULL);
    for (int i = 0; i < CACHE_BLOCK_COUNT; i++) {
        cache_block_t *b = &s_blocks[i];
        uint32_t block_start = b->start;
        uint32_t block_end = block_start + b->count;
        uint32_t write_start = start_address;
        uint32_t write_end = write_start + count;

        if (write_start >= block_end || write_end <= block_start) {
            continue;
        }

        uint32_t overlap_start = write_start > block_start ? write_start : block_start;
        uint32_t overlap_end = write_end < block_end ? write_end : block_end;
        for (uint32_t addr = overlap_start; addr < overlap_end; addr++) {
            b->data[addr - block_start] = data[addr - write_start];
        }
        b->valid = true;
        b->last_updated = now;
    }
    xSemaphoreGive(s_mutex);
}

int reg_cache_lookup(uint16_t address, uint16_t *out_value)
{
    return reg_cache_lookup_with_meta(address, out_value, NULL);
}

int reg_cache_lookup_with_meta(uint16_t address, uint16_t *out_value, time_t *out_last_updated)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < CACHE_BLOCK_COUNT; i++) {
        cache_block_t *b = &s_blocks[i];
        if (address >= b->start && address < (uint16_t)(b->start + b->count)) {
            if (!b->valid) {
                xSemaphoreGive(s_mutex);
                return REG_CACHE_STALE;     /* → Modbus Exception 0x04 */
            }
            *out_value = b->data[address - b->start];
            if (out_last_updated) {
                *out_last_updated = b->last_updated;
            }
            xSemaphoreGive(s_mutex);
            return REG_CACHE_OK;
        }
    }
    xSemaphoreGive(s_mutex);
    return REG_CACHE_NOT_FOUND;             /* → Modbus Exception 0x02 */
}

bool reg_cache_is_valid(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ok = s_blocks[1].valid && s_blocks[2].valid; /* B + C = essential blocks */
    xSemaphoreGive(s_mutex);
    return ok;
}

time_t reg_cache_last_updated(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    time_t ts = s_blocks[1].last_updated;
    for (int i = 2; i < CACHE_BLOCK_COUNT; i++) {
        if (s_blocks[i].last_updated > ts) {
            ts = s_blocks[i].last_updated;
        }
    }
    xSemaphoreGive(s_mutex);
    return ts;
}
