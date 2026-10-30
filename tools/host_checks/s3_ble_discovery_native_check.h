/* Actual scanner API/GAP callbacks and original raw Sound classifier. */
#include "s3_ble_discovery_inventory.h"
static unsigned discovery_native_checks;
#define DN(condition) do { ++discovery_native_checks; assert(condition); } while (0)

static void check_discovery_initial_diagnostics(void)
{
    cerberus_s3_ble_discovery_meta_t meta;
    memset(&meta, 0xa5, sizeof(meta));
    DN(!scan_mutex && !snapshot && !allocations && !mac_radio_starts);
    DN(cerberus_s3_ble_scanner_discovery_diagnostics(&meta) == ESP_OK && all_zero(&meta, sizeof(meta)));
    DN(!scan_mutex && !snapshot && !allocations && !mac_radio_starts && !mutex_takes);
    DN(cerberus_s3_ble_scanner_discovery_diagnostics(NULL) == ESP_ERR_INVALID_ARG);
}

static void discovery_native_complete(void)
{
    struct ble_gap_event complete = {.type = BLE_GAP_EVENT_DISC_COMPLETE};
    radio_active = false;
    radio_callback(&complete, radio_arg);
}

static void check_discovery_native(void)
{
    const uint8_t ordinary[] = {2,1,6};
    const uint8_t findmy[] = {3,3,0x44,0xfd};
    cerberus_s3_ble_discovery_meta_t meta, other;
    cerberus_s3_ble_discovery_bank_t bank, frozen;
    cerberus_s3_ble_discovery_inventory_t history;
    DN(cerberus_s3_ble_discovery_inventory_init(&history, CERBERUS_S3_BLE_FILTER_FIND_MY));
    cerberus_ble_scan_status_payload_t generic;
    DN(!radio_active && !owner && !discovery_attached && !mac_monitor);
    head = tail = 0U;
    expected_active = true;
    clock_base_us = 0U;
    tick = 0U;
    unsigned starts = mac_radio_starts, allocs = allocations;
    DN(cerberus_s3_ble_scanner_discovery_status(1U, &meta) == ESP_ERR_NOT_FOUND);
    DN(all_zero(&meta, sizeof(meta)) && mac_radio_starts == starts && allocations == allocs);
    fail_discovery_allocation = true;
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FIND_MY, 0U, &meta) == ESP_ERR_NO_MEM);
    DN(all_zero(&meta, sizeof(meta)) && !owner && !discovery_attached && mac_radio_starts == starts);
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FIND_MY, 0U, &meta) == ESP_OK);
    uint64_t epoch = meta.scan_epoch;
    DN(epoch && meta.attached && meta.owned && !meta.stopping && meta.started_ms == 0U &&
        meta.status.state == CERBERUS_BLE_SCAN_RUNNING && !radio_active);
    DN(cerberus_s3_ble_scanner_discovery_diagnostics(&other) == ESP_OK &&
        other.scan_epoch == epoch && other.revision == meta.revision && other.owned && other.attached &&
        other.family == CERBERUS_S3_BLE_FILTER_FIND_MY && other.status.generation == meta.status.generation);
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_ERR_NOT_FINISHED);
    DN(cerberus_s3_ble_scanner_start(&generic) == ESP_ERR_INVALID_STATE);
    DN(cerberus_s3_ble_scanner_track_begin((uint8_t[6]){1}, 1U) == ESP_ERR_INVALID_STATE);
    DN(cerberus_s3_ble_scanner_analyzer_begin() == ESP_ERR_INVALID_STATE);
    pump();
    DN(radio_active);
    for (unsigned i = 0; i < 48U; ++i)
        emit_hc_report(ordinary, sizeof(ordinary), -35, false, (uint8_t)i, 3U);
    emit_hc_report(ordinary, sizeof(ordinary), -75, false, 90U, 3U);
    uint32_t original_time = tick;
    emit_hc_report(findmy, sizeof(findmy), -10, true, 90U, 3U);
    unsigned locks = mutex_takes;
    allocs = allocations;
    starts = mac_radio_starts;
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_OK);
    DN(mutex_takes == locks + 1U && allocations == allocs && mac_radio_starts == starts);
    DN(bank.meta.status.stored_count == 1U && bank.meta.binding_overflows == 41U &&
        bank.meta.family == CERBERUS_S3_BLE_FILTER_FIND_MY && bank.meta.source_now_ms == tick);
    DN(bank.records[0].observed_ms == original_time && bank.records[0].peer.rssi == -75 &&
        bank.records[0].focus.discovery.scan_response_observed_ms == tick &&
        bank.records[0].advertisement.length == sizeof(ordinary));
    DN(cerberus_s3_ble_discovery_bank_valid(&bank));
    DN(cerberus_s3_ble_discovery_inventory_apply(&history, &bank) == CERBERUS_BLE_INVENTORY_APPLIED &&
        history.count == 1U && history.entries[0].original_source_ms == original_time);
    const cerberus_s3_ble_discovery_record_t *row = &bank.records[0];
    DN(cerberus_findmy_ui_raw_profile(row->peer.address, row->peer.address_type,
        row->advertisement.address, row->advertisement.address_type, row->advertisement.event_type,
        row->advertisement.flags, row->advertisement.data, row->advertisement.length) == CERBERUS_FINDMY_PROFILE_NONE);
    frozen = bank;
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_OK && bank.meta.revision == frozen.meta.revision);
    DN(cerberus_s3_ble_discovery_inventory_apply(&history, &bank) == CERBERUS_BLE_INVENTORY_DUPLICATE);
    fail_discovery_lock = true;
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_ERR_TIMEOUT && all_zero(&bank, sizeof(bank)));
    fail_discovery_lock = true;
    DN(cerberus_s3_ble_scanner_discovery_diagnostics(&other) == ESP_ERR_TIMEOUT && all_zero(&other, sizeof(other)));
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch + 1U, &bank) == ESP_ERR_NOT_FOUND && all_zero(&bank, sizeof(bank)));
    DN(cerberus_s3_ble_scanner_stop_discovery(epoch + 1U, &other) == ESP_ERR_NOT_FOUND && all_zero(&other, sizeof(other)));
    DN(radio_active);
    emit_hc_report(ordinary, sizeof(ordinary), -50, false, 90U, 3U);
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_OK && bank.meta.status.stored_count == 1U);
    DN(!(bank.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY));
    DN(cerberus_s3_ble_discovery_inventory_apply(&history, &bank) == CERBERUS_BLE_INVENTORY_APPLIED && history.count == 0U);
    DN(frozen.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY);
    /* One coherent invalid record rejects the whole copy before any output. */
    uint8_t saved_length = snapshot->records[0].name_length;
    snapshot->records[0].name_length = 33U;
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_ERR_INVALID_STATE && all_zero(&bank, sizeof(bank)));
    snapshot->records[0].name_length = saved_length;
    snapshot->records[0].focus.discovery.generation ^= 1U;
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_ERR_INVALID_STATE && all_zero(&bank, sizeof(bank)));
    snapshot->records[0].focus.discovery.generation ^= 1U;
    unsigned saved_tick = tick;
    tick = original_time - 1U;
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_ERR_INVALID_STATE && all_zero(&bank, sizeof(bank)));
    tick = saved_tick;
    discovery_native_complete();
    DN(cerberus_s3_ble_scanner_discovery_status(epoch, &meta) == ESP_OK && !meta.owned && meta.attached);
    DN(cerberus_s3_ble_scanner_start_active(&generic) == ESP_ERR_INVALID_STATE);
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FLIPPER, epoch, &other) == ESP_ERR_INVALID_STATE);
    DN(all_zero(&other, sizeof(other)));
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FIND_MY, 0U, &other) == ESP_ERR_INVALID_STATE);
    DN(all_zero(&other, sizeof(other)));

    /* Same-family continuous procedures reuse allocation but never identity;
     * 256 public generation starts cannot make a stale epoch current again. */
    const uint64_t original_epoch = epoch;
    uint8_t original_generation = meta.status.generation;
    allocs = allocations;
    for (unsigned i = 0U; i < 256U; ++i) {
        head = tail = 0U;
        tick += 10U;
        DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FIND_MY, epoch, &meta) == ESP_OK);
        DN(meta.scan_epoch > epoch && meta.attached && meta.owned && meta.status.stored_count == 0U);
        epoch = meta.scan_epoch;
        pump();
        discovery_native_complete();
        DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_OK && bank.meta.status.stored_count == 0U);
        DN(cerberus_s3_ble_discovery_inventory_apply(&history, &bank) == CERBERUS_BLE_INVENTORY_APPLIED &&
            history.count == 0U && history.epoch == epoch);
        DN(cerberus_s3_ble_scanner_copy_discovery(original_epoch, &bank) == ESP_ERR_NOT_FOUND);
    }
    DN(meta.status.generation == original_generation && allocations == allocs);
    DN(cerberus_s3_ble_scanner_release_discovery(original_epoch) == ESP_ERR_NOT_FOUND);
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_OK && !discovery_attached && !discovery_bindings);

    /* Cancellation failure retains the cache and host until real cleanup. */
    head = tail = 0U;
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FIND_MY, 0U, &meta) == ESP_OK);
    epoch = meta.scan_epoch;
    pump();
    mac_cancel_error = 91;
    DN(cerberus_s3_ble_scanner_stop_discovery(epoch, &meta) == ESP_OK && meta.stopping && meta.owned);
    pump();
    DN(cerberus_s3_ble_scanner_discovery_status(epoch, &meta) == ESP_OK && meta.owned && meta.status.error_code == 91);
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_ERR_NOT_FINISHED && discovery_bindings);
    mac_cancel_error = 0;
    DN(cerberus_s3_ble_scanner_stop_discovery(epoch, &meta) == ESP_OK);
    pump();
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_OK);
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_ERR_NOT_FOUND && all_zero(&bank, sizeof(bank)));

    /* Host reset with an old queued Start cannot free before drain or touch
     * the replacement central owner. */
    head = tail = 0U;
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_ALL, 0U, &meta) == ESP_OK);
    epoch = meta.scan_epoch;
    starts = mac_radio_starts;
    ++host_epoch;
    owner = CERBERUS_S3_BLE_OWNER_CENTRAL;
    DN(cerberus_s3_ble_scanner_discovery_diagnostics(&other) == ESP_OK && other.scan_epoch == epoch &&
        other.owned && other.stopping && other.status.state == CERBERUS_BLE_SCAN_ERROR);
    DN(cerberus_s3_ble_scanner_discovery_status(epoch, &meta) == ESP_OK && meta.owned && meta.stopping &&
        meta.status.state == CERBERUS_BLE_SCAN_ERROR);
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_ERR_NOT_FINISHED);
    pump();
    DN(cerberus_s3_ble_scanner_stop_discovery(epoch, &meta) == ESP_OK && !meta.owned);
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_OK && owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    DN(mac_radio_starts == starts);
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    head = tail = 0U;

    /* Accepted asynchronous SDK failure retains the returned own epoch. */
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_ALL, 0U, &meta) == ESP_OK);
    epoch = meta.scan_epoch;
    mac_start_error = 33;
    pump();
    mac_start_error = 0;
    DN(cerberus_s3_ble_scanner_discovery_status(epoch, &meta) == ESP_OK && !meta.owned &&
        meta.status.state == CERBERUS_BLE_SCAN_ERROR && meta.status.error_code == 33);
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_OK);

    /* Accepted Start returns its reservation even if the next telemetry
     * clock observation would roll backwards. Exactly one validated sample
     * anchors the start and deterministic reply. */
    head = tail = 0U;
    clock_base_us = 100000U;
    tick = 0U;
    discovery_clock_rollback = true;
    unsigned before_clock_reads = clock_reads;
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_ALL, 0U, &meta) == ESP_OK);
    epoch = meta.scan_epoch;
    DN(epoch && meta.owned && meta.started_ms == 100U && meta.source_now_ms == 100U &&
        clock_reads == before_clock_reads + 1U);
    DN(cerberus_s3_ble_scanner_discovery_status(epoch, &other) == ESP_ERR_INVALID_STATE && all_zero(&other, sizeof(other)));
    DN(discovery_attached && owner == CERBERUS_S3_BLE_OWNER_SCANNER);
    clock_base_us = 100000U;
    starts = mac_radio_starts;
    DN(cerberus_s3_ble_scanner_stop_discovery(epoch, &meta) == ESP_OK && meta.stopping);
    pump(); /* Cancelled queued Start does not initiate RF. */
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_ERR_NOT_FINISHED);
    pump();
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_OK && mac_radio_starts == starts);
    DN(!radio_active && !owner && !discovery_attached);
    /* No accepted token or foreign attachment leaks from an exhausted ID or
     * a negative source clock before reservation. */
    head = tail = 0U;
    uint64_t saved_epoch = watch_epoch;
    watch_epoch = UINT64_MAX;
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_ALL, 0U, &meta) == ESP_ERR_INVALID_STATE);
    DN(all_zero(&meta, sizeof(meta)) && !owner && !discovery_attached);
    watch_epoch = saved_epoch;
    clock_base_us = UINT64_MAX;
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FIND_MY, 0U, &meta) == ESP_ERR_INVALID_STATE);
    DN(all_zero(&meta, sizeof(meta)) && !owner && !discovery_attached && watch_epoch == saved_epoch);

    /* Full-width source anchors disambiguate low32 timer rollover. */
    head = tail = 0U;
    clock_base_us = ((uint64_t)UINT32_MAX - 1U) * 1000U;
    tick = 0U;
    DN(cerberus_s3_ble_scanner_start_discovery(CERBERUS_S3_BLE_FILTER_FIND_MY, 0U, &meta) == ESP_OK);
    epoch = meta.scan_epoch;
    pump();
    emit_hc_report(ordinary, sizeof(ordinary), -55, false, 5U, 3U);
    emit_hc_report(findmy, sizeof(findmy), -55, true, 5U, 3U);
    DN(cerberus_s3_ble_scanner_copy_discovery(epoch, &bank) == ESP_OK);
    DN(bank.meta.started_ms == (uint64_t)UINT32_MAX - 1U && bank.meta.source_now_ms == (uint64_t)UINT32_MAX + 1U &&
        bank.records[0].observed_ms == UINT32_MAX && bank.records[0].focus.discovery.scan_response_observed_ms == 0U);
    DN(cerberus_s3_ble_discovery_inventory_apply(&history, &bank) == CERBERUS_BLE_INVENTORY_APPLIED &&
        history.count == 1U && history.entries[0].original_source_ms == UINT32_MAX);
    discovery_native_complete();
    DN(cerberus_s3_ble_scanner_release_discovery(epoch) == ESP_OK);
    clock_base_us = 0U;
    tick = 100000U;
    head = tail = 0U;
    expected_active = false;
    starts = mac_radio_starts;
    allocs = allocations;
    DN(cerberus_s3_ble_scanner_discovery_diagnostics(&other) == ESP_OK && all_zero(&other, sizeof(other)));
    DN(mac_radio_starts == starts && allocations == allocs && !discovery_attached);
    printf("Native focused discovery: %u checks; bank %zu bytes; atomic active copies, typed provenance, epoch/rearm/Stop/reset ownership passed.\n",
        discovery_native_checks, sizeof(bank));
}
#undef DN
