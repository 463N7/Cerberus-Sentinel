#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENTINEL_VIEW_VISIBLE_ROWS 4U

typedef enum {
    SENTINEL_VIEW_WIFI = 0,
    SENTINEL_VIEW_BLE,
    SENTINEL_VIEW_ENERGY,
    SENTINEL_VIEW_RADIO_LIST,
} sentinel_view_kind_t;

typedef enum {
    SENTINEL_VIEW_IDLE = 0,
    SENTINEL_VIEW_LIVE,
    SENTINEL_VIEW_PAUSED,
    SENTINEL_VIEW_COMPLETE,
    SENTINEL_VIEW_ERROR,
    SENTINEL_VIEW_OFFLINE,
} sentinel_view_state_t;

typedef struct {
    const char *name;
    const char *detail;
    const char *badge;
    int8_t rssi;
    bool signal_valid;
    bool marked;
    bool review;
    bool selected;
} sentinel_scan_row_t;

typedef struct {
    sentinel_view_kind_t kind;
    sentinel_view_state_t state;
    const char *subtitle;
    const char *count_label; /* Optional summary label; at most 18 visible characters. */
    const char *sort_label; /* Optional BLE sort control in the upper-right badge. */
    const char *age_label; /* Optional Wi-Fi/BLE age control below the state/sort badge. */
    const char *message;
    const char *hint; /* Capture age / context; always visible at y352 when nonempty. */
    const char *tools_label; /* Optional action for the bottom-right control. */
    bool tools_disabled;
    bool selection_mode;
    uint8_t selection_count;
    uint16_t total_count;
    uint16_t page;       /* Zero-based page index. */
    uint16_t page_count;
    bool has_previous;
    bool has_next;
    bool progress_known;
    uint8_t progress_percent;
    uint8_t row_count;
    sentinel_scan_row_t rows[SENTINEL_VIEW_VISIBLE_ROWS];
    int8_t energy_dbm[16]; /* Channel 11 at index 0; use mask for actual samples. */
    uint16_t energy_valid_mask;
} sentinel_scan_view_t;

typedef enum {
    SENTINEL_VIEW_NONE = 0,
    SENTINEL_VIEW_PREV,
    SENTINEL_VIEW_PAUSE, /* Stop while LIVE; scan/again in other states. */
    SENTINEL_VIEW_NEXT,
    SENTINEL_VIEW_TOOLS,
    SENTINEL_VIEW_ROW0,
    SENTINEL_VIEW_ROW1,
    SENTINEL_VIEW_ROW2,
    SENTINEL_VIEW_ROW3,
    SENTINEL_VIEW_CLEAR,
    SENTINEL_VIEW_SORT,
    SENTINEL_VIEW_AGE,
} sentinel_view_action_t;

/* Owns y=48..410 only. Caller owns the header and the navigation footer.
 * Result cards: x8,y126+56*row,w304,h52 (4 per page).
 * Controls: y364,h42; Prev x8,w68; Stop/Scan x84,w68; Next x160,w68;
 * Tools x236,w76. Disabled controls are not returned by hit testing.
 * BLE summary controls: x224..311,y48..97 is Sort unless age_label is
 * nonempty; then Sort uses y48..73 and Age y74..97. Wi-Fi age_label uses
 * the same lower Age area with a noninteractive state badge above it.
 * Age is unavailable for other kinds, selection-mode or OFFLINE views.
 * Left summary ends before x216. Wi-Fi never returns Sort.
 * Strings need only remain valid for the duration of each call.
 * Preserve the last actual records during a new sweep by passing them again.
 */
esp_err_t sentinel_scan_view_draw(const sentinel_scan_view_t *view, uint32_t frame);
/* Draw on page entry. update() owns a small copy of displayed values/strings
 * and repaints only changed components. An unchanged completed/idle view writes
 * no pixels; live marker animation writes only y103..105; age writes y352..358.
 * Rows, charts, summary, subtitle and controls are updated independently.
 * A display failure invalidates the cache and the next call restores the area.
 * Call draw() after another page has painted over this screen. No heap is used.
 */
esp_err_t sentinel_scan_view_update(const sentinel_scan_view_t *view, uint32_t frame);
/* Compatibility alias for update(). */
esp_err_t sentinel_scan_view_animate(const sentinel_scan_view_t *view, uint32_t frame);
sentinel_view_action_t sentinel_scan_view_hit_test(const sentinel_scan_view_t *view,
                                                 uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
