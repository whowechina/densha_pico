/*
 * TMC2209 Motor Controller
 * WHowe <github.com/whowechina>
 */

#ifndef TMC2209_H
#define TMC2209_H

#include <stdint.h>
#include <stdbool.h>

#include "hardware/pio.h"
#include "hardware/uart.h"

/* Pass TMC2209_NO_PIN as enable_pin if your driver board has no ENABLE line. */
#define TMC2209_NO_PIN (-1)

/* Initialize the stepper motor.
 *   step_pin   : GPIO for STEP signal (active-high pulse)
 *   dir_pin    : GPIO for DIR signal
 *   enable_pin : GPIO for /ENABLE signal (active-low); pass TMC2209_NO_PIN
 *                if the pin is not connected or always tied low on the board.
 *                The pin is driven LOW (enabled) automatically after init.
 */
void tmc2209_init(PIO pio, uint step_pin, uint dir_pin, uint enable_pin);

/* Enable the driver (drive /ENABLE pin LOW). No-op if no pin was configured. */
void tmc2209_enable();

/* Disable the driver (drive /ENABLE pin HIGH). No-op if no pin was configured.
 * Disabling de-energizes the coils; the motor shaft becomes free to rotate. */
void tmc2209_disable();

/* Queue a move and block until there is FIFO space.
 *   steps         : number of steps; positive = forward, negative = reverse
 *                   valid range per command: [-32768, -1] or [1, 32768]
 *   step_period_us : period per step in microseconds.
 *                    e.g. 1 -> 1us, 100 -> 100us, 1000 -> 1ms.
 * The PIO handles all timing; no delay is needed in user code.
 */
void tmc2209_move(int steps, uint16_t step_period_us);

/* Initialize TMC2209 UART control channel.
 *   uart           : UART instance (uart0 or uart1)
 *   tx_pin, rx_pin : MCU UART pins connected to TMC2209 UART
 *   addr           : TMC2209 address (0..3), typically 0
 */
void tmc2209_uart_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint8_t addr);

/* Set TMC2209 motor current register values.
 *   irun       : run current (0..31)
 *   ihold      : hold current (0..31)
 *   iholddelay : hold delay (0..15)
 * Returns false if UART is not initialized.
 */
bool tmc2209_set_current(uint8_t irun, uint8_t ihold,
						 uint8_t iholddelay);

/* Read TMC2209 successful-write counter IFCNT (0..255). */
bool tmc2209_get_ifcnt(uint32_t *ifcnt);

/* Read TMC2209 microstep counter MSCNT (0..1023). */
bool tmc2209_get_mscnt(uint32_t *mscnt);

/* Raw register access helpers for diagnostics.
 * reg: 0x00..0x7F, value: 32-bit register payload.
 */
bool tmc2209_read_reg(uint8_t reg, uint32_t *value);
bool tmc2209_write_reg(uint8_t reg, uint32_t value);

/* Set microstep resolution. */
bool tmc2209_set_microsteps(uint16_t steps);

/* Check whether TMC2209 is present/responding on UART. */
bool tmc2209_present();

/* Returns true if the motor is still executing queued moves.
 * Based on a software wall-clock estimate; accurate as long as the step
 * rate is not changed while moves are in-flight.
 */
bool tmc2209_is_busy();

/* Immediately stop the motor: flushes the FIFO and resets the state machine.
 * The STEP pin is driven low and the motor is ready to accept new moves.
 */
void tmc2209_stop();

#endif
