/*
 * Train Master Lever Abstraction
 */

#ifndef LEVER_H
#define LEVER_H

#include <stdint.h>
#include <stdbool.h>

void lever_init();
void lever_update();

bool lever_mt6701_ready();
bool lever_tmc2209_ready();

void lever_calibrate();
void lever_follow();

int lever_get_mscnt();

uint16_t lever_get_angle();
float lever_get_angle_deg();


#endif