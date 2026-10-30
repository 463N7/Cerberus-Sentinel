/* Private to wifi_scanner.c. Shares its STA radio, RX callback and mutex. */

static const uint8_t channel_survey_24[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
};
static const uint8_t channel_survey_5[] = {
    36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116,
    120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165,
};
_Static_assert(sizeof(channel_survey_5) <= CERBERUS_WIFI_CHANNEL_SURVEY_MAX_RESULTS,
    "channel survey result array is too small");

static void channel_survey_receive(const wifi_promiscuous_pkt_t *packet,
    wifi_promiscuous_pkt_type_t type)
{
    if (!packet || type == WIFI_PKT_MISC || packet->rx_ctrl.rx_state != 0 ||
        packet->rx_ctrl.sig_len <= 4U) return;
    portENTER_CRITICAL(&channel_survey_mux);
    if (channel_survey_receiving &&
        packet->rx_ctrl.channel == channel_survey_receive_channel) {
        if (channel_survey_packet_count < UINT32_MAX) ++channel_survey_packet_count;
        channel_survey_rssi_sum += packet->rx_ctrl.rssi;
        if (packet->rx_ctrl.rssi > channel_survey_strongest_rssi)
            channel_survey_strongest_rssi = packet->rx_ctrl.rssi;
    }
    portEXIT_CRITICAL(&channel_survey_mux);
}

static void channel_survey_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        bool running = channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING;
        uint8_t generation = channel_survey_status.generation;
        uint8_t band = channel_survey_status.band;
        uint8_t count = channel_survey_status.result_count;
        xSemaphoreGive(scanner_mutex);
        if (!running) continue;

        esp_err_t error = ESP_OK;
        bool promiscuous = false;
        if (!atomic_load_explicit(&channel_survey_cancelled, memory_order_acquire)) {
            wifi_promiscuous_filter_t filter = {
                .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                    WIFI_PROMIS_FILTER_MASK_CTRL | WIFI_PROMIS_FILTER_MASK_DATA,
            };
            error = esp_wifi_set_promiscuous_filter(&filter);
            if (error == ESP_OK) {
                error = esp_wifi_set_promiscuous(true);
                promiscuous = error == ESP_OK;
                if (promiscuous)
                    atomic_store_explicit(&channel_survey_radio_reserved, true,
                        memory_order_release);
            }
        }

        while (error == ESP_OK && promiscuous &&
            !atomic_load_explicit(&channel_survey_cancelled, memory_order_acquire)) {
            uint8_t successful_channels = 0;
            esp_err_t first_channel_error = ESP_OK;
            for (uint8_t index = 0; index < count; ++index) {
                if (atomic_load_explicit(&channel_survey_cancelled, memory_order_acquire)) break;
                /* This background observer yields to operations using the same C5 radio. */
                if (another_radio_operation_is_active() || cerberus_c5_test_running()) {
                    atomic_store_explicit(&channel_survey_cancelled, true, memory_order_release);
                    break;
                }
                uint8_t channel = channel_survey_channels[index];
                xSemaphoreTake(scanner_mutex, portMAX_DELAY);
                channel_survey_status.current_channel = channel;
                xSemaphoreGive(scanner_mutex);
                esp_err_t next = esp_wifi_set_band(
                    band == CERBERUS_WIFI_CHANNEL_SURVEY_BAND_24 ? WIFI_BAND_2G : WIFI_BAND_5G);
                if (next == ESP_OK)
                    next = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
                if (next != ESP_OK) {
                    if (first_channel_error == ESP_OK) first_channel_error = next;
                    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
                    channel_survey_status.completed_count = (uint8_t)(index + 1U);
                    xSemaphoreGive(scanner_mutex);
                    continue; /* Country and driver support determine usable channels. */
                }
                ++successful_channels;
                portENTER_CRITICAL(&channel_survey_mux);
                channel_survey_receive_channel = channel;
                channel_survey_packet_count = 0;
                channel_survey_rssi_sum = 0;
                channel_survey_strongest_rssi = -127;
                channel_survey_receiving = true;
                portEXIT_CRITICAL(&channel_survey_mux);
                uint16_t elapsed = 0;
                while (elapsed < CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS &&
                    !atomic_load_explicit(&channel_survey_cancelled, memory_order_acquire)) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    elapsed += 10U;
                }
                uint32_t packets;
                int64_t rssi_sum;
                int8_t strongest;
                portENTER_CRITICAL(&channel_survey_mux);
                channel_survey_receiving = false;
                packets = channel_survey_packet_count;
                rssi_sum = channel_survey_rssi_sum;
                strongest = channel_survey_strongest_rssi;
                portEXIT_CRITICAL(&channel_survey_mux);
                if (elapsed == CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS &&
                    !atomic_load_explicit(&channel_survey_cancelled, memory_order_acquire)) {
                    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
                    cerberus_wifi_channel_survey_result_payload_t *result =
                        &channel_survey_results[index];
                    uint32_t updated_ms = (uint32_t)((esp_timer_get_time() -
                        channel_survey_started_us) / 1000LL);
                    cerberus_wifi_channel_survey_metrics_update(result,
                        packets, rssi_sum, strongest, updated_ms);
                    channel_survey_status.completed_count = (uint8_t)(index + 1U);
                    channel_survey_status.elapsed_ms = result->updated_ms;
                    channel_survey_status.total_packets =
                        UINT32_MAX - channel_survey_status.total_packets < packets
                        ? UINT32_MAX : channel_survey_status.total_packets + packets;
                    xSemaphoreGive(scanner_mutex);
                }
            }
            if (atomic_load_explicit(&channel_survey_cancelled, memory_order_acquire)) break;
            if (successful_channels == 0U) {
                error = first_channel_error == ESP_OK ? ESP_ERR_NOT_SUPPORTED : first_channel_error;
                break;
            }
            xSemaphoreTake(scanner_mutex, portMAX_DELAY);
            if (channel_survey_status.total_sweeps < UINT16_MAX)
                ++channel_survey_status.total_sweeps;
            channel_survey_status.completed_count = 0;
            xSemaphoreGive(scanner_mutex);
        }

        portENTER_CRITICAL(&channel_survey_mux);
        channel_survey_receiving = false;
        portEXIT_CRITICAL(&channel_survey_mux);
        if (promiscuous) {
            esp_err_t cleanup = esp_wifi_set_promiscuous(false);
            if (cleanup == ESP_OK)
                atomic_store_explicit(&channel_survey_radio_reserved, false,
                    memory_order_release);
            else error = cleanup;
        }
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        if (channel_survey_status.generation == generation) {
            channel_survey_status.current_channel = 0U;
            channel_survey_status.elapsed_ms = (uint32_t)((esp_timer_get_time() -
                channel_survey_started_us) / 1000LL);
            channel_survey_status.error_code = error;
            channel_survey_status.state = error == ESP_OK
                ? CERBERUS_WIFI_CHANNEL_SURVEY_IDLE : CERBERUS_WIFI_CHANNEL_SURVEY_ERROR;
        }
        xSemaphoreGive(scanner_mutex);
    }
}

esp_err_t cerberus_wifi_channel_survey_preempt(void)
{
    if (!scanner_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool running = channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING;
    if (running)
        atomic_store_explicit(&channel_survey_cancelled, true, memory_order_release);
    xSemaphoreGive(scanner_mutex);
    uint32_t started = (uint32_t)(esp_timer_get_time() / 1000LL);
    while (running && (uint32_t)((esp_timer_get_time() / 1000LL) - started) < 1000U) {
        vTaskDelay(pdMS_TO_TICKS(10));
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        running = channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING;
        xSemaphoreGive(scanner_mutex);
    }
    if (running) return ESP_ERR_TIMEOUT;
    if (atomic_load_explicit(&channel_survey_radio_reserved, memory_order_acquire)) {
        /* A failed disable keeps ownership until a later checked retry. */
        esp_err_t cleanup = esp_wifi_set_promiscuous(false);
        if (cleanup == ESP_OK) {
            atomic_store_explicit(&channel_survey_radio_reserved, false,
                memory_order_release);
            xSemaphoreTake(scanner_mutex, portMAX_DELAY);
            channel_survey_status.state = CERBERUS_WIFI_CHANNEL_SURVEY_IDLE;
            channel_survey_status.error_code = ESP_OK;
            xSemaphoreGive(scanner_mutex);
            return ESP_OK;
        }
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        channel_survey_status.error_code = cleanup;
        xSemaphoreGive(scanner_mutex);
        return cleanup;
    }
    return ESP_OK;
}

esp_err_t cerberus_wifi_channel_survey_start(uint8_t band)
{
    if (band != CERBERUS_WIFI_CHANNEL_SURVEY_BAND_24 &&
        band != CERBERUS_WIFI_CHANNEL_SURVEY_BAND_5) return ESP_ERR_INVALID_ARG;
    if (!scanner_mutex || !channel_survey_task_handle) return ESP_ERR_INVALID_STATE;
    if (cerberus_c5_test_running() || another_radio_operation_is_active())
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (!scanner_ready || station_reserved || atomic_load_explicit(&mac_radio_reserved,memory_order_acquire) || atomic_load_explicit(&csi_radio_reserved,memory_order_acquire) ||
        scan_status.state == CERBERUS_WIFI_SCAN_RUNNING ||
        defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING ||
        wifi_connection_is_active_locked() ||
        atomic_load_explicit(&channel_survey_radio_reserved, memory_order_acquire)) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING) {
        bool same = channel_survey_status.band == band;
        xSemaphoreGive(scanner_mutex);
        return same ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    const uint8_t *channels = band == CERBERUS_WIFI_CHANNEL_SURVEY_BAND_24
        ? channel_survey_24 : channel_survey_5;
    uint8_t count = band == CERBERUS_WIFI_CHANNEL_SURVEY_BAND_24
        ? sizeof(channel_survey_24) : sizeof(channel_survey_5);
    memcpy(channel_survey_channels, channels, count);
    memset(channel_survey_results, 0, sizeof(channel_survey_results));
    uint8_t generation = (uint8_t)(channel_survey_status.generation + 1U);
    if (generation == 0U) generation = 1U;
    for (uint8_t index = 0; index < count; ++index) {
        channel_survey_results[index].generation = generation;
        channel_survey_results[index].index = index;
        channel_survey_results[index].channel = channels[index];
        channel_survey_results[index].average_rssi = -127;
        channel_survey_results[index].strongest_rssi = -127;
        channel_survey_results[index].dwell_ms = CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS;
    }
    channel_survey_status = (cerberus_wifi_channel_survey_status_payload_t){
        .state = CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING,
        .generation = generation,
        .band = band,
        .result_count = count,
        .dwell_ms = CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS,
    };
    channel_survey_started_us = esp_timer_get_time();
    atomic_store_explicit(&channel_survey_cancelled, false, memory_order_release);
    xSemaphoreGive(scanner_mutex);
    xTaskNotifyGive(channel_survey_task_handle);
    return ESP_OK;
}

void cerberus_wifi_channel_survey_get_status(
    cerberus_wifi_channel_survey_status_payload_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!scanner_mutex) {
        status->state = CERBERUS_WIFI_CHANNEL_SURVEY_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    *status = channel_survey_status;
    if (status->state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING)
        status->elapsed_ms = (uint32_t)((esp_timer_get_time() -
            channel_survey_started_us) / 1000LL);
    xSemaphoreGive(scanner_mutex);
}

esp_err_t cerberus_wifi_channel_survey_get_result(uint8_t generation, uint8_t index,
    cerberus_wifi_channel_survey_result_payload_t *result)
{
    if (!result || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (generation != channel_survey_status.generation ||
        index >= channel_survey_status.result_count) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *result = channel_survey_results[index];
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}
