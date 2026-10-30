#pragma once
#include "mac_monitor_inventory.h"
#include "display.h"

enum { CERBERUS_MAC_UI_READY, CERBERUS_MAC_UI_RUNNING, CERBERUS_MAC_UI_STOPPING,
    CERBERUS_MAC_UI_STOPPED, CERBERUS_MAC_UI_ERROR };
typedef struct {
    cerberus_mac_monitor_page_t page;
    cerberus_mac_monitor_selection_t selection;
    uint64_t now_us;
    const char *source_labels[4], *selected_source_label;
    const char *wifi_status, *ble_status;
    uint32_t invalid[2], replacements[2];
    uint8_t state;
    bool detail, start_available, stop_available, back_available;
} cerberus_mac_monitor_ui_view_t;
enum { CERBERUS_MAC_UI_NONE, CERBERUS_MAC_UI_START, CERBERUS_MAC_UI_STOP,
    CERBERUS_MAC_UI_BACK, CERBERUS_MAC_UI_SELECT, CERBERUS_MAC_UI_PREV,
    CERBERUS_MAC_UI_NEXT, CERBERUS_MAC_UI_SORT };
typedef struct { unsigned action; cerberus_mac_monitor_record_t selected; } cerberus_mac_monitor_ui_event_t;
typedef struct {
    char wifi[49], ble[49], state[25], summary[37], sort[13];
    char rows[4][3][49], detail[12][49];
    bool detail_mode, start, stop, back, previous, next;
} cerberus_mac_monitor_ui_model_t;
typedef struct {
    cerberus_mac_monitor_ui_model_t painted, staging;
    cerberus_mac_monitor_record_t rows[4];
    uint8_t row_count;
    bool valid;
} cerberus_mac_monitor_ui_cache_t;

esp_err_t cerberus_mac_monitor_ui_update(cerberus_mac_monitor_ui_cache_t *cache,
    const cerberus_mac_monitor_ui_view_t *view, bool full);
void cerberus_mac_monitor_ui_invalidate(cerberus_mac_monitor_ui_cache_t *cache);
cerberus_mac_monitor_ui_event_t cerberus_mac_monitor_ui_tap(
    const cerberus_mac_monitor_ui_cache_t *cache, uint16_t x, uint16_t y);
