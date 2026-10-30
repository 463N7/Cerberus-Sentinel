/* Private to wifi_scanner.c. SDK operations run only on its existing worker.
 * Commands serialize with scanner_mutex; the callback uses only csi_mux. */
static portMUX_TYPE csi_mux = portMUX_INITIALIZER_UNLOCKED;
static cerberus_csi_start_request_t csi_request;
static cerberus_csi_status_t csi_status;
static cerberus_csi_sample_t *csi_ring;
static uint64_t csi_boot_id, csi_epoch_counter, csi_armed_us, csi_deadline_us, csi_clock_floor;
static uint32_t csi_token, csi_inflight, csi_sequence, csi_last_ack;
static uint32_t csi_accepted, csi_invalid, csi_overflow, csi_acked;
static uint8_t csi_head, csi_count;
static bool csi_accepting, csi_saved, csi_disable_pending, csi_rx_disable_pending;
static bool csi_callback_registered, csi_sequence_exhausted;
static wifi_band_mode_t csi_previous_mode;
static uint8_t csi_previous_channel;
static wifi_second_chan_t csi_previous_second;
static wifi_promiscuous_filter_t csi_previous_filter;
static wifi_csi_config_t csi_previous_config;

static void csi_increment(uint32_t *value) { if (*value != UINT32_MAX) ++*value; }
static void csi_boot_locked(void)
{
    if (!csi_boot_id) {
        esp_fill_random(&csi_boot_id, sizeof(csi_boot_id));
        if (!csi_boot_id) csi_boot_id = 1U;
    }
}

static bool csi_channel_valid(uint8_t channel)
{
    return (channel >= 1U && channel <= 14U) ||
        (channel >= 36U && channel <= 64U && !(channel % 4U)) ||
        (channel >= 100U && channel <= 144U && !(channel % 4U)) ||
        (channel >= 149U && channel <= 177U && (channel - 149U) % 4U == 0U);
}

static bool csi_binding_valid(const cerberus_csi_binding_t *binding, bool allow_zero)
{
    return binding && csi_boot_id && binding->boot_id == csi_boot_id &&
        binding->job_id && binding->job_id == csi_status.job_id &&
        csi_status.epoch && (binding->epoch == csi_status.epoch || (allow_zero && !binding->epoch));
}

static void csi_status_locked(cerberus_csi_status_t *out)
{
    *out = csi_status;
    out->version = CERBERUS_CSI_VERSION;
    out->flags = atomic_load_explicit(&csi_radio_reserved, memory_order_acquire) ? CERBERUS_CSI_FLAG_OWNED : 0U;
    portENTER_CRITICAL(&csi_mux);
    out->queued = csi_count;
    if (csi_accepting) out->flags |= CERBERUS_CSI_FLAG_RECEIVING;
    out->channel = csi_accepting ? csi_request.channel : 0U;
    portEXIT_CRITICAL(&csi_mux);
}

/* The separate, never-reused pointer-sized token rejects delayed SDK delivery
 * across jobs on this 32-bit CPU. Full epoch identity remains in every record
 * binding. Source time and clock watermark are sampled under the same lock. */
static void wifi_csi_callback(void *context, wifi_csi_info_t *info)
{
    int64_t entry = esp_timer_get_time();
    uint32_t token = (uint32_t)(uintptr_t)context;
    uint64_t epoch, source;
    portENTER_CRITICAL(&csi_mux);
    int64_t now = esp_timer_get_time();
    if (!info || !csi_accepting || !csi_ring || !token || token != csi_token ||
        entry < 0 || (uint64_t)entry < csi_armed_us || now < entry ||
        (uint64_t)now < csi_clock_floor || (uint64_t)now >= csi_deadline_us) {
        portEXIT_CRITICAL(&csi_mux); return;
    }
    epoch = csi_status.epoch; source = (uint64_t)now; csi_clock_floor = source; ++csi_inflight;
    portEXIT_CRITICAL(&csi_mux);

    bool target = !memcmp(info->mac, csi_request.bssid, 6U);
    bool valid = target && info->buf && info->len == CERBERUS_CSI_BYTES &&
        info->rx_ctrl.rx_state == 0U && info->rx_ctrl.rx_channel_estimate_info_vld &&
        ((info->rx_ctrl.cur_bb_format >= 1U && info->rx_ctrl.cur_bb_format <= 7U) ||
            info->rx_ctrl.cur_bb_format == 11U) &&
        info->rx_ctrl.channel == csi_request.channel && info->rx_ctrl.second <= WIFI_SECOND_CHAN_BELOW &&
        info->rx_ctrl.rssi >= -127 && info->rx_ctrl.rssi <= 20;
    cerberus_csi_sample_t sample = {0};
    if (valid) {
        sample.source_us = source; sample.rssi = info->rx_ctrl.rssi;
        sample.channel = info->rx_ctrl.channel; sample.secondary = info->rx_ctrl.second;
        sample.phy = info->rx_ctrl.cur_bb_format;
        sample.flags = CERBERUS_CSI_SAMPLE_ESTIMATE_VALID |
            (info->first_word_invalid ? CERBERUS_CSI_SAMPLE_FIRST_INVALID : 0U);
        sample.format = CERBERUS_CSI_FORMAT_LLTF_I8;
        memcpy(sample.data, info->buf, sizeof(sample.data));
    }
    portENTER_CRITICAL(&csi_mux);
    now = esp_timer_get_time();
    if (csi_accepting && csi_ring && token == csi_token && epoch == csi_status.epoch &&
        now >= 0 && (uint64_t)now >= source && (uint64_t)now < csi_deadline_us) {
        if (target && !valid) csi_increment(&csi_invalid);
        else if (valid && csi_sequence == UINT32_MAX) {
            csi_sequence_exhausted = true; csi_accepting = false;
        } else if (valid && csi_count == CERBERUS_CSI_RING_CAPACITY) csi_increment(&csi_overflow);
        else if (valid) {
            sample.sequence = ++csi_sequence;
            csi_ring[(csi_head + csi_count) % CERBERUS_CSI_RING_CAPACITY] = sample;
            ++csi_count; csi_increment(&csi_accepted);
        }
    }
    --csi_inflight;
    portEXIT_CRITICAL(&csi_mux);
}

static void csi_end_locked(esp_err_t error)
{
    csi_status.state = error == ESP_OK ? CERBERUS_CSI_STOPPING : CERBERUS_CSI_ERROR;
    if (error != ESP_OK) csi_status.error_code = error;
    portENTER_CRITICAL(&csi_mux); csi_accepting = false; portEXIT_CRITICAL(&csi_mux);
}

static esp_err_t csi_pause_locked(void)
{
    portENTER_CRITICAL(&csi_mux); csi_accepting = false; portEXIT_CRITICAL(&csi_mux);
    esp_err_t error = ESP_OK;
    if (csi_disable_pending) {
        error = esp_wifi_set_csi(false);
        if (error != ESP_OK) return error;
        csi_disable_pending = false;
    }
    if (csi_rx_disable_pending) {
        error = esp_wifi_set_promiscuous(false);
        if (error != ESP_OK) return error;
        csi_rx_disable_pending = false;
    }
    int64_t started = esp_timer_get_time();
    for (;;) {
        portENTER_CRITICAL(&csi_mux); uint32_t active = csi_inflight; portEXIT_CRITICAL(&csi_mux);
        if (!active) break;
        int64_t now = esp_timer_get_time();
        if (started < 0 || now < started || now - started >= 50000) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (csi_callback_registered) {
        error = esp_wifi_set_csi_rx_cb(NULL, NULL);
        if (error == ESP_OK) csi_callback_registered = false;
    }
    return error;
}

static void csi_cleanup_locked(void)
{
    esp_err_t error = csi_pause_locked();
    if (error == ESP_OK && csi_saved) {
        error = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        if (error == ESP_OK && csi_previous_channel) {
            error = esp_wifi_set_band(csi_previous_channel <= 14U ? WIFI_BAND_2G : WIFI_BAND_5G);
            if (error == ESP_OK) error = esp_wifi_set_channel(csi_previous_channel, csi_previous_second);
        }
        if (error == ESP_OK) error = esp_wifi_set_band_mode(csi_previous_mode);
        if (error == ESP_OK) error = esp_wifi_set_promiscuous_filter(&csi_previous_filter);
        if (error == ESP_OK) error = esp_wifi_set_csi_config(&csi_previous_config);
        if (error == ESP_OK) csi_saved = false;
    }
    if (error != ESP_OK) { csi_end_locked(error); return; }
    portENTER_CRITICAL(&csi_mux);
    cerberus_csi_sample_t *ring = csi_ring; csi_ring = NULL; csi_head = csi_count = 0U;
    portEXIT_CRITICAL(&csi_mux);
    free(ring);
    csi_status.state = csi_status.error_code ? CERBERUS_CSI_ERROR : CERBERUS_CSI_COMPLETE;
    atomic_store_explicit(&csi_radio_reserved, false, memory_order_release);
}

static esp_err_t csi_prepare_locked(void)
{
    bool promiscuous = false;
    esp_err_t error = esp_wifi_get_promiscuous(&promiscuous);
    if (error != ESP_OK || promiscuous) return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    error = esp_wifi_get_csi_config(&csi_previous_config);
    if (error != ESP_OK || csi_previous_config.enable) return error == ESP_OK ? ESP_ERR_INVALID_STATE : error;
    error = esp_wifi_get_band_mode(&csi_previous_mode);
    if (error == ESP_OK) error = esp_wifi_get_channel(&csi_previous_channel, &csi_previous_second);
    if (error == ESP_OK) error = esp_wifi_get_promiscuous_filter(&csi_previous_filter);
    if (error != ESP_OK) return error;
    csi_saved = true;
    error = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    if (error == ESP_OK) error = esp_wifi_set_band(csi_request.channel <= 14U ? WIFI_BAND_2G : WIFI_BAND_5G);
    if (error == ESP_OK) error = esp_wifi_set_channel(csi_request.channel, WIFI_SECOND_CHAN_NONE);
    if (error == ESP_OK) {
        int64_t tuned = esp_timer_get_time();
        if (tuned < 0 || (uint64_t)tuned < csi_clock_floor || (uint64_t)tuned >= csi_deadline_us)
            error = ESP_ERR_TIMEOUT;
    }
    wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
    if (error == ESP_OK) error = esp_wifi_set_promiscuous_filter(&filter);
    wifi_csi_config_t config = {
        .enable = 1, .acquire_csi_legacy = 1, .acquire_csi_force_lltf = 1,
        .acquire_csi_ht20 = 1, .acquire_csi_ht40 = 1, .acquire_csi_vht = 1,
        .acquire_csi_su = 1, .acquire_csi_mu = 1, .acquire_csi_dcm = 1,
        .acquire_csi_beamformed = 1, .acquire_csi_he_stbc_mode = ESP_CSI_ACQUIRE_STBC_HELTF1,
        .val_scale_cfg = 0, .dump_ack_en = 0, .lltf_bit_mode = 1,
    };
    if (error == ESP_OK) error = esp_wifi_set_csi_rx_cb(wifi_csi_callback, (void *)(uintptr_t)csi_token);
    if (error == ESP_OK) csi_callback_registered = true;
    if (error == ESP_OK) { csi_disable_pending = true; error = esp_wifi_set_csi_config(&config); }
    if (error == ESP_OK) error = esp_wifi_set_csi(true);
    if (error == ESP_OK) { csi_rx_disable_pending = true; error = esp_wifi_set_promiscuous(true); }
    if (error != ESP_OK) return error;
    portENTER_CRITICAL(&csi_mux);
    int64_t armed = esp_timer_get_time();
    if (armed < 0 || (uint64_t)armed < csi_clock_floor || (uint64_t)armed >= csi_deadline_us) error = ESP_ERR_TIMEOUT;
    else { csi_clock_floor = csi_armed_us = (uint64_t)armed; csi_accepting = true; }
    portEXIT_CRITICAL(&csi_mux);
    return error;
}

static void csi_worker_tick(void)
{
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (!atomic_load_explicit(&csi_radio_reserved, memory_order_acquire)) { xSemaphoreGive(scanner_mutex); return; }
    portENTER_CRITICAL(&csi_mux);
    int64_t now = esp_timer_get_time();
    bool expired = now < 0 || (uint64_t)now < csi_clock_floor || (uint64_t)now >= csi_deadline_us;
    bool exhausted = csi_sequence_exhausted;
    portEXIT_CRITICAL(&csi_mux);
    if (csi_status.state == CERBERUS_CSI_STARTING || csi_status.state == CERBERUS_CSI_RUNNING) {
        if (expired || exhausted) csi_end_locked(expired ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE);
        else if (csi_status.state == CERBERUS_CSI_STARTING) {
            esp_err_t error = csi_prepare_locked();
            if (error != ESP_OK) csi_end_locked(error); else csi_status.state = CERBERUS_CSI_RUNNING;
        }
    }
    if (csi_status.state == CERBERUS_CSI_STOPPING || csi_status.state == CERBERUS_CSI_ERROR) csi_cleanup_locked();
    xSemaphoreGive(scanner_mutex);
}

esp_err_t cerberus_wifi_csi_start(const cerberus_csi_start_request_t *request, cerberus_csi_status_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    uint8_t any = 0U;
    if (request) for (unsigned i = 0; i < 6U; ++i) any |= request->bssid[i];
    if (!request || !out || !request->job_id || !any || (request->bssid[0] & 1U) ||
        !csi_channel_valid(request->channel) || request->version != CERBERUS_CSI_VERSION ||
        request->lease_ms < CERBERUS_CSI_LEASE_MIN_MS || request->lease_ms > CERBERUS_CSI_LEASE_MAX_MS)
        return ESP_ERR_INVALID_ARG;
    if (!scanner_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    csi_boot_locked();
    if (request->boot_id != csi_boot_id) { xSemaphoreGive(scanner_mutex); return ESP_ERR_INVALID_STATE; }
    if (request->job_id == csi_request.job_id) {
        esp_err_t error = memcmp(request, &csi_request, sizeof(*request)) ? ESP_ERR_INVALID_ARG : ESP_OK;
        if (error == ESP_OK) csi_status_locked(out);
        xSemaphoreGive(scanner_mutex); return error;
    }
    int64_t now = esp_timer_get_time();
    if (!scanner_ready || !scanner_task_handle || request->job_id < csi_request.job_id ||
        now < 0 || csi_token == UINT32_MAX || csi_epoch_counter == UINT64_MAX ||
        another_radio_operation_is_active() || cerberus_c5_test_running() || station_reserved ||
        atomic_load_explicit(&channel_survey_radio_reserved, memory_order_acquire) ||
        channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING ||
        scan_status.state == CERBERUS_WIFI_SCAN_RUNNING || defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING ||
        wifi_connection_is_active_locked()) { xSemaphoreGive(scanner_mutex); return ESP_ERR_INVALID_STATE; }
    cerberus_csi_sample_t *ring = heap_caps_calloc(CERBERUS_CSI_RING_CAPACITY, sizeof(*ring), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ring) { xSemaphoreGive(scanner_mutex); return ESP_ERR_NO_MEM; }
    if (!csi_epoch_counter) {
        esp_fill_random(&csi_epoch_counter, sizeof(csi_epoch_counter));
        if (!csi_epoch_counter) csi_epoch_counter = 1U;
        if (csi_epoch_counter == UINT64_MAX) { free(ring); xSemaphoreGive(scanner_mutex); return ESP_ERR_INVALID_STATE; }
    }
    portENTER_CRITICAL(&csi_mux);
    csi_request = *request; memset(&csi_status, 0, sizeof(csi_status));
    csi_status.job_id = request->job_id; csi_status.epoch = ++csi_epoch_counter;
    csi_status.state = CERBERUS_CSI_STARTING; ++csi_token;
    csi_ring = ring; csi_head = csi_count = 0U; csi_sequence = csi_last_ack = 0U;
    csi_accepted = csi_invalid = csi_overflow = csi_acked = 0U;
    csi_sequence_exhausted = csi_accepting = false;
    csi_clock_floor = csi_armed_us = (uint64_t)now;
    csi_deadline_us = (uint64_t)now + (uint64_t)request->lease_ms * 1000U;
    portEXIT_CRITICAL(&csi_mux);
    csi_saved = csi_disable_pending = csi_rx_disable_pending = csi_callback_registered = false;
    atomic_store_explicit(&csi_radio_reserved, true, memory_order_release);
    csi_status_locked(out); xSemaphoreGive(scanner_mutex); xTaskNotifyGive(scanner_task_handle);
    return ESP_OK;
}

esp_err_t cerberus_wifi_csi_get_status(const cerberus_csi_binding_t *binding, cerberus_csi_status_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !binding || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    /* A boot-bound status read reports authoritative current/last identity,
     * including an absent old job. It never adopts or changes that job. */
    bool valid = binding->boot_id && binding->boot_id == csi_boot_id;
    if (valid) csi_status_locked(out);
    xSemaphoreGive(scanner_mutex); return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_wifi_csi_stop(const cerberus_csi_binding_t *binding, cerberus_csi_status_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool valid = csi_binding_valid(binding, true);
    if (valid) {
        if (atomic_load_explicit(&csi_radio_reserved, memory_order_acquire)) csi_end_locked(ESP_OK);
        csi_status_locked(out);
    }
    xSemaphoreGive(scanner_mutex);
    if (valid) xTaskNotifyGive(scanner_task_handle);
    return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_wifi_csi_renew(const cerberus_csi_renew_request_t *request, cerberus_csi_status_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!request || !out || !scanner_mutex || request->lease_ms < CERBERUS_CSI_LEASE_MIN_MS ||
        request->lease_ms > CERBERUS_CSI_LEASE_MAX_MS) return ESP_ERR_INVALID_ARG;
    cerberus_csi_binding_t binding = {.boot_id=request->boot_id, .job_id=request->job_id, .epoch=request->epoch};
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool valid = csi_binding_valid(&binding, false);
    if (valid) {
        if (csi_status.state == CERBERUS_CSI_STARTING || csi_status.state == CERBERUS_CSI_RUNNING) {
            portENTER_CRITICAL(&csi_mux);
            int64_t now = esp_timer_get_time();
            bool expired = now < 0 || (uint64_t)now < csi_clock_floor || (uint64_t)now >= csi_deadline_us;
            if (!expired) csi_deadline_us = (uint64_t)now + (uint64_t)request->lease_ms * 1000U;
            portEXIT_CRITICAL(&csi_mux);
            if (expired) csi_end_locked(ESP_ERR_TIMEOUT);
        }
        csi_status_locked(out);
    }
    xSemaphoreGive(scanner_mutex); return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_wifi_csi_get_clock(const cerberus_csi_binding_t *binding, cerberus_csi_clock_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    esp_err_t error = csi_binding_valid(binding, false) ? ESP_OK : ESP_ERR_NOT_FOUND;
    portENTER_CRITICAL(&csi_mux);
    int64_t now = esp_timer_get_time();
    if (error == ESP_OK && (now < 0 || (uint64_t)now < csi_clock_floor)) error = ESP_ERR_INVALID_STATE;
    if (error == ESP_OK) {
        out->epoch = csi_status.epoch; out->source_now_us = (uint64_t)now; out->accepted = csi_accepted;
        csi_clock_floor = (uint64_t)now;
        out->version = CERBERUS_CSI_VERSION;
        uint32_t pending = csi_count + csi_inflight; out->pending = pending > UINT8_MAX ? UINT8_MAX : (uint8_t)pending;
    }
    portEXIT_CRITICAL(&csi_mux); xSemaphoreGive(scanner_mutex); return error;
}

esp_err_t cerberus_wifi_csi_get_counters(const cerberus_csi_binding_t *binding, cerberus_csi_counters_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool valid = csi_binding_valid(binding, false);
    if (valid) {
        portENTER_CRITICAL(&csi_mux);
        out->epoch = csi_status.epoch; out->invalid = csi_invalid; out->overflow = csi_overflow;
        out->acked = csi_acked; out->version = CERBERUS_CSI_VERSION;
        portEXIT_CRITICAL(&csi_mux);
    }
    xSemaphoreGive(scanner_mutex); return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_wifi_csi_peek(const cerberus_csi_binding_t *binding, cerberus_csi_peek_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool valid = csi_binding_valid(binding, false);
    if (valid) {
        portENTER_CRITICAL(&csi_mux);
        out->epoch = csi_status.epoch; out->queued = csi_count; out->format = CERBERUS_CSI_FORMAT_LLTF_I8;
        if (csi_ring && csi_count) {
            const cerberus_csi_sample_t *sample = &csi_ring[csi_head];
            out->sequence = sample->sequence; out->length = sizeof(*sample);
            out->crc32 = cerberus_csi_crc32(sample, sizeof(*sample));
            out->format = sample->format;
        }
        portEXIT_CRITICAL(&csi_mux);
    }
    xSemaphoreGive(scanner_mutex); return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_wifi_csi_read(const cerberus_csi_read_request_t *request, cerberus_csi_chunk_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!request || !out || !scanner_mutex || request->offset >= sizeof(cerberus_csi_sample_t) ||
        request->offset % CERBERUS_CSI_CHUNK_BYTES) return ESP_ERR_INVALID_ARG;
    cerberus_csi_binding_t binding = {.boot_id=csi_boot_id, .job_id=request->job_id, .epoch=request->epoch};
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    portENTER_CRITICAL(&csi_mux);
    bool valid = csi_binding_valid(&binding, false) && csi_ring && csi_count &&
        request->sequence == csi_ring[csi_head].sequence;
    if (valid) {
        out->offset = request->offset; size_t remaining = sizeof(cerberus_csi_sample_t) - request->offset;
        out->length = remaining < CERBERUS_CSI_CHUNK_BYTES ? remaining : CERBERUS_CSI_CHUNK_BYTES;
        memcpy(out->data, (const uint8_t *)&csi_ring[csi_head] + request->offset, out->length);
    }
    portEXIT_CRITICAL(&csi_mux); xSemaphoreGive(scanner_mutex); return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_wifi_csi_ack(const cerberus_csi_ack_request_t *request, cerberus_csi_status_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!request || !request->sequence || !out || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    cerberus_csi_binding_t binding = {.boot_id=csi_boot_id, .job_id=request->job_id, .epoch=request->epoch};
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    portENTER_CRITICAL(&csi_mux);
    bool valid = csi_binding_valid(&binding, false) && (request->sequence == csi_last_ack ||
        (csi_ring && csi_count && request->sequence == csi_ring[csi_head].sequence));
    if (valid && request->sequence != csi_last_ack) {
        csi_last_ack = request->sequence; csi_head = (csi_head + 1U) % CERBERUS_CSI_RING_CAPACITY;
        --csi_count; csi_increment(&csi_acked);
    }
    portEXIT_CRITICAL(&csi_mux);
    if (valid) csi_status_locked(out);
    xSemaphoreGive(scanner_mutex); return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_wifi_csi_get_info(cerberus_csi_info_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    csi_boot_locked(); int64_t now = esp_timer_get_time();
    if (now >= 0) {
        out->boot_id = csi_boot_id; out->source_now_us = (uint64_t)now;
        out->version = CERBERUS_CSI_VERSION; out->format = CERBERUS_CSI_FORMAT_LLTF_I8;
        out->capacity = CERBERUS_CSI_RING_CAPACITY;
    }
    xSemaphoreGive(scanner_mutex); return now >= 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_wifi_csi_get_target(const cerberus_csi_binding_t *binding, cerberus_csi_target_payload_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool valid = csi_binding_valid(binding, false);
    if (valid) {
        out->epoch = csi_status.epoch; memcpy(out->bssid, csi_request.bssid, 6U);
        out->channel = csi_request.channel; out->version = CERBERUS_CSI_VERSION;
        out->lease_ms = csi_request.lease_ms;
    }
    xSemaphoreGive(scanner_mutex); return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}
