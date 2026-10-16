/* Exercise the real firmware renderer and event handler against bounded display
 * and transactional NVS fakes. This test executable also exports SVG previews. */
#include "sentinel_guide.h"
#include "display.h"
#include "nvs.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool has_record;
static uint32_t committed_record;
static uint32_t staged_record;
static bool has_staged;
static esp_err_t open_error;
static esp_err_t set_error;
static esp_err_t commit_error;
static esp_err_t display_error;
static unsigned nvs_operations;
static unsigned commits;
static char screen_text[16384];
static size_t screen_length;
static FILE *svg;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "sentinel_guide") == 0);
    ++nvs_operations;
    if (open_error != ESP_OK) { return open_error; }
    if (mode == NVS_READONLY && !has_record) { return ESP_ERR_NVS_NOT_FOUND; }
    *handle = 1U;
    return ESP_OK;
}

esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *value)
{
    assert(handle == 1U && strcmp(key, "progress_v1") == 0);
    ++nvs_operations;
    if (!has_record) { return ESP_ERR_NVS_NOT_FOUND; }
    *value = committed_record;
    return ESP_OK;
}

esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
    assert(handle == 1U && strcmp(key, "progress_v1") == 0);
    ++nvs_operations;
    if (set_error != ESP_OK) { return set_error; }
    staged_record = value;
    has_staged = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1U);
    ++nvs_operations;
    ++commits;
    if (commit_error != ESP_OK) { return commit_error; }
    if (has_staged) { committed_record = staged_record; has_record = true; }
    has_staged = false;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    assert(handle == 1U);
    ++nvs_operations;
    has_staged = false;
}

static unsigned rgb(uint16_t color)
{
    unsigned r = (color >> 11) & 31U;
    unsigned g = (color >> 5) & 63U;
    unsigned b = color & 31U;
    return ((r * 255U / 31U) << 16) | ((g * 255U / 63U) << 8) | (b * 255U / 31U);
}

esp_err_t cerberus_display_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    assert(width > 0U && height > 0U);
    assert((unsigned)x + width <= CERBERUS_LCD_WIDTH);
    assert((unsigned)y + height <= CERBERUS_LCD_HEIGHT);
    if (display_error != ESP_OK) { return display_error; }
    if (svg != NULL) {
        fprintf(svg, "<rect x=\"%u\" y=\"%u\" width=\"%u\" height=\"%u\" fill=\"#%06x\"/>\n", x, y, width, height, rgb(color));
    }
    return ESP_OK;
}

esp_err_t cerberus_display_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t background, uint8_t scale)
{
    (void)background;
    const size_t length = strlen(text);
    assert((unsigned)x + length * 6U * scale <= CERBERUS_LCD_WIDTH);
    assert((unsigned)y + 7U * scale <= CERBERUS_LCD_HEIGHT);
    assert(screen_length + length + 2U < sizeof(screen_text));
    if (display_error != ESP_OK) { return display_error; }
    memcpy(screen_text + screen_length, text, length);
    screen_length += length;
    screen_text[screen_length++] = '\n';
    screen_text[screen_length] = '\0';
    if (svg != NULL && length > 0U) {
        fprintf(svg, "<text x=\"%u\" y=\"%u\" font-family=\"monospace\" font-size=\"%u\" textLength=\"%u\" lengthAdjust=\"spacingAndGlyphs\" fill=\"#%06x\">", x, y + 7U * scale, 8U * scale, (unsigned)length * 6U * scale, rgb(color));
        for (size_t i = 0U; i < length; ++i) {
            assert((unsigned char)text[i] >= 32U && (unsigned char)text[i] <= 126U);
            if (text[i] == '&') { fputs("&amp;", svg); }
            else if (text[i] == '<') { fputs("&lt;", svg); }
            else if (text[i] == '>') { fputs("&gt;", svg); }
            else { fputc(text[i], svg); }
        }
        fputs("</text>\n", svg);
    }
    return ESP_OK;
}

static void draw(void)
{
    screen_length = 0U;
    screen_text[0] = '\0';
    const esp_err_t err = sentinel_guide_draw();
    if (err != ESP_OK) {
        fprintf(stderr, "Draw failed (%d), text so far:\n%s", err, screen_text);
        abort();
    }
}

static void contains(const char *phrase)
{
    if (strstr(screen_text, phrase) == NULL) {
        fprintf(stderr, "Missing screen text '%s':\n%s", phrase, screen_text);
        abort();
    }
}

static void snapshot(const char *directory, const char *name)
{
    if (directory == NULL) { draw(); return; }
    char path[1024];
    const int count = snprintf(path, sizeof(path), "%s/%s.svg", directory, name);
    assert(count > 0 && (size_t)count < sizeof(path));
    svg = fopen(path, "wb");
    assert(svg != NULL);
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"320\" height=\"480\" viewBox=\"0 0 320 480\">\n", svg);
    draw();
    fputs("</svg>\n", svg);
    fclose(svg);
    svg = NULL;
}

static sentinel_guide_action_t tap(uint16_t x, uint16_t y)
{
    const sentinel_guide_action_t action = sentinel_guide_handle_touch(x, y);
    draw();
    return action;
}

static void next(void) { assert(tap(260U, 444U) == SENTINEL_GUIDE_REDRAW); }

static void fresh(void)
{
    has_record = false;
    has_staged = false;
    committed_record = 0U;
    open_error = ESP_OK;
    set_error = ESP_OK;
    commit_error = ESP_OK;
    assert(sentinel_guide_init() == ESP_OK);
    assert(sentinel_guide_completed_count() == 0U);
}

static const uint8_t correct_answers[] = {1U, 1U, 0U, 1U, 1U, 0U};
static const sentinel_guide_action_t tools[] = {
    SENTINEL_GUIDE_OPEN_WIFI, SENTINEL_GUIDE_OPEN_WIFI_CHANNELS,
    SENTINEL_GUIDE_OPEN_BLE, SENTINEL_GUIDE_OPEN_BLE_SIGNAL,
    SENTINEL_GUIDE_OPEN_RADIO, SENTINEL_GUIDE_OPEN_WIFI_BASELINE,
};

static void answer_lesson(uint8_t lesson)
{
    sentinel_guide_open_lesson(lesson);
    draw();
    next(); next(); next();
    assert(tap(140U, (uint16_t)(240U + correct_answers[lesson] * 66U)) == SENTINEL_GUIDE_REDRAW);
    contains("Correct.");
    next();
    contains("ACTIVITY COMPLETE");
}

static void test_lessons(const char *directory)
{
    fresh();
    snapshot(directory, "guide-menu");
    assert(tap(12U, 100U) == SENTINEL_GUIDE_NONE);
    assert(tap(320U, 0U) == SENTINEL_GUIDE_NONE);
    assert(tap(20U, 480U) == SENTINEL_GUIDE_NONE);
    assert(tap(20U, 137U) == SENTINEL_GUIDE_NONE); /* gap between rows */
    for (uint8_t lesson = 0U; lesson < SENTINEL_GUIDE_LESSON_COUNT; ++lesson) {
        sentinel_guide_open();
        assert(tap(140U, (uint16_t)(110U + lesson * 48U)) == SENTINEL_GUIDE_REDRAW);
        contains("01 / GOAL");
        char filename[64];
        snprintf(filename, sizeof(filename), "lesson-%u-goal", lesson + 1U);
        snapshot(directory, filename);
        assert(tap(160U, 444U) == SENTINEL_GUIDE_NONE); /* disabled back */
        next();
        contains("02 / TRY IT");
        assert(tap(160U, 376U) == tools[lesson]);
        contains("02 / TRY IT"); /* tool launch does not lose lesson state */
        next();
        contains("03 / WHAT IT MEANS");
        next();
        contains("04 / QUICK CHECK");
        assert(sentinel_guide_completed_count() == lesson);
        assert(tap(260U, 444U) == SENTINEL_GUIDE_NONE); /* cannot skip quiz */
        const uint8_t wrong = (uint8_t)(1U - correct_answers[lesson]);
        assert(tap(160U, (uint16_t)(240U + wrong * 66U)) == SENTINEL_GUIDE_REDRAW);
        contains("Try again.");
        assert(tap(260U, 444U) == SENTINEL_GUIDE_NONE);
        assert(sentinel_guide_completed_count() == lesson);
        assert(tap(160U, (uint16_t)(240U + correct_answers[lesson] * 66U)) == SENTINEL_GUIDE_REDRAW);
        contains("Correct.");
        assert(sentinel_guide_completed_count() == lesson); /* still needs acknowledgement */
        snprintf(filename, sizeof(filename), "lesson-%u-check", lesson + 1U);
        snapshot(directory, filename);
        next();
        assert(sentinel_guide_completed_count() == lesson + 1U);
        contains("Progress saved locally.");
        assert(sentinel_guide_init() == ESP_OK); /* power-cycle read */
        assert(sentinel_guide_completed_count() == lesson + 1U);
    }
    draw();
    contains("6 / 6 activities completed");
    const unsigned before = commits;
    answer_lesson(2U);
    assert(commits == before); /* rereading a lesson causes no flash write */
    assert(sentinel_guide_completed_count() == 6U);
    assert(sentinel_guide_reset_progress() == ESP_OK);
    assert(sentinel_guide_init() == ESP_OK);
    assert(sentinel_guide_completed_count() == 0U);
}

static void test_persistence_faults(const char *directory)
{
    fresh();
    commit_error = ESP_FAIL;
    answer_lesson(0U);
    assert(sentinel_guide_completed_count() == 1U);
    contains("Saving failed.");
    contains("Retry save");
    snapshot(directory, "progress-save-failed");
    assert(!has_record); /* failed commit survives only within this session */
    commit_error = ESP_OK;
    next(); /* explicit retry */
    contains("Progress saved locally.");
    assert(sentinel_guide_init() == ESP_OK);
    assert(sentinel_guide_completed_count() == 1U);

    set_error = ESP_FAIL;
    answer_lesson(1U);
    contains("Saving failed.");
    assert(sentinel_guide_completed_count() == 2U);
    assert(sentinel_guide_init() == ESP_OK);
    assert(sentinel_guide_completed_count() == 1U);
    set_error = ESP_OK;

    commit_error = ESP_FAIL;
    assert(sentinel_guide_reset_progress() == ESP_FAIL);
    assert(sentinel_guide_completed_count() == 1U);
    commit_error = ESP_OK;
    assert(sentinel_guide_init() == ESP_OK);
    assert(sentinel_guide_completed_count() == 1U);

    fresh();
    open_error = ESP_FAIL;
    assert(sentinel_guide_init() == ESP_FAIL);
    draw(); contains("Progress storage unavailable.");
    answer_lesson(3U);
    assert(sentinel_guide_completed_count() == 1U);
    contains("Saving failed.");
    open_error = ESP_OK;
    next();
    assert(sentinel_guide_init() == ESP_OK);
    assert(sentinel_guide_completed_count() == 1U);

    /* Corruption, future format, and out-of-range completion bits must never
     * turn into badges. Bad data is retained until an explicit progress write. */
    const uint32_t invalid_records[] = {0U, 0xFFFFFFFFU, 0xCB0201FEU, 0xCB01807FU, 0xCB0100FEU};
    for (size_t index = 0U; index < sizeof(invalid_records) / sizeof(invalid_records[0]); ++index) {
        has_record = true;
        committed_record = invalid_records[index];
        const unsigned writes = commits;
        assert(sentinel_guide_init() == ESP_ERR_INVALID_STATE);
        assert(sentinel_guide_completed_count() == 0U);
        assert(commits == writes);
        draw(); contains("Unreadable progress");
    }
    answer_lesson(5U);
    assert(sentinel_guide_init() == ESP_OK);
    assert(sentinel_guide_completed_count() == 1U);
}

static void test_demo(const char *directory)
{
    fresh();
    const unsigned operations = nvs_operations;
    sentinel_guide_open_demo();
    for (uint8_t panel = 0U; panel < 3U; ++panel) {
        char filename[64];
        snprintf(filename, sizeof(filename), "demo-%u", panel + 1U);
        snapshot(directory, filename);
        contains("DEMO / SAMPLE DATA");
        contains("FICTIONAL EXAMPLES / NO LIVE RADIO DATA");
        contains("nothing is logged");
        for (uint32_t time = 0U; time < 30000U; time += 901U) {
            (void)sentinel_guide_tick(time);
            draw(); /* includes every deterministic value and phase */
        }
        next();
    }
    /* Every possible touched pixel either stays in demo or requests home.
     * The module cannot launch a radio mode from any demo control. */
    for (uint16_t y = 0U; y < 480U; ++y) {
        for (uint16_t x = 0U; x < 320U; ++x) {
            const sentinel_guide_action_t action = sentinel_guide_handle_touch(x, y);
            assert(action == SENTINEL_GUIDE_NONE || action == SENTINEL_GUIDE_REDRAW || action == SENTINEL_GUIDE_HOME);
        }
    }
    assert(nvs_operations == operations);
    assert(sentinel_guide_completed_count() == 0U);
    sentinel_guide_open_demo();
    assert(!sentinel_guide_tick(UINT32_MAX - 400U));
    assert(!sentinel_guide_tick(300U));
    assert(sentinel_guide_tick(500U)); /* elapsed calculation across rollover */
    sentinel_guide_open();
    assert(!sentinel_guide_tick(3000U));
    display_error = ESP_FAIL;
    assert(sentinel_guide_draw() == ESP_FAIL);
    display_error = ESP_OK;
}

int main(int argc, char **argv)
{
    const char *directory = argc > 1 ? argv[1] : NULL;
    test_lessons(directory);
    test_persistence_faults(directory);
    test_demo(directory);
    puts("PASS: all lessons, tool returns, quiz gates, saved progress, NVS faults/corruption, reset, drawing bounds, demo isolation, timer rollover");
    return 0;
}
