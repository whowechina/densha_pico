/*
 * TMC2209 Motor Controller
 * WHowe <github.com/whowechina>
 */

#include "tmc2209.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "config.h"

#include "tmc2209.pio.h"

#define TMC2209_SM_CLKDIV 16
#define TMC2209_MAX_STEPS_PER_MOVE 32768

#define TMC2209_REG_GCONF 0x00
#define TMC2209_REG_GSTAT 0x01
#define TMC2209_REG_IFCNT 0x02
#define TMC2209_REG_MSCNT 0x6A
#define TMC2209_REG_TPOWERDOWN 0x11
#define TMC2209_REG_CHOPCONF 0x6C
#define TMC2209_REG_PWMCONF 0x70
#define TMC2209_REG_IHOLD_IRUN 0x10
#define TMC2209_REG_IOIN 0x06

#define TMC2209_GCONF_PDN_DISABLE (1u << 6)
#define TMC2209_GCONF_MSTEP_REG_SELECT (1u << 7)
#define TMC2209_GCONF_MULTISTEP_FILT (1u << 8)

#define TMC2209_PWMCONF_AUTOSCALE (1u << 18)

/* Compute half-period delay N (16 bits) from step period in microseconds.
 * With fixed SM divider (TMC2209_SM_CLKDIV):
 *   step period (seconds) = TMC2209_SM_CLKDIV * (2*N + 5) / clk_sys
 *   N = (step_period_s * clk_sys / TMC2209_SM_CLKDIV - 5) / 2
 */
static uint16_t us_to_delay(uint16_t step_period_us)
{
    if (step_period_us == 0) {
        step_period_us = 1;
    }

    uint32_t sm_clk = clock_get_hz(clk_sys) / TMC2209_SM_CLKDIV;
    uint64_t cycles = ((uint64_t)sm_clk * step_period_us) / 1000000u;
    if (cycles <= 5u) {
        return 0;
    }

    uint64_t n = (cycles - 5u) / 2u;
    if (n > 0xFFFFu) {
        n = 0xFFFFu;
    }
    return (uint16_t)n;
}

static struct {
    PIO pio;
    uint sm;
    uint offset;
    uint64_t busy_until_us;
    uint enable_pin;
} s;

static struct {
    bool ready;
    uart_inst_t *uart;
    uint8_t addr;
} tmc2209;

static uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t current = data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (((crc >> 7) ^ (current & 0x01)) != 0) {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            } else {
                crc <<= 1;
            }
            current >>= 1;
        }
    }
    return crc;
}

static void flush_uart()
{
    while (uart_is_readable(tmc2209.uart)) {
        uart_getc(tmc2209.uart);
    }
}

static int read_uart(uart_inst_t *uart, uint8_t *buf, int size, uint32_t wait_us)
{
    int got = 0;
    uint64_t deadline = time_us_64() + wait_us;

    while ((got < size) && (time_us_64() < deadline)) {
        if (uart_is_readable(uart)) {
            uint8_t c = uart_getc(uart);
            if (buf) {
                buf[got] = c;
            }
            got++;
        }
    }

    return got;
}

static void discard_uart_loopback(int bytes)
{
    int got = read_uart(tmc2209.uart, NULL, bytes, bytes * 100 + 1000);
    if (densha_runtime.debug.uart) {
        printf("[UART] Discarded loopback: %d bytes\n", got);
    }
}

static void dump_uart_frame(const char *prefix, const uint8_t *data, int len)
{
    if (!densha_runtime.debug.uart) {
        return;
    }

    printf("[UART] %s", prefix);
    for (int i = 0; i < len; i++) {
        printf(" %02X", data[i]);
    }
    printf("\n");
}

bool write_reg(uint8_t reg, uint32_t value)
{
    uint32_t ifcnt_old = 0;
    if (!tmc2209_get_ifcnt(&ifcnt_old)) {
        return false;
    }
    sleep_us(100);

    uint8_t frame[8] = {
        0x05, tmc2209.addr, reg | 0x80,
        value >> 24, value >> 16, value >> 8, value,
    };
    frame[7] = crc8(frame, 7);
    dump_uart_frame("TX(Reg Write):", frame, sizeof(frame));
    uart_write_blocking(tmc2209.uart, frame, sizeof(frame));
    discard_uart_loopback(sizeof(frame));

    sleep_us(50);

    uint32_t ifcnt_new = 0;
    if (!tmc2209_get_ifcnt(&ifcnt_new)) {
        return false;
    }

    return ifcnt_new == ((ifcnt_old + 1) & 0xff);
}

static bool read_reg(uint8_t reg, uint32_t *value)
{
    if (!tmc2209.ready || (value == NULL)) {
        return false;
    }

    flush_uart();

    uint8_t req[4] = {
        0x05, tmc2209.addr, reg & 0x7F,
    };
    req[3] = crc8(req, 3);
    dump_uart_frame("TX(Reg Read):", req, sizeof(req));
    uart_write_blocking(tmc2209.uart, req, sizeof(req));
    discard_uart_loopback(sizeof(req));

    uint8_t buf[8];
    int got = read_uart(tmc2209.uart, buf, sizeof(buf), 8000);
    dump_uart_frame("RX:", buf, got);

    if ((got != 8) || (buf[0] != 0x05) || (buf[1] != 0xFF) ||
        ((buf[2] & 0x80) != 0) || ((buf[2] & 0x7F) != (reg & 0x7F)) ||
        (crc8(buf, 7) != buf[7])) {
        return false;
    }

    *value = ((uint32_t)buf[3] << 24) | (buf[4] << 16) | (buf[5] << 8) | buf[6];
    return true;
}

bool tmc2209_read_reg(uint8_t reg, uint32_t *value)
{
    return read_reg((uint8_t)(reg & 0x7Fu), value);
}

bool tmc2209_write_reg(uint8_t reg, uint32_t value)
{
    if (!tmc2209.ready) {
        return false;
    }
    return write_reg((uint8_t)(reg & 0x7Fu), value);
}

bool tmc2209_get_ifcnt(uint32_t *ifcnt)
{
    return read_reg(TMC2209_REG_IFCNT, ifcnt);
}

bool tmc2209_get_mscnt(uint32_t *mscnt)
{
    return read_reg(TMC2209_REG_MSCNT, mscnt);
}

void tmc2209_init(PIO pio, uint step_pin, uint dir_pin, uint enable_pin)
{
    s.pio = pio;
    s.busy_until_us = 0;
    s.enable_pin = enable_pin;
    gpio_init(s.enable_pin);
    gpio_set_dir(s.enable_pin, GPIO_OUT);
    gpio_put(s.enable_pin, 0); /* active-low: drive LOW to enable */

    s.sm = pio_claim_unused_sm(pio, true);
    s.offset = pio_add_program(pio, &tmc2209_program);

    tmc2209_program_init(pio, s.sm, s.offset, step_pin, dir_pin);
    pio_sm_set_clkdiv_int_frac(pio, s.sm, TMC2209_SM_CLKDIV, 0);
}

void tmc2209_enable()
{
    gpio_put(s.enable_pin, 0); /* active-low */
}

void tmc2209_disable()
{
    gpio_put(s.enable_pin, 1);
}

/* Push one packed move to TX FIFO:
 *   bit 31    : direction
 *   bits 30:16: step_count - 1 (15-bit)
 *   bits 15:0 : delay N
 */
static void push_move(int steps, uint16_t step_period_us, bool blocking)
{
    bool reverse = steps < 0;
    uint32_t count = reverse ? (uint32_t)-steps : (uint32_t)steps;
    if (count == 0) {
        return;
    }
    if (count > TMC2209_MAX_STEPS_PER_MOVE) {
        count = TMC2209_MAX_STEPS_PER_MOVE;
    }

    uint16_t delay = us_to_delay(step_period_us);
    uint32_t move_word = ((reverse ? 1u : 0u) << 31) |
                         (((count - 1u) & 0x7FFFu) << 16) |
                         delay;

    if (blocking) {
        pio_sm_put_blocking(s.pio, s.sm, move_word);
    } else {
        pio_sm_put(s.pio, s.sm, move_word);
    }

    /* Advance the software timestamp estimating when this move finishes */
    uint64_t move_us = (uint64_t)count * step_period_us;
    uint64_t now = time_us_64();
    if (s.busy_until_us < now) {
        s.busy_until_us = now + move_us;
    } else {
        s.busy_until_us += move_us;
    }
}

void tmc2209_move(int steps, uint16_t step_period_us)
{
    if (steps == 0) return;
    push_move(steps, step_period_us, true);
}

bool tmc2209_set_microsteps(uint16_t steps)
{
    if (!tmc2209.ready) {
        return false;
    }

    int mres = 8 - __builtin_ctz(steps);
    if (steps != (1 << (8 - mres))) {
        return false;
    }

    uint32_t chopconf = 0;
    if (!read_reg(TMC2209_REG_CHOPCONF, &chopconf)) {
        return false;
    }

    chopconf = (chopconf & ~(0x0f << 24)) | (mres << 24) | (1 << 17);
    return write_reg(TMC2209_REG_CHOPCONF, chopconf);
}

static void init_registers()
{
    // microstep by uart, not by pins
    write_reg(TMC2209_REG_GCONF,
              TMC2209_GCONF_PDN_DISABLE |
              TMC2209_GCONF_MSTEP_REG_SELECT |
              TMC2209_GCONF_MULTISTEP_FILT);

    // clear reset flag
    sleep_us(100);
    write_reg(TMC2209_REG_GSTAT, 0x01);
}

void tmc2209_uart_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint8_t addr)
{
    uart_init(uart, 115200);
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    uart_set_hw_flow(uart, false, false);
    uart_set_format(uart, 8, 1, UART_PARITY_NONE);

    tmc2209.addr = addr & 0x03;
    tmc2209.uart = uart;
    tmc2209.ready = true;

    init_registers();
}

bool tmc2209_set_current(uint8_t irun, uint8_t ihold, uint8_t iholddelay)
{
    if (!tmc2209.ready) {
        return false;
    }

    if (irun > 31) {
        irun = 31;
    }
    if (ihold > 31) {
        ihold = 31;
    }
    if (iholddelay > 15) {
        iholddelay = 15;
    }

    uint32_t ihold_irun = (iholddelay << 16) | (irun << 8) | ihold;

    return write_reg(TMC2209_REG_IHOLD_IRUN, ihold_irun);
}

bool tmc2209_present()
{
    if (!tmc2209.ready) {
        return false;
    }

    for (int retry = 0; retry < 3; retry++) {
        uint32_t reg;
        if (read_reg(TMC2209_REG_IOIN, &reg)) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

bool tmc2209_is_busy()
{
    return time_us_64() < s.busy_until_us;
}

void tmc2209_stop()
{
    pio_sm_set_enabled(s.pio, s.sm, false);
    pio_sm_clear_fifos(s.pio, s.sm);
    pio_sm_restart(s.pio, s.sm);
    /* Jump back to the start of program so the SM waits for the next move */
    pio_sm_exec(s.pio, s.sm, pio_encode_jmp(s.offset));
    pio_sm_set_enabled(s.pio, s.sm, true);
    s.busy_until_us = 0;
}
