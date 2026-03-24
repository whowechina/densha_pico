/*
 * NV3007 Buffered Display Driver for Pico with DMA Support
 * WHowe <github.com/whowechina>
 * 
 * LEDK is driven by PWM to adjust brightness
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

#include "nv3007.h"

#define WIDTH 142
#define HEIGHT 428

static struct {
    spi_inst_t *spi;
    uint8_t dc;
    uint8_t rst;
    uint8_t ledk;
    int spi_dma;
    dma_channel_config spi_dma_cfg;
    int mem_dma;
    dma_channel_config mem_dma_cfg;    
} ctx;

static struct crop_ctx {
    uint16_t x;
    uint16_t y;
    uint16_t vx;
    uint16_t vy;
    uint16_t w;
    uint16_t h;
} crop = { .w = WIDTH, .h = HEIGHT };

static struct {
    int x;
    int y;
} scroll;

static uint16_t vram[HEIGHT * WIDTH];

static void send_cmd(uint8_t cmd, const void *data, size_t len)
{
    spi_set_format(ctx.spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_put(ctx.dc, 0);
    spi_write_blocking(ctx.spi, &cmd, sizeof(cmd));
    gpio_put(ctx.dc, 1);

    if (len > 0) {
        spi_write_blocking(ctx.spi, data, len);
    }
}

void nv3007_init_spi(spi_inst_t *port, uint8_t sck, uint8_t tx, uint8_t csn)
{
    // SPI overclock
    uint32_t freq = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, freq, freq);
    spi_init(port, freq / 2);

    gpio_set_function(tx, GPIO_FUNC_SPI);
    gpio_set_function(sck, GPIO_FUNC_SPI);
    gpio_init(csn);
    gpio_set_dir(csn, GPIO_OUT);
    gpio_put(csn, 0);
}

static void init_gpio()
{
    gpio_init(ctx.dc);
    gpio_set_dir(ctx.dc, GPIO_OUT);

    gpio_init(ctx.rst);
    gpio_set_dir(ctx.rst, GPIO_OUT);

    gpio_init(ctx.ledk);
    gpio_set_function(ctx.ledk, GPIO_FUNC_PWM);
    gpio_set_drive_strength(ctx.ledk, GPIO_DRIVE_STRENGTH_12MA);
}

static void init_pwm()
{
    uint slice_num = pwm_gpio_to_slice_num(ctx.ledk);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 4.f);
    pwm_init(slice_num, &cfg, true);
    pwm_set_wrap(slice_num, 255);
    pwm_set_enabled(slice_num, true);
}

static void init_dma()
{
    ctx.spi_dma = dma_claim_unused_channel(true);
    ctx.spi_dma_cfg = dma_channel_get_default_config(ctx.spi_dma);
    channel_config_set_transfer_data_size(&ctx.spi_dma_cfg, DMA_SIZE_16);
    channel_config_set_dreq(&ctx.spi_dma_cfg, spi_get_dreq(ctx.spi, true));

    ctx.mem_dma = dma_claim_unused_channel(true);
    ctx.mem_dma_cfg = dma_channel_get_default_config(ctx.mem_dma);
    channel_config_set_transfer_data_size(&ctx.mem_dma_cfg, DMA_SIZE_32);
    channel_config_set_write_increment(&ctx.mem_dma_cfg, true);
}

static const struct {
    uint8_t cmd;
    uint8_t data_len;
    uint8_t data[4];
    uint16_t delay_ms;
} init_seq[] = {
    {0xff, 1, {0xa5}},
    {0x9a, 1, {0x08}},
    {0x9b, 1, {0x08}},
    {0x9c, 1, {0xb0}},
    {0x9d, 1, {0x16}},
    {0x9e, 1, {0xc4}},
    {0x8f, 2, {0x55, 0x04}},
    {0x84, 1, {0x90}},
    {0x83, 1, {0x7b}},
    {0x85, 1, {0x33}},
    {0x60, 1, {0x00}},
    {0x70, 1, {0x00}},
    {0x61, 1, {0x02}},
    {0x71, 1, {0x02}},
    {0x62, 1, {0x04}},
    {0x72, 1, {0x04}},
    {0x6c, 1, {0x29}},
    {0x7c, 1, {0x29}},
    {0x6d, 1, {0x31}},
    {0x7d, 1, {0x31}},
    {0x6e, 1, {0x0f}},
    {0x7e, 1, {0x0f}},
    {0x66, 1, {0x21}},
    {0x76, 1, {0x21}},
    {0x68, 1, {0x3A}},
    {0x78, 1, {0x3A}},
    {0x63, 1, {0x07}},
    {0x73, 1, {0x07}},
    {0x64, 1, {0x05}},
    {0x74, 1, {0x05}},
    {0x65, 1, {0x02}},
    {0x75, 1, {0x02}},
    {0x67, 1, {0x23}},
    {0x77, 1, {0x23}},
    {0x69, 1, {0x08}},
    {0x79, 1, {0x08}},
    {0x6a, 1, {0x13}},
    {0x7a, 1, {0x13}},
    {0x6b, 1, {0x13}},
    {0x7b, 1, {0x13}},
    {0x6f, 1, {0x00}},
    {0x7f, 1, {0x00}},
    {0x50, 1, {0x00}},
    {0x52, 1, {0xd6}},
    {0x53, 1, {0x08}},
    {0x54, 1, {0x08}},
    {0x55, 1, {0x1e}},
    {0x56, 1, {0x1c}},
    {0xa0, 3, {0x2b, 0x24, 0x00}},
    {0xa1, 1, {0x87}},
    {0xa2, 1, {0x86}},
    {0xa5, 1, {0x00}},
    {0xa6, 1, {0x00}},
    {0xa7, 1, {0x00}},
    {0xa8, 1, {0x36}},
    {0xa9, 1, {0x7e}},
    {0xaa, 1, {0x7e}},
    {0xb9, 1, {0x85}},
    {0xba, 1, {0x84}},
    {0xbb, 1, {0x83}},
    {0xbc, 1, {0x82}},
    {0xbd, 1, {0x81}},
    {0xbe, 1, {0x80}},
    {0xbf, 1, {0x01}},
    {0xc0, 1, {0x02}},
    {0xc1, 1, {0x00}},
    {0xc2, 1, {0x00}},
    {0xc3, 1, {0x00}},
    {0xc4, 1, {0x33}},
    {0xc5, 1, {0x7e}},
    {0xc6, 1, {0x7e}},
    {0xc8, 2, {0x33, 0x33}},
    {0xc9, 1, {0x68}},
    {0xca, 1, {0x69}},
    {0xcb, 1, {0x6a}},
    {0xcc, 1, {0x6b}},
    {0xcd, 2, {0x33, 0x33}},
    {0xce, 1, {0x6c}},
    {0xcf, 1, {0x6d}},
    {0xd0, 1, {0x6e}},
    {0xd1, 1, {0x6f}},
    {0xab, 2, {0x03, 0x67}},
    {0xac, 2, {0x03, 0x6b}},
    {0xad, 2, {0x03, 0x68}},
    {0xae, 2, {0x03, 0x6c}},
    {0xb3, 1, {0x00}},
    {0xb4, 1, {0x00}},
    {0xb5, 1, {0x00}},
    {0xb6, 1, {0x32}},
    {0xb7, 1, {0x7e}},
    {0xb8, 1, {0x7e}},
    {0xe0, 1, {0x00}},
    {0xe1, 2, {0x03, 0x0f}},
    {0xe2, 1, {0x04}},
    {0xe3, 1, {0x01}},
    {0xe4, 1, {0x0e}},
    {0xe5, 1, {0x01}},
    {0xe6, 1, {0x19}},
    {0xe7, 1, {0x10}},
    {0xe8, 1, {0x10}},
    {0xea, 1, {0x12}},
    {0xeb, 1, {0xd0}},
    {0xec, 1, {0x04}},
    {0xed, 1, {0x07}},
    {0xee, 1, {0x07}},
    {0xef, 1, {0x09}},
    {0xf0, 1, {0xd0}},
    {0xf1, 1, {0x0e}},
    {0xe9, 1, {0x29}},
    {0xec, 1, {0x04}},
    {0x35, 1, {0x00}},
    {0x44, 2, {0x00, 0x10}},
    {0x46, 1, {0x10}},
    {0xff, 1, {0x00}},
    {0x3a, 1, {0x05}},
    {0x11, 0, {0}, 220}, // sleep out, delay 220ms
    {0x29, 0, {200}},      // display on
};

static void init_lcd(void)
{
    gpio_put(ctx.rst, 0);
    sleep_ms(100);
    gpio_put(ctx.rst, 1);
    sleep_ms(120);

    size_t n = count_of(init_seq);
    for (size_t i = 0; i < n; ++i) {
        send_cmd(init_seq[i].cmd, init_seq[i].data, init_seq[i].data_len);
        if (init_seq[i].delay_ms) {
            sleep_ms(init_seq[i].delay_ms);
        }
        sleep_ms(1);
    }
    nv3007_crop(12, 0, WIDTH, HEIGHT, true);
}

void nv3007_init(spi_inst_t *port, uint8_t dc, uint8_t rst, uint8_t ledk)
{
    ctx.spi = port;
    ctx.dc = dc;
    ctx.rst = rst;
    ctx.ledk = ledk;

    init_gpio();
    init_pwm();
    init_dma();
    init_lcd();
}

static void update_addr()
{
    uint16_t xs = crop.x + crop.vx;
    uint16_t xe = xs + crop.w - 1;
    uint8_t ca[] = { xs >> 8, xs & 0xff, xe >> 8, xe & 0xff };
    printf("xs: %d xe: %d\n", xs, xe);
    send_cmd(0x2a, ca, sizeof(ca));

    uint16_t ys = crop.y + crop.vy;
    uint16_t ye = ys + crop.h - 1;
    uint8_t ra[] = { ys >> 8, ys & 0xff, ye >> 8, ye & 0xff };
    send_cmd(0x2b, ra, sizeof(ra));
}

void nv3007_crop(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool absolute)
{
    if (absolute) {
        crop.x = x;
        crop.y = y;
        crop.vx = 0;
        crop.vy = 0;
    } else {
        crop.vx = x;
        crop.vy = y;
    }
    crop.w = w;
    crop.h = h;
    update_addr();
}

uint16_t nv3007_get_crop_width()
{
    return crop.w;
}

uint16_t nv3007_get_crop_height()
{
    return crop.h;
}

void nv3007_dimmer(uint8_t level)
{
    pwm_set_gpio_level(ctx.ledk, level);
}

void nv3007_vsync()
{
    dma_channel_wait_for_finish_blocking(ctx.spi_dma);
}

void nv3007_flush(bool vsync)
{
    if (dma_channel_is_busy(ctx.spi_dma)) {
        return;
    }

    send_cmd(0x2c, NULL, 0);
    spi_set_format(ctx.spi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    dma_channel_configure(ctx.spi_dma, &ctx.spi_dma_cfg,
                          &spi_get_hw(ctx.spi)->dr, // write to
                          vram, // read from
                          crop.w * crop.h, // element count
                          true); // start right now
    if (vsync) {
        nv3007_vsync();
    }
}

static void vram_dma(uint32_t offset, const void *src, bool inc, size_t pixels)
{
    channel_config_set_read_increment(&ctx.mem_dma_cfg, inc);
    dma_channel_configure(ctx.mem_dma, &ctx.mem_dma_cfg,
                          &vram[offset], src, pixels / 2, true);
    dma_channel_wait_for_finish_blocking(ctx.mem_dma);
}

void static inline put_pixel(int x, int y, uint16_t color)
{
    x += scroll.x;
    y += scroll.y;
    if ((x < 0) || (x >= crop.w) || (y < 0) || (y >= crop.h)) {
        return;
    }
    vram[y * crop.w + x] = color;
}

static void soft_fill(uint16_t *pattern, size_t size)
{
    int x = scroll.x > 0 ? scroll.x : 0;
    int y = scroll.y > 0 ? scroll.y : 0;
    int w = crop.w - abs(scroll.x);
    int h = crop.h - abs(scroll.y);
    if ((crop.w <= 0) || (crop.h <= 0)) {
        return;
    }

    int p = 0;
    for (int i = 0; i < w; i++) {
        for (int j = 0; j < h; j++) {
            vram[(y + j) * crop.w + x + i] = pattern[p];
            p = (p + 1) % size;
        }
    }
}

void nv3007_clear(uint16_t color, bool raw)
{
    if (raw || !(scroll.x || scroll.y)) {
        uint32_t c32 = (color << 16) | color;
        vram_dma(0, &c32, false, crop.w * crop.h);
        return;
    }

    soft_fill(&color, 1);
}

void nv3007_fill(uint16_t *pattern, size_t size, bool raw)
{
    if (raw || !(scroll.x || scroll.y)) {
        int remain = crop.w * crop.h;
        int offset = 0;
        while (remain > 0) {
            int to_copy = remain < size ? remain : size;
            vram_dma(offset, pattern, true, to_copy);
            offset += to_copy;
            remain -= to_copy;
        }
        return;
    }

    soft_fill(pattern, size);
}

void nv3007_vramcpy(uint32_t offset, const void *src, size_t pixels)
{
    vram_dma(offset, src, true, pixels);
}

void nv3007_scroll(int dx, int dy)
{
    scroll.x = dx;
    scroll.y = dy;
}

void static inline mix_pixel(int x, int y, uint16_t color, uint8_t mix, uint8_t bits)
{
    if (mix == 0) {
        return;
    }

    x += scroll.x;
    y += scroll.y;
    if ((x < 0) || (x >= crop.w) || (y < 0) || (y >= crop.h)) {
        return;
    }

    if (mix == (1L << bits) - 1) {
        vram[y * crop.w + x] = color;
        return;
    }

    uint16_t bg = vram[y * crop.w + x];
    uint16_t bg_ratio = (1L << bits) - mix;
    uint8_t r = ((bg >> 11) * bg_ratio + (color >> 11) * mix) >> bits;
    uint8_t g = (((bg >> 5) & 0x3f) * bg_ratio + ((color >> 5) & 0x3f) * mix) >> bits;
    uint8_t b = ((bg & 0x1f) * bg_ratio + (color & 0x1f) * mix) >> bits;
    vram[y * crop.w + x] = (r << 11) | (g << 5) | b;
}

void nv3007_pixel_raw(int x, int y, uint16_t color)
{
    vram[y * crop.w + x] = color;
}

void nv3007_pixel(int x, int y, uint16_t color, uint8_t mix, uint8_t bits)
{
    mix_pixel(x, y, color, mix, bits);
}

void nv3007_hline(int x, int y, uint16_t w, uint16_t color, uint8_t mix)
{
    for (int i = 0; i < w; i++) {
        mix_pixel(x + i, y, color, mix, 8);
    }
}

void nv3007_vline(int x, int y, uint16_t h, uint16_t color, uint8_t mix)
{
    for (int i = 0; i < h; i++) {
        mix_pixel(x, y + i, color, mix, 8);
    }
}

void nv3007_bar(int x, int y, uint16_t w, uint16_t h, uint16_t color, uint8_t mix)
{
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            mix_pixel(x + j, y + i, color, mix, 8);
        }
    }
}

void nv3007_line(int x0, int y0, int x1, int y1, uint16_t color, uint8_t mix)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;

    for (;;) {
        mix_pixel(x0, y0, color, mix, 8);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

uint16_t *nv3007_vram(uint16_t x, uint16_t y)
{
    return &vram[y * crop.w + x];
}
