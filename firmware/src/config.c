/*
 * Contro1ler Config and Runtime Data
 * WHowe <github.com/whowechina>
 * 
 * Config is a global data structure that stores all the configuration
 * Runtime is something to share between files.
 */

#include "config.h"
#include "savedata.h"

densha_cfg_t *densha_cfg;

static densha_cfg_t default_cfg = {
    .light = {
        .level = 128,
    },
    .lcd = {
        .backlight = 200,
    },
    .mascon = {
        .hold = 10,
        .run = 15,
    },
};

densha_runtime_t densha_runtime;

static void config_loaded()
{
    config_changed();
    savedata_read_global(0, densha_runtime.msmap, sizeof(densha_runtime.msmap));
}

void config_changed()
{
    savedata_request(false);
}

void config_factory_reset()
{
    *densha_cfg = default_cfg;
    savedata_request(true);
}

void config_init()
{
    densha_cfg = (densha_cfg_t *)savedata_alloc(sizeof(*densha_cfg), &default_cfg, config_loaded);
}
