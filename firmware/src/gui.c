
/*
 * GUI on ST7789
 * WHowe <github.com/whowechina>
 * 
 */

#include <stdio.h>
#include <math.h>

#include "board_defs.h"

#include "res/resource.h"
#include "gfx.h"
#include "gui.h"

#include "pico/multicore.h"

#include "light.h"
#include "nv3007.h"
#include "config.h"

#include "cli.h"

void gui_init()
{
    nv3007_init_spi(NV3007_SPI, NV3007_SCK_PIN, NV3007_TX_PIN, NV3007_CSN_PIN);
    nv3007_init(NV3007_SPI, NV3007_DC_PIN, NV3007_RST_PIN, NV3007_LEDK_PIN);
    gui_level(densha_cfg->lcd.backlight);
}

void gui_level(uint8_t level)
{
    nv3007_dimmer(level);
}

static const int8_t sin_table[256] = {
     0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
    48, 51, 54, 57, 60, 62, 65, 68, 70, 73, 75, 78, 80, 83, 85, 87,
    90, 92, 94, 96, 98,100,102,104,106,107,109,111,112,114,115,117,
   118,119,121,122,123,124,125,126,126,127,127,127,127,127,127,127,
   127,127,127,127,127,127,126,126,125,124,123,122,121,119,118,117,
   115,114,112,111,109,107,106,104,102,100, 98, 96, 94, 92, 90, 87,
    85, 83, 80, 78, 75, 73, 70, 68, 65, 62, 60, 57, 54, 51, 48, 46,
    43, 40, 37, 34, 31, 28, 25, 22, 19, 16, 12,  9,  6,  3,  0, -3,
    -6, -9,-12,-16,-19,-22,-25,-28,-31,-34,-37,-40,-43,-46,-48,-51,
   -54,-57,-60,-62,-65,-68,-70,-73,-75,-78,-80,-83,-85,-87,-90,-92,
   -94,-96,-98,-100,-102,-104,-106,-107,-109,-111,-112,-114,-115,-117,
  -118,-119,-121,-122,-123,-124,-125,-126,-126,-127,-127,-127,-127,-127,
  -127,-127,-127,-127,-127,-127,-127,-126,-126,-125,-124,-123,-122,-121,
  -119,-118,-117,-115,-114,-112,-111,-109,-107,-106,-104,-102,-100,-98,
   -96,-94,-92,-90,-87,-85,-83,-80,-78,-75,-73,-70,-68,-65,-62,-60,
   -57,-54,-51,-48,-46,-43,-40,-37,-34,-31,-28,-25,-22,-19,-16,-12,
    -9, -6, -3
};

uint16_t hsv_to_rgb565(uint8_t h)
{
    uint8_t region = h / 43;
    uint8_t remainder = (h - (region * 43)) * 6;

    uint8_t q = (255 - remainder);
    uint8_t t = remainder;

    uint8_t r,g,b;

    switch(region)
    {
        case 0: r=255; g=t;   b=0;   break;
        case 1: r=q;   g=255; b=0;   break;
        case 2: r=0;   g=255; b=t;   break;
        case 3: r=0;   g=q;   b=255; break;
        case 4: r=t;   g=0;   b=255; break;
        default:r=255; g=0;   b=q;   break;
    }

    return ((r & 0xF8) << 8) |
           ((g & 0xFC) << 3) |
           (b >> 3);
}

static uint32_t frame = 0;

static void sin_wave(int w, int h)
{
    for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
            int16_t v =
                sin_table[(x * 4 + frame) & 255] +
                sin_table[(y * 4 + frame) & 255];

            uint8_t hue = (uint8_t)(v + frame * 2);
            nv3007_pixel_raw(x, y, hsv_to_rgb565(hue));
        }
    }
}

void fractal_zoom(int w, int h)
{
    static int32_t zoom = 300;   // 越小放大越多
    static int32_t cx = -743;    // 中心点
    static int32_t cy = 131;

    for(int py = 0; py < h; py++) {
        for(int px = 0; px < w; px++) {
            int32_t x0 = cx + (px - w/2) * zoom / w;
            int32_t y0 = cy + (py - h/2) * zoom / w;

            int32_t x = 0;
            int32_t y = 0;

            uint8_t iter = 0;
            const uint8_t max_iter = 40;

            while(iter < max_iter){
                int32_t xx = (x * x) >> 10;
                int32_t yy = (y * y) >> 10;

                if(xx + yy > 4000)
                    break;

                int32_t xy = (x * y) >> 9;

                y = xy + y0;
                x = xx - yy + x0;

                iter++;
            }


            nv3007_pixel_raw(px, py, hsv_to_rgb565(iter * 8 + frame));
        }
    }

    zoom = (zoom * 253) >> 8;
    if(zoom < 20)
        zoom = 300;

    frame++;
}

void tunnel_effect(int w, int h)
{
    int cx = w / 2;
    int cy = h / 2;

    for(int y = 0; y < h; y++)
    {
        for(int x = 0; x < w; x++)
        {
            int dx = x - cx;
            int dy = y - cy;

            int dist = (dx*dx + dy*dy) >> 6;

           // int angle = (dx * 32) / (abs(dy) + 1);

            uint8_t color_index = dist + frame;
    
            nv3007_pixel_raw(x, y, hsv_to_rgb565(color_index));
        }
    }
}

static void plasma_zoom(int w, int h)
{
    int cx = w/2;
    int cy = h/2;

    for(int y = 0; y < h; y++)
    {
        for(int x = 0; x < w; x++)
        {
            int dx = x - cx;
            int dy = y - cy;

            int v =
                (dx*dx >> 7) +
                (dy*dy >> 7);

            uint8_t c = v + frame;

            nv3007_pixel_raw(x, y, hsv_to_rgb565(c));
        }
    }
}

void fake_feedback_zoom(int w, int h)
{
    int cx = w/2;
    int cy = h/2;

    for(int y=0;y<h;y++)
    {
        for(int x=0;x<w;x++)
        {
            int dx = x - cx;
            int dy = y - cy;

            int r = (dx*dx + dy*dy) >> 7;

            uint8_t c = r + frame;

            nv3007_pixel_raw(x, y, hsv_to_rgb565(c));
        }
    }
}

static void mascon_bar(int v, int pitch, int num, uint16_t color, const char *label)
{
    char buf[2] = { 0 };

    for (int i = 0; i < num; i++) {
        int y = v + pitch * i;
        nv3007_bar(10, y, 70, 3, color, 0xff);

        buf[0] = '1' + i;
        const char *text = label ? label : buf;
        gfx_text_draw(100, y - 6, text, &lv_dejavu16, color, ALIGN_LEFT);
        pitch += (pitch > 0) ? 1 : -1;
    }
}

static void densha_mascon()
{
    const int vcenter = 280;

    nv3007_clear(0, false);

    mascon_bar(vcenter + 20, 20, 5, nv3007_rgb565(0xc0c0c0), NULL);
    mascon_bar(vcenter - 20, -20, 8, nv3007_rgb565(0xc0c0c0), NULL);
    mascon_bar(vcenter - 242, -20, 1, nv3007_rgb565(0xff1010), "EB");

    nv3007_bar(2, vcenter, 92, 6, nv3007_rgb565(0x10ff10), 0xff);

    nv3007_bar(2, vcenter - 242, 10, 241, nv3007_rgb565(0xff1010), 0xff);
    nv3007_bar(2, vcenter + 7, 10, 113, nv3007_rgb565(0x1010ff), 0xff);

    gfx_text_draw(132, vcenter - 40, "B", &lv_lts20, nv3007_rgb565(0xff1010), ALIGN_CENTER);
    gfx_text_draw(132, vcenter - 7, "N", &lv_lts20, nv3007_rgb565(0x10ff10), ALIGN_CENTER);
    gfx_text_draw(132, vcenter + 28, "P", &lv_lts20, nv3007_rgb565(0x1010ff), ALIGN_CENTER);
};


static void draw_fps()
{
    int fps = cli_get_fps(1);
    if (fps > 0) {
        char buf[16];
        sprintf(buf, "%d", fps);
        gfx_text_draw(1, 1, buf, &lv_lts13, nv3007_rgb565(0xffffff), ALIGN_LEFT);
    }
}

static void draw_page()
{
    fflush(stdout);

    int w = nv3007_get_crop_width();
    int h = nv3007_get_crop_height();

    uint32_t effect = time_us_32() / 1000000 / 4 % 6;

    switch (effect) {
        case 0: sin_wave(w, h); break;
        case 1: fractal_zoom(w, h); break;
        case 2: tunnel_effect(w, h); break;
        case 3: plasma_zoom(w, h); break;
        case 4: fake_feedback_zoom(w, h); break;
        case 5: densha_mascon(); break;
    }

    draw_fps();
    frame++;
}

static void sync_brightness()
{
    gui_level(densha_cfg->lcd.backlight);
}

void gui_loop()
{
    sync_brightness();
    draw_page();
    nv3007_vsync();
    nv3007_flip();
}
