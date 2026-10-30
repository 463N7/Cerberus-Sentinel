/* UI-task-only integration. One on-demand PSRAM allocation owns both producer
 * adapters, immutable snapshot staging, accumulated records and painted rows. */
#include "mac_monitor_link.h"
#include "mac_monitor_ui.h"
#include "s3_ble_mac_monitor.h"

typedef struct {
    cerberus_mac_monitor_inventory_t inventory;
    cerberus_mac_monitor_session_t session;
    cerberus_mac_monitor_ui_view_t view;
    cerberus_mac_monitor_ui_cache_t cache;
    cerberus_mac_c5_link_t wifi;
    cerberus_s3_ble_mac_monitor_t ble;
    cerberus_mac_monitor_ops_t adapters[2], dispatch;
    char identifiers[5][24], phases[2][49];
    size_t offset;
    uint32_t last_tick, last_draw;
    uint8_t sort;
    bool detail, exit_pending;
} mac_monitor_live_store_t;
static mac_monitor_live_store_t *mac_monitor_live;
static esp_err_t mac_monitor_open_error;

static bool mac_monitor_takeover_ready(void)
{
    return remote_id_takeover_ready() &&
        (!remote_id_live || cerberus_remote_id_session_released(&remote_id_live->session)) &&
        (!csi_live || cerberus_csi_session_released(&csi_live->session));
}

static uint64_t mac_monitor_clock(void *context)
{ (void)context; return (uint64_t)esp_timer_get_time(); }

static int32_t mac_monitor_reserve(void *context, uint8_t radio, cerberus_mac_binding_t *binding)
{
    mac_monitor_live_store_t *store = context;
    if (radio > CERBERUS_MAC_BLE) return ESP_ERR_INVALID_ARG;
    if (!mac_monitor_takeover_ready()) return ESP_ERR_INVALID_STATE;
    cerberus_mac_monitor_ops_t *ops = &store->adapters[radio];
    return ops->reserve(ops->context, radio, binding);
}

#define MAC_DISPATCH(name, request_type, output_type) \
static cerberus_mac_monitor_operation_t mac_monitor_##name(void *context, uint8_t radio, \
    const request_type *request, output_type *out) \
{ \
    mac_monitor_live_store_t *store = context; \
    if (radio > CERBERUS_MAC_BLE) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false}; \
    cerberus_mac_monitor_ops_t *ops = &store->adapters[radio]; \
    return ops->name(ops->context, radio, request, out); \
}
MAC_DISPATCH(start, cerberus_mac_start_request_t, cerberus_mac_monitor_observation_t)
MAC_DISPATCH(poll, cerberus_mac_binding_t, cerberus_mac_monitor_observation_t)
MAC_DISPATCH(renew, cerberus_mac_renew_request_t, cerberus_mac_monitor_observation_t)
MAC_DISPATCH(stop, cerberus_mac_binding_t, cerberus_mac_monitor_observation_t)
#undef MAC_DISPATCH

static cerberus_mac_monitor_operation_t mac_monitor_read_step(void *context, uint8_t radio,
    const cerberus_mac_binding_t *binding, cerberus_mac_snapshot_t *snapshot,
    cerberus_mac_monitor_transfer_t *transfer)
{
    mac_monitor_live_store_t *store = context;
    if (radio > CERBERUS_MAC_BLE) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    cerberus_mac_monitor_ops_t *ops = &store->adapters[radio];
    return ops->read_step(ops->context, radio, binding, snapshot, transfer);
}

static cerberus_mac_monitor_operation_t mac_monitor_ack(void *context, uint8_t radio,
    const cerberus_mac_ack_request_t *request)
{
    mac_monitor_live_store_t *store = context;
    if (radio > CERBERUS_MAC_BLE) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    cerberus_mac_monitor_ops_t *ops = &store->adapters[radio];
    return ops->ack(ops->context, radio, request);
}

static void mac_monitor_phase(mac_monitor_live_store_t *store, uint8_t radio)
{
    const cerberus_mac_monitor_radio_t *state = &store->session.radios[radio];
    if (state->error) {
        snprintf(store->phases[radio], sizeof(store->phases[radio]), "ERROR %ld%s",
            (long)state->error, state->owned ? " / STOP TO RETRY" : "");
    } else if (state->stop_requested) {
        snprintf(store->phases[radio], sizeof(store->phases[radio]), "STOPPING");
    } else if (state->owned && state->observed) {
        const cerberus_mac_status_t *status = &state->observation.status;
        if (status->flags & CERBERUS_MAC_RECEIVING) {
            if (radio == CERBERUS_MAC_WIFI)
                snprintf(store->phases[radio], sizeof(store->phases[radio]), "RECEIVING / CH %u", status->channel);
            else snprintf(store->phases[radio], sizeof(store->phases[radio]), "RECEIVING");
        } else snprintf(store->phases[radio], sizeof(store->phases[radio]), "%s",
            status->state == CERBERUS_MAC_RUNNING ?
                (radio == CERBERUS_MAC_WIFI ? "BETWEEN CHANNELS" : "BETWEEN SWEEPS") : "STARTING");
    } else snprintf(store->phases[radio], sizeof(store->phases[radio]), "%s",
        state->owned || store->session.running ? "STARTING" : state->attempted ? "STOPPED" : "READY");
}

static void mac_monitor_source_label(const cerberus_mac_monitor_record_t *record, char *out, size_t capacity)
{
    if (record->row.radio == CERBERUS_MAC_WIFI)
        cerberus_privacy_format_wifi_identifier(record->row.address, privacy_addresses_masked(), out, capacity);
    else cerberus_privacy_format_ble_identifier(record->row.address, privacy_addresses_masked(), out, capacity);
}

static esp_err_t mac_monitor_draw(bool full)
{
    mac_monitor_live_store_t *store = mac_monitor_live;
    if (!store) {
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, 0x0042), TAG, "MAC page");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(82, 22, "MAC MONITOR", 0xE77F, 0x0042, 2), TAG, "MAC title");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(20, 100, esp_err_to_name(mac_monitor_open_error), 0xF38B, 0x0042, 1), TAG, "MAC error");
        return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){64,420,192,44}, "BACK", 1, true, UI_WIDGET_THEME);
    }
    cerberus_mac_monitor_ui_view_t *view = &store->view;
    memset(view, 0, sizeof(*view));
    view->now_us = mac_monitor_clock(store);
    (void)cerberus_mac_monitor_inventory_page(&store->inventory, store->sort, store->offset,
        view->now_us, &view->page);
    store->offset = view->page.offset;
    for (unsigned i = 0; i < view->page.copied; ++i) {
        mac_monitor_source_label(&view->page.rows[i], store->identifiers[i], sizeof(store->identifiers[i]));
        view->source_labels[i] = store->identifiers[i];
    }
    view->selection = store->inventory.selected;
    if (view->selection.valid) {
        mac_monitor_source_label(&view->selection.frozen, store->identifiers[4], sizeof(store->identifiers[4]));
        view->selected_source_label = store->identifiers[4];
    }
    view->detail = store->detail;
    bool released = cerberus_mac_monitor_session_released(&store->session);
    bool failed = store->session.radios[0].error || store->session.radios[1].error;
    bool attempted = store->session.radios[0].attempted || store->session.radios[1].attempted;
    view->state = !store->session.running && !released ? CERBERUS_MAC_UI_STOPPING :
        failed ? CERBERUS_MAC_UI_ERROR : store->session.running ? CERBERUS_MAC_UI_RUNNING :
        attempted ? CERBERUS_MAC_UI_STOPPED : CERBERUS_MAC_UI_READY;
    view->start_available = !store->exit_pending && !store->session.running && released && mac_monitor_takeover_ready();
    view->stop_available = store->session.running || !released;
    view->back_available = true;
    for (unsigned radio = 0; radio < 2U; ++radio) {
        mac_monitor_phase(store, (uint8_t)radio);
        view->invalid[radio] = store->inventory.banks[radio].header.invalid;
        view->replacements[radio] = store->inventory.banks[radio].header.replacements;
    }
    view->wifi_status = store->phases[0]; view->ble_status = store->phases[1];
    esp_err_t error = cerberus_mac_monitor_ui_update(&store->cache, view, full);
    if (error == ESP_OK) store->last_draw = (uint32_t)(view->now_us / 1000U);
    return error;
}

static void mac_monitor_open(void)
{
    current_page = UI_PAGE_MAC_MONITOR;
    ui_capability_blocked = false; ui_capability_blocked_reason = NULL;
    if (!mac_monitor_live) {
        mac_monitor_live = heap_caps_calloc(1, sizeof(*mac_monitor_live), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (mac_monitor_live) {
            mac_monitor_live_store_t *store = mac_monitor_live;
            cerberus_mac_monitor_inventory_init(&store->inventory);
            cerberus_mac_monitor_session_init(&store->session);
            cerberus_mac_c5_link_init(&store->wifi, &store->adapters[0]);
            cerberus_s3_ble_scanner_mac_ops(&store->ble, &store->adapters[1]);
            store->dispatch = (cerberus_mac_monitor_ops_t){.context = store, .now_us = mac_monitor_clock,
                .reserve = mac_monitor_reserve, .start = mac_monitor_start, .poll = mac_monitor_poll,
                .renew = mac_monitor_renew, .read_step = mac_monitor_read_step, .ack = mac_monitor_ack,
                .stop = mac_monitor_stop};
        }
    }
    mac_monitor_open_error = mac_monitor_live ? ESP_OK : ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(mac_monitor_draw(true));
}

static bool mac_monitor_finish_exit(void)
{
    if (mac_monitor_live && !cerberus_mac_monitor_session_released(&mac_monitor_live->session)) return false;
    heap_caps_free(mac_monitor_live); mac_monitor_live = NULL;
    if (current_page == UI_PAGE_MAC_MONITOR) {
        current_page = UI_PAGE_FIELD_MENU; field_menu.attack = false; field_menu.page = 0;
        ESP_ERROR_CHECK(draw_current_page());
    }
    return true;
}

static void mac_monitor_handle_tap(uint16_t x, uint16_t y)
{
    if (!mac_monitor_live) {
        if (x >= 64U && x < 256U && y >= 420U && y < 464U) (void)mac_monitor_finish_exit();
        return;
    }
    mac_monitor_live_store_t *store = mac_monitor_live;
    cerberus_mac_monitor_ui_event_t event = cerberus_mac_monitor_ui_tap(&store->cache, x, y);
    switch (event.action) {
    case CERBERUS_MAC_UI_START:
        if (!store->exit_pending && mac_monitor_takeover_ready()) (void)cerberus_mac_monitor_session_start(&store->session);
        break;
    case CERBERUS_MAC_UI_STOP: cerberus_mac_monitor_session_stop(&store->session); break;
    case CERBERUS_MAC_UI_BACK:
        if (store->detail && !store->exit_pending) {
            store->detail = false;
            cerberus_mac_monitor_inventory_clear_selection(&store->inventory);
        } else {
            store->exit_pending = true; cerberus_mac_monitor_session_stop(&store->session);
            if (mac_monitor_finish_exit()) return;
        }
        break;
    case CERBERUS_MAC_UI_SELECT:
        if (!store->exit_pending && cerberus_mac_monitor_inventory_select(&store->inventory, &event.selected)) store->detail = true;
        break;
    case CERBERUS_MAC_UI_PREV: if (store->offset >= 4U) store->offset -= 4U; break;
    case CERBERUS_MAC_UI_NEXT: store->offset += 4U; break;
    case CERBERUS_MAC_UI_SORT: store->sort = (uint8_t)((store->sort + 1U) % 3U); store->offset = 0; break;
    default: return;
    }
    ESP_ERROR_CHECK(mac_monitor_draw(false));
}

static bool mac_monitor_keeps_awake(void)
{
    return mac_monitor_live && (mac_monitor_live->session.running ||
        !cerberus_mac_monitor_session_released(&mac_monitor_live->session));
}

static void mac_monitor_tick(uint32_t now, bool asleep)
{
    mac_monitor_live_store_t *store = mac_monitor_live;
    if (!store) return;
    if (current_page != UI_PAGE_MAC_MONITOR) store->exit_pending = true;
    if (store->exit_pending || asleep) cerberus_mac_monitor_session_stop(&store->session);
    if ((uint32_t)(now - store->last_tick) < 50U) return;
    store->last_tick = now;
    cerberus_mac_monitor_session_tick(&store->session, &store->inventory, &store->dispatch);
    if (store->exit_pending && mac_monitor_finish_exit()) return;
    if (current_page == UI_PAGE_MAC_MONITOR && !asleep && (uint32_t)(now - store->last_draw) >= 200U)
        ESP_ERROR_CHECK(mac_monitor_draw(false));
}
