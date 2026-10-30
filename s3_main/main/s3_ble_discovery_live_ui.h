/* Private ordinary-list integration. UI task owns this on-demand PSRAM store;
 * producer callbacks only touch the separately locked scanner snapshot. */
#include "s3_ble_discovery_inventory.h"
#include "s3_ble_discovery_session.h"

typedef struct {
    cerberus_ble_discovery_session_t session;
    cerberus_ble_discovery_ops_t ops;
    cerberus_s3_ble_discovery_inventory_t inventory;
    cerberus_s3_ble_discovery_bank_t staging;
    cerberus_s3_ble_discovery_entry_t painted[4], selected;
    live_scan_record_t presentation;
    uint64_t last_draw_ms;
    uint16_t binding_overflows, producer_replacements;
    uint8_t painted_count;
    uint8_t destination; /* 1: tools, 2: Find My monitor, after release only. */
    bool selection_pending, visible, dirty;
} ble_discovery_live_store_t;

static ble_discovery_live_store_t *ble_discovery_live;
static esp_err_t ble_discovery_open_error;
static void live_make_view(void);
static esp_err_t live_paint(bool full);

static bool ble_discovery_page(void)
{
    return !ui_capability_blocked && current_page == UI_PAGE_BLE &&
        ble_mode == UI_BLE_MODE_LIST && !ble_attack_selecting;
}

static bool ble_discovery_presenting(void)
{
    return ble_discovery_page() && ble_discovery_live != NULL;
}

static bool ble_discovery_keeps_awake(void)
{
    return ble_discovery_page() && ble_discovery_live &&
        (ble_discovery_live->session.enabled || ble_discovery_live->session.epoch != 0U);
}

/* The local BLE list must not wait for optional, synchronous SPI telemetry.
 * Any outstanding C5 work is still polled by its owning live session. */
static bool ble_discovery_defers_c5_telemetry(void)
{
    return ble_discovery_page();
}

static void ble_discovery_leave(void)
{
    if (!ble_discovery_live) return;
    ble_discovery_live->selection_pending = false;
    ble_discovery_live->destination = 0U;
    cerberus_ble_discovery_session_stop(&ble_discovery_live->session);
}

_Static_assert(CERBERUS_BLE_DISCOVERY_FAMILY_COUNT == CERBERUS_S3_BLE_FILTER_COUNT,
    "Discovery family mapping");
_Static_assert((int)CERBERUS_BLE_DISCOVERY_RUNNING == (int)CERBERUS_BLE_SCAN_RUNNING &&
    (int)CERBERUS_BLE_DISCOVERY_COMPLETE == (int)CERBERUS_BLE_SCAN_COMPLETE &&
    (int)CERBERUS_BLE_DISCOVERY_ERROR == (int)CERBERUS_BLE_SCAN_ERROR, "Discovery state mapping");

static cerberus_ble_discovery_operation_t ble_discovery_operation(esp_err_t error)
{
    return (cerberus_ble_discovery_operation_t){error, error == ESP_ERR_NOT_FOUND};
}

static bool ble_discovery_observation(const cerberus_s3_ble_discovery_meta_t *meta,
    cerberus_ble_discovery_observation_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!meta->attached || !meta->scan_epoch ||
        meta->family >= CERBERUS_S3_BLE_FILTER_COUNT) return false;
    *out = (cerberus_ble_discovery_observation_t){
        .scan_epoch = meta->scan_epoch, .revision = meta->revision,
        .error = meta->status.error_code, .family = (uint8_t)meta->family,
        .state = meta->status.state, .owned = meta->owned, .stopping = meta->stopping};
    return true;
}

static cerberus_ble_discovery_operation_t ble_discovery_start(void *context,
    uint8_t family, uint64_t previous, cerberus_ble_discovery_observation_t *out)
{
    (void)context;
    cerberus_s3_ble_discovery_meta_t meta = {0};
    esp_err_t error = cerberus_s3_ble_scanner_start_discovery(
        (cerberus_s3_ble_filter_t)family, previous, &meta);
    if (!ble_discovery_observation(&meta, out) && error == ESP_OK)
        error = ESP_ERR_INVALID_RESPONSE;
    return ble_discovery_operation(error);
}

static cerberus_ble_discovery_operation_t ble_discovery_poll(void *context,
    uint64_t epoch, cerberus_ble_discovery_observation_t *out)
{
    (void)context;
    cerberus_s3_ble_discovery_meta_t meta = {0};
    esp_err_t error = cerberus_s3_ble_scanner_discovery_status(epoch, &meta);
    if (!ble_discovery_observation(&meta, out) && error == ESP_OK)
        error = ESP_ERR_INVALID_RESPONSE;
    return ble_discovery_operation(error);
}

static cerberus_ble_discovery_operation_t ble_discovery_stop(void *context,
    uint64_t epoch, cerberus_ble_discovery_observation_t *out)
{
    (void)context;
    cerberus_s3_ble_discovery_meta_t meta = {0};
    esp_err_t error = cerberus_s3_ble_scanner_stop_discovery(epoch, &meta);
    if (!ble_discovery_observation(&meta, out) && error == ESP_OK)
        error = ESP_ERR_INVALID_RESPONSE;
    return ble_discovery_operation(error);
}

static cerberus_ble_discovery_operation_t ble_discovery_release(void *context,
    uint64_t epoch)
{
    (void)context;
    return ble_discovery_operation(cerberus_s3_ble_scanner_release_discovery(epoch));
}

static cerberus_ble_discovery_operation_t ble_discovery_copy(void *context,
    uint64_t epoch, uint8_t family, cerberus_ble_discovery_observation_t *out)
{
    ble_discovery_live_store_t *store = context;
    esp_err_t error = cerberus_s3_ble_scanner_copy_discovery(epoch, &store->staging);
    const cerberus_s3_ble_discovery_meta_t *meta = &store->staging.meta;
    memset(out, 0, sizeof(*out));
    if (error != ESP_OK) return ble_discovery_operation(error);
    if (meta->scan_epoch != epoch || meta->family != family ||
        !ble_discovery_observation(meta, out) ||
        !cerberus_s3_ble_discovery_bank_valid(&store->staging))
        return ble_discovery_operation(ESP_ERR_INVALID_RESPONSE);
    if (store->inventory.family != meta->family)
        (void)cerberus_s3_ble_discovery_inventory_init(&store->inventory, meta->family);
    cerberus_s3_ble_discovery_apply_t applied =
        cerberus_s3_ble_discovery_inventory_apply(&store->inventory, &store->staging);
    if (applied == CERBERUS_BLE_INVENTORY_INVALID)
        return ble_discovery_operation(ESP_ERR_INVALID_RESPONSE);
    if (applied == CERBERUS_BLE_INVENTORY_APPLIED) {
        store->presentation.captured = true;
        store->presentation.saved_generation = meta->status.generation;
        store->presentation.captured_ms = (uint32_t)meta->source_now_ms;
        store->presentation.count = store->inventory.count;
        store->binding_overflows = meta->binding_overflows;
        store->producer_replacements = meta->replacements;
        store->dirty = live_ble_order_dirty = true;
    }
    return ble_discovery_operation(ESP_OK);
}

static bool ble_discovery_acquire(void)
{
    if (ble_discovery_live) return true;
    if (ble_discovery_open_error != ESP_OK) return false;
    ble_discovery_live = heap_caps_calloc(1U, sizeof(*ble_discovery_live),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ble_discovery_live) {
        ble_discovery_open_error = ESP_ERR_NO_MEM;
        live_records[SENTINEL_SCAN_BLE].error = ESP_ERR_NO_MEM;
        live_records[SENTINEL_SCAN_BLE].valid = true;
        return false;
    }
    ble_discovery_open_error = ESP_OK;
    cerberus_ble_discovery_session_init(&ble_discovery_live->session, (uint8_t)ble_filter);
    (void)cerberus_s3_ble_discovery_inventory_init(&ble_discovery_live->inventory, ble_filter);
    ble_discovery_live->ops = (cerberus_ble_discovery_ops_t){
        .context = ble_discovery_live, .start = ble_discovery_start,
        .poll = ble_discovery_poll, .copy = ble_discovery_copy,
        .stop = ble_discovery_stop, .release = ble_discovery_release};
    ble_discovery_live->presentation.valid = true;
    /* Entering Scan is an explicit discovery request; later Back/Stop does not
     * silently rearm the retained list. Family changes preserve that intent. */
    (void)cerberus_ble_discovery_session_start(&ble_discovery_live->session);
    return true;
}

static void ble_discovery_select_painted(unsigned row)
{
    ble_discovery_live_store_t *store = ble_discovery_live;
    if (!store || row >= store->painted_count) return;
    store->selected = store->painted[row];
    store->selection_pending = true;
    store->destination = 0U;
    cerberus_ble_discovery_session_stop(&store->session);
    store->dirty = true;
}

static void ble_discovery_toggle(void)
{
    const bool already_allocated = ble_discovery_live != NULL;
    ble_discovery_open_error = ESP_OK;
    if (!ble_discovery_acquire()) return;
    ble_discovery_live_store_t *store = ble_discovery_live;
    if (!already_allocated) {
        store->dirty = true;
        return;
    }
    store->selection_pending = false;
    store->destination = 0U;
    if (store->session.enabled || !cerberus_ble_discovery_session_released(&store->session))
        cerberus_ble_discovery_session_stop(&store->session);
    else
        (void)cerberus_ble_discovery_session_start(&store->session);
    store->dirty = true;
}

static void ble_discovery_tick(uint64_t now_ms, bool asleep)
{
    bool visible = ble_discovery_page() && !asleep;
    if (visible && !ble_discovery_acquire()) return;
    ble_discovery_live_store_t *store = ble_discovery_live;
    if (!store) return;
    if (!visible) {
        store->selection_pending = false;
        store->destination = 0U;
        cerberus_ble_discovery_session_stop(&store->session);
    } else if (store->session.requested_family != (uint8_t)ble_filter) {
        store->selection_pending = false;
        store->destination = 0U;
        (void)cerberus_ble_discovery_session_select(&store->session, (uint8_t)ble_filter);
        store->dirty = true;
    }
    store->visible = visible;
    /* First drain any legacy Watch sweep. Local discovery does not require a
     * successful C5 poll, and it never adopts a generic scanner's claim. */
    if (store->session.epoch || live_ble_session.active == SENTINEL_SCAN_NONE)
        cerberus_ble_discovery_session_tick(&store->session, now_ms, &store->ops);
    store->presentation.valid = true;
    store->presentation.error = store->session.error;
    store->presentation.state = store->session.enabled ||
        !cerberus_ble_discovery_session_released(&store->session) ?
        SENTINEL_SCAN_RUNNING : SENTINEL_SCAN_COMPLETE;
    if (visible && store->selection_pending &&
        cerberus_ble_discovery_session_released(&store->session)) {
        const cerberus_s3_ble_discovery_record_t *proof = &store->selected.record;
        cerberus_findmy_monitor_record_t frozen = {
            .peer = proof->peer, .advertisement = proof->advertisement,
            .focus = proof->focus, .generation = proof->generation,
            .index = proof->index, .observed_ms = proof->observed_ms};
        store->selection_pending = false;
        ble_select_frozen_live(&frozen);
        ble_selected_original_time64_valid = true;
        ble_selected_original_ms64 = store->selected.original_source_ms;
        ble_selected_source_ceiling_ms64 = store->selected.source_now_ms;
        return;
    }
    if (visible && store->destination &&
        cerberus_ble_discovery_session_released(&store->session)) {
        uint8_t destination = store->destination;
        store->destination = 0U;
        if (destination == 2U) ble_findmy_monitor_open();
        else ble_mode = UI_BLE_MODE_MENU;
        ESP_ERROR_CHECK(draw_current_page());
        return;
    }
    if (visible && (store->dirty || now_ms - store->last_draw_ms >= 200U)) {
        store->last_draw_ms = now_ms;
        ++live_frame;
        live_make_view();
        ESP_ERROR_CHECK(live_paint(false));
        store->dirty = false;
    }
}

static uint32_t ble_discovery_visible_mask(uint64_t now_ms)
{
    if (!ble_discovery_live) return 0U;
    uint32_t mask = 0U;
    const cerberus_s3_ble_discovery_inventory_t *inventory = &ble_discovery_live->inventory;
    for (uint8_t i = 0U; i < inventory->count; ++i) {
        const cerberus_s3_ble_discovery_entry_t *entry = &inventory->entries[i];
        uint8_t ignored;
        if (!cerberus_s3_ble_focus_page(&entry->record.focus.category_bits, 1U,
                ble_filter, 0U, &ignored, 1U)) continue;
        if (live_ble_recent_only && cerberus_s3_ble_recent_supported(ble_filter) &&
            !cerberus_s3_ble_discovery_entry_recent(entry, ble_filter, now_ms)) continue;
        mask |= UINT32_C(1) << i;
    }
    return mask;
}
