/*
 * Controller Flash Save and Load
 * WHowe <github.com/whowechina>
 */

#ifndef SAVEDATA_H
#define SAVEDATA_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "pico/multicore.h"

uint32_t board_id_32();
uint64_t board_id_64();

void savedata_init(uint32_t magic);

void savedata_loop();

void *savedata_alloc(size_t size, void *def, void (*after_load)());
void savedata_request(bool immediately);

/* Global fixed area: second-to-last flash sector, not wear-leveled */
void savedata_read_global(size_t offset, void *data, size_t size);
void savedata_write_global(size_t offset, const void *data, size_t size);

#endif
