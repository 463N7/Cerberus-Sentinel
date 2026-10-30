/* Private to wifi_scanner.c: shares its STA radio, RX callback and mutex. */

static void packet_monitor_receive(const wifi_promiscuous_pkt_t *packet,
    wifi_promiscuous_pkt_type_t type)
{
    if (!packet || type != WIFI_PKT_MGMT || packet->rx_ctrl.rx_state != 0U ||
        packet->rx_ctrl.sig_len < 28U) return;
    portENTER_CRITICAL(&packet_monitor_mux);
    if (packet_monitor_receiving &&
        packet->rx_ctrl.channel == packet_monitor_receive_channel)
        cerberus_wifi_packet_monitor_observe(&packet_monitor_counts,
            packet->payload, packet->rx_ctrl.sig_len - 4U);
    portEXIT_CRITICAL(&packet_monitor_mux);
}

/* Keep the reservation until both RX and the prior Wi-Fi band/channel are
 * restored. A failed restore leaves a retryable fault instead of allowing a
 * dual-band AP scan to inherit the monitor's fixed channel. */
static esp_err_t packet_monitor_restore_radio(void)
{
    if (packet_monitor_disable_pending) {
        esp_err_t error = esp_wifi_set_promiscuous(false);
        if (error != ESP_OK) return error;
        packet_monitor_disable_pending = false;
    }
    if (packet_monitor_restore_pending) {
        esp_err_t error = esp_wifi_set_band_mode(
            packet_monitor_previous_band_mode);
        if (error != ESP_OK) return error;
        if (packet_monitor_previous_channel != 0U) {
            error = esp_wifi_set_channel(packet_monitor_previous_channel,
                packet_monitor_previous_second);
            if (error != ESP_OK) return error;
        }
        packet_monitor_restore_pending = false;
    }
    atomic_store_explicit(&packet_monitor_radio_reserved, false,
        memory_order_release);
    return ESP_OK;
}

/* One exclusive scanner job. Stop keeps the radio reserved until cleanup. */
static void packet_monitor_run(void)
{
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool running = packet_monitor_status.state ==
        CERBERUS_WIFI_PACKET_MONITOR_RUNNING;
    uint8_t generation = packet_monitor_status.generation;
    uint8_t channel = packet_monitor_status.channel;
    xSemaphoreGive(scanner_mutex);
    if (!running) return;

    esp_err_t error = ESP_OK;
    bool promiscuous = false;
    if (!atomic_load_explicit(&packet_monitor_cancelled,
            memory_order_acquire)) {
        error = esp_wifi_get_band_mode(
            &packet_monitor_previous_band_mode);
        if (error == ESP_OK)
            error = esp_wifi_get_channel(&packet_monitor_previous_channel,
                &packet_monitor_previous_second);
        if (error == ESP_OK) {
            packet_monitor_restore_pending = true;
            error = esp_wifi_set_band(channel <= 14U ?
                WIFI_BAND_2G : WIFI_BAND_5G);
        }
        if (error == ESP_OK)
            error = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        wifi_promiscuous_filter_t filter = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT,
        };
        if (error == ESP_OK) error = esp_wifi_set_promiscuous_filter(&filter);
        if (error == ESP_OK) {
            packet_monitor_disable_pending = true;
            error = esp_wifi_set_promiscuous(true);
            promiscuous = error == ESP_OK;
        }
    }
    if (promiscuous) {
        portENTER_CRITICAL(&packet_monitor_mux);
        packet_monitor_counts = (cerberus_wifi_packet_monitor_counters_t){0};
        packet_monitor_receive_channel = channel;
        packet_monitor_receiving = true;
        portEXIT_CRITICAL(&packet_monitor_mux);
        TickType_t last_sample = xTaskGetTickCount();
        while (!atomic_load_explicit(&packet_monitor_cancelled,
                memory_order_acquire)) {
            vTaskDelayUntil(&last_sample,
                pdMS_TO_TICKS(CERBERUS_WIFI_PACKET_MONITOR_SAMPLE_MS));
            if (atomic_load_explicit(&packet_monitor_cancelled,
                    memory_order_acquire)) break;
            portENTER_CRITICAL(&packet_monitor_mux);
            cerberus_wifi_packet_monitor_counters_t counts =
                packet_monitor_counts;
            packet_monitor_counts =
                (cerberus_wifi_packet_monitor_counters_t){0};
            portEXIT_CRITICAL(&packet_monitor_mux);
            uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() -
                packet_monitor_started_us) / 1000LL);
            xSemaphoreTake(scanner_mutex, portMAX_DELAY);
            if (packet_monitor_status.generation == generation &&
                packet_monitor_status.state ==
                    CERBERUS_WIFI_PACKET_MONITOR_RUNNING) {
                cerberus_wifi_packet_monitor_ring_push(&packet_monitor_ring,
                    generation, channel, &counts, elapsed_ms);
                packet_monitor_status.latest_sequence =
                    packet_monitor_ring.latest_sequence;
                packet_monitor_status.stored_count =
                    packet_monitor_ring.stored_count;
                packet_monitor_status.elapsed_ms = elapsed_ms;
            }
            xSemaphoreGive(scanner_mutex);
        }
    }
    portENTER_CRITICAL(&packet_monitor_mux);
    packet_monitor_receiving = false;
    portEXIT_CRITICAL(&packet_monitor_mux);
    esp_err_t cleanup = packet_monitor_restore_radio();
    if (cleanup != ESP_OK) error = cleanup;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (packet_monitor_status.generation == generation) {
        packet_monitor_status.elapsed_ms =
            (uint32_t)((esp_timer_get_time() - packet_monitor_started_us) /
                1000LL);
        packet_monitor_status.error_code = error;
        packet_monitor_status.state = error == ESP_OK
            ? CERBERUS_WIFI_PACKET_MONITOR_IDLE
            : CERBERUS_WIFI_PACKET_MONITOR_ERROR;
    }
    xSemaphoreGive(scanner_mutex);
}

bool cerberus_wifi_packet_monitor_active(void)
{
    return atomic_load_explicit(&packet_monitor_radio_reserved,
        memory_order_acquire);
}

esp_err_t cerberus_wifi_packet_monitor_start(uint8_t channel)
{
    if (!cerberus_wifi_packet_monitor_channel_valid(channel))
        return ESP_ERR_INVALID_ARG;
    if (!scanner_mutex || !scanner_task_handle)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_RUNNING) {
        bool same = packet_monitor_status.channel == channel;
        xSemaphoreGive(scanner_mutex);
        return same ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    xSemaphoreGive(scanner_mutex);
    if (cerberus_c5_test_running()) return ESP_ERR_INVALID_STATE;
    if (atomic_load_explicit(&mac_radio_reserved,memory_order_acquire)) return ESP_ERR_INVALID_STATE;
    esp_err_t error = cerberus_wifi_channel_survey_preempt();
    if (error != ESP_OK) return error;
    if (another_radio_operation_is_active()) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (!scanner_ready || station_reserved || atomic_load_explicit(&mac_radio_reserved,memory_order_acquire) || atomic_load_explicit(&csi_radio_reserved,memory_order_acquire) ||
        scan_status.state == CERBERUS_WIFI_SCAN_RUNNING ||
        defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING ||
        wifi_connection_is_active_locked() ||
        atomic_load_explicit(&packet_monitor_radio_reserved,
            memory_order_acquire)) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t generation = (uint8_t)(packet_monitor_status.generation + 1U);
    if (generation == 0U) generation = 1U;
    memset(&packet_monitor_ring, 0, sizeof(packet_monitor_ring));
    packet_monitor_restore_pending = false;
    packet_monitor_disable_pending = false;
    packet_monitor_previous_channel = 0U;
    packet_monitor_status = (cerberus_wifi_packet_monitor_status_payload_t){
        .state = CERBERUS_WIFI_PACKET_MONITOR_RUNNING,
        .generation = generation,
        .channel = channel,
        .sample_ms = CERBERUS_WIFI_PACKET_MONITOR_SAMPLE_MS,
    };
    packet_monitor_started_us = esp_timer_get_time();
    atomic_store_explicit(&packet_monitor_cancelled, false, memory_order_release);
    atomic_store_explicit(&packet_monitor_radio_reserved, true,
        memory_order_release);
    xSemaphoreGive(scanner_mutex);
    xTaskNotifyGive(scanner_task_handle);
    return ESP_OK;
}

esp_err_t cerberus_wifi_packet_monitor_stop(uint8_t generation)
{
    if (!scanner_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (generation == 0U || generation != packet_monitor_status.generation) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_RUNNING) {
        atomic_store_explicit(&packet_monitor_cancelled, true,
            memory_order_release);
        xSemaphoreGive(scanner_mutex);
        return ESP_OK;
    }
    bool held = atomic_load_explicit(&packet_monitor_radio_reserved,
        memory_order_acquire);
    if (!held) {
        if (packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_ERROR) {
            packet_monitor_status.state = CERBERUS_WIFI_PACKET_MONITOR_IDLE;
            packet_monitor_status.error_code = ESP_OK;
        }
        xSemaphoreGive(scanner_mutex);
        return ESP_OK;
    }
    esp_err_t error = packet_monitor_restore_radio();
    if (error == ESP_OK) {
        packet_monitor_status.state = CERBERUS_WIFI_PACKET_MONITOR_IDLE;
        packet_monitor_status.error_code = ESP_OK;
    } else {
        packet_monitor_status.state = CERBERUS_WIFI_PACKET_MONITOR_ERROR;
        packet_monitor_status.error_code = error;
    }
    xSemaphoreGive(scanner_mutex);
    return error;
}

void cerberus_wifi_packet_monitor_get_status(
    cerberus_wifi_packet_monitor_status_payload_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!scanner_mutex) {
        status->state = CERBERUS_WIFI_PACKET_MONITOR_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    *status = packet_monitor_status;
    if (status->state == CERBERUS_WIFI_PACKET_MONITOR_RUNNING)
        status->elapsed_ms = (uint32_t)((esp_timer_get_time() -
            packet_monitor_started_us) / 1000LL);
    xSemaphoreGive(scanner_mutex);
}

esp_err_t cerberus_wifi_packet_monitor_get_sample(uint8_t generation,
    uint16_t sequence, cerberus_wifi_packet_monitor_sample_payload_t *sample)
{
    if (!sample || !scanner_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool valid = generation == packet_monitor_status.generation &&
        cerberus_wifi_packet_monitor_ring_get(&packet_monitor_ring,
            sequence, sample);
    xSemaphoreGive(scanner_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}
