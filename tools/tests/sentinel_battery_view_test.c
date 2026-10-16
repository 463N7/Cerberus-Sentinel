#include "sentinel_battery_view.h"
#include "sentinel_watch_view.h"

static bool in_battery(unsigned x, unsigned y)
{
    return x >= 244U && x < 312U && y >= 3U && y < 16U;
}

static void update(bool valid, uint8_t percent, uint16_t background, unsigned writes)
{
    memcpy(expected, framebuffer, sizeof(expected));
    const unsigned before = battery_writes;
    battery_only = true;
    assert(sentinel_battery_view_update(valid, percent, background) == ESP_OK);
    battery_only = false;
    assert(battery_writes == before + writes);
    for (unsigned y = 0; y < 480U; ++y) {
        for (unsigned x = 0; x < 320U; ++x) {
            if (!in_battery(x, y)) {
                assert(expected[y * 320U + x] == framebuffer[y * 320U + x]);
            }
        }
    }
}

static void text_matches(const char *label, uint16_t color, uint16_t background)
{
    const unsigned left = 244U + 66U - (unsigned)strlen(label) * 6U;
    for (unsigned index = 0; label[index] != '\0'; ++index) {
        const uint8_t *shape = font5x7(label[index]);
        for (unsigned row = 0; row < 7U; ++row) {
            for (unsigned col = 0; col < 6U; ++col) {
                const bool on = col < 5U && (shape[row] & (1U << (4U - col)));
                assert(framebuffer[(row + 6U) * 320U + left + index * 6U + col] ==
                    (on ? color : background));
            }
        }
    }
}

static void render_tests(void)
{
    static const uint8_t levels[] = {0, 1, 10, 20, 21, 99, 100};
    static const uint16_t colors[] = {0xF471, 0xF471, 0xF471, 0xFDEA, 0x3F1B, 0x3F1B, 0x3F1B};
    sentinel_battery_view_invalidate();
    assert(cerberus_display_fill_rect(0, 0, 320, 480, 0x0062) == ESP_OK);
    for (unsigned index = 0; index < sizeof(levels); ++index) {
        const uint8_t percent = levels[index];
        update(true, percent, 0x0062, 1);
        char label[5];
        snprintf(label, sizeof(label), "%u%%", percent);
        text_matches(label, colors[index], 0x0062);
        /* Empty, minimum visible charge, and full icons remain distinguishable. */
        assert(framebuffer[7U * 320U + 254U] == (percent == 0 ? 0x0062 : colors[index]));
        assert(framebuffer[7U * 320U + 270U] == (percent >= 99 ? colors[index] : 0x0062));
        update(true, percent, 0x0062, 0);
    }

    update(true, 101, 0x0062, 0);
    update(true, 255, 0x0062, 0);
    text_matches("100%", 0x3F1B, 0x0062);
    update(false, 100, 0x0062, 1);
    text_matches("--%", 0x84B5, 0x0062);
    update(false, 0, 0x0062, 0);
    update(false, 255, 0x0062, 0);

    /* Every change below the reserved top row leaves the battery cache intact. */
    assert(cerberus_display_fill_rect(0, 16, 320, 1, 0xFFFF) == ESP_OK);
    assert(cerberus_display_fill_rect(0, 100, 320, 220, 0x1111) == ESP_OK);
    update(false, 0, 0x0062, 0);

    /* Even an unrelated top draw advances the shared revision. */
    assert(cerberus_display_fill_rect(0, 0, 1, 1, 0xFFFF) == ESP_OK);
    update(false, 0, 0x0062, 1);
    assert(cerberus_display_fill_rect(0, 0, 320, 16, 0x2222) == ESP_OK);
    update(false, 0, 0x0062, 1);
    text_matches("--%", 0x84B5, 0x0062);

    update(true, 73, 0x0863, 1);
    text_matches("73%", 0x3F1B, 0x0863);
    assert(framebuffer[3U * 320U + 244U] == 0x0863);
    sentinel_battery_view_invalidate();
    update(true, 73, 0x0863, 1);

    fail_display = 1;
    battery_only = true;
    assert(sentinel_battery_view_update(true, 41, 0x0863) == ESP_FAIL);
    battery_only = false;
    fail_display = 0;
    update(true, 41, 0x0863, 1);
    text_matches("41%", 0x3F1B, 0x0863);
    update(true, 41, 0x0863, 0);

    top_revision = UINT32_MAX;
    sentinel_battery_view_invalidate();
    update(true, 41, 0x0863, 1);
    assert(top_revision == 0U);
    update(true, 41, 0x0863, 0);
}

static void normal_header(void)
{
    assert(cerberus_display_fill_rect(0, 0, 320, 480, 0x0062) == ESP_OK);
    assert(cerberus_display_fill_rect(8, 7, 3, 28, 0x3F1B) == ESP_OK);
    assert(cerberus_display_draw_text(20, 5, "CERBERUS / SENTINEL", 0x84B5, 0x0062, 1) == ESP_OK);
    assert(cerberus_display_draw_text(20, 20, "Wi-Fi Watch", 0xE77E, 0x0062, 2) == ESP_OK);
    assert(cerberus_display_fill_rect(8, 44, 304, 1, 0x08A4) == ESP_OK);
    assert(cerberus_display_fill_rect(8, 44, 46, 1, 0x3F1B) == ESP_OK);
    assert(cerberus_display_draw_text(20, 70, "TOP STATUS / EXAMPLE SCREEN", 0x84B5, 0x0062, 1) == ESP_OK);
}

static void previews(void)
{
    const sentinel_dashboard_status_t status = {
        .c5_online = true, .touch_online = true, .privacy_masked = true,
        .wifi_count = 12, .ble_count = 8, .lessons_complete = 2,
        .wifi_observed = true, .ble_observed = true,
    };
    assert(sentinel_dashboard_draw(&status, 0) == ESP_OK);
    update(true, 73, 0x0062, 1);
    save("battery-home.ppm");
    assert(sentinel_dashboard_animate(&status, 1) == ESP_OK);
    update(true, 73, 0x0062, 0);

    sentinel_watch_view_t watch = {.radio_online = true, .phase = "BLE LISTENING / 4S LEFT"};
    assert(sentinel_watch_view_draw(&watch, 0) == ESP_OK);
    update(true, 73, 0x0042, 1);
    save("battery-watch.ppm");
    assert(sentinel_watch_view_update(&watch, 400) == ESP_OK);
    update(true, 73, 0x0042, 0);
    watch.phase = "WI-FI SWEEP / 2S LEFT";
    assert(sentinel_watch_view_update(&watch, 400) == ESP_OK);
    update(true, 73, 0x0042, 1);
    text_matches("73%", 0x3F1B, 0x0042);

    normal_header();
    update(true, 100, 0x0062, 1);
    save("battery-normal.ppm");
    normal_header();
    update(true, 20, 0x0062, 1);
    save("battery-low.ppm");
    normal_header();
    update(true, 8, 0x0062, 1);
    save("battery-critical.ppm");
    normal_header();
    update(false, 0, 0x0062, 1);
    save("battery-unknown.ppm");
}

int main(void)
{
    render_tests();
    previews();
    puts("Battery: percentages, unknown state, clamping, colors, single bitmap bounds, cache, top redraw restoration, lower-page immunity and failure retry passed.");
    puts("Home and Watch composition: battery preserved during animation; header redraw repaired; surrounding pixels unchanged.");
    return 0;
}
