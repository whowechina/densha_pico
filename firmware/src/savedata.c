/*
 * Controller Config Save and Load
 * WHowe <github.com/whowechina>
 * 
 * Config is stored in last sector of flash
 */

#include "savedata.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <memory.h>


#include "pico/bootrom.h"
#include "pico/stdio.h"

#include "hardware/flash.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"

static struct {
    size_t size;
    size_t offset;
    void (*after_load)();
} modules[8] = {0};
static int module_num = 0;

static uint32_t my_magic = 0xcafec00e;

#define SAVE_TIMEOUT_US 5000000

// Note: Somehow firmware update erases last sector so we use second last
#define SAVE_SECTOR_OFFSET (PICO_FLASH_SIZE_BYTES - 2 * FLASH_SECTOR_SIZE)

#define GLOBAL_SECTOR_NUM 2
#define GLOBAL_SECTOR_SIZE (GLOBAL_SECTOR_NUM * FLASH_SECTOR_SIZE)
#define GLOBAL_SECTOR_OFFSET (SAVE_SECTOR_OFFSET - GLOBAL_SECTOR_SIZE)

typedef struct __attribute ((packed)) {
    uint32_t magic;
    uint8_t data[FLASH_PAGE_SIZE - 4];
} page_t;

static page_t old_data = {0};
static page_t new_data = {0};
static page_t default_data = {0};
static int data_page = -1;

static bool requesting_save = false;
static uint64_t requesting_time = 0;

static void __not_in_flash_func(save_program)()
{
    old_data = new_data;

    data_page = (data_page + 1) % (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE);
    printf("\nProgram Flash %d %8lx\n", data_page, old_data.magic);

    multicore_lockout_start_blocking();

    if (data_page == 0) {
        flash_range_erase(SAVE_SECTOR_OFFSET, FLASH_SECTOR_SIZE);
    }
    flash_range_program(SAVE_SECTOR_OFFSET + data_page * FLASH_PAGE_SIZE,
                        (uint8_t *)&old_data, FLASH_PAGE_SIZE);

    multicore_lockout_end_blocking();
}

static void load_default()
{
    printf("Load Default\n");
    new_data = default_data;
    new_data.magic = my_magic;
}

static const page_t *get_page(int id)
{
    int addr = XIP_BASE + SAVE_SECTOR_OFFSET;
    return (page_t *)(addr + FLASH_PAGE_SIZE * id);
}

static void save_load()
{
    for (int i = 0; i < FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE; i++) {
        if (get_page(i)->magic != my_magic) {
            break;
        }
        data_page = i;
    }

    if (data_page < 0) {
        load_default();
        savedata_request(false);
        return;
    }

    old_data = *get_page(data_page);
    new_data = old_data;
    printf("Page Loaded %d %8lx\n", data_page, new_data.magic);
}

static void save_loaded()
{
    for (int i = 0; i < module_num; i++) {
        modules[i].after_load();
    }
}

static union __attribute__((packed)) {
    pico_unique_board_id_t id;
    struct {
        uint32_t id32h;
        uint32_t id32l;
    };
    uint64_t id64;
} board_id;

uint32_t board_id_32()
{
    pico_get_unique_board_id(&board_id.id);
    return board_id.id32h ^ board_id.id32l;
}

uint64_t board_id_64()
{
    pico_get_unique_board_id(&board_id.id);
    return board_id.id64;
}

void savedata_init(uint32_t magic)
{
    my_magic = magic;
    save_load();
    savedata_loop();
    save_loaded();
}

void savedata_loop()
{
    if (requesting_save && (time_us_64() - requesting_time > SAVE_TIMEOUT_US)) {
        requesting_save = false;
        /* only when data is actually changed */
        if (memcmp(&old_data, &new_data, sizeof(old_data)) == 0) {
            return;
        }
        save_program();
    }
}

void *savedata_alloc(size_t size, void *def, void (*after_load)())
{
    size_t offset = 0;
    if (module_num > 0) {
        offset = modules[module_num - 1].offset + modules[module_num - 1].size;
    }
    if (offset + size > sizeof(default_data.data)) {
        return NULL;
    }

    modules[module_num].size = size;
    modules[module_num].offset = offset;
    modules[module_num].after_load = after_load;
    module_num++;
    memcpy(default_data.data + offset, def, size); // backup the default
    return new_data.data + offset;
}

void savedata_read_global(size_t offset, void *data, size_t size)
{
    if ((data == NULL) || (size == 0) || (offset >= GLOBAL_SECTOR_SIZE)) {
        return;
    }
    if (size > GLOBAL_SECTOR_SIZE - offset) {
        size = GLOBAL_SECTOR_SIZE - offset;
    }
    memcpy(data, (void *)(XIP_BASE + GLOBAL_SECTOR_OFFSET + offset), size);
}

void savedata_write_global(size_t offset, const void *data, size_t size)
{
    multicore_lockout_start_blocking();

    flash_range_erase(GLOBAL_SECTOR_OFFSET, GLOBAL_SECTOR_SIZE);
    flash_range_program(GLOBAL_SECTOR_OFFSET + offset, (const uint8_t *)data, size);

    multicore_lockout_end_blocking();
}

void savedata_request(bool immediately)
{
    if (!requesting_save) {
        printf("Save requested.\n");
        requesting_save = true;
        new_data.magic = my_magic;
        requesting_time = time_us_64();
    }
    if (immediately) {
        requesting_time = 0;
        savedata_loop();
    }
}
