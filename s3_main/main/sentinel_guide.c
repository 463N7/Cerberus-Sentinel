#include "sentinel_guide.h"

#include "display.h"
#include "ui_widgets.h"

#include "nvs.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* This module deliberately has no radio, protocol, SD, or observation-log
 * dependency. Demonstration data can never become a live observation. */
#define GUIDE_BG 0x0863U
#define GUIDE_PANEL 0x10E5U
#define GUIDE_BORDER 0x29AAU
#define GUIDE_TEXT 0xEF9EU
#define GUIDE_MUTED 0x94D3U
#define GUIDE_MINT 0x5F39U
#define GUIDE_AMBER 0xFE28U
#define GUIDE_VIOLET 0xBB3FU
#define GUIDE_VALID_MASK ((1U << SENTINEL_GUIDE_LESSON_COUNT) - 1U)
#define GUIDE_RECORD_HEADER 0xCB010000UL
#define GUIDE_DEMO_INTERVAL_MS 900U
#define TRY_DRAW(call) do { const esp_err_t result_ = (call); if (result_ != ESP_OK) { return result_; } } while (0)

typedef enum { VIEW_LIST, VIEW_LESSON, VIEW_DEMO } guide_view_t;
typedef struct {
    const char *title;
    const char *category;
    const char *goal;
    const char *experiment;
    const char *explanation;
    const char *question;
    const char *answers[2];
    const char *feedback;
    sentinel_guide_action_t tool;
    uint8_t correct_answer;
    uint16_t accent;
} guide_lesson_t;

static const guide_lesson_t lessons[SENTINEL_GUIDE_LESSON_COUNT] = {
    {
        "Meet Your Network", "WATCH / WI-FI",
        "Learn what your router announces. Find its name, channel and advertised security. These details describe a network; they do not prove who runs it.",
        "Open Wi-Fi Watch. Select your own network. Note its channel and security. If several entries share its name, compare their BSSIDs. Return here when ready.",
        "A name (SSID) is a label. A BSSID identifies an access-point radio, but can be copied. WPA2/WPA3 describe advertised protection; a scan cannot check your password.",
        "Does a familiar network name prove it is your router?",
        {"Yes, names are unique", "No, names can be copied"},
        "Names can be reused. Compare your own router's known details before connecting.",
        SENTINEL_GUIDE_OPEN_WIFI, 1U, GUIDE_MINT,
    },
    {
        "Crowded Air", "WATCH / CHANNELS",
        "Explore how nearby Wi-Fi networks share channels. Compare the number of observed networks with their signal levels. A busy-looking list is a clue to investigate.",
        "Open the channel view. Compare two channels, then repeat the scan from another room. Note which networks grow stronger and which disappear. Return here to review.",
        "Network counts are not airtime measurements. One busy network can use more time than many quiet ones. Nearby channels can overlap, especially with wider channels.",
        "Does the channel with fewest networks always perform best?",
        {"Yes, fewer always means faster", "No, traffic and overlap matter"},
        "Counts alone do not measure traffic, interference, or available airtime.",
        SENTINEL_GUIDE_OPEN_WIFI_CHANNELS, 1U, GUIDE_MINT,
    },
    {
        "Bluetooth Footprints", "SCOUT / ADVERTISEMENTS",
        "See what your BLE device broadcasts before a connection. Advertisements may include a name, company code or services. Some devices reveal very little.",
        "Open Bluetooth Scout near a device you own. Compare its advertisements with the device on and off. Inspect any name and service fields. Return here after exploring.",
        "A company code describes an advertised format; it is not proof of identity. Devices can rotate addresses. An unfamiliar advertisement alone does not prove tracking.",
        "An unfamiliar BLE advertisement proves what?",
        {"Only that an advertisement was seen", "That a tracker is following you"},
        "More context is needed. A scan alone cannot establish intent or ownership.",
        SENTINEL_GUIDE_OPEN_BLE, 0U, GUIDE_VIOLET,
    },
    {
        "Follow the Signal", "SCOUT / SIGNAL HISTORY",
        "Use your own BLE device to explore signal strength. Keep the device still, then change distance and orientation. Watch the trend instead of a single number.",
        "Choose your BLE device in Scout and open its signal view. Compare nearby and farther readings, then turn the device. Keep the same device selected. Return here to review.",
        "RSSI is received power. A value closer to zero is stronger. Walls, reflections, antennas and orientation all affect it. RSSI is not a reliable distance ruler.",
        "A stronger BLE signal guarantees that a device is closer?",
        {"Yes, RSSI gives exact distance", "No, surroundings affect RSSI"},
        "Distance is one factor. Orientation and reflections can also change RSSI.",
        SENTINEL_GUIDE_OPEN_BLE_SIGNAL, 1U, GUIDE_VIOLET,
    },
    {
        "Shared Airspace", "SENSE / 802.15.4",
        "Explore 802.15.4 channels in the 2.4 GHz band. Zigbee and Thread use this radio technology. Wi-Fi and other signals may overlap the same frequencies.",
        "Open Radio Sense and run an energy scan. Compare stronger and quieter channels. Repeat nearby while your own Wi-Fi is active. Return here to interpret the result.",
        "Energy detection measures received energy. It does not identify a protocol or decode encrypted traffic. Activity and beacons add clues, but do not explain every signal.",
        "High energy on an 802.15.4 channel proves what?",
        {"A Zigbee device is present", "Energy was detected there"},
        "The source could be Wi-Fi or another signal. Energy alone cannot identify it.",
        SENTINEL_GUIDE_OPEN_RADIO, 1U, GUIDE_AMBER,
    },
    {
        "What Changed?", "WATCH / TRUSTED BASELINE",
        "Save known details for a network you own. Later, compare a new scan with that baseline. A changed observation starts an investigation; it is not an attack verdict.",
        "Open your network in Wi-Fi Watch. Verify its details, then add it to your trusted list. Scan again and compare. Only update the baseline after explaining a change.",
        "A mesh node, new router or setting change can alter a result. A copied name is another possibility. Verify with your router settings before trusting a new baseline.",
        "What should you do with an unexplained baseline change?",
        {"Verify it using your own router", "Immediately label it an attack"},
        "Check the router and expected changes. The observation does not establish intent.",
        SENTINEL_GUIDE_OPEN_WIFI_BASELINE, 0U, GUIDE_MINT,
    },
};

static const cerberus_ui_widget_theme_t theme = {
    .background = GUIDE_BG, .panel = GUIDE_PANEL, .border = GUIDE_BORDER,
    .text = GUIDE_TEXT, .muted = GUIDE_MUTED,
};
static guide_view_t view;
static uint8_t active_lesson;
static uint8_t lesson_step;
static int8_t selected_answer;
static uint8_t completed_mask;
static uint8_t persisted_mask;
static esp_err_t progress_error;
static bool invalid_record;
static uint8_t demo_panel;
static uint8_t demo_phase;
static uint32_t demo_last_ms;
static bool demo_clock_started;

static bool inside(uint16_t x, uint16_t y, cerberus_ui_rect_t bounds)
{
    return x >= bounds.x && y >= bounds.y &&
        (uint32_t)x < (uint32_t)bounds.x + bounds.width &&
        (uint32_t)y < (uint32_t)bounds.y + bounds.height;
}

static esp_err_t text(uint16_t x, uint16_t y, const char *value, uint16_t color, uint8_t scale)
{
    return cerberus_display_draw_text(x, y, value, color, GUIDE_BG, scale);
}

/* All wrapping is bounded by the rectangle and a fixed one-line buffer. Return
 * a layout error instead of silently dropping text if content no longer fits. */
static esp_err_t paragraph(cerberus_ui_rect_t bounds, const char *value, uint16_t color, uint8_t scale)
{
    if (value == NULL || scale == 0U || bounds.width > CERBERUS_LCD_WIDTH) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t columns = bounds.width / (6U * scale);
    const uint16_t line_height = 10U * scale;
    char line[54];
    if (columns == 0U || columns >= sizeof(line)) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint16_t y = bounds.y;
    while (*value != '\0') {
        while (*value == ' ') { ++value; }
        if (*value == '\0') { break; }
        if ((uint32_t)y + 7U * scale > (uint32_t)bounds.y + bounds.height) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t count = 0U;
        size_t last_space = 0U;
        while (count < columns && value[count] != '\0' && value[count] != '\n') {
            if (value[count] == ' ') { last_space = count; }
            ++count;
        }
        if (count == columns && value[count] != '\0' && value[count] != ' ' &&
            value[count] != '\n' && last_space > 0U) {
            count = last_space;
        }
        memcpy(line, value, count);
        line[count] = '\0';
        TRY_DRAW(text(bounds.x, y, line, color, scale));
        value += count;
        if (*value == '\n') { ++value; }
        y += line_height;
    }
    return ESP_OK;
}

static esp_err_t button(cerberus_ui_rect_t bounds, const char *label, bool enabled)
{
    uint8_t scale = strlen(label) * 12U + 12U <= bounds.width ? 2U : 1U;
    return cerberus_ui_widget_draw_button(bounds, label, scale, enabled, &theme);
}

static uint32_t progress_record(uint8_t mask)
{
    return GUIDE_RECORD_HEADER | ((uint32_t)mask << 8U) | (uint8_t)~mask;
}

static bool valid_progress_record(uint32_t record)
{
    const uint8_t mask = (uint8_t)(record >> 8U);
    return (record & 0xFFFF0000UL) == GUIDE_RECORD_HEADER &&
        (mask & ~GUIDE_VALID_MASK) == 0U && (uint8_t)record == (uint8_t)~mask;
}

static esp_err_t save_progress(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sentinel_guide", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, "progress_v1", progress_record(completed_mask));
        if (err == ESP_OK) { err = nvs_commit(handle); }
        nvs_close(handle);
    }
    progress_error = err;
    if (err == ESP_OK) {
        persisted_mask = completed_mask;
        invalid_record = false;
    }
    return err;
}

esp_err_t sentinel_guide_init(void)
{
    view = VIEW_LIST;
    active_lesson = 0U;
    lesson_step = 0U;
    selected_answer = -1;
    completed_mask = 0U;
    persisted_mask = 0U;
    invalid_record = false;
    progress_error = ESP_OK;
    demo_clock_started = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("sentinel_guide", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { return ESP_OK; }
    if (err == ESP_OK) {
        uint32_t record = 0U;
        err = nvs_get_u32(handle, "progress_v1", &record);
        nvs_close(handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) { return ESP_OK; }
        if (err == ESP_OK) {
            if (valid_progress_record(record)) {
                completed_mask = (uint8_t)(record >> 8U);
                persisted_mask = completed_mask;
            } else {
                invalid_record = true;
                err = ESP_ERR_INVALID_STATE;
            }
        }
    }
    progress_error = err;
    return err;
}

esp_err_t sentinel_guide_reset_progress(void)
{
    const uint8_t previous_mask = completed_mask;
    completed_mask = 0U;
    const esp_err_t err = save_progress();
    if (err != ESP_OK) {
        completed_mask = previous_mask;
    } else {
        sentinel_guide_open();
    }
    return err;
}

void sentinel_guide_open(void) { view = VIEW_LIST; }

void sentinel_guide_open_lesson(uint8_t lesson)
{
    if (lesson >= SENTINEL_GUIDE_LESSON_COUNT) { return; }
    view = VIEW_LESSON;
    active_lesson = lesson;
    lesson_step = 0U;
    selected_answer = -1;
}

void sentinel_guide_open_demo(void)
{
    view = VIEW_DEMO;
    demo_panel = 0U;
    demo_phase = 0U;
    demo_clock_started = false;
}

uint8_t sentinel_guide_completed_count(void)
{
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < SENTINEL_GUIDE_LESSON_COUNT; ++index) {
        if ((completed_mask & (1U << index)) != 0U) { ++count; }
    }
    return count;
}

static esp_err_t draw_list(void)
{
    TRY_DRAW(text(16U, 18U, "FIELD GUIDE", GUIDE_MINT, 2U));
    TRY_DRAW(text(16U, 46U, "EXPLORE. OBSERVE. UNDERSTAND.", GUIDE_MUTED, 1U));
    char count[32];
    snprintf(count, sizeof(count), "%u / %u activities completed", sentinel_guide_completed_count(), SENTINEL_GUIDE_LESSON_COUNT);
    TRY_DRAW(text(16U, 68U, count, GUIDE_TEXT, 1U));
    for (uint8_t index = 0U; index < SENTINEL_GUIDE_LESSON_COUNT; ++index) {
        const cerberus_ui_rect_t row = {16U, 92U + index * 48U, 288U, 44U};
        const bool done = (completed_mask & (1U << index)) != 0U;
        TRY_DRAW(cerberus_display_fill_rect(row.x, row.y, row.width, row.height, GUIDE_PANEL));
        TRY_DRAW(cerberus_display_draw_text(row.x + 10U, row.y + 5U, lessons[index].title,
            GUIDE_TEXT, GUIDE_PANEL, 2U));
        TRY_DRAW(cerberus_display_draw_text(row.x + 10U, row.y + 28U,
            done ? "COMPLETE" : lessons[index].category, done ? GUIDE_MINT : GUIDE_MUTED, GUIDE_PANEL, 1U));
        TRY_DRAW(cerberus_display_fill_rect(row.x, row.y, 3U, row.height, lessons[index].accent));
    }
    const char *status = "Progress stays on this device.";
    if (completed_mask != persisted_mask) { status = "Session progress only. Retry from lesson."; }
    else if (invalid_record) { status = "Unreadable progress; no badges restored."; }
    else if (progress_error != ESP_OK) { status = "Progress storage unavailable. Try a lesson."; }
    TRY_DRAW(paragraph((cerberus_ui_rect_t){16U, 386U, 288U, 24U}, status, progress_error == ESP_OK ? GUIDE_MUTED : GUIDE_AMBER, 1U));
    TRY_DRAW(button((cerberus_ui_rect_t){16U, 424U, 88U, 44U}, "Home", true));
    return button((cerberus_ui_rect_t){116U, 424U, 188U, 44U}, "Offline demo", true);
}

static esp_err_t draw_lesson(void)
{
    const guide_lesson_t *lesson = &lessons[active_lesson];
    static const char *const headings[] = {"01 / GOAL", "02 / TRY IT", "03 / WHAT IT MEANS", "04 / QUICK CHECK", "ACTIVITY COMPLETE"};
    TRY_DRAW(text(16U, 18U, lesson->title, lesson->accent, 2U));
    TRY_DRAW(text(16U, 47U, lesson->category, GUIDE_MUTED, 1U));
    TRY_DRAW(text(16U, 79U, headings[lesson_step], lesson->accent, 1U));
    if (lesson_step < 3U) {
        const char *body = lesson_step == 0U ? lesson->goal : lesson_step == 1U ? lesson->experiment : lesson->explanation;
        TRY_DRAW(paragraph((cerberus_ui_rect_t){16U, 112U, 288U, lesson_step == 1U ? 230U : 286U}, body, GUIDE_TEXT, 2U));
        if (lesson_step == 1U) {
            TRY_DRAW(button((cerberus_ui_rect_t){16U, 354U, 288U, 48U}, "Open tool / return to guide", true));
        }
    } else if (lesson_step == 3U) {
        TRY_DRAW(paragraph((cerberus_ui_rect_t){16U, 112U, 288U, 96U}, lesson->question, GUIDE_TEXT, 2U));
        for (uint8_t answer = 0U; answer < 2U; ++answer) {
            const cerberus_ui_rect_t box = {16U, 216U + answer * 66U, 288U, 56U};
            TRY_DRAW(button(box, lesson->answers[answer], true));
            if (selected_answer == (int8_t)answer) {
                TRY_DRAW(cerberus_display_fill_rect(box.x, box.y, 4U, box.height, answer == lesson->correct_answer ? GUIDE_MINT : GUIDE_AMBER));
            }
        }
        if (selected_answer >= 0) {
            const bool correct = (uint8_t)selected_answer == lesson->correct_answer;
            TRY_DRAW(text(16U, 354U, correct ? "Correct. Tap Complete to save progress." : "Try again. Here is a clue:", correct ? GUIDE_MINT : GUIDE_AMBER, 1U));
            TRY_DRAW(paragraph((cerberus_ui_rect_t){16U, 376U, 288U, 30U}, lesson->feedback, GUIDE_MUTED, 1U));
        }
    } else {
        const bool saved = completed_mask == persisted_mask && progress_error == ESP_OK;
        TRY_DRAW(paragraph((cerberus_ui_rect_t){16U, 112U, 288U, 120U}, "You explored a useful signal and checked what the evidence can tell you.", GUIDE_TEXT, 2U));
        TRY_DRAW(paragraph((cerberus_ui_rect_t){16U, 252U, 288U, 146U}, saved ? "Progress saved locally. Revisit any activity when you want a refresher." : "Completed this session. Saving failed. Tap Retry save to keep your progress after restart.", saved ? GUIDE_MINT : GUIDE_AMBER, 2U));
    }
    TRY_DRAW(button((cerberus_ui_rect_t){16U, 424U, 88U, 44U}, "Guide", true));
    TRY_DRAW(button((cerberus_ui_rect_t){116U, 424U, 88U, 44U}, lesson_step == 4U ? "Home" : "Back", lesson_step != 0U));
    const bool ready = lesson_step != 3U || selected_answer == (int8_t)lesson->correct_answer;
    const bool pending = completed_mask != persisted_mask || progress_error != ESP_OK;
    const char *next = lesson_step < 3U ? "Next" : lesson_step == 3U ? "Complete" : pending ? "Retry save" : "Review";
    return button((cerberus_ui_rect_t){216U, 424U, 88U, 44U}, next, ready);
}

static esp_err_t draw_demo(void)
{
    static const char *const panels[] = {"01 / 03   WI-FI WATCH", "02 / 03   BLUETOOTH SCOUT", "03 / 03   RADIO SENSE"};
    static const char *const notes[] = {
        "Stronger bars show received power, not speed or trust.",
        "Signal varies. This is a time history, not a distance ruler.",
        "Energy alone cannot identify a signal or protocol.",
    };
    TRY_DRAW(text(16U, 18U, "DEMO / SAMPLE DATA", GUIDE_AMBER, 2U));
    TRY_DRAW(text(16U, 46U, panels[demo_panel], GUIDE_TEXT, 1U));
    TRY_DRAW(text(16U, 68U, "FICTIONAL EXAMPLES / NO LIVE RADIO DATA", GUIDE_AMBER, 1U));
    int16_t values[16];
    const uint8_t count = demo_panel == 0U ? 3U : 16U;
    const uint16_t accent = demo_panel == 0U ? GUIDE_MINT : demo_panel == 1U ? GUIDE_VIOLET : GUIDE_AMBER;
    if (demo_panel == 0U) {
        static const int16_t powers[] = {-46, -73, -60};
        for (uint8_t index = 0U; index < count; ++index) {
            values[index] = powers[index] + (int16_t)((demo_phase + index) % 3U) - 1;
        }
        TRY_DRAW(text(16U, 99U, "EXAMPLE-A / EXAMPLE-B / EXAMPLE-C", GUIDE_MUTED, 1U));
        TRY_DRAW(text(16U, 118U, "RECEIVED POWER (dBm): -100 TO -30", GUIDE_MUTED, 1U));
    } else if (demo_panel == 1U) {
        static const int16_t history[] = {-78, -77, -72, -64, -69, -61, -58, -65, -60, -56, -63, -71, -68, -74, -79, -75};
        for (uint8_t index = 0U; index < count; ++index) {
            values[index] = history[(index + demo_phase) % 16U];
        }
        TRY_DRAW(text(16U, 99U, "EXAMPLE-SENSOR / FICTIONAL SIGNAL HISTORY", GUIDE_MUTED, 1U));
        TRY_DRAW(text(16U, 118U, "RSSI (dBm): -100 TO -30", GUIDE_MUTED, 1U));
    } else {
        static const int16_t energy[] = {-92, -90, -72, -62, -59, -68, -86, -93, -91, -84, -79, -82, -90, -94, -93, -91};
        for (uint8_t index = 0U; index < count; ++index) {
            values[index] = energy[index] + (int16_t)((demo_phase + index) % 3U) - 1;
        }
        TRY_DRAW(text(16U, 99U, "ILLUSTRATIVE 802.15.4 ENERGY SCAN", GUIDE_MUTED, 1U));
        TRY_DRAW(text(16U, 118U, "ENERGY (dBm): -100 TO -30", GUIDE_MUTED, 1U));
    }
    TRY_DRAW(cerberus_ui_widget_draw_bar_graph((cerberus_ui_rect_t){30U, 143U, 260U, 142U}, values, NULL, count, -100, -30, accent, &theme));
    TRY_DRAW(text(16U, 298U, demo_panel == 0U ? "NETWORK A       B             C" : demo_panel == 1U ? "OLDER <----------------------> NEWER" : "CHANNEL 11 <--------------------> 26", GUIDE_MUTED, 1U));
    TRY_DRAW(paragraph((cerberus_ui_rect_t){16U, 324U, 288U, 76U}, notes[demo_panel], GUIDE_TEXT, 2U));
    TRY_DRAW(text(16U, 406U, "DEMO: nothing is logged or learned from samples.", GUIDE_AMBER, 1U));
    TRY_DRAW(button((cerberus_ui_rect_t){16U, 424U, 88U, 44U}, "Home", true));
    TRY_DRAW(button((cerberus_ui_rect_t){116U, 424U, 88U, 44U}, "Previous", true));
    return button((cerberus_ui_rect_t){216U, 424U, 88U, 44U}, "Next", true);
}

esp_err_t sentinel_guide_draw(void)
{
    TRY_DRAW(cerberus_display_fill_rect(0U, 0U, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, GUIDE_BG));
    if (view == VIEW_LIST) { return draw_list(); }
    if (view == VIEW_DEMO) { return draw_demo(); }
    return draw_lesson();
}

sentinel_guide_action_t sentinel_guide_handle_touch(uint16_t x, uint16_t y)
{
    if (x >= CERBERUS_LCD_WIDTH || y >= CERBERUS_LCD_HEIGHT) { return SENTINEL_GUIDE_NONE; }
    const bool left = inside(x, y, (cerberus_ui_rect_t){16U, 424U, 88U, 44U});
    const bool middle = inside(x, y, (cerberus_ui_rect_t){116U, 424U, 88U, 44U});
    const bool right = inside(x, y, (cerberus_ui_rect_t){216U, 424U, 88U, 44U});
    if (view == VIEW_LIST) {
        if (left) { return SENTINEL_GUIDE_HOME; }
        if (inside(x, y, (cerberus_ui_rect_t){116U, 424U, 188U, 44U})) {
            sentinel_guide_open_demo();
            return SENTINEL_GUIDE_REDRAW;
        }
        for (uint8_t index = 0U; index < SENTINEL_GUIDE_LESSON_COUNT; ++index) {
            if (inside(x, y, (cerberus_ui_rect_t){16U, 92U + index * 48U, 288U, 44U})) {
                sentinel_guide_open_lesson(index);
                return SENTINEL_GUIDE_REDRAW;
            }
        }
    } else if (view == VIEW_DEMO) {
        if (left) { return SENTINEL_GUIDE_HOME; }
        if (middle || right) {
            demo_panel = (demo_panel + (middle ? 2U : 1U)) % 3U;
            return SENTINEL_GUIDE_REDRAW;
        }
    } else {
        const guide_lesson_t *lesson = &lessons[active_lesson];
        if (left) { sentinel_guide_open(); return SENTINEL_GUIDE_REDRAW; }
        if (middle && lesson_step == 4U) { return SENTINEL_GUIDE_HOME; }
        if (middle && lesson_step > 0U) { --lesson_step; return SENTINEL_GUIDE_REDRAW; }
        if (lesson_step == 1U && inside(x, y, (cerberus_ui_rect_t){16U, 354U, 288U, 48U})) {
            return lesson->tool;
        }
        if (lesson_step == 3U) {
            for (uint8_t answer = 0U; answer < 2U; ++answer) {
                if (inside(x, y, (cerberus_ui_rect_t){16U, 216U + answer * 66U, 288U, 56U})) {
                    selected_answer = (int8_t)answer;
                    return SENTINEL_GUIDE_REDRAW;
                }
            }
        }
        if (right) {
            if (lesson_step < 3U) { ++lesson_step; return SENTINEL_GUIDE_REDRAW; }
            if (lesson_step == 3U && selected_answer == (int8_t)lesson->correct_answer) {
                completed_mask |= 1U << active_lesson;
                if (completed_mask != persisted_mask || progress_error != ESP_OK) { (void)save_progress(); }
                lesson_step = 4U;
                return SENTINEL_GUIDE_REDRAW;
            }
            if (lesson_step == 4U) {
                if (completed_mask != persisted_mask || progress_error != ESP_OK) { (void)save_progress(); }
                else { sentinel_guide_open_lesson(active_lesson); }
                return SENTINEL_GUIDE_REDRAW;
            }
        }
    }
    return SENTINEL_GUIDE_NONE;
}

bool sentinel_guide_tick(uint32_t now_ms)
{
    if (view != VIEW_DEMO) { return false; }
    if (!demo_clock_started) {
        demo_clock_started = true;
        demo_last_ms = now_ms;
        return false;
    }
    if ((uint32_t)(now_ms - demo_last_ms) < GUIDE_DEMO_INTERVAL_MS) { return false; }
    demo_last_ms = now_ms;
    demo_phase = (demo_phase + 1U) % 16U;
    return true;
}
