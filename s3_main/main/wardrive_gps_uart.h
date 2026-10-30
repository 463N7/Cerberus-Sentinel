#pragma once

#include "esp_err.h"
#include "wardrive_gps_core.h"

/* 9600 baud produces at most 960 bytes/s with 8N1 framing. The reader polls
 * every 100ms; this ring retains over one second of input without a 4KiB pool. */
#define CERBERUS_WARDRIVE_GPS_RX_BUFFER_BYTES 1024U
#define CERBERUS_WARDRIVE_GPS_TASK_STACK_BYTES 3584U

/* Optional receiver on UART2 RX GPIO1, 9600 8N1. The reader and UART driver
 * exist only during an integrated Wardriving session. No PPS pin is claimed. */
esp_err_t cerberus_wardrive_gps_uart_start(void);
esp_err_t cerberus_wardrive_gps_uart_stop(void);
void cerberus_wardrive_gps_uart_snapshot(
    cerberus_wardrive_gps_snapshot_t *out, esp_err_t *uart_error);
