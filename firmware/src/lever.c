/*
 * Train Master Lever Abstraction
 */

#include "lever.h"

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "board_defs.h"
#include "button.h"
#include "mt6701.h"
#include "tmc2209.h"

#include "savedata.h"
#include "config.h"

static struct {
    bool mt6701_ready;
	bool tmc2209_ready;
	uint16_t raw_angle;
    uint16_t origin_angle;
    uint16_t mscnt;
    int16_t speed;
    bool following;
} ctx;

static void mt6701_port_init()
{
	i2c_init(SENSOR_I2C, 400 * 1000);
	gpio_set_function(SENSOR_SCL_PIN, GPIO_FUNC_I2C);
	gpio_set_function(SENSOR_SDA_PIN, GPIO_FUNC_I2C);
	gpio_pull_up(SENSOR_SCL_PIN);
	gpio_pull_up(SENSOR_SDA_PIN);
}

void lever_init()
{
    mt6701_port_init();
    mt6701_init(SENSOR_I2C);
    mt6701_init_sensor();
    ctx.mt6701_ready = mt6701_is_present();

	tmc2209_init(pio1, TMC2209_STEP_PIN, TMC2209_DIR_PIN, TMC2209_ENABLE_PIN);
	tmc2209_enable();
	tmc2209_uart_init(TMC2209_UART, TMC2209_TX_PIN, TMC2209_RX_PIN, 0);
	ctx.tmc2209_ready = tmc2209_present();
    tmc2209_set_microsteps(64);
    tmc2209_set_current(densha_cfg->mascon.run, densha_cfg->mascon.hold, 0);

	ctx.raw_angle = -1;
}

bool lever_mt6701_ready()
{
    return ctx.mt6701_ready;
}

bool lever_tmc2209_ready()
{
    return ctx.tmc2209_ready;
}

#define AVG_WINDOW 2
static uint16_t avg_buf[AVG_WINDOW];

#define LEVER_SUCTION_ANGLE 2500
#define LEVER_SUCTION_RANGE 100
#define LEVER_SUCTION_DEADBAND 8
#define LEVER_SUCTION_STEP_DIV 24
#define LEVER_SUCTION_MAX_STEP 5

static void sync_microstep()
{
    uint32_t mscnt = 0;
    if (tmc2209_get_mscnt(&mscnt)) {
        int new_speed = (mscnt - ctx.mscnt + 512) % 0x3ff - 512;
        ctx.mscnt = mscnt;
        ctx.speed = new_speed;
    }
}

static void read_angle()
{
    if (ctx.mt6701_ready) {
        int angle = 0;
        for (int i = 1; i < AVG_WINDOW; i++) {
            avg_buf[i] = avg_buf[i - 1];
            angle += avg_buf[i];
        }
        int new_reading = mt6701_read_angle();
        if (new_reading < 0) {
            return;
        }
        avg_buf[0] = new_reading;
        angle += new_reading;
        angle /= AVG_WINDOW;
        if (angle != ctx.raw_angle) {
            ctx.raw_angle = angle;
            if (densha_runtime.debug.hall) {
                printf("A:%5d:%6.2f\n", ctx.raw_angle, lever_get_angle_deg());
            }
        }
    }
}

static void do_follow()
{
    int target_mscnt = densha_runtime.msmap[ctx.raw_angle];
    target_mscnt = (target_mscnt - ctx.speed / 8) % 256;
    int current_mscnt = ctx.mscnt >> 2;
    int error = ((target_mscnt - current_mscnt + 128) & 0xff) - 128;

    if (error) {
        printf("Angle: %4d ", ctx.raw_angle);
        printf("F: TA:%4d MA:%4d", target_mscnt, current_mscnt);
        if ((abs(error) > 4) && (abs(error) < 32)) {
            printf(" SP:%3d E:%3d", ctx.speed, error);
            tmc2209_move(error, 1);
        }
        printf("\n");
    }
}

void lever_update()
{
    read_angle();
    if (0) sync_microstep();

    if (!ctx.tmc2209_ready) {
		return;
	}

	uint16_t button = button_read();
	if (button & 0x01) {
		tmc2209_move(1, 1);
	} else if (button & 0x02) {
		tmc2209_move(-1, 1);
	}

    if (ctx.following) {
        do_follow();
    }
}

uint16_t lever_get_angle()
{
	return ctx.raw_angle;
}

float lever_get_angle_deg()
{
    return (ctx.raw_angle * 360.0f) / mt6701_get_ppr();
}

void lever_follow()
{
    ctx.origin_angle = ctx.raw_angle;
    ctx.following = true;
}

static struct {
    uint8_t value;
    bool valid;
} ms_samples[4096];

static bool circular_avg(uint8_t *avg, int index, int depth)
{
    int count = 0;
    int ref = 0;
    int diff_sum = 0;

    if (ms_samples[index].valid) {
        ref = ms_samples[index].value;
        count = 1;
    }

    for (int k = 1; k <= depth; k++) {
        int left = (index - k + 4096) % 4096;
        int right = (index + k) % 4096;

        if (!(ms_samples[left].valid && ms_samples[right].valid)) {
            continue;
        }

        if (count == 0) {
            ref = ms_samples[left].value;  // establish ref from first valid pair
        }

        int left_diff = ((ms_samples[left].value - ref + 128) & 0xff) - 128;
        int right_diff = ((ms_samples[right].value - ref + 128) & 0xff) - 128;
        diff_sum += (left_diff + right_diff) / 2;
        count++;
    }

    if (count == 0) {
        return false;
    }

    diff_sum += diff_sum >= 0 ? (count / 2) : -(count / 2);
    int avg_diff = diff_sum / count;

    *avg = (ref + avg_diff) & 0xff;
    return true;
}

void lever_calibrate()
{
    static uint8_t ms_avg[4096];

    memset(ms_samples, 0, sizeof(ms_samples));
    memset(ms_avg, 0, sizeof(ms_avg));

    tmc2209_set_current(31, 15, 5);
    tmc2209_move(1, 1);
    sleep_ms(500);
    tmc2209_move(-1, 1);
    sleep_ms(500);

    for (int i = 0; i < 64 * 200; i++) {
        tmc2209_move(1, 1);
        sleep_ms(5);
        read_angle();
        uint32_t mscnt = 0;
        if (tmc2209_get_mscnt(&mscnt)) {
            ms_samples[ctx.raw_angle].value = mscnt >> 2;
            ms_samples[ctx.raw_angle].valid = true;
        }
    }

    bool all_valid = true;

    for (int i = 0; i < 4096; i++) {
        bool valid = circular_avg(&ms_avg[i], i, 7);
        if (!valid) {
            all_valid = false;
        }

        printf("ANGLE[%4d] = New:%3d Old:%3d\n", i, valid ? ms_avg[i] : -1, densha_runtime.msmap[i]);
    }

    if (!all_valid) {
        printf("Calibration failed: missing samples.\n");
        return;
    }

    bool incr = true;
    bool decr = true;

    for (int i = 1; i < 4096; i++) {
        int diff = ((ms_avg[i] - ms_avg[i - 1] + 128) & 0xff) - 128;
        if (diff < 0) {
            incr = false;
        }
        if (diff > 0) {
            decr = false;
        }
    }

    bool monotonic = incr || decr;
    bool monotonic_increasing = incr;

    if (monotonic) {
        printf("Calibration successful (%s).\n", monotonic_increasing ? "increasing" : "decreasing");
        memcpy(densha_runtime.msmap, ms_avg, sizeof(densha_runtime.msmap));
        savedata_write_global(0, densha_runtime.msmap, sizeof(densha_runtime.msmap));
    } else {
        printf("Calibration failed: map is not monotonic.\n");
    }
}