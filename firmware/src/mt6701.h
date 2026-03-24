/*
 * MT6701 Angular Hall Sensor
 * WHowe <github.com/whowechina>
 */

#ifndef MT6701_H
#define MT6701_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

void mt6701_init(i2c_inst_t *i2c_port);
bool mt6701_init_sensor();
bool mt6701_is_present();
int mt6701_read_angle();

uint32_t mt6701_get_ppr();

#endif
