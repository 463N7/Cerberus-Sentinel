#include "battery.h"

#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"

#include <stddef.h>

#define MAX17048_I2C_ADDR 0x36U
#define MAX17048_REG_VCELL 0x02U
#define MAX17048_REG_SOC 0x04U
#define MAX17048_REG_VERSION 0x08U
#define BATTERY_READ_TIMEOUT_MS 10
#define BATTERY_POLL_MS 5000
#define BATTERY_RETRY_MS 30000
/* Application sanity limit, not a documented MAX17048 register maximum. */
#define BATTERY_SOC_SANITY_MAX_RAW (110U * 256U)

static i2c_master_dev_handle_t battery_device;
/* Device handle and probe state belong only to the UI task. */
static bool battery_probed;
static uint16_t battery_version;
static portMUX_TYPE battery_status_lock = portMUX_INITIALIZER_UNLOCKED;
static cerberus_battery_status_t battery_status = {
    .last_read_ms = -1,
    .last_attempt_ms = -1,
    .last_error = ESP_ERR_INVALID_STATE,
};

static void publish_status(const cerberus_battery_status_t *status)
{
    portENTER_CRITICAL(&battery_status_lock);
    battery_status = *status;
    portEXIT_CRITICAL(&battery_status_lock);
}

void cerberus_battery_get_status(cerberus_battery_status_t *out)
{
    if (out == NULL) {
        return;
    }
    portENTER_CRITICAL(&battery_status_lock);
    *out = battery_status;
    portEXIT_CRITICAL(&battery_status_lock);
}

void cerberus_battery_init(void)
{
    const cerberus_battery_status_t empty = {
        .last_read_ms = -1,
        .last_attempt_ms = -1,
        .last_error = ESP_ERR_INVALID_STATE,
    };
    /* Retain an existing bus handle if init is called again: no duplicate device. */
    battery_probed = false;
    battery_version = 0;
    publish_status(&empty);
}

static esp_err_t read_word(uint8_t reg, uint16_t *value)
{
    uint8_t data[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(battery_device, &reg, 1,
        data, sizeof(data), BATTERY_READ_TIMEOUT_MS);
    if (err == ESP_OK) {
        *value = ((uint16_t)data[0] << 8) | data[1];
    }
    return err;
}

static esp_err_t sample(cerberus_battery_status_t *next, int64_t now_ms)
{
    if (battery_device == NULL) {
        esp_err_t err = cerberus_i2c_bus_add_device(MAX17048_I2C_ADDR, &battery_device);
        if (err != ESP_OK) {
            battery_device = NULL;
            return err;
        }
    }
    if (!battery_probed) {
        uint16_t version = 0;
        esp_err_t err = read_word(MAX17048_REG_VERSION, &version);
        if (err != ESP_OK) {
            return err;
        }
        if (version == 0 || version == UINT16_MAX) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        battery_version = version;
        battery_probed = true;
    }

    uint16_t vcell = 0, soc = 0;
    esp_err_t err = read_word(MAX17048_REG_VCELL, &vcell);
    if (err != ESP_OK) {
        return err;
    }
    err = read_word(MAX17048_REG_SOC, &soc);
    if (err != ESP_OK) {
        return err;
    }

    /* MAX17048 datasheet: VCELL = raw * 78.125uV; SOC = raw / 256 percent.
     * The datasheet does not promise saturation at 100%, so allow a modest
     * model overrange and clamp only the displayed estimate. The 110% ceiling
     * is our plausibility policy; larger values (including 0xFFFF) stay unknown.
     * Accept the specified 2.5V..4.5V supply range. A plausible battery-node
     * voltage cannot establish cell presence when a charger drives that node.
     * https://www.analog.com/media/en/technical-documentation/data-sheets/MAX17048-MAX17049.pdf
     */
    if (vcell < 32000U || vcell > 57600U || soc > BATTERY_SOC_SANITY_MAX_RAW) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    next->valid = true;
    next->percent = soc >= 25600U ? 100U : (uint8_t)(soc / 256U);
    next->voltage_mv = (uint16_t)(((uint32_t)vcell * 625U) / 8000U);
    next->version = battery_version;
    next->last_read_ms = now_ms;
    return ESP_OK;
}

bool cerberus_battery_poll(int64_t now_ms)
{
    if (now_ms < 0) {
        return false;
    }
    cerberus_battery_status_t previous;
    cerberus_battery_get_status(&previous);
    const int64_t interval = previous.valid ? BATTERY_POLL_MS : BATTERY_RETRY_MS;
    if (previous.last_attempt_ms >= 0 && now_ms >= previous.last_attempt_ms &&
        now_ms - previous.last_attempt_ms < interval) {
        return false;
    }

    /* Publish only once, after both readings have passed. A failure replaces the
     * entire prior sample, so callers never see stale percentage/new voltage.
     */
    cerberus_battery_status_t next = {
        .last_read_ms = -1,
        .last_attempt_ms = now_ms,
    };
    next.last_error = sample(&next, now_ms);
    if (next.last_error != ESP_OK) {
        battery_probed = false;
        battery_version = 0;
    }
    publish_status(&next);
    return true;
}
