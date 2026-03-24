#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "pico/stdio.h"
#include "pico/stdlib.h"

#include "config.h"
#include "savedata.h"
#include "cli.h"

#include "light.h"
#include "lever.h"

#include "tmc2209.h"
#include "nv3007.h"

#include "usb_descriptors.h"

#define SENSE_LIMIT_MAX 9
#define SENSE_LIMIT_MIN -9

void cli_ctrl_c_cb(void)
{
    printf("\nAll debug controls off.\n");
    densha_runtime.debug.uart = false;
}

static void disp_light()
{
    printf("[Light]\n");
    printf("  Level: %d.\n", densha_cfg->light.level);
    printf("\n");
}

static void disp_handle()
{
    printf("[Handle]\n");
    printf("  MT6701 Present: %s\n", lever_mt6701_ready() ? "Yes" : "No");
    printf("  TMC2209 Present: %s\n", lever_tmc2209_ready() ? "Yes" : "No");
}

void handle_display(int argc, char *argv[])
{
    const char *usage = "Usage: display [light|handle]\n";
    if (argc > 1) {
        printf(usage);
        return;
    }

    if (argc == 0) {
        printf("CFG:%p\n", densha_cfg);
        disp_light();
        disp_handle();
        return;
    }

    const char *choices[] = {"light", "handle"};
    switch (cli_match_prefix(choices, count_of(choices), argv[0])) {
        case 0:
            disp_light();
            break;
        case 1:
            disp_handle();
            break;
        default:
            printf(usage);
            break;
    }
}

static void handle_level(int argc, char *argv[])
{
    const char *usage = "Usage: level <0..255>\n";
    if (argc != 1) {
        printf(usage);
        return;
    }

    int level = cli_extract_non_neg_int(argv[0], 0);
    if ((level < 0) || (level > 255)) {
        printf(usage);
        return;
    }

    densha_cfg->light.level = level;
    config_changed();
    disp_light();
}

static void handle_lcd(int argc, char *argv[])
{
    const char *usage = "Usage: lcd <backlight>\n"
                        "  backlight: 0..255\n";
    if (argc != 1) {
        printf(usage);
        return;
    }

    int level = cli_extract_non_neg_int(argv[0], 0);
    if (level < 0 || level > 255) {
        printf(usage);
        return;
    }

    densha_cfg->lcd.backlight = level;
    printf("LCD backlight set to %d.\n", level);
}

static void handle_step(int argc, char *argv[])
{
    const char *usage = "Usage: step [<steps> [period_us]]\n";

    uint32_t mscnt = 0;
    if (tmc2209_get_mscnt(&mscnt)) {
        printf("%5d(%3.2f): MSCNT = %lu\n",
                lever_get_angle(), lever_get_angle_deg(), mscnt);
    }

    if (argc == 0) {
        return;
    }

    if (argc > 2) {
        printf(usage);
        return;
    }

    int period_us = 5000;
    if (argc == 2) {
        period_us = cli_extract_non_neg_int(argv[1], 0);
    }

    if (period_us < 1) {
        period_us = 1;
    }

    int steps;
    if ((period_us > 65535) || !cli_extract_int(&steps, argv[0], 0)) {
        printf(usage);
        return;
    }

    tmc2209_move(steps, period_us);
}

static void handle_follow(int argc, char *argv[])
{
    lever_follow();
}

static bool read_tmc2209(uint8_t reg)
{
    uint32_t value;
    if (!tmc2209_read_reg(reg, &value)) {
        return false;
    }
    printf("TMC2209 REG[0x%02X] = 0x%08lX\n", reg, value);
    return true;
}

static void handle_ms(int argc, char *argv[])
{
    const char *usage = "Usage: ms <microsteps>\n"
                        "  microsteps: 1, 2, 4, 8, 16, 32, 64, 128, 256\n";
    if (argc != 1) {
        printf(usage);
        return;
    }

    int ms = cli_extract_non_neg_int(argv[0], 0);

    if (!tmc2209_set_microsteps(ms)) {
        printf("Set microsteps failed.\n");
        return;
    }

    printf("Microsteps set to %d.\n", ms);
}

static void handle_calibrate(int argc, char *argv[])
{
    lever_calibrate();
}

static bool write_tmc2209(uint8_t reg, uint32_t value)
{
    if (!tmc2209_write_reg(reg, value)) {
        return false;
    }
    printf("TMC2209 REG[0x%02X] set to 0x%08lX\n", reg, value);
    return true;
}

static bool read_uint_arg(char *arg, uint32_t *value, uint32_t min_v, uint32_t max_v)
{
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(arg, &end, 0);
    if ((errno != 0) || (end == arg) || (*end != '\0') ||
        (parsed > 0xFFFFFFFFull) || (parsed < min_v) || (parsed > max_v)) {
        return false;
    }

    *value = (uint32_t)parsed;
    return true;
}

static void handle_tmc2209(int argc, char *argv[])
{
    const char *usage = "Usage: tmc2209 <read> <reg>\n"
                        "       tmc2209 <write> <reg> <value>\n"
                        "  reg: 0..127\n"
                        "  value: 32-bit unsigned integer\n";
    if (argc < 2) {
        printf(usage);
        return;
    }

    const char *choices[] = {"read", "write"};
    int choice = cli_match_prefix(choices, count_of(choices), argv[0]);
    if (choice < 0) {
        printf(usage);
        return;
    }

    uint32_t reg = 0;
    if (!read_uint_arg(argv[1], &reg, 0, 127)) {
        printf(usage);
        return;
    }

    if (choice == 0) {
        if (argc != 2) {
            printf(usage);
            return;
        }
        if (!read_tmc2209((uint8_t)reg)) {
            printf("Read reg 0x%02X failed.\n", (uint8_t)reg);
        }
    }

    if (choice == 1) {
        if (argc != 3) {
            printf(usage);
            return;
        }

        uint32_t value = 0;
        if (!read_uint_arg(argv[2], &value, 0, 0xFFFFFFFFU)) {
            printf(usage);
            return;
        }

        if (!write_tmc2209((uint8_t)reg, value)) {
            printf("Write reg 0x%02X failed.\n", (uint8_t)reg);
        }
    }
}

static void handle_current(int argc, char *argv[])
{
    const char *usage = "Usage: current <irun> <ihold> <iholddelay>\n"
                        "  irun: 0..31\n"
                        "  ihold: 0..31\n"
                        "  iholddelay: 0..15\n";
    if (argc != 3) {
        printf(usage);
        return;
    }

    int irun = cli_extract_non_neg_int(argv[0], 0);
    int ihold = cli_extract_non_neg_int(argv[1], 0);
    int iholddelay = cli_extract_non_neg_int(argv[2], 0);

    if ((irun < 0) || (irun > 31) ||
        (ihold < 0) || (ihold > 31) ||
        (iholddelay < 0) || (iholddelay > 15)) {
        printf(usage);
        return;
    }

    if (!tmc2209_set_current(irun, ihold, iholddelay)) {
        printf("Set current failed.\n");
        return;
    }

    printf("Current set.\n");
}

static void handle_save()
{
    savedata_request(true);
}

static void handle_factory_reset()
{
    config_factory_reset();
    printf("Factory reset done.\n");
}

static void handle_debug(int argc, char *argv[])
{
    const char *usage = "Usage: debug <uart>\n";
    if (argc != 1) {
        printf(usage);
        return;
    }

    const char *choices[] = {"uart"};
    int choice = cli_match_prefix(choices, count_of(choices), argv[0]);
    if (choice < 0) {
        printf(usage);
        return;
    }

    densha_runtime.debug.uart = !densha_runtime.debug.uart;

    printf("UART debug: %s\n", densha_runtime.debug.uart ? "on" : "off");
}

void commands_init()
{
    cli_register("display", handle_display, "Display all config.");
    cli_register("level", handle_level, "Set LED brightness level.");
    cli_register("lcd", handle_lcd, "Set LCD backlight.");
    cli_register("step", handle_step, "TMC2209 movement.");
    cli_register("follow", handle_follow, "TMC2209 follow mode.");
    cli_register("ms", handle_ms, "Set microsteps.");
    cli_register("calibrate", handle_calibrate, "Calibrate microsteps.");
    cli_register("tmc2209", handle_tmc2209, "Read/write TMC2209 register.");
    cli_register("current", handle_current, "Set TMC2209 current");
    cli_register("save", handle_save, "Save config to flash.");
    cli_register("factory", handle_factory_reset, "Reset everything to default.");
    cli_register("debug", handle_debug, "Toggle debug controls.");
}
