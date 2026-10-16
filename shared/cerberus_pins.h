#pragma once

#define CERBERUS_I2C_SDA_GPIO 8
#define CERBERUS_I2C_SCL_GPIO 9

#define CERBERUS_LCD_CS_GPIO 16
#define CERBERUS_LCD_DC_GPIO 4
#define CERBERUS_LCD_RST_GPIO 5
#define CERBERUS_LCD_SCLK_GPIO 17
#define CERBERUS_LCD_MOSI_GPIO 18
#define CERBERUS_LCD_BL_GPIO 6

#define CERBERUS_TOUCH_INT_GPIO 7
/* Touch reset is not connected on the current board revision. */
#define CERBERUS_TOUCH_RST_GPIO -1
#define CERBERUS_TOUCH_I2C_ADDR 0x5D

#define CERBERUS_SD_D0_GPIO 35
#define CERBERUS_SD_CLK_GPIO 36
#define CERBERUS_SD_CMD_GPIO 37
/* Production cards use their board pull-up network. Only the prototype
   connects DAT3 to GPIO38; the production firmware leaves that GPIO alone. */
#ifdef CERBERUS_PRODUCTION_BOARD
#define CERBERUS_SD_DAT3_TEST_GPIO -1
#else
#define CERBERUS_SD_DAT3_TEST_GPIO 38
#endif

#define CERBERUS_C5_UART_TX_GPIO 43
#define CERBERUS_C5_UART_RX_GPIO 44
#define CERBERUS_C5_UART_BAUD 115200

/* During normal operation, the C5-to-S3 UART wire is a SPI READY handshake.
   The flash bridge restores GPIO44 to UART RX before entering the C5 ROM. */
#define CERBERUS_ONBOARD_C5_READY_GPIO CERBERUS_C5_UART_RX_GPIO
#define CERBERUS_C5_READY_GPIO 11

#define CERBERUS_C5_EN_GPIO 21
#define CERBERUS_C5_BOOT_GPIO 40

/* Onboard S3-to-C5 SPI3 hardware bus. */
#define CERBERUS_ONBOARD_C5_SPI_CS_GPIO 10
#define CERBERUS_ONBOARD_C5_SPI_MOSI_GPIO 11
#define CERBERUS_ONBOARD_C5_SPI_SCLK_GPIO 12
#define CERBERUS_ONBOARD_C5_SPI_MISO_GPIO 13
/* Onboard C5 side of the link (hardware SPI slave). */
#ifndef CERBERUS_C5_SLAVE_SPI_SCLK_GPIO
#define CERBERUS_C5_SLAVE_SPI_SCLK_GPIO 4
#endif
#ifndef CERBERUS_C5_SLAVE_SPI_MOSI_GPIO
#define CERBERUS_C5_SLAVE_SPI_MOSI_GPIO 5
#endif
#ifndef CERBERUS_C5_SLAVE_SPI_MISO_GPIO
#define CERBERUS_C5_SLAVE_SPI_MISO_GPIO 6
#endif
#ifndef CERBERUS_C5_SLAVE_SPI_CS_GPIO
#define CERBERUS_C5_SLAVE_SPI_CS_GPIO 8
#endif
