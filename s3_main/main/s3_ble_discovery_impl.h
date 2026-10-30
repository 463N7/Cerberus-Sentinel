/* Native discovery attachment, included in the existing scanner unit. */

static bool discovery_owns_host_locked(void)
{
    return scan_host_epoch == cerberus_s3_ble_host_epoch() &&
        cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_SCANNER;
}

static bool discovery_pending_locked(void)
{
    return (start_event_pending && start_queued_epoch == discovery_scan_epoch) ||
        (stop_event_pending && stop_epoch == discovery_scan_epoch);
}

static bool discovery_meta_at_locked(cerberus_s3_ble_discovery_meta_t *meta, uint64_t source_now_ms)
{
    if (!discovery_attached || source_now_ms < discovery_started_ms) return false;
    *meta = (cerberus_s3_ble_discovery_meta_t){
        .scan_epoch = discovery_epoch,
        .revision = snapshot->revision,
        .started_ms = discovery_started_ms,
        .source_now_ms = source_now_ms,
        .status = snapshot->status,
        .family = snapshot->family,
        .replacements = snapshot->inventory_replacements,
        .binding_overflows = snapshot->bindings ? snapshot->bindings->overflows : 0U,
        .attached = true,
        .owned = snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING ||
            discovery_pending_locked() || discovery_owns_host_locked(),
        .stopping = cancel_requested || (stop_event_pending && stop_epoch == discovery_scan_epoch) ||
            (snapshot->status.state != CERBERUS_BLE_SCAN_RUNNING && discovery_pending_locked()),
    };
    return true;
}

static bool discovery_meta_locked(cerberus_s3_ble_discovery_meta_t *meta)
{
    int64_t clock_us = esp_timer_get_time();
    return clock_us >= 0 && discovery_meta_at_locked(meta, (uint64_t)clock_us / 1000U);
}

static esp_err_t discovery_lock(uint64_t expected_epoch)
{
    if (!expected_epoch) return ESP_ERR_INVALID_ARG;
    if (!scan_mutex || !snapshot) return ESP_ERR_NOT_FOUND;
    if (!xSemaphoreTake(scan_mutex, pdMS_TO_TICKS(25U))) return ESP_ERR_TIMEOUT;
    reconcile_host_locked();
    if (!discovery_attached || discovery_epoch != expected_epoch) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_start_discovery(cerberus_s3_ble_filter_t family,
    uint64_t expected_previous_epoch, cerberus_s3_ble_discovery_meta_t *meta)
{
    if (meta) memset(meta, 0, sizeof(*meta));
    if (!meta || (unsigned)family >= CERBERUS_S3_BLE_FILTER_COUNT) return ESP_ERR_INVALID_ARG;
    cerberus_ble_scan_status_payload_t status;
    return start_scan_mode(true, NULL, &status, true, family, expected_previous_epoch, meta);
}

esp_err_t cerberus_s3_ble_scanner_discovery_status(uint64_t expected_epoch,
    cerberus_s3_ble_discovery_meta_t *meta)
{
    if (meta) memset(meta, 0, sizeof(*meta));
    if (!meta) return ESP_ERR_INVALID_ARG;
    esp_err_t error = discovery_lock(expected_epoch);
    if (error != ESP_OK) return error;
    bool valid = discovery_meta_locked(meta);
    xSemaphoreGive(scan_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_discovery_diagnostics(cerberus_s3_ble_discovery_meta_t *meta)
{
    if (meta) memset(meta, 0, sizeof(*meta));
    if (!meta) return ESP_ERR_INVALID_ARG;
    if (!scan_mutex || !snapshot) return ESP_OK;
    if (!xSemaphoreTake(scan_mutex, pdMS_TO_TICKS(25U))) return ESP_ERR_TIMEOUT;
    reconcile_host_locked();
    bool valid = !discovery_attached || discovery_meta_locked(meta);
    xSemaphoreGive(scan_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static bool discovery_time_valid(const cerberus_s3_ble_discovery_meta_t *meta, uint32_t observed)
{
    if (meta->source_now_ms < meta->started_ms ||
        meta->source_now_ms - meta->started_ms > UINT32_MAX) return false;
    uint32_t elapsed = (uint32_t)meta->source_now_ms - observed;
    return meta->source_now_ms >= elapsed && meta->source_now_ms - elapsed >= meta->started_ms;
}

static bool discovery_row_valid(const cerberus_s3_ble_record_t *record,
    const cerberus_s3_ble_discovery_meta_t *meta)
{
    const cerberus_s3_ble_discovery_evidence_t *source = &record->focus.discovery;
    return record->address_type <= 3U && record->name_length <= CERBERUS_BLE_NAME_MAX_LENGTH &&
        (record->uuid_length == 0U || record->uuid_length == 2U ||
         record->uuid_length == 4U || record->uuid_length == 16U) &&
        record->advertisement_length <= CERBERUS_BLE_ADVERTISEMENT_MAX_LENGTH &&
        source->generation == meta->status.generation &&
        source->address_type == record->address_type &&
        memcmp(source->address, record->address, 6U) == 0 &&
        discovery_time_valid(meta, record->last_advertisement_ms) &&
        (!source->advertisement_sequence || discovery_time_valid(meta, source->advertisement_observed_ms)) &&
        (!(source->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) ||
         discovery_time_valid(meta, source->scan_response_observed_ms)) &&
        (!record->focus.ibeacon_valid || discovery_time_valid(meta, record->focus.ibeacon_observed_ms));
}

static void discovery_copy_row(const cerberus_s3_ble_record_t *record,
    uint8_t generation, uint8_t index, cerberus_s3_ble_discovery_record_t *out)
{
    memcpy(out->peer.address, record->address, 6U);
    memcpy(out->peer.name, record->name, record->name_length);
    out->peer.rssi = record->rssi;
    out->peer.address_type = record->address_type;
    out->peer.manufacturer_id = record->manufacturer_id;
    out->peer.flags = record->flags;
    out->peer.uuid_length = record->uuid_length;
    memcpy(out->peer.uuid, record->uuid, record->uuid_length);
    memcpy(out->advertisement.address, record->address, 6U);
    out->advertisement.address_type = record->address_type;
    out->advertisement.length = record->advertisement_length;
    out->advertisement.event_type = record->advertisement_event_type;
    out->advertisement.flags = record->advertisement_flags;
    out->advertisement.reports_seen = record->reports_seen;
    memcpy(out->advertisement.data, record->advertisement, record->advertisement_length);
    out->focus = record->focus;
    out->observed_ms = record->last_advertisement_ms;
    out->generation = generation;
    out->index = index;
}

esp_err_t cerberus_s3_ble_scanner_copy_discovery(uint64_t expected_epoch,
    cerberus_s3_ble_discovery_bank_t *bank)
{
    if (bank) memset(bank, 0, sizeof(*bank));
    if (!bank) return ESP_ERR_INVALID_ARG;
    esp_err_t error = discovery_lock(expected_epoch);
    if (error != ESP_OK) return error;
    bool valid = (snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING ||
        snapshot->status.state == CERBERUS_BLE_SCAN_COMPLETE) &&
        snapshot->status.stored_count <= CERBERUS_BLE_SCAN_MAX_RESULTS;
    if (valid) valid = discovery_meta_locked(&bank->meta);
    if (valid) valid = bank->meta.source_now_ms - bank->meta.started_ms <= UINT32_MAX;
    for (size_t i = 0U; valid && i < snapshot->status.stored_count; ++i)
        valid = discovery_row_valid(&snapshot->records[i], &bank->meta);
    for (size_t i = 0U; valid && i < snapshot->status.stored_count; ++i)
        discovery_copy_row(&snapshot->records[i], snapshot->status.generation, (uint8_t)i,
            &bank->records[i]);
    xSemaphoreGive(scan_mutex);
    if (!valid) memset(bank, 0, sizeof(*bank));
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_stop_discovery(uint64_t expected_epoch,
    cerberus_s3_ble_discovery_meta_t *meta)
{
    if (meta) memset(meta, 0, sizeof(*meta));
    if (!meta) return ESP_ERR_INVALID_ARG;
    esp_err_t error = discovery_lock(expected_epoch);
    if (error != ESP_OK) return error;
    bool queue_stop = false;
    if (snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING) {
        if (!cancel_requested && snapshot->revision < UINT64_MAX - 1U) ++snapshot->revision;
        cancel_requested = true;
        if (!stop_event_pending) {
            stop_event_pending = true;
            stop_epoch = scan_epoch;
            ble_npl_event_set_arg(&stop_event, (void *)(uintptr_t)scan_epoch);
            queue_stop = true;
        }
    } else if (!discovery_pending_locked() && discovery_owns_host_locked()) {
        /* Retry a failed host release only for this exact lease. */
        error = cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_SCANNER);
    }
    bool valid = discovery_meta_locked(meta);
    xSemaphoreGive(scan_mutex);
    if (queue_stop) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &stop_event);
    if (error != ESP_OK || !valid) memset(meta, 0, sizeof(*meta));
    return error != ESP_OK ? error : valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_release_discovery(uint64_t expected_epoch)
{
    esp_err_t error = discovery_lock(expected_epoch);
    if (error != ESP_OK) return error;
    if (snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING ||
        discovery_pending_locked() || discovery_owns_host_locked()) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_NOT_FINISHED;
    }
    cerberus_s3_ble_binding_cache_t *released = discovery_bindings;
    discovery_bindings = NULL;
    snapshot->bindings = NULL;
    discovery_attached = false;
    xSemaphoreGive(scan_mutex);
    heap_caps_free(released);
    return ESP_OK;
}
