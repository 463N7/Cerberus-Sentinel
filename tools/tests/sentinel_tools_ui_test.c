#include "sentinel_tools.h"
#include "display.h"
#include "keyboard.h"
#include "qr_renderer.h"
#include "storage.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ESP_ERROR_CHECK(call) assert((call) == ESP_OK)
#define ESP_RETURN_ON_ERROR(call, ...) do { const esp_err_t e_ = (call); if (e_ != ESP_OK) return e_; } while (0)
#define ESP_LOGW(...) ((void)0)
#define UI_BACK_Y 420U
#define UI_QR_CUSTOM_CAPACITY 181U
#define UI_QR_WIFI_SSID_CAPACITY 33U
#define UI_QR_WIFI_PASSWORD_CAPACITY 64U

/* ACTUAL_ENUMS */

static const cerberus_ui_widget_theme_t colors = {0x0062, 0x08A4, 0x3F1B, 0xE77E, 0x84B5};
#define UI_BACKGROUND colors.background
#define UI_WIDGET_THEME (&colors)
static ui_page_t current_page, tool_parent_page;
static sentinel_tools_group_t tools_group;
static bool learning_tool_active, ui_capability_blocked;
static const char *ui_capability_blocked_reason;
static unsigned paints, home_draws, child_opens, detail_draws, benchmark_calls, qr_exports;
static char header_title[32];
static esp_err_t draw_tools_menu(void);
static esp_err_t draw_main_menu(void) { ++home_draws; return ESP_OK; }
static esp_err_t draw_current_page(void) { assert(current_page == UI_PAGE_TOOLS); return draw_tools_menu(); }
static esp_err_t draw_detail_page(void) { ++detail_draws; return ESP_OK; }
static esp_err_t draw_qr_input(void) { ++detail_draws; return ESP_OK; }
static void present_ui_frame(void) {}
static esp_err_t draw_header(const char *title) { snprintf(header_title, sizeof(header_title), "%s", title); return ESP_OK; }
static void open_tool_page(ui_page_t page) { ++child_opens; current_page = page; }

esp_err_t cerberus_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{ (void)x; (void)y; (void)w; (void)h; (void)color; ++paints; return ESP_OK; }
esp_err_t cerberus_display_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg, uint8_t scale)
{ (void)x; (void)y; (void)text; (void)fg; (void)bg; (void)scale; ++paints; return ESP_OK; }
esp_err_t cerberus_ui_widget_draw_button(cerberus_ui_rect_t bounds, const char *label, uint8_t scale,
    bool enabled, const cerberus_ui_widget_theme_t *theme)
{ (void)bounds; (void)label; (void)scale; (void)enabled; (void)theme; ++paints; return ESP_OK; }

static ui_qr_mode_t qr_mode;
static ui_qr_wifi_security_t qr_wifi_security;
static cerberus_keyboard_t qr_keyboard;
static cerberus_keyboard_action_t next_key_action;
static char qr_payload[320], qr_wifi_ssid[33], qr_wifi_password[64];
static bool qr_custom_is_url, qr_save_checked;
static esp_err_t qr_save_error;
static uint8_t qr_save_modules[CERBERUS_QR_MAX_BITMAP_BYTES];
static char qr_save_file_name[CERBERUS_STORAGE_NAME_MAX];
static bool storage_safely_removed, storage_checked, log_checked;
static esp_err_t storage_last_error;
static cerberus_storage_status_t storage_status;
static size_t file_entry_count;

esp_err_t cerberus_keyboard_begin(cerberus_keyboard_t *keyboard, char *buffer, size_t capacity, const char *initial)
{
    *keyboard = (cerberus_keyboard_t){.buffer = buffer, .capacity = capacity};
    snprintf(buffer, capacity, "%s", initial);
    return ESP_OK;
}
void cerberus_keyboard_set_masked(cerberus_keyboard_t *keyboard, bool masked) { keyboard->masked = masked; }
esp_err_t cerberus_keyboard_handle_tap(cerberus_keyboard_t *keyboard, uint16_t x, uint16_t y,
    cerberus_keyboard_action_t *action)
{ (void)keyboard; (void)x; (void)y; *action = next_key_action; return ESP_OK; }
esp_err_t cerberus_qr_encode_text(const char *text, uint8_t *modules, size_t capacity, uint16_t *count)
{ assert(text[0] != '\0'); memset(modules, 1, capacity); *count = 21; return ESP_OK; }
esp_err_t cerberus_storage_write_qr_bmp(const uint8_t *modules, uint16_t count, char *name, size_t capacity)
{ assert(modules[0] == 1 && count == 21); ++qr_exports; snprintf(name, capacity, "test.bmp"); return ESP_OK; }
esp_err_t cerberus_storage_get_status(cerberus_storage_status_t *status)
{ *status = (cerberus_storage_status_t){.mounted = true}; return ESP_OK; }
esp_err_t cerberus_storage_log_event(cerberus_log_level_t level, const char *component, const char *event, const char *detail)
{ (void)level; (void)component; (void)event; (void)detail; return ESP_OK; }

static ui_self_test_mode_t self_test_mode;
static bool touch_test_has_sample;
static uint16_t touch_test_last_x, touch_test_last_y, touch_test_min_x, touch_test_max_x, touch_test_min_y, touch_test_max_y;
static uint8_t touch_test_corner_mask;
static bool storage_benchmark_running, storage_benchmark_checked;
static esp_err_t storage_benchmark_error, next_benchmark_error;
static cerberus_storage_benchmark_result_t storage_benchmark_result;
esp_err_t cerberus_storage_run_benchmark(cerberus_storage_benchmark_result_t *result)
{
    assert(storage_benchmark_running);
    ++benchmark_calls;
    *result = (cerberus_storage_benchmark_result_t){.data_verified = next_benchmark_error == ESP_OK};
    return next_benchmark_error;
}

/* ACTUAL_FUNCTIONS */

static void test_routes(void)
{
    static const struct { sentinel_tools_group_t group; unsigned row; ui_page_t page; } routes[] = {
        {SENTINEL_TOOLS_MAIN, 0, UI_PAGE_SETTINGS}, {SENTINEL_TOOLS_MAIN, 1, UI_PAGE_FILES},
        {SENTINEL_TOOLS_MAIN, 2, UI_PAGE_LOGS}, {SENTINEL_TOOLS_MAIN, 3, UI_PAGE_WEB},
        {SENTINEL_TOOLS_CARE, 0, UI_PAGE_DASHBOARD}, {SENTINEL_TOOLS_CARE, 1, UI_PAGE_SELF_TEST},
        {SENTINEL_TOOLS_CARE, 2, UI_PAGE_USB}, {SENTINEL_TOOLS_EXTRAS, 0, UI_PAGE_QR},
        {SENTINEL_TOOLS_EXTRAS, 1, UI_PAGE_SCRIPTS},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        tools_group = SENTINEL_TOOLS_EXTRAS;
        learning_tool_active = true;
        open_tools_menu();
        assert(current_page == UI_PAGE_TOOLS && tools_group == SENTINEL_TOOLS_MAIN && !learning_tool_active);
        if (routes[i].group != SENTINEL_TOOLS_MAIN) {
            handle_tools_tap(100, (uint16_t)(80U + (routes[i].group == SENTINEL_TOOLS_CARE ? 4U : 5U) * 54U));
        }
        assert(tools_group == routes[i].group);
        unsigned before = child_opens;
        handle_tools_tap(100, (uint16_t)(80U + routes[i].row * 54U));
        assert(child_opens == before + 1 && current_page == routes[i].page);
        assert(tool_parent_page == UI_PAGE_TOOLS && !learning_tool_active);
        ui_capability_blocked = true;
        ui_capability_blocked_reason = "previous screen";
        return_to_launcher();
        assert(current_page == UI_PAGE_TOOLS && tools_group == routes[i].group);
        assert(!ui_capability_blocked && ui_capability_blocked_reason == NULL);
        assert(strcmp(header_title, tools_group_title()) == 0);
        if (routes[i].group != SENTINEL_TOOLS_MAIN) {
            handle_tools_tap(160, 430);
            assert(current_page == UI_PAGE_TOOLS && tools_group == SENTINEL_TOOLS_MAIN);
        }
        handle_tools_tap(160, 430);
        assert(current_page == UI_PAGE_MAIN && tool_parent_page == UI_PAGE_MAIN);
    }
    assert(home_draws == 9 && child_opens == 9 && benchmark_calls == 0 && qr_exports == 0);

    static const uint16_t inert[][2] = {{0,0}, {11,80}, {308,80}, {100,69}, {100,118},
        {100,123}, {100,394}, {100,419}, {100,464}, {320,80}, {UINT16_MAX,UINT16_MAX}};
    for (unsigned group = 0; group < 3; ++group) {
        current_page = UI_PAGE_TOOLS;
        tools_group = (sentinel_tools_group_t)group;
        for (size_t i = 0; i < sizeof(inert) / sizeof(inert[0]); ++i) {
            unsigned before = paints;
            handle_tools_tap(inert[i][0], inert[i][1]);
            assert(current_page == UI_PAGE_TOOLS && tools_group == group && paints == before && child_opens == 9);
        }
        if (group != SENTINEL_TOOLS_MAIN) {
            handle_tools_tap(100, 300); /* Notes are not action rows. */
            assert(tools_group == group && child_opens == 9);
        }
    }
    tools_group = (sentinel_tools_group_t)99;
    handle_tools_tap(160, 430);
    assert(tools_group == 99 && current_page == UI_PAGE_TOOLS);
    open_tools_menu();
    assert(tools_group == SENTINEL_TOOLS_MAIN);
}

static void test_qr_routes(void)
{
    static const uint16_t inert[][2] = {{23,100}, {296,100}, {100,93}, {100,148},
        {100,165}, {100,220}, {100,237}, {100,292}, {100,340}};
    for (size_t i = 0; i < sizeof(inert) / sizeof(inert[0]); ++i) {
        qr_mode = UI_QR_MODE_MENU;
        unsigned before = detail_draws;
        handle_qr_tap(inert[i][0], inert[i][1]);
        assert(qr_mode == UI_QR_MODE_MENU && detail_draws == before && qr_exports == 0);
    }
    strcpy(qr_wifi_ssid, "old-network"); strcpy(qr_wifi_password, "old-password"); strcpy(qr_payload, "old-payload");
    qr_save_checked = true;
    handle_qr_tap(24, 94);
    assert(qr_mode == UI_QR_MODE_WIFI_SECURITY && !qr_save_checked);
    assert(qr_wifi_ssid[0] == '\0' && qr_wifi_password[0] == '\0' && qr_payload[0] == '\0');
    handle_qr_tap(100, 150);
    assert(qr_mode == UI_QR_MODE_WIFI_SSID_INPUT && qr_wifi_security == UI_QR_WIFI_SECURITY_WPA);
    strcpy(qr_wifi_ssid, "Learning;Lab");
    next_key_action = CERBERUS_KEYBOARD_ACTION_SUBMIT;
    handle_qr_tap(100, 100);
    assert(qr_mode == UI_QR_MODE_WIFI_PASSWORD_INPUT && qr_keyboard.masked);
    strcpy(qr_wifi_password, "Example:Password");
    handle_qr_tap(100, 100);
    assert(qr_mode == UI_QR_MODE_WIFI_CODE && qr_mode_has_code(qr_mode));
    assert(strcmp(qr_payload, "WIFI:T:WPA;S:Learning\\;Lab;P:Example\\:Password;H:false;;") == 0);
    assert(qr_exports == 0); /* Entering a QR screen never exports it. */

    qr_mode = UI_QR_MODE_MENU;
    handle_qr_tap(295, 219);
    assert(qr_mode == UI_QR_MODE_INPUT && !qr_custom_is_url && qr_payload[0] == '\0');
    strcpy(qr_payload, "Learning message");
    handle_qr_tap(100, 100);
    assert(qr_mode == UI_QR_MODE_CUSTOM && prepare_qr_payload(qr_mode) == ESP_OK);

    qr_mode = UI_QR_MODE_MENU;
    handle_qr_tap(24, 238);
    assert(qr_mode == UI_QR_MODE_INPUT && qr_custom_is_url && strcmp(qr_payload, "https://") == 0);
    strcpy(qr_payload, "https://example.org/learn");
    handle_qr_tap(100, 100);
    assert(qr_mode == UI_QR_MODE_CUSTOM && prepare_qr_payload(qr_mode) == ESP_OK);
    assert(prepare_qr_payload(UI_QR_MODE_MENU) == ESP_ERR_INVALID_STATE);
    assert(prepare_qr_payload((ui_qr_mode_t)99) == ESP_ERR_INVALID_STATE);
    assert(strcmp(qr_payload, "https://example.org/learn") == 0); /* No legacy-link fallback. */
    strcpy(qr_payload, "not-a-url");
    handle_qr_tap(100, 380);
    assert(qr_save_error == ESP_ERR_INVALID_ARG && qr_exports == 0);
    strcpy(qr_payload, "https://example.org/learn");
    handle_qr_tap(100, 380);
    assert(qr_save_error == ESP_OK && qr_exports == 1);
    for (size_t i = 0; i < sizeof(qr_save_modules); ++i) assert(qr_save_modules[i] == 0);
}

static void test_hardware_actions(void)
{
    static const uint16_t inert[][2] = {{23,180}, {296,180}, {100,157}, {100,206},
        {100,349}, {100,408}, {23,380}, {296,380}, {100,300}};
    self_test_mode = UI_SELF_TEST_MODE_SUMMARY;
    for (size_t i = 0; i < sizeof(inert) / sizeof(inert[0]); ++i) {
        unsigned before = detail_draws;
        handle_self_test_tap(inert[i][0], inert[i][1]);
        assert(self_test_mode == UI_SELF_TEST_MODE_SUMMARY && detail_draws == before && benchmark_calls == 0);
    }
    handle_self_test_tap(24, 158);
    assert(self_test_mode == UI_SELF_TEST_MODE_TOUCH && !touch_test_has_sample && benchmark_calls == 0);
    handle_self_test_tap(10, 60);
    handle_self_test_tap(310, 60);
    handle_self_test_tap(10, 470);
    handle_self_test_tap(310, 470);
    assert(touch_test_corner_mask == 15 && touch_test_has_sample && benchmark_calls == 0);
    handle_self_test_tap(160, 430);
    assert(self_test_mode == UI_SELF_TEST_MODE_SUMMARY);
    storage_benchmark_running = true;
    handle_self_test_tap(24, 350);
    assert(benchmark_calls == 0);
    storage_benchmark_running = false;
    next_benchmark_error = ESP_FAIL;
    handle_self_test_tap(24, 350);
    assert(benchmark_calls == 1 && storage_benchmark_checked && !storage_benchmark_running);
    assert(storage_benchmark_error == ESP_FAIL && storage_last_error == ESP_FAIL);
    next_benchmark_error = ESP_OK;
    handle_self_test_tap(295, 407);
    assert(benchmark_calls == 2 && storage_benchmark_result.data_verified && storage_last_error == ESP_OK);
}

int main(void)
{
    test_routes();
    test_qr_routes();
    test_hardware_actions();
    puts("PASS: nine Tools routes and group returns, inert gaps, QR flows, explicit hardware-test boundaries");
    return 0;
}
