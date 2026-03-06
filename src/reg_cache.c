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

/* Static data arrays — total 274 × 2 = 548 bytes */
static uint16_t s_data_a[BLOCK_A_COUNT];
static uint16_t s_data_b[BLOCK_B_COUNT];
static uint16_t s_data_c[BLOCK_C_COUNT];
static uint16_t s_data_d[BLOCK_D_COUNT];

typedef struct {
    uint16_t  start;
    uint16_t  count;
    bool      valid;
    time_t    last_updated;
    uint16_t *data;
} cache_block_t;

static cache_block_t s_blocks[4] = {
    { BLOCK_A_START, BLOCK_A_COUNT, false, 0, s_data_a },
    { BLOCK_B_START, BLOCK_B_COUNT, false, 0, s_data_b },
    { BLOCK_C_START, BLOCK_C_COUNT, false, 0, s_data_c },
    { BLOCK_D_START, BLOCK_D_COUNT, false, 0, s_data_d },
};

static SemaphoreHandle_t s_mutex;

void reg_cache_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
}

void reg_cache_invalidate_all(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < 4; i++) {
        s_blocks[i].valid = false;
    }
    xSemaphoreGive(s_mutex);
}

void reg_cache_write_block(uint8_t block_id, const uint16_t *data, uint16_t count)
{
    if (block_id >= 4) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    cache_block_t *b = &s_blocks[block_id];
    if (count > b->count) count = b->count;
    memcpy(b->data, data, count * sizeof(uint16_t));
    b->valid        = true;
    b->last_updated = time(NULL);
    xSemaphoreGive(s_mutex);
}

int reg_cache_lookup(uint16_t address, uint16_t *out_value)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (int i = 0; i < 4; i++) {
        cache_block_t *b = &s_blocks[i];
        if (address >= b->start && address < (uint16_t)(b->start + b->count)) {
            if (!b->valid) {
                xSemaphoreGive(s_mutex);
                return REG_CACHE_STALE;     /* → Modbus Exception 0x04 */
            }
            *out_value = b->data[address - b->start];
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
    if (s_blocks[2].last_updated > ts) ts = s_blocks[2].last_updated;
    xSemaphoreGive(s_mutex);
    return ts;
}
