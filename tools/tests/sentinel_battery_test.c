#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "battery.h"
#include "i2c_bus.h"

static unsigned add_calls, read_calls, lock_depth, locks;
static esp_err_t add_error;
static uint8_t fail_register;
static uint16_t version = 0x0011, voltage = 51200, soc = 51 * 256 + 64;
static uint8_t read_registers[8];

void battery_test_enter(void *lock)
{ assert(lock && lock_depth == 0); ++lock_depth; ++locks; }
void battery_test_exit(void *lock)
{ assert(lock && lock_depth == 1); --lock_depth; }

esp_err_t cerberus_i2c_bus_add_device(uint8_t address, i2c_master_dev_handle_t *out)
{
    assert(lock_depth == 0 && address == 0x36 && out);
    ++add_calls;
    if (add_error) return add_error;
    *out = (void *)(uintptr_t)1;
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
    const uint8_t *write_data, size_t write_size, uint8_t *read_data,
    size_t read_size, int timeout_ms)
{
    assert(lock_depth == 0 && device == (void *)(uintptr_t)1);
    assert(write_data && write_size == 1 && read_data && read_size == 2);
    assert(timeout_ms == 10 && read_calls < sizeof(read_registers));
    uint8_t reg = write_data[0];
    read_registers[read_calls++] = reg;
    /* Simulate torn bytes from a failed transaction; they must never publish. */
    if (reg == fail_register) {
        read_data[0] = 0xff;
        read_data[1] = 0x12;
        return ESP_ERR_TIMEOUT;
    }
    uint16_t word;
    if (reg == 0x08) word = version;
    else if (reg == 0x02) word = voltage;
    else { assert(reg == 0x04); word = soc; }
    read_data[0] = (uint8_t)(word >> 8);
    read_data[1] = (uint8_t)word;
    return ESP_OK;
}

static cerberus_battery_status_t status(void)
{
    cerberus_battery_status_t out;
    memset(&out, 0xaa, sizeof(out));
    unsigned reads_before = read_calls, adds_before = add_calls, locks_before = locks;
    cerberus_battery_get_status(&out);
    assert(locks == locks_before + 1 && lock_depth == 0);
    assert(read_calls == reads_before && add_calls == adds_before);
    return out;
}

static void unknown(esp_err_t error, int64_t attempted)
{
    cerberus_battery_status_t out = status();
    assert(!out.valid && out.percent == 0 && out.voltage_mv == 0 && out.version == 0);
    assert(out.last_read_ms == -1 && out.last_attempt_ms == attempted && out.last_error == error);
}

static void valid(uint8_t percent, uint16_t mv, int64_t timestamp)
{
    cerberus_battery_status_t out = status();
    assert(out.valid && out.percent == percent && out.voltage_mv == mv);
    assert(out.version == version && out.last_read_ms == timestamp);
    assert(out.last_attempt_ms == timestamp && out.last_error == ESP_OK);
}

static bool poll(int64_t now)
{ read_calls = 0; memset(read_registers, 0, sizeof(read_registers)); return cerberus_battery_poll(now); }

static void unavailable_and_recovery(void)
{
    unknown(ESP_ERR_INVALID_STATE, -1);
    cerberus_battery_get_status(NULL);
    cerberus_battery_init();
    assert(add_calls == 0 && read_calls == 0);
    assert(!poll(-1)); unknown(ESP_ERR_INVALID_STATE, -1);
    add_error = ESP_ERR_NO_MEM;
    assert(poll(0) && add_calls == 1 && read_calls == 0);
    unknown(ESP_ERR_NO_MEM, 0);
    assert(!poll(29999) && add_calls == 1 && read_calls == 0);
    add_error = ESP_OK; fail_register = 0x08;
    assert(poll(30000) && add_calls == 2 && read_calls == 1);
    unknown(ESP_ERR_TIMEOUT, 30000);
    assert(!poll(59999) && read_calls == 0);
    fail_register = 0;
    assert(poll(60000) && add_calls == 2 && read_calls == 3);
    assert(read_registers[0] == 0x08 && read_registers[1] == 0x02 && read_registers[2] == 0x04);
    valid(51, 4000, 60000);
    assert(!poll(64999) && read_calls == 0); valid(51, 4000, 60000);
    voltage = 48640; soc = 18 * 256 + 255;
    assert(poll(65000) && read_calls == 2);
    assert(read_registers[0] == 0x02 && read_registers[1] == 0x04);
    valid(18, 3800, 65000);
}

static void whole_snapshot_failure(void)
{
    /* A voltage failure does not attempt SOC and clears the old snapshot. */
    fail_register = 0x02;
    assert(poll(70000) && read_calls == 1); unknown(ESP_ERR_TIMEOUT, 70000);
    assert(!poll(99999) && read_calls == 0);
    /* Recovery probes again, but even a successful voltage cannot publish if SOC fails. */
    fail_register = 0x04; voltage = 53760; soc = 90 * 256;
    assert(poll(100000) && read_calls == 3); unknown(ESP_ERR_TIMEOUT, 100000);
    fail_register = 0; version = 0x0012;
    assert(poll(130000) && read_calls == 3); valid(90, 4200, 130000);
    /* A caller's copy cannot mutate the shared cache. */
    cerberus_battery_status_t copy = status(); copy.percent = 1; copy.valid = false;
    (void)copy;
    valid(90, 4200, 130000);
}

static void boundaries_and_reinit(void)
{
    unsigned old_adds = add_calls;
    cerberus_battery_init(); unknown(ESP_ERR_INVALID_STATE, -1);
    assert(add_calls == old_adds);
    const uint16_t bad_versions[] = {0, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(bad_versions) / sizeof(bad_versions[0]); ++i) {
        cerberus_battery_init(); version = bad_versions[i];
        assert(poll(100) && read_calls == 1); unknown(ESP_ERR_INVALID_RESPONSE, 100);
    }
    version = 0x0011;
    const uint16_t bad_voltages[] = {0, 31999, 57601, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(bad_voltages) / sizeof(bad_voltages[0]); ++i) {
        cerberus_battery_init(); voltage = bad_voltages[i]; soc = 50 * 256;
        assert(poll(200) && read_calls == 3); unknown(ESP_ERR_INVALID_RESPONSE, 200);
    }
    const uint16_t bad_soc[] = {110 * 256 + 1, 200 * 256, UINT16_MAX};
    for (unsigned i = 0; i < sizeof(bad_soc) / sizeof(bad_soc[0]); ++i) {
        cerberus_battery_init(); voltage = 51200; soc = bad_soc[i];
        assert(poll(300) && read_calls == 3); unknown(ESP_ERR_INVALID_RESPONSE, 300);
    }
    cerberus_battery_init(); voltage = 32000; soc = 0;
    assert(poll(400)); valid(0, 2500, 400);
    voltage = 57600; soc = 100 * 256;
    assert(poll(5400)); valid(100, 4500, 5400);
    /* Model estimates slightly above full must not make a full pack unavailable.
     * Clamp within our allowance; 110% is an application policy, not a chip limit.
     */
    const uint16_t full_soc[] = {25601, 100 * 256 + 128, 101 * 256, 110 * 256};
    for (unsigned i = 0; i < sizeof(full_soc) / sizeof(full_soc[0]); ++i) {
        soc = full_soc[i];
        assert(poll(10400 + (int64_t)i * 5000)); valid(100, 4500, 10400 + (int64_t)i * 5000);
    }
    /* A monotonic clock reset refreshes immediately rather than hiding the gauge for days. */
    assert(poll(0)); valid(100, 4500, 0);
    assert(!poll(4999) && read_calls == 0);
    assert(add_calls == old_adds && lock_depth == 0);
}

int main(void)
{
    unavailable_and_recovery();
    whole_snapshot_failure();
    boundaries_and_reinit();
    puts("PASS: actual battery driver decoding, 0/100 and voltage bounds, unknown/partial-failure handling, initial sample, 5s/30s cadence, recovery, cached copies, and short snapshot locks with I2C outside locks.");
    return 0;
}
