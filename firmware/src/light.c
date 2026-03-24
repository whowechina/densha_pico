/*
 * WS2812B Lights Control (Base + Left and Right Gimbals)
 * WHowe <github.com/whowechina>
 * 
 */

#include "light.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "bsp/board.h"
#include "hardware/pio.h"
#include "hardware/timer.h"

#include "ws2812.pio.h"

#include "board_defs.h"
#include "config.h"

#define HID_TIMEOUT 2*1000*1000

static uint32_t light_buf[8];

uint32_t rgb32(uint32_t r, uint32_t g, uint32_t b, bool gamma_fix)
{
    if (gamma_fix) {
        r = ((r + 1) * (r + 1) - 1) >> 8;
        g = ((g + 1) * (g + 1) - 1) >> 8;
        b = ((b + 1) * (b + 1) - 1) >> 8;
    }

    return (r << 16) | (g << 8) | (b << 0);
}

uint32_t rgb32_from_hsv(uint8_t h, uint8_t s, uint8_t v)
{
    uint32_t region, remainder, p, q, t;

    if (s == 0) {
        return v << 16 | v << 8 | v;
    }

    region = h / 43;
    remainder = (h % 43) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:
            return rgb32(v, t, p, false);
        case 1:
            return rgb32(q, v, p, false);
        case 2:
            return rgb32(p, v, t, false);
        case 3:
            return rgb32(p, q, v, false);
        case 4:
            return rgb32(t, p, v, false);
        default:
            return rgb32(v, p, q, false);
    }
}

static void drive_led()
{
    for (int i = 0; i < count_of(light_buf); i++) {
        pio_sm_put_blocking(pio0, 0, light_buf[i] << 8u);
    }
}

static inline uint32_t fix_order(uint32_t color)
{
#if RGB_ORDER == GRB
    uint8_t c1 = (color >> 16) & 0xff;
    uint8_t c2 = (color >> 8) & 0xff;
    uint8_t c3 = color & 0xff;
    return c2 << 16 | c1 << 8 | c3;
#else
    return color;
#endif
}

static inline uint32_t apply_level(uint32_t color, uint8_t level)
{
    unsigned r = (color >> 16) & 0xff;
    unsigned g = (color >> 8) & 0xff;
    unsigned b = color & 0xff;

    r = r * level / 255;
    g = g * level / 255;
    b = b * level / 255;

    return r << 16 | g << 8 | b;
}

void light_init()
{
    uint offset = pio_add_program(pio0, &ws2812_program);
    ws2812_program_init(pio0, 0, offset, RGB_PIN, 800000, false);
}

void light_update()
{
    static uint64_t last = 0;
    uint64_t now = time_us_64();
    if (now - last < 5000) { // 200Hz
        return;
    }

    last = now;

    drive_led();
}

static bool bypass_key_light(bool hid)
{
    static uint64_t key_timeout = 0;
    uint64_t now = time_us_64();

    if (hid) {
        key_timeout = now + HID_TIMEOUT;
        return false;
    }

    return now < key_timeout;
}

void light_set(uint8_t index, uint32_t color, bool hid)
{
    if (bypass_key_light(hid)) {
        return;
    }

    if (index >= count_of(light_buf)) {
        return;
    }
    light_buf[index] = apply_level(fix_order(color), densha_cfg->light.level);
}
