#include "sentinel_dashboard.h"

#include "display.h"
#include "cerberus_protocol.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define BG       RGB(7, 12, 21)
#define PANEL    RGB(13, 23, 36)
#define BORDER   RGB(35, 53, 72)
#define WHITE    RGB(225, 237, 246)
#define MUTED    RGB(129, 151, 170)
#define CYAN     RGB(60, 224, 218)
#define VIOLET   RGB(171, 154, 255)
#define AMBER    RGB(255, 187, 81)
#define STEEL    RGB(91, 119, 142)
#define ARMOR    RGB(43, 66, 86)
#define SHADOW   RGB(20, 36, 52)
#define DARK     RGB(9, 18, 29)
#define TRY(call) do { esp_err_t result_ = (call); if (result_ != ESP_OK) return result_; } while (0)

typedef struct { int16_t x, y; } point_t;
typedef struct {
    uint16_t x, y, width, height;
    sentinel_dashboard_action_t action;
} target_t;

/* Shared by rendering and hit testing: no invisible or overlapping buttons. */
static const target_t targets[] = {
    { 12, 246, 94, 98, SENTINEL_DASHBOARD_WIFI },
    { 113, 246, 94, 98, SENTINEL_DASHBOARD_BLE },
    { 214, 246, 94, 98, SENTINEL_DASHBOARD_RADIO },
    { 12, 356, 144, 52, SENTINEL_DASHBOARD_SCAN },
    { 12, 418, 144, 46, SENTINEL_DASHBOARD_WATCH },
    { 164, 418, 144, 46, SENTINEL_DASHBOARD_TOOLS },
    { 164, 356, 144, 52, SENTINEL_DASHBOARD_ATTACK },
};

/* 4,736 bytes; no full framebuffer, heap allocation or external RAM required. */
#define STRIPE_WIDTH 296
#define STRIPE_ROWS 8
static uint16_t stripe[STRIPE_WIDTH * STRIPE_ROWS];
static int clip_x, clip_y, clip_width, clip_height;

static void pixel(int x, int y, uint16_t color)
{
    if (x >= clip_x && x < clip_x + clip_width && y >= clip_y && y < clip_y + clip_height) {
        stripe[(y - clip_y) * clip_width + x - clip_x] = color;
    }
}

static void box(int x, int y, int width, int height, uint16_t color)
{
    int left = x > clip_x ? x : clip_x;
    int top = y > clip_y ? y : clip_y;
    int right = x + width < clip_x + clip_width ? x + width : clip_x + clip_width;
    int bottom = y + height < clip_y + clip_height ? y + height : clip_y + clip_height;
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            stripe[(row - clip_y) * clip_width + column - clip_x] = color;
        }
    }
}

static void line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

/* Polygon interiors use pixel centers and half-open edges for stable seams. */
static void polygon(const point_t *points, size_t count, uint16_t color)
{
    for (int y = clip_y; y < clip_y + clip_height; ++y) {
        int intersections[16];
        size_t used = 0;
        for (size_t i = 0, j = count - 1; i < count; j = i++) {
            int y0 = points[j].y, y1 = points[i].y;
            if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
                if (used < sizeof(intersections) / sizeof(intersections[0])) {
                    intersections[used++] = points[j].x +
                        (y - y0) * (points[i].x - points[j].x) / (y1 - y0);
                }
            }
        }
        for (size_t i = 1; i < used; ++i) {
            int value = intersections[i];
            size_t j = i;
            while (j > 0 && intersections[j - 1] > value) {
                intersections[j] = intersections[j - 1];
                --j;
            }
            intersections[j] = value;
        }
        for (size_t i = 0; i + 1 < used; i += 2) {
            box(intersections[i], y, intersections[i + 1] - intersections[i] + 1, 1, color);
        }
    }
}

#define POLY(color, ...) do { const point_t p_[] = { __VA_ARGS__ }; polygon(p_, sizeof(p_) / sizeof(p_[0]), (color)); } while (0)

static void ellipse(int cx, int cy, int rx, int ry, uint16_t color)
{
    /* Integer implicit ellipse; only inspect rows covered by the current stripe. */
    for (int y = clip_y; y < clip_y + clip_height; ++y) {
        int yy = y - cy;
        if (yy < -ry || yy > ry) continue;
        int previous = 0;
        for (int x = 0; x <= rx; ++x) {
            int64_t distance = (int64_t)x * x * ry * ry + (int64_t)yy * yy * rx * rx;
            if (distance <= (int64_t)rx * rx * ry * ry) previous = x;
            else break;
        }
        pixel(cx - previous, y, color);
        pixel(cx + previous, y, color);
    }
}

static void side_head(bool right, uint16_t accent, bool blink)
{
    /* One profile is mirrored to make three distinct, readable canine heads. */
    const point_t silhouette[] = {
        {109, 176}, {69, 164}, {49, 149}, {28, 142}, {26, 126}, {49, 116},
        {61, 99}, {59, 78}, {81, 90}, {104, 82}, {105, 106}, {121, 128},
    };
    point_t p[sizeof(silhouette) / sizeof(silhouette[0])];
    for (size_t i = 0; i < sizeof(p) / sizeof(p[0]); ++i) {
        p[i] = silhouette[i];
        if (right) p[i].x = 320 - p[i].x;
    }
    polygon(p, sizeof(p) / sizeof(p[0]), STEEL);

    /* Use a local mirror for all armor plates and circuit details. */
#define SX(x) (right ? 320 - (x) : (x))
#define SLINE(x0, y0, x1, y1, c) line(SX(x0), y0, SX(x1), y1, c)
#define SPOLY(c, a, b, d, e, f, g, h, i) POLY(c, {SX(a), b}, {SX(d), e}, {SX(f), g}, {SX(h), i})
    SPOLY(ARMOR, 63, 86, 78, 96, 65, 108, 63, 98);
    SPOLY(SHADOW, 99, 89, 100, 112, 86, 103, 88, 95);
    SPOLY(ARMOR, 65, 109, 87, 98, 113, 129, 76, 134);
    SPOLY(SHADOW, 79, 136, 114, 131, 106, 167, 76, 155);
    SPOLY(ARMOR, 49, 119, 74, 124, 61, 140, 32, 137);
    SPOLY(DARK, 29, 127, 43, 125, 43, 132, 29, 137);
    SPOLY(DARK, 49, 144, 70, 144, 82, 159, 58, 150);
    SLINE(34, 141, 60, 146, STEEL);
    SLINE(57, 147, 60, 150, WHITE);
    SLINE(63, 149, 66, 153, WHITE);
    SLINE(74, 142, 81, 157, STEEL);
    SLINE(82, 160, 104, 169, BORDER);
    SLINE(67, 92, 72, 97, accent);
    SLINE(94, 94, 94, 100, accent);
    SPOLY(DARK, 54, 118, 66, 110, 79, 112, 69, 123);
    if (blink) {
        SLINE(58, 119, 73, 115, accent);
    } else {
        SPOLY(accent, 57, 116, 69, 112, 76, 114, 66, 120);
        SLINE(58, 116, 69, 113, WHITE);
    }
    SLINE(84, 120, 91, 127, STEEL);
    SLINE(91, 127, 100, 127, STEEL);
    SLINE(84, 141, 88, 145, accent);
    SLINE(88, 145, 100, 145, accent);
    SLINE(91, 152, 101, 152, STEEL);
    SLINE(85, 150, 86, 156, BORDER);
    SLINE(85, 156, 96, 161, BORDER);
#undef SPOLY
#undef SLINE
#undef SX
}

static void guardian_scene(uint32_t frame, bool online)
{
    /* Static instrumentation is decorative. Only real status drives the lights. */
    const bool blink = frame % 13U == 12U;
    const uint16_t core = online ? (frame % 4U == 0U ? WHITE : CYAN) : MUTED;
    ellipse(160, 140, 117, 62, RGB(21, 40, 55));
    ellipse(160, 140, 109, 57, RGB(15, 30, 44));
    line(13, 79, 13, 68, BORDER); line(13, 68, 33, 68, BORDER);
    line(306, 79, 306, 68, BORDER); line(306, 68, 286, 68, BORDER);
    line(13, 196, 13, 208, BORDER); line(13, 208, 33, 208, BORDER);
    line(306, 196, 306, 208, BORDER); line(306, 208, 286, 208, BORDER);
    for (int x = 28; x < 300; x += 12) {
        box(x, 203, 2, 1, BORDER);
    }
    line(17, 162, 34, 162, BORDER); line(34, 162, 48, 177, BORDER);
    line(48, 177, 63, 177, BORDER); box(62, 175, 3, 3, CYAN);
    line(303, 162, 286, 162, BORDER); line(286, 162, 272, 177, BORDER);
    line(272, 177, 257, 177, BORDER); box(255, 175, 3, 3, AMBER);
    line(29, 99, 38, 99, BORDER); line(282, 99, 291, 99, BORDER);

    /* Shoulder armor connects all three necks to a single guardian. */
    POLY(SHADOW, {72, 178}, {106, 156}, {141, 155}, {160, 168}, {178, 155}, {216, 155}, {248, 178}, {231, 204}, {88, 204});
    POLY(ARMOR, {76, 179}, {110, 163}, {133, 177}, {115, 201}, {90, 201});
    POLY(ARMOR, {244, 179}, {210, 163}, {187, 177}, {205, 201}, {230, 201});
    line(81, 180, 107, 168, STEEL); line(239, 180, 213, 168, STEEL);
    line(89, 190, 113, 178, CYAN); line(231, 190, 207, 178, AMBER);
    line(97, 194, 118, 183, BORDER); line(223, 194, 202, 183, BORDER);

    side_head(false, CYAN, blink);
    side_head(true, AMBER, blink);

    /* The forward-facing head has tall ears, brow plates and a long muzzle. */
    POLY(STEEL, {119, 118}, {124, 86}, {126, 66}, {147, 84}, {160, 81}, {173, 84}, {194, 66}, {196, 86}, {201, 118}, {190, 145}, {177, 170}, {143, 170}, {130, 145});
    POLY(SHADOW, {128, 74}, {143, 88}, {135, 101}, {128, 94});
    POLY(SHADOW, {192, 74}, {177, 88}, {185, 101}, {192, 94});
    line(131, 81, 136, 88, VIOLET); line(189, 81, 184, 88, VIOLET);
    POLY(ARMOR, {143, 91}, {160, 85}, {177, 91}, {187, 108}, {179, 125}, {160, 130}, {141, 125}, {133, 108});
    POLY(SHADOW, {124, 116}, {135, 103}, {143, 124}, {140, 147}, {133, 143});
    POLY(SHADOW, {196, 116}, {185, 103}, {177, 124}, {180, 147}, {187, 143});
    line(128, 114, 132, 133, STEEL); line(192, 114, 188, 133, STEEL);
    line(131, 134, 137, 141, VIOLET); line(189, 134, 183, 141, VIOLET);
    POLY(DARK, {133, 107}, {153, 114}, {151, 123}, {137, 118});
    POLY(DARK, {187, 107}, {167, 114}, {169, 123}, {183, 118});
    if (blink) {
        line(137, 115, 151, 119, VIOLET); line(183, 115, 169, 119, VIOLET);
    } else {
        POLY(VIOLET, {136, 111}, {151, 116}, {150, 120}, {139, 116});
        POLY(VIOLET, {184, 111}, {169, 116}, {170, 120}, {181, 116});
        line(137, 111, 150, 116, WHITE); line(183, 111, 170, 116, WHITE);
    }
    POLY(STEEL, {156, 110}, {164, 110}, {176, 144}, {170, 161}, {150, 161}, {144, 144});
    POLY(ARMOR, {160, 112}, {164, 112}, {173, 144}, {163, 146});
    POLY(DARK, {151, 140}, {169, 140}, {169, 147}, {160, 152}, {151, 147});
    line(154, 140, 165, 140, WHITE);
    line(160, 151, 160, 158, DARK);
    line(150, 157, 159, 160, DARK); line(161, 160, 170, 157, DARK);
    POLY(DARK, {142, 150}, {148, 160}, {172, 160}, {178, 150}, {175, 166}, {145, 166});
    POLY(WHITE, {145, 154}, {149, 158}, {149, 164});
    POLY(WHITE, {175, 154}, {171, 158}, {171, 164});
    line(154, 164, 166, 164, STEEL);
    line(160, 91, 160, 104, VIOLET);
    line(151, 97, 156, 99, BORDER); line(169, 97, 164, 99, BORDER);

    /* Shared chest crest: three small illuminated cores, no pseudo telemetry. */
    POLY(STEEL, {135, 170}, {185, 170}, {185, 187}, {160, 209}, {135, 187});
    POLY(DARK, {139, 174}, {181, 174}, {180, 185}, {160, 203}, {140, 185});
    line(142, 180, 150, 180, CYAN);
    line(170, 180, 178, 180, AMBER);
    POLY(core, {156, 177}, {164, 177}, {169, 184}, {160, 193}, {151, 184});
    POLY(SHADOW, {158, 181}, {162, 181}, {164, 184}, {160, 188}, {156, 184});
    line(146, 190, 155, 198, BORDER); line(174, 190, 165, 198, BORDER);
}

static esp_err_t guardian_region(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                 uint32_t frame, bool online)
{
    if (width == 0 || width > STRIPE_WIDTH || x + width > CERBERUS_LCD_WIDTH ||
        y + height > CERBERUS_LCD_HEIGHT) return ESP_ERR_INVALID_ARG;
    for (uint16_t row = 0; row < height; row += STRIPE_ROWS) {
        uint16_t rows = height - row > STRIPE_ROWS ? STRIPE_ROWS : height - row;
        clip_x = x;
        clip_y = y + row;
        clip_width = width;
        clip_height = rows;
        for (size_t i = 0; i < (size_t)width * rows; ++i) stripe[i] = BG;
        guardian_scene(frame, online);
        TRY(cerberus_display_draw_rgb565_bitmap(x, y + row, width, rows, stripe));
    }
    return ESP_OK;
}

esp_err_t sentinel_guardian_draw(uint32_t frame, bool c5_online)
{
    return guardian_region(12, 64, 296, 148, frame, c5_online);
}

static esp_err_t text_center(uint16_t x, uint16_t y, uint16_t width, const char *text,
                             uint16_t color, uint16_t background, uint8_t scale)
{
    size_t length = strlen(text) * 6U * scale;
    return cerberus_display_draw_text(x + (length < width ? (width - length) / 2U : 0U),
                                      y, text, color, background, scale);
}

static esp_err_t panel(const target_t *target, uint16_t accent)
{
    TRY(cerberus_display_fill_rect(target->x, target->y, target->width, target->height, BORDER));
    TRY(cerberus_display_fill_rect(target->x + 1, target->y + 1,
                                  target->width - 2, target->height - 2, PANEL));
    return cerberus_display_fill_rect(target->x + 1, target->y + 1, target->width - 2, 2, accent);
}

static esp_err_t observation_counts(const sentinel_dashboard_status_t *status)
{
    char label[24];
    const bool observed[] = { status->wifi_observed, status->ble_observed };
    const unsigned counts[] = { status->wifi_count, status->ble_count };
    for (size_t i = 0; i < 2; ++i) {
        if (observed[i]) snprintf(label, sizeof(label), "%u SEEN", counts[i]);
        else snprintf(label, sizeof(label), "EXPLORE >");
        TRY(cerberus_display_fill_rect(targets[i].x + 3, 305, 88, 9, PANEL));
        TRY(text_center(targets[i].x, 305, targets[i].width, label, WHITE, PANEL, 1));
    }
    TRY(cerberus_display_fill_rect(217, 305, 88, 9, PANEL));
    TRY(text_center(214, 305, 94, "EXPLORE >", WHITE, PANEL, 1));
    return ESP_OK;
}

static esp_err_t status_line(const sentinel_dashboard_status_t *status)
{
    const char *link = status->c5_online ? "C5 ONLINE" : "C5 OFFLINE";
    TRY(cerberus_display_fill_rect(230, 17, 78, 16, PANEL));
    TRY(cerberus_display_fill_rect(235, 22, 4, 4, status->c5_online ? CYAN : AMBER));
    TRY(cerberus_display_draw_text(244, 22, link, status->c5_online ? CYAN : AMBER, PANEL, 1));
    TRY(cerberus_display_fill_rect(12, 229, 296, 8, BG));
    const char *state = !status->touch_online ? "TOUCH UNAVAILABLE / USE USB" :
        !status->c5_online ? "CONNECTING TO THE ONBOARD RADIO" :
        status->radio_state & CERBERUS_C5_RADIO_WIFI_STATIONS ? "WI-FI STATION CAPTURE" :
        status->radio_state & CERBERUS_C5_RADIO_TEST ? "RADIO TEST RUNNING" :
        status->radio_state & CERBERUS_C5_RADIO_WIFI_DEFENSE_SURVEY ? "WI-FI SURVEY RUNNING" :
        status->radio_state & CERBERUS_C5_RADIO_WIFI_SCAN ? "WI-FI SCAN RUNNING" :
        status->radio_state & CERBERUS_C5_RADIO_BLE_SCAN ? "BLUETOOTH SCAN RUNNING" :
        status->radio_state & CERBERUS_C5_RADIO_IEEE802154_SCAN ? "802.15.4 SCAN RUNNING" :
        status->radio_state & CERBERUS_C5_RADIO_BLE_CONNECTION ? "BLUETOOTH DEVICE CONNECTED" :
        status->radio_state & CERBERUS_C5_RADIO_WIFI_CONNECTED ? "WI-FI NETWORK CONNECTED" :
        "CHOOSE A TOOL TO START EXPLORING";
    TRY(text_center(12, 229, 296, state, MUTED, BG, 1));
    TRY(cerberus_display_fill_rect(206, 42, 102, 7, BG));
    return cerberus_display_draw_text(206, 42,
        status->privacy_masked ? "PRIVACY: MASKED" : "PRIVACY: VISIBLE", MUTED, BG, 1);
}

esp_err_t sentinel_dashboard_draw(const sentinel_dashboard_status_t *status, uint32_t frame)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    TRY(cerberus_display_fill_rect(0, 0, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, BG));
    TRY(cerberus_display_draw_text(12, 13, "CERBERUS", WHITE, BG, 3));
    TRY(cerberus_display_draw_text(13, 42, "F I E L D  K I T", CYAN, BG, 1));
    TRY(cerberus_display_fill_rect(12, 57, 296, 1, BORDER));
    TRY(cerberus_display_fill_rect(12, 57, 52, 1, CYAN));
    TRY(sentinel_guardian_draw(frame, status->c5_online));
    TRY(text_center(12, 216, 296, "OBSERVE. UNDERSTAND. PROTECT.", WHITE, BG, 1));

    const uint16_t accents[] = { CYAN, VIOLET, AMBER };
    const char *head_names[] = { "01 / WATCH", "02 / SCOUT", "03 / SENSE" };
    const char *tools[] = { "WI-FI", "BLE", "RADIO" };
    const char *subtitles[] = { "2.4 + 5 GHZ", "BLUETOOTH", "802.15.4" };
    for (size_t i = 0; i < 3; ++i) {
        TRY(panel(&targets[i], accents[i]));
        TRY(text_center(targets[i].x, 259, targets[i].width, head_names[i], accents[i], PANEL, 1));
        TRY(text_center(targets[i].x, 280, targets[i].width, tools[i], WHITE, PANEL, 2));
        TRY(text_center(targets[i].x, 328, targets[i].width, subtitles[i], MUTED, PANEL, 1));
    }

    TRY(panel(&targets[3], VIOLET));
    TRY(text_center(12, 367, 144, "SCAN", WHITE, PANEL, 2));
    TRY(text_center(12, 389, 144, "NETWORK / BLE", MUTED, PANEL, 1));
    TRY(panel(&targets[6], AMBER));
    TRY(text_center(164, 367, 144, "ATTACK", WHITE, PANEL, 2));
    TRY(text_center(164, 389, 144, "WI-FI / BLUETOOTH", MUTED, PANEL, 1));

    TRY(panel(&targets[4], CYAN));
    TRY(text_center(12, 430, 144, "RECON", WHITE, PANEL, 2));
    TRY(text_center(12, 449, 144, "PASSIVE LOOKOUT", MUTED, PANEL, 1));
    TRY(panel(&targets[5], BORDER));
    TRY(text_center(164, 430, 144, "TOOLS", WHITE, PANEL, 2));
    TRY(text_center(164, 449, 144, "FILES / SETTINGS", MUTED, PANEL, 1));

    TRY(status_line(status));
    return observation_counts(status);
}

esp_err_t sentinel_dashboard_animate(const sentinel_dashboard_status_t *status, uint32_t frame)
{
    if (status == NULL) return ESP_ERR_INVALID_ARG;
    /* Restore vector content under the animation; no blinking rectangles. */
    TRY(guardian_region(50, 107, 220, 17, frame, status->c5_online));
    TRY(guardian_region(151, 177, 19, 17, frame, status->c5_online));
    TRY(status_line(status));
    return observation_counts(status);
}

sentinel_dashboard_action_t sentinel_dashboard_hit_test(uint16_t x, uint16_t y)
{
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
        const target_t *target = &targets[i];
        if (x >= target->x && y >= target->y && x < target->x + target->width &&
            y < target->y + target->height) return target->action;
    }
    return SENTINEL_DASHBOARD_NONE;
}
