#include "sentinel_scan_view.h"

#include "display.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define BG      RGB(7, 12, 21)
#define PANEL   RGB(13, 23, 36)
#define BORDER  RGB(35, 53, 72)
#define TEXT    RGB(225, 237, 246)
#define MUTED   RGB(129, 151, 170)
#define CYAN    RGB(60, 224, 218)
#define VIOLET  RGB(171, 154, 255)
#define AMBER   RGB(255, 187, 81)
#define ERROR   RGB(244, 140, 135)
#define TRY(call) do { esp_err_t result_ = (call); if (result_ != ESP_OK) return result_; } while (0)

/* The UI rewrites its strings in place. Cache displayed contents, not pointers. */
typedef struct {
    sentinel_scan_view_t view;
    char subtitle[51], count_label[19], message[51], hint[51];
    char names[4][33], details[4][38], badges[4][34], tools_label[13], sort_label[13];
    char age_label[13];
    uint32_t frame;
    bool valid;
} render_cache_t;
static render_cache_t rendered;

static uint16_t accent(const sentinel_scan_view_t *view)
{
    return view->kind == SENTINEL_VIEW_WIFI ? CYAN : view->kind == SENTINEL_VIEW_BLE ? VIOLET : AMBER;
}

static void safe_text(char *output, size_t capacity, const char *input, size_t maximum)
{
    if (capacity == 0) return;
    if (maximum >= capacity) maximum = capacity - 1;
    size_t count = 0;
    if (input != NULL) {
        while (input[count] != '\0' && count < maximum) {
            unsigned char value = (unsigned char)input[count];
            output[count] = value >= 32U && value <= 126U ? (char)value : '?';
            ++count;
        }
        if (input[count] != '\0' && count >= 3) {
            output[count - 3] = '.';
            output[count - 2] = '.';
            output[count - 1] = '.';
        }
    }
    output[count] = '\0';
}

static esp_err_t text(uint16_t x, uint16_t y, const char *input, size_t max_chars,
                       uint16_t color, uint16_t background, uint8_t scale)
{
    char buffer[52];
    safe_text(buffer, sizeof(buffer), input, max_chars);
    if (buffer[0] == '\0') return ESP_OK;
    return cerberus_display_draw_text(x, y, buffer, color, background, scale);
}

static esp_err_t centered(uint16_t x, uint16_t y, uint16_t width, const char *input,
                           uint16_t color, uint16_t background, uint8_t scale)
{
    char buffer[52];
    safe_text(buffer, sizeof(buffer), input, width / (6U * scale));
    size_t length = strlen(buffer) * 6U * scale;
    if (length == 0) return ESP_OK;
    return cerberus_display_draw_text(x + (uint16_t)((width - length) / 2U), y,
                                      buffer, color, background, scale);
}

static esp_err_t guardian_mark(uint16_t color)
{
    /* Three canine silhouettes in a small mechanical crest. */
    static const uint8_t head[] = {
        0x80, 0x02, 0xC0, 0x06, 0xE0, 0x0E, 0xFC, 0x7E,
        0xFF, 0xFE, 0x7F, 0xFC, 0x3F, 0xF8, 0x38, 0x38,
        0x3B, 0xB8, 0x1F, 0xF0, 0x1C, 0x70, 0x0E, 0xE0,
        0x07, 0xC0, 0x03, 0x80,
    };
    TRY(cerberus_display_draw_mono_bitmap(15, 66, 16, 14, head, BORDER, BG));
    TRY(cerberus_display_draw_mono_bitmap(45, 66, 16, 14, head, BORDER, BG));
    TRY(cerberus_display_draw_mono_bitmap(30, 58, 16, 14, head, color, BG));
    TRY(cerberus_display_fill_rect(27, 84, 21, 1, color));
    return cerberus_display_fill_rect(34, 88, 7, 2, color);
}

static const char *state_label(sentinel_view_state_t state)
{
    switch (state) {
    case SENTINEL_VIEW_LIVE: return "LIVE";
    case SENTINEL_VIEW_PAUSED: return "PAUSED";
    case SENTINEL_VIEW_COMPLETE: return "CAPTURED";
    case SENTINEL_VIEW_ERROR: return "ERROR";
    case SENTINEL_VIEW_OFFLINE: return "OFFLINE";
    default: return "READY";
    }
}

static unsigned energy_count(const sentinel_scan_view_t *view)
{
    unsigned count = 0;
    for (unsigned index = 0; index < 16; ++index) {
        if (view->energy_valid_mask & (1U << index)) ++count;
    }
    return count;
}

static esp_err_t padded_text(uint16_t x, uint16_t y, const char *input, size_t columns)
{
    char buffer[52];
    if (columns >= sizeof(buffer)) return ESP_ERR_INVALID_ARG;
    safe_text(buffer, sizeof(buffer), input, columns);
    size_t length = strlen(buffer);
    memset(buffer + length, ' ', columns - length);
    buffer[columns] = '\0';
    /* One transfer includes the background: no visible erase-then-text step. */
    return cerberus_display_draw_text(x, y, buffer, MUTED, BG, 1);
}

static const char *count_label(const sentinel_scan_view_t *view)
{
    if (view->count_label != NULL && view->count_label[0] != '\0') return view->count_label;
    return view->kind == SENTINEL_VIEW_WIFI ? "NETWORKS SEEN" :
        view->kind == SENTINEL_VIEW_BLE ? "DEVICES SEEN" :
        view->kind == SENTINEL_VIEW_ENERGY ? "CHANNELS READ" : "OBSERVATIONS";
}

static unsigned summary_count(const sentinel_scan_view_t *view)
{
    if(view->selection_mode)return view->selection_count;
    return view->kind == SENTINEL_VIEW_ENERGY ? energy_count(view) : view->total_count;
}

static bool has_age_control(const sentinel_scan_view_t *view)
{
    return (view->kind == SENTINEL_VIEW_BLE || view->kind == SENTINEL_VIEW_WIFI) &&
        !view->selection_mode &&
        view->age_label != NULL && view->age_label[0] != '\0';
}

static esp_err_t summary(const sentinel_scan_view_t *view)
{
    uint16_t color = accent(view);
    uint16_t state_color = view->state == SENTINEL_VIEW_ERROR ? ERROR :
        view->state == SENTINEL_VIEW_OFFLINE ? MUTED : color;
    char buffer[52];
    TRY(cerberus_display_fill_rect(0, 48, 320, 50, BG));
    TRY(guardian_mark(color));
    snprintf(buffer, sizeof(buffer), "%u", summary_count(view));
    TRY(text(75, 56, buffer, 5, TEXT, BG, 3));
    TRY(text(76, 84, count_label(view), 18, MUTED, BG, 1));
    bool age_control = has_age_control(view);
    if (age_control) {
        TRY(cerberus_display_fill_rect(224, 50, 88, 22, PANEL));
        TRY(cerberus_display_fill_rect(224, 50, 2, 22, state_color));
        TRY(centered(228, 58, 80,
            view->kind == SENTINEL_VIEW_BLE && view->sort_label && view->sort_label[0] ?
                view->sort_label : state_label(view->state),
            state_color, PANEL, 1));
        uint16_t age_color = view->state == SENTINEL_VIEW_OFFLINE ? MUTED : color;
        TRY(cerberus_display_fill_rect(224, 76, 88, 21, PANEL));
        TRY(cerberus_display_fill_rect(224, 76, 2, 21, age_color));
        safe_text(buffer, sizeof(buffer), view->age_label, 12);
        TRY(centered(228, 83, 80, buffer, age_color, PANEL, 1));
    } else {
        TRY(cerberus_display_fill_rect(224, 57, 88, 23, PANEL));
        TRY(cerberus_display_fill_rect(224, 57, 2, 23, state_color));
        TRY(centered(228, 65, 80, view->selection_mode?"CLEAR":
            view->kind == SENTINEL_VIEW_BLE && view->sort_label && view->sort_label[0] ?
                view->sort_label : state_label(view->state),
            view->selection_mode&&!view->selection_count?MUTED:state_color, PANEL, 1));
    }
    if (view->page_count > 1 && view->kind != SENTINEL_VIEW_ENERGY) {
        snprintf(buffer, sizeof(buffer), age_control ? "%u/%u" : "%u / %u",
            (unsigned)view->page + 1U, (unsigned)view->page_count);
        /* Keep the page marker outside both the count's five-digit footprint
         * and the split controls. Wi-Fi/BLE inventories retain at most 32 rows. */
        if (age_control) TRY(centered(168, 64, 44, buffer, MUTED, BG, 1));
        else TRY(centered(220, 85, 92, buffer, MUTED, BG, 1));
    }
    return ESP_OK;
}

static esp_err_t progress(const sentinel_scan_view_t *view, uint32_t frame)
{
    uint16_t color = accent(view);
    TRY(cerberus_display_fill_rect(8, 103, 304, 3, BORDER));
    if (view->progress_known) {
        unsigned value = view->progress_percent > 100 ? 100 : view->progress_percent;
        unsigned width = 304U * value / 100U;
        if (width != 0) TRY(cerberus_display_fill_rect(8, 103, (uint16_t)width, 3, color));
    } else if (view->state == SENTINEL_VIEW_LIVE) {
        uint16_t position = (uint16_t)((frame % 16U) * 17U);
        TRY(cerberus_display_fill_rect(8 + position, 103, 32, 3, color));
    }
    return ESP_OK;
}

static const char *subtitle(const sentinel_scan_view_t *view)
{
    if (view->subtitle != NULL && view->subtitle[0] != '\0') return view->subtitle;
    if ((view->state == SENTINEL_VIEW_ERROR || view->state == SENTINEL_VIEW_OFFLINE) &&
        view->message != NULL && view->message[0] != '\0') return view->message;
    return view->state == SENTINEL_VIEW_LIVE ? "LISTENING / LAST RESULTS STAY VISIBLE" :
        view->state == SENTINEL_VIEW_PAUSED ? "STOPPED / YOUR LAST OBSERVATIONS" :
        view->kind == SENTINEL_VIEW_ENERGY ? "ENERGY ONLY / CHANNELS 11-26" :
        "TAP A RESULT TO EXPLORE";
}

static const char *scan_label(const sentinel_scan_view_t *view)
{
    return view->state == SENTINEL_VIEW_LIVE ? "STOP" :
        view->state == SENTINEL_VIEW_COMPLETE ? "AGAIN" : "SCAN";
}

static esp_err_t button(uint16_t x, uint16_t width, const char *label,
                         uint16_t color, bool enabled, bool primary)
{
    TRY(cerberus_display_fill_rect(x, 364, width, 42, enabled ? color : BORDER));
    uint16_t fill = primary && enabled ? color : PANEL;
    TRY(cerberus_display_fill_rect(x + 1, 365, width - 2, 40, fill));
    return centered(x, 381, width, label, enabled ? (primary ? BG : TEXT) : MUTED, fill, 1);
}

static esp_err_t controls(const sentinel_scan_view_t *view)
{
    uint16_t color = accent(view);
    TRY(button(8, 68, "< PREV", color, view->has_previous, false));
    TRY(button(84, 68, scan_label(view), color, view->state != SENTINEL_VIEW_OFFLINE, true));
    TRY(button(160, 68, "NEXT >", color, view->has_next, false));
    return button(236, 76, view->tools_label?view->tools_label:"TOOLS", color, !view->tools_disabled, view->tools_label!=NULL);
}

static unsigned signal_level(int8_t rssi)
{
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -80) return 2;
    return 1;
}

static esp_err_t result_card(const sentinel_scan_view_t *view, unsigned index)
{
    const sentinel_scan_row_t *row = &view->rows[index];
    uint16_t y = 126 + (uint16_t)index * 56;
    uint16_t color = accent(view);
    uint16_t mark = row->review ? AMBER : color;
    TRY(cerberus_display_fill_rect(8, y, 304, 52, row->selected?color:BORDER));
    TRY(cerberus_display_fill_rect(9, y + 1, 302, 50, PANEL));
    TRY(cerberus_display_fill_rect(9, y + 1, 2, 50, mark));
    const char *name=row->name == NULL || row->name[0] == '\0' ? "UNNAMED" : row->name;
    /* Keep all 32 SSID bytes visible when they do not fit the large font. */
    uint8_t scale=view->kind==SENTINEL_VIEW_WIFI&&strlen(name)>18?1:2;
    TRY(text(18, y + (scale==1?10:6), name, scale==1?32:18, TEXT, PANEL, scale));
    TRY(text(18, y + 25, row->detail, 37, MUTED, PANEL, 1));
    TRY(text(18, y + 39, row->badge, 33, row->marked || row->review ? mark : MUTED, PANEL, 1));
    if (row->signal_valid) {
        unsigned level = signal_level(row->rssi);
        for (unsigned bar = 0; bar < 4; ++bar) {
            uint16_t height = 4 + (uint16_t)bar * 4;
            TRY(cerberus_display_fill_rect(272 + (uint16_t)bar * 7, y + 22 - height,
                                           5, height, bar < level ? color : BORDER));
        }
        char value[16];
        snprintf(value, sizeof(value), "%d dBm", row->rssi);
        TRY(centered(248, y + 28, 59, value, TEXT, PANEL, 1));
    }
    return ESP_OK;
}

static const char *empty_message(const sentinel_scan_view_t *view)
{
    if (view->message != NULL && view->message[0] != '\0') return view->message;
    return view->state == SENTINEL_VIEW_OFFLINE ? "RADIO OFFLINE" :
        view->state == SENTINEL_VIEW_ERROR ? "SCAN UNAVAILABLE" :
        view->state == SENTINEL_VIEW_LIVE ? "LISTENING" :
        view->state == SENTINEL_VIEW_PAUSED ? "SURVEY STOPPED" :
        view->state == SENTINEL_VIEW_COMPLETE ? "NOTHING HEARD" : "READY TO EXPLORE";
}

static const char *empty_explanation(const sentinel_scan_view_t *view)
{
    /* Capture age belongs exclusively to the small line below the results. */
    return view->state == SENTINEL_VIEW_LIVE ? "RESULTS APPEAR WHEN THE SWEEP FINISHES" :
        view->state == SENTINEL_VIEW_OFFLINE ? "CHECK THE ONBOARD C5 CONNECTION" :
        view->state == SENTINEL_VIEW_ERROR ? "OPEN TOOLS OR TRY ANOTHER SCAN" :
        "TAP SCAN TO OBSERVE YOUR SURROUNDINGS";
}

static esp_err_t empty_state(const sentinel_scan_view_t *view)
{
    uint16_t color = accent(view);
    TRY(cerberus_display_fill_rect(20, 144, 280, 181, PANEL));
    TRY(cerberus_display_fill_rect(20, 144, 280, 1, BORDER));
    for (unsigned bar = 0; bar < 5; ++bar) {
        uint16_t height = 10 + (uint16_t)(bar < 3 ? bar : 4 - bar) * 9;
        TRY(cerberus_display_fill_rect(129 + (uint16_t)bar * 14, 202 - height, 7, height, color));
    }
    TRY(centered(24, 223, 272, empty_message(view), view->state == SENTINEL_VIEW_ERROR ? ERROR : TEXT, PANEL, 2));
    TRY(centered(24, 255, 272, empty_explanation(view), MUTED, PANEL, 1));
    return centered(24, 288, 272, "OBSERVE. UNDERSTAND. PROTECT.", color, PANEL, 1);
}

static esp_err_t energy_chart(const sentinel_scan_view_t *view)
{
    uint16_t color = accent(view);
    TRY(cerberus_display_fill_rect(8, 126, 304, 157, PANEL));
    TRY(text(17, 137, "CHANNEL ENERGY", 25, TEXT, PANEL, 1));
    TRY(text(270, 137, "dBm", 5, MUTED, PANEL, 1));
    const int lower = -100, upper = -20;
    const uint16_t graph_bottom = 257;
    for (int level = lower; level <= upper; level += 40) {
        uint16_t y = graph_bottom - (uint16_t)((level - lower) * 100 / (upper - lower));
        char label[8];
        snprintf(label, sizeof(label), "%d", level);
        TRY(text(15, y - 3, label, 4, MUTED, PANEL, 1));
        TRY(cerberus_display_fill_rect(44, y, 256, 1, BORDER));
    }
    unsigned low_index = 0, high_index = 0;
    bool found = false;
    for (unsigned index = 0; index < 16; ++index) {
        uint16_t x = 45 + (uint16_t)index * 16;
        if (view->energy_valid_mask & (1U << index)) {
            int power = view->energy_dbm[index];
            if (!found || power < view->energy_dbm[low_index]) low_index = index;
            if (!found || power > view->energy_dbm[high_index]) high_index = index;
            found = true;
            if (power < lower) power = lower;
            if (power > upper) power = upper;
            uint16_t height = (uint16_t)((power - lower) * 100 / (upper - lower));
            if (height == 0) height = 1;
            TRY(cerberus_display_fill_rect(x, graph_bottom - height, 11, height, color));
            TRY(cerberus_display_fill_rect(x, graph_bottom - height, 11, 1, TEXT));
        } else {
            TRY(cerberus_display_fill_rect(x, graph_bottom - 1, 11, 1, MUTED));
        }
        if (index == 0 || index == 4 || index == 8 || index == 12 || index == 15) {
            char label[8];
            snprintf(label, sizeof(label), "%u", index + 11U);
            TRY(text(x, 266, label, 2, MUTED, PANEL, 1));
        }
    }
    if (found) {
        const unsigned indices[] = { low_index, high_index };
        const char *labels[] = { "QUIETEST OBSERVED", "STRONGEST OBSERVED" };
        for (unsigned index = 0; index < 2; ++index) {
            uint16_t x = index == 0 ? 8 : 164;
            TRY(cerberus_display_fill_rect(x, 291, 148, 55, PANEL));
            TRY(text(x + 8, 302, labels[index], 22, MUTED, PANEL, 1));
            char line[32];
            snprintf(line, sizeof(line), "CH%u  %d dBm", indices[index] + 11U,
                     view->energy_dbm[indices[index]]);
            TRY(text(x + 8, 321, line, 22, color, PANEL, 1));
        }
    }
    return ESP_OK;
}

static bool valid(const sentinel_scan_view_t *view)
{
    return view != NULL && view->kind >= SENTINEL_VIEW_WIFI && view->kind <= SENTINEL_VIEW_RADIO_LIST &&
        view->state >= SENTINEL_VIEW_IDLE && view->state <= SENTINEL_VIEW_OFFLINE &&
        view->row_count <= SENTINEL_VIEW_VISIBLE_ROWS;
}

static bool same_text(const char *left, const char *right, size_t maximum)
{
    char a[52], b[52];
    safe_text(a, sizeof(a), left, maximum);
    safe_text(b, sizeof(b), right, maximum);
    return strcmp(a, b) == 0;
}

static void remember(const sentinel_scan_view_t *view, uint32_t frame)
{
    rendered.view = *view;
#define COPY_STRING(member, buffer, maximum) do { \
        safe_text(rendered.buffer, sizeof(rendered.buffer), view->member, maximum); \
        rendered.view.member = rendered.buffer; \
    } while (0)
    COPY_STRING(subtitle, subtitle, 50);
    COPY_STRING(count_label, count_label, 18);
    COPY_STRING(message, message, 50);
    COPY_STRING(hint, hint, 50);
    if(view->tools_label){COPY_STRING(tools_label, tools_label, 12);}
    if(view->sort_label){COPY_STRING(sort_label, sort_label, 12);}
    if(view->age_label){COPY_STRING(age_label, age_label, 12);}
#undef COPY_STRING
    for (unsigned i = 0; i < SENTINEL_VIEW_VISIBLE_ROWS; ++i) {
        safe_text(rendered.names[i], sizeof(rendered.names[i]), view->rows[i].name, 32);
        safe_text(rendered.details[i], sizeof(rendered.details[i]), view->rows[i].detail, 37);
        safe_text(rendered.badges[i], sizeof(rendered.badges[i]), view->rows[i].badge, 33);
        rendered.view.rows[i].name = rendered.names[i];
        rendered.view.rows[i].detail = rendered.details[i];
        rendered.view.rows[i].badge = rendered.badges[i];
    }
    rendered.frame = frame;
    rendered.valid = true;
}

static unsigned body_kind(const sentinel_scan_view_t *view)
{
    if (view->kind == SENTINEL_VIEW_ENERGY) return view->energy_valid_mask != 0 ? 2U : 0U;
    return view->row_count != 0 ? 1U : 0U;
}

static esp_err_t body(const sentinel_scan_view_t *view)
{
    unsigned kind = body_kind(view);
    if (kind == 2) return energy_chart(view);
    if (kind == 1) {
        for (unsigned i = 0; i < view->row_count; ++i) TRY(result_card(view, i));
        return ESP_OK;
    }
    return empty_state(view);
}

static bool same_summary(const sentinel_scan_view_t *left, const sentinel_scan_view_t *right)
{
    if (left->selection_mode!=right->selection_mode||summary_count(left) != summary_count(right) || left->state != right->state ||
        !same_text(count_label(left), count_label(right), 18) ||
        (left->kind == SENTINEL_VIEW_BLE && !same_text(left->sort_label, right->sort_label, 12)) ||
        has_age_control(left) != has_age_control(right) ||
        (has_age_control(left) && !same_text(left->age_label, right->age_label, 12))) return false;
    if (left->kind == SENTINEL_VIEW_ENERGY) return true;
    if (left->page_count <= 1 && right->page_count <= 1) return true;
    return left->page == right->page && left->page_count == right->page_count;
}

static unsigned progress_key(const sentinel_scan_view_t *view, uint32_t frame)
{
    if (view->progress_known) return 1U + (view->progress_percent > 100 ? 100 : view->progress_percent);
    return view->state == SENTINEL_VIEW_LIVE ? 200U + frame % 16U : 0U;
}

static bool same_row(const sentinel_scan_row_t *left, const sentinel_scan_row_t *right)
{
    const char *a = left->name == NULL || left->name[0] == '\0' ? "UNNAMED" : left->name;
    const char *b = right->name == NULL || right->name[0] == '\0' ? "UNNAMED" : right->name;
    return same_text(a, b, 32) && same_text(left->detail, right->detail, 37) &&
        same_text(left->badge, right->badge, 33) && left->signal_valid == right->signal_valid &&
        (!left->signal_valid || left->rssi == right->rssi) &&
        left->marked == right->marked && left->review == right->review && left->selected == right->selected;
}

static bool same_energy(const sentinel_scan_view_t *left, const sentinel_scan_view_t *right)
{
    if (left->energy_valid_mask != right->energy_valid_mask) return false;
    for (unsigned i = 0; i < 16; ++i) {
        if ((left->energy_valid_mask & (1U << i)) && left->energy_dbm[i] != right->energy_dbm[i]) return false;
    }
    return true;
}

esp_err_t sentinel_scan_view_draw(const sentinel_scan_view_t *view, uint32_t frame)
{
    rendered.valid = false;
    if (!valid(view)) return ESP_ERR_INVALID_ARG;
    TRY(cerberus_display_fill_rect(0, 48, 320, 363, BG));
    TRY(summary(view));
    TRY(progress(view, frame));
    TRY(padded_text(9, 113, subtitle(view), 50));
    TRY(body(view));
    TRY(padded_text(9, 352, view->hint, 50));
    TRY(controls(view));
    remember(view, frame);
    return ESP_OK;
}

esp_err_t sentinel_scan_view_update(const sentinel_scan_view_t *view, uint32_t frame)
{
    if (!valid(view)) {
        rendered.valid = false;
        return ESP_ERR_INVALID_ARG;
    }
    if (!rendered.valid || rendered.view.kind != view->kind) return sentinel_scan_view_draw(view, frame);
    /* Any failed transfer leaves the cache invalid so the next call repairs the
     * entire owned area. Commit the new snapshot only after every write succeeds. */
    rendered.valid = false;
    const sentinel_scan_view_t *previous = &rendered.view;
    if (!same_summary(previous, view)) TRY(summary(view));
    if (progress_key(previous, rendered.frame) != progress_key(view, frame)) TRY(progress(view, frame));
    if (!same_text(subtitle(previous), subtitle(view), 50)) TRY(padded_text(9, 113, subtitle(view), 50));
    if (!same_text(previous->hint, view->hint, 50)) TRY(padded_text(9, 352, view->hint, 50));

    unsigned old_body = body_kind(previous), new_body = body_kind(view);
    if (old_body != new_body) {
        TRY(cerberus_display_fill_rect(8, 126, 304, 220, BG));
        TRY(body(view));
    } else if (new_body == 1) {
        unsigned maximum = previous->row_count > view->row_count ? previous->row_count : view->row_count;
        for (unsigned i = 0; i < maximum; ++i) {
            if (i >= view->row_count) {
                TRY(cerberus_display_fill_rect(8, 126 + (uint16_t)i * 56, 304, 52, BG));
            } else if (i >= previous->row_count || !same_row(&previous->rows[i], &view->rows[i])) {
                TRY(result_card(view, i));
            }
        }
    } else if (new_body == 2) {
        if (!same_energy(previous, view)) TRY(energy_chart(view));
    } else if (!same_text(empty_message(previous), empty_message(view), 22) ||
               !same_text(empty_explanation(previous), empty_explanation(view), 45) ||
               (previous->state == SENTINEL_VIEW_ERROR) != (view->state == SENTINEL_VIEW_ERROR)) {
        TRY(empty_state(view));
    }

    uint16_t color = accent(view);
    if (previous->has_previous != view->has_previous)
        TRY(button(8, 68, "< PREV", color, view->has_previous, false));
    if (strcmp(scan_label(previous), scan_label(view)) != 0 ||
        (previous->state == SENTINEL_VIEW_OFFLINE) != (view->state == SENTINEL_VIEW_OFFLINE))
        TRY(button(84, 68, scan_label(view), color, view->state != SENTINEL_VIEW_OFFLINE, true));
    if (previous->has_next != view->has_next)
        TRY(button(160, 68, "NEXT >", color, view->has_next, false));
    if (!same_text(previous->tools_label,view->tools_label,12) || previous->tools_disabled!=view->tools_disabled ||
        (previous->tools_label==NULL)!=(view->tools_label==NULL))
        TRY(button(236,76,view->tools_label?view->tools_label:"TOOLS",color,!view->tools_disabled,view->tools_label!=NULL));
    remember(view, frame);
    return ESP_OK;
}

esp_err_t sentinel_scan_view_animate(const sentinel_scan_view_t *view, uint32_t frame)
{
    return sentinel_scan_view_update(view, frame);
}

sentinel_view_action_t sentinel_scan_view_hit_test(const sentinel_scan_view_t *view,
                                                 uint16_t x, uint16_t y)
{
    if (!valid(view)) return SENTINEL_VIEW_NONE;
    if(view->selection_mode&&view->selection_count&&x>=224&&x<312&&y>=57&&y<80)return SENTINEL_VIEW_CLEAR;
    if (!view->selection_mode &&
        x >= 224 && x < 312 && y >= 48 && y < 98) {
        if (has_age_control(view) && y >= 74)
            return view->state != SENTINEL_VIEW_OFFLINE ? SENTINEL_VIEW_AGE : SENTINEL_VIEW_NONE;
        if (view->kind == SENTINEL_VIEW_BLE && view->sort_label && view->sort_label[0])
            return SENTINEL_VIEW_SORT;
    }
    if (y >= 364 && y < 406) {
        if (x >= 8 && x < 76 && view->has_previous) return SENTINEL_VIEW_PREV;
        if (x >= 84 && x < 152 && view->state != SENTINEL_VIEW_OFFLINE) return SENTINEL_VIEW_PAUSE;
        if (x >= 160 && x < 228 && view->has_next) return SENTINEL_VIEW_NEXT;
        if (x >= 236 && x < 312 && !view->tools_disabled) return SENTINEL_VIEW_TOOLS;
    }
    if (view->kind != SENTINEL_VIEW_ENERGY && x >= 8 && x < 312 && y >= 126 && y < 346) {
        unsigned row = (y - 126U) / 56U;
        if (row < view->row_count && (y - 126U) % 56U < 52U) {
            return (sentinel_view_action_t)(SENTINEL_VIEW_ROW0 + row);
        }
    }
    return SENTINEL_VIEW_NONE;
}
