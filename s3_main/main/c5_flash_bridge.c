#include "c5_flash_bridge.h"

#ifdef CERBERUS_C5_FLASH_BRIDGE

#include "c5_control.h"
#include "cerberus_pins.h"
#include "recovery.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#include <stdint.h>
#include <stdio.h>

#define C5_BRIDGE_UART_NUM UART_NUM_1
#define C5_BRIDGE_UART_BUFFER_SIZE 4096
#define C5_BRIDGE_CHUNK_SIZE 32

#ifndef CERBERUS_C5_BRIDGE_BAUD
#define CERBERUS_C5_BRIDGE_BAUD CERBERUS_C5_UART_BAUD
#endif

static const char *TAG = "c5_flash_bridge";
static volatile bool s3_download_requested;
static volatile bool c5_download_requested;
static volatile bool bridge_data_enabled = true;
static volatile bool bridge_identity_requested;
static volatile bool forwarding_paused;
static char usb_serial[18];
static const char *usb_strings[] = {
    (const char[]){0x09, 0x04},
    "Cerberus",
    "Cerberus C5 Flash Bridge",
    usb_serial,
    "Cerberus C5 Bridge",
};

static esp_err_t bridge_enter_s3_download_mode(bool forwarding_task_started)
{
    /* esp_restart() preserves USB routing. Merely forcing ROM download while
     * the internal PHY still belongs to OTG leaves the native USB/JTAG loader
     * unreachable. Stop bridge users before releasing TinyUSB, then return the
     * internal PHY to USB Serial/JTAG. Physical BOOT + RESET remains the
     * recovery path if a host does not enumerate the new USB device. */
    s3_download_requested = true;
    vTaskDelay(pdMS_TO_TICKS(250));
    if (forwarding_task_started &&
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0U) {
        return ESP_ERR_TIMEOUT;
    }
    c5_control_hold_reset();
    (void)tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0), TAG, "CDC deinit failed");
    ESP_RETURN_ON_ERROR(tinyusb_driver_uninstall(), TAG, "TinyUSB uninstall failed");
    usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_SERIAL_JTAG,
        .target = USB_PHY_TARGET_INT,
    };
    usb_phy_handle_t native_usb_phy;
    ESP_RETURN_ON_ERROR(usb_new_phy(&phy_config, &native_usb_phy), TAG, "Native USB PHY restore failed");
    return cerberus_recovery_enter_download_mode();
}

static void cdc_line_coding_callback(int interface, cdcacm_event_t *event)
{
    (void)interface;
    /* 1200 baud is an explicit host request to leave the temporary bridge.
     * 2400 baud resets C5 into ROM for a retry without reinstalling the bridge.
     * 9600 baud pauses forwarding and returns the S3 identity over USB only.
     * C5 UART stays at its fixed flashing baud. Restart from the main task,
     * after the USB callback has returned, never from the TinyUSB task. */
    if (event == NULL || event->line_coding_changed_data.p_line_coding == NULL) return;
    uint32_t baud = event->line_coding_changed_data.p_line_coding->bit_rate;
    if (baud == 1200U &&
        cerberus_recovery_download_mode_available()) {
        s3_download_requested = true;
    }
#ifndef CERBERUS_C5_LOG_BRIDGE
    if (baud == 2400U && !s3_download_requested) {
        bridge_data_enabled = false;
        bridge_identity_requested = false;
        c5_download_requested = true;
    } else if (baud == 9600U && !s3_download_requested) {
        bridge_data_enabled = false;
        forwarding_paused = false;
        bridge_identity_requested = true;
    } else if (baud == CERBERUS_C5_BRIDGE_BAUD && !s3_download_requested) {
        bridge_identity_requested = false;
        bridge_data_enabled = true;
    }
#endif
}

static void cdc_rx_callback(int interface, cdcacm_event_t *event)
{
    (void)interface;
    (void)event;
    uint8_t buffer[C5_BRIDGE_CHUNK_SIZE];

    if (s3_download_requested) return;

    while (true) {
        size_t received = 0;
        esp_err_t err = tinyusb_cdcacm_read(
                TINYUSB_CDC_ACM_0,
                buffer,
                sizeof(buffer),
                &received);
        if (err != ESP_OK || received == 0U) {
            break;
        }
        /* Discard host bytes in control mode; they must never be replayed
         * into C5 when the host returns to the data baud. */
        if (bridge_data_enabled && !c5_download_requested) {
            (void)uart_write_bytes(C5_BRIDGE_UART_NUM, buffer, received);
        }
    }
}

static void c5_to_cdc_task(void *arg)
{
    TaskHandle_t main_task = (TaskHandle_t)arg;
    uint8_t buffer[C5_BRIDGE_CHUNK_SIZE];

    while (!s3_download_requested) {
        if (!bridge_data_enabled || c5_download_requested) {
            forwarding_paused = true;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        forwarding_paused = false;
        int read = uart_read_bytes(C5_BRIDGE_UART_NUM, buffer, sizeof(buffer), pdMS_TO_TICKS(20));
        if (read > 0) {
            size_t offset = 0;
            while (offset < (size_t)read && !s3_download_requested &&
                    !c5_download_requested && bridge_data_enabled) {
                size_t written = tinyusb_cdcacm_write_queue(
                    TINYUSB_CDC_ACM_0,
                    buffer + offset,
                    (size_t)read - offset);
                offset += written;
                (void)tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
                if (written == 0U) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
            }
        }
    }
    xTaskNotifyGive(main_task);
    vTaskDelete(NULL);
}

esp_err_t c5_flash_bridge_run(void)
{
    esp_log_level_set("*", ESP_LOG_NONE);

    ESP_RETURN_ON_ERROR(c5_control_init(), TAG, "C5 control init failed");

    uart_config_t uart_config = {
        .baud_rate = CERBERUS_C5_BRIDGE_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_RETURN_ON_ERROR(uart_driver_install(C5_BRIDGE_UART_NUM, C5_BRIDGE_UART_BUFFER_SIZE, C5_BRIDGE_UART_BUFFER_SIZE, 0, NULL, 0), TAG, "UART install failed");
    ESP_RETURN_ON_ERROR(uart_param_config(C5_BRIDGE_UART_NUM, &uart_config), TAG, "UART config failed");
    ESP_RETURN_ON_ERROR(uart_set_pin(C5_BRIDGE_UART_NUM, CERBERUS_C5_UART_TX_GPIO, CERBERUS_C5_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "UART pin config failed");

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_efuse_mac_get_default(mac), TAG, "S3 identity read failed");
    (void)snprintf(usb_serial, sizeof(usb_serial), "%02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    usb_config.descriptor.string = usb_strings;
    usb_config.descriptor.string_count = sizeof(usb_strings) / sizeof(usb_strings[0]);
    ESP_RETURN_ON_ERROR(
        tinyusb_driver_install(&usb_config),
        TAG,
        "TinyUSB driver install failed");
    tinyusb_config_cdcacm_t cdc_config = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = cdc_line_coding_callback,
    };
    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_init(&cdc_config),
        TAG,
        "TinyUSB CDC install failed");

#ifdef CERBERUS_C5_LOG_BRIDGE
    /* Keep the C5 in reset until the host-side recorder is listening.  Without
       this handshake the C5 can panic before USB Serial/JTAG reconnects after
       uploading this bridge, which loses the only useful diagnostic text. */
    uint8_t trigger = 0;
    while (trigger != 'L') {
        if (s3_download_requested) {
            (void)bridge_enter_s3_download_mode(false);
            (void)cerberus_recovery_enter_download_mode();
        }
        size_t received = 0;
        if (tinyusb_cdcacm_read(
                TINYUSB_CDC_ACM_0,
                &trigger,
                1,
                &received) != ESP_OK || received == 0U) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    c5_control_reset_normal();
#else
    c5_control_enter_bootloader();
#endif

    BaseType_t c5_task = xTaskCreate(c5_to_cdc_task, "c5_to_cdc", 4096, xTaskGetCurrentTaskHandle(), 10, NULL);
    if (c5_task != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    while (true) {
        if (s3_download_requested) {
            (void)bridge_enter_s3_download_mode(true);
            /* If teardown failed, still honor ROM recovery. A physical
             * BOOT + RESET can restore the native USB port if required. */
            (void)cerberus_recovery_enter_download_mode();
        }
#ifndef CERBERUS_C5_LOG_BRIDGE
        if (bridge_identity_requested) {
            /* Wait for the UART forwarding task to leave its USB writer.
             * This response identifies the carrier S3, not the C5 target. */
            TickType_t started = xTaskGetTickCount();
            while (!forwarding_paused && !s3_download_requested &&
                    bridge_identity_requested &&
                    (xTaskGetTickCount() - started) < pdMS_TO_TICKS(500)) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (forwarding_paused && !s3_download_requested &&
                    bridge_identity_requested && !bridge_data_enabled) {
                char identity[64];
                int length = snprintf(identity, sizeof(identity),
                        "\r\nCERBERUS_BRIDGE_V1 s3_mac=%s\r\n", usb_serial);
                /* Discard queued C5 output. A preceding CRLF also separates
                 * the identity from any packet already in flight to USB. */
                (void)tud_cdc_n_write_clear(TINYUSB_CDC_ACM_0);
                (void)tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0,
                        (const uint8_t *)identity, (size_t)length);
                (void)tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(200));
            }
            bridge_identity_requested = false;
        }
        if (c5_download_requested) {
            /* The host closes 2400 baud, waits at least 1000 ms, then opens
             * 115200. No UART payload or DTR/RTS state is a reset command. */
            uint8_t discarded[C5_BRIDGE_CHUNK_SIZE];
            size_t received;
            do {
                received = 0;
                (void)tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, discarded,
                        sizeof(discarded), &received);
            } while (received != 0U);
            (void)uart_wait_tx_done(C5_BRIDGE_UART_NUM, pdMS_TO_TICKS(500));
            (void)uart_flush_input(C5_BRIDGE_UART_NUM);
            c5_control_enter_bootloader();
            c5_download_requested = false;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

#endif /* CERBERUS_C5_FLASH_BRIDGE */
