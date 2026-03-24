
/*
 * GUI on ST7789
 * WHowe <github.com/whowechina>
 * 
 */

#include "board_defs.h"
#include "res/resource.h"
#include "gfx.h"
#include "gui.h"

#include "light.h"
#include "nv3007.h"
#include "config.h"

void gui_init()
{
    nv3007_init_spi(NV3007_SPI, NV3007_SCK_PIN, NV3007_TX_PIN, NV3007_CSN_PIN);
    nv3007_init(NV3007_SPI, NV3007_DC_PIN, NV3007_RST_PIN, NV3007_LEDK_PIN);
}

void gui_level(uint8_t level)
{
    nv3007_dimmer(level);
}

static void run_background()
{
    /*
    for (int i = 0; i < nv3007_get_crop_width(); i++) {
        for (int j = 0; j < nv3007_get_crop_height(); j++) {
            uint32_t color = rgb32_from_hsv(i + j, 240, 255);
            nv3007_pixel_raw(i, j, nv3007_rgb565(color));
        }
    }
    */
   nv3007_clear(0xf800, true);
}

void gui_loop()
{
    run_background();

    gui_level(densha_cfg->lcd.backlight);
    nv3007_flush(true);
}

