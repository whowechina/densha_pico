/*
 * TMC2209 Motor Controller
 * WHowe <github.com/whowechina>
 */

#ifndef TMC2209_H
#define TMC2209_H

#include <stdint.h>
#include <stdbool.h>

#include "hardware/pio.h"
#include "hardware/uart.h"

void tmc2209_init(PIO pio, uint step_pin, uint dir_pin, uint enable_pin);

void tmc2209_enable();
void tmc2209_disable();

void tmc2209_move(int steps, uint16_t step_period_us);

void tmc2209_uart_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint8_t addr);

bool tmc2209_set_current(uint8_t irun, uint8_t ihold,
						 uint8_t iholddelay);

bool tmc2209_get_ifcnt(uint32_t *ifcnt);

bool tmc2209_get_mscnt(uint32_t *mscnt);

bool tmc2209_read_reg(uint8_t reg, uint32_t *value);
bool tmc2209_write_reg(uint8_t reg, uint32_t value);

bool tmc2209_set_microsteps(uint16_t steps);

bool tmc2209_present();

bool tmc2209_is_busy();

void tmc2209_stop();

#endif
