void nv3007_flush_sw(void);
/*
 * NV3007 Buffered Display Driver for Pico with DMA Support
 * WHowe <github.com/whowechina>
 * 
 * LEDK is driven by PWM to adjust brightness
 */

#include <stdint.h>
#include <stdbool.h>

#include "hardware/spi.h"

void nv3007_init_spi(spi_inst_t *port, uint8_t sck, uint8_t tx, uint8_t csn);
void nv3007_init(spi_inst_t *port, uint8_t dc, uint8_t rst, uint8_t ledk);
void nv3007_crop(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool absolute);
uint16_t nv3007_get_crop_width();
uint16_t nv3007_get_crop_height();
void nv3007_dimmer(uint8_t level);
void nv3007_vsync();
void nv3007_flip();

#define nv3007_rgb32(r, g, b) ((r << 16) | (g << 8) | b)
#define nv3007_rgb565(rgb32) ((rgb32 >> 8) & 0xf800) | ((rgb32 >> 5) & 0x0780) | ((rgb32 >> 3) & 0x001f)
#define nv3007_gray(value) ((value >> 3 << 11) | (value >> 3 << 6) | (value >> 3))  

void nv3007_clear(uint16_t color, bool raw);
void nv3007_fill(uint16_t *pattern, size_t size, bool raw);
uint16_t *nv3007_vram(uint16_t x, uint16_t y);
void nv3007_vramcpy(uint32_t offset, const void *src, size_t count);
void nv3007_pixel_raw(int x, int y, uint16_t color);
void nv3007_pixel(int x, int y, uint16_t color, uint8_t mix, uint8_t bits);
void nv3007_hline(int x, int y, uint16_t w, uint16_t color, uint8_t mix);
void nv3007_vline(int x, int y, uint16_t h, uint16_t color, uint8_t mix);
void nv3007_bar(int x, int y, uint16_t w, uint16_t h, uint16_t color, uint8_t mix);
void nv3007_line(int x0, int y0, int x1, int y1, uint16_t color, uint8_t mix);

void nv3007_scroll(int dx, int dy);
