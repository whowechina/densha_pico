/*
 * Controller Config
 * WHowe <github.com/whowechina>
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

typedef struct __attribute__((packed)) {
    struct {
        uint8_t level;
    } light;
    struct {
        uint8_t backlight;
    } lcd;
} densha_cfg_t;

typedef struct {
    uint8_t msmap[4096];
    struct {
        bool uart;
    } debug;
} densha_runtime_t;

extern densha_cfg_t *densha_cfg;
extern densha_runtime_t densha_runtime;

void config_init();
void config_changed(); // Notify the config has changed
void config_factory_reset(); // Reset the config to factory default

#endif
