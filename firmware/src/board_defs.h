/*
 * Densha pico Board Definitions
 * WHowe <github.com/whowechina>
 */

#if defined BOARD_DENSHA_PICO

#define RGB_PIN 16

#define RGB_ORDER GRB // or RGB

#define BUTTON_GPIO { 28, 15 }
#define BUTTON_PULL_UPDOWN {  }

#define TMC2209_ENABLE_PIN 6
#define TMC2209_STEP_PIN 7
#define TMC2209_DIR_PIN 10

#define TMC2209_UART uart1
#define TMC2209_TX_PIN 8
#define TMC2209_RX_PIN 9

#define SENSOR_I2C i2c0
#define SENSOR_SCL_PIN 13
#define SENSOR_SDA_PIN 12

#define NV3007_SPI spi0
#define NV3007_SCK_PIN 2
#define NV3007_TX_PIN 3
#define NV3007_CSN_PIN 5
#define NV3007_DC_PIN 4
#define NV3007_RST_PIN 1
#define NV3007_LEDK_PIN 0

#endif
