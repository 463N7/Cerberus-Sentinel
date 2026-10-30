#include "radio_bus.h"
#include "cerberus_pins.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

static SemaphoreHandle_t mutex;
static spi_device_handle_t onboard, expansion;
static const int selects[] = {14, 15, 41, 42, 45, 48};

esp_err_t radio_bus_init(void)
{
    if (mutex) return ESP_OK;
    SemaphoreHandle_t created = xSemaphoreCreateMutex();
    if (!created) return ESP_ERR_NO_MEM;
    uint64_t mask = 0;
    for (unsigned i = 0; i < 6; ++i) {
        mask |= 1ULL << selects[i];
        gpio_set_level(selects[i], 1);
    }
    gpio_config_t cs = {.pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT};
    esp_err_t err = gpio_config(&cs);
    if (err != ESP_OK) { vSemaphoreDelete(created); return err; }
    for (unsigned i = 0; i < 6; ++i) gpio_set_level(selects[i], 1);
    spi_bus_config_t config = {
        .mosi_io_num = CERBERUS_ONBOARD_C5_SPI_MOSI_GPIO,
        .miso_io_num = CERBERUS_ONBOARD_C5_SPI_MISO_GPIO,
        .sclk_io_num = CERBERUS_ONBOARD_C5_SPI_SCLK_GPIO,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 64,
    };
    err = spi_bus_initialize(SPI3_HOST, &config, SPI_DMA_DISABLED);
    if (err != ESP_OK) { vSemaphoreDelete(created); return err; }
    spi_device_interface_config_t device = {
        .clock_speed_hz = 40000000, .mode = 0,
        .spics_io_num = CERBERUS_ONBOARD_C5_SPI_CS_GPIO, .queue_size = 1,
    };
    err = spi_bus_add_device(SPI3_HOST, &device, &onboard);
    if (err == ESP_OK) {
        device.clock_speed_hz = 250000;
        device.spics_io_num = -1;
        err = spi_bus_add_device(SPI3_HOST, &device, &expansion);
    }
    if (err != ESP_OK) {
        if (onboard) spi_bus_remove_device(onboard);
        onboard = NULL;
        spi_bus_free(SPI3_HOST);
        vSemaphoreDelete(created);
        return err;
    }
    mutex = created;
    return ESP_OK;
}

SemaphoreHandle_t radio_bus_mutex(void) { return mutex; }
spi_device_handle_t radio_bus_onboard(void) { return onboard; }

esp_err_t radio_bus_expansion_transfer(unsigned slot, const void *tx, void *rx, size_t size)
{
    if (!expansion) return ESP_ERR_INVALID_STATE;
    if (slot >= 6 || !tx || !rx || size != 64) return ESP_ERR_INVALID_ARG;
    /* Caller owns radio_bus_mutex for the entire request/response exchange. */
    spi_transaction_t transaction = {.length = size * 8, .tx_buffer = tx, .rx_buffer = rx};
    gpio_set_level(selects[slot], 0);
    esp_rom_delay_us(2);
    esp_err_t err = spi_device_transmit(expansion, &transaction);
    esp_rom_delay_us(2);
    gpio_set_level(selects[slot], 1);
    return err;
}
