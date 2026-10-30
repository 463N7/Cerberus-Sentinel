/* Private implementation included by c5_spi_link.c after exchange helpers. */

static bool remote_id_channel_valid(uint8_t channel);

static bool remote_id_start_valid(const cerberus_remote_id_start_request_t *request)
{
    if (!request || !request->job_id ||
        request->duration_ms < CERBERUS_REMOTE_ID_WINDOW_MIN_MS ||
        request->duration_ms > CERBERUS_REMOTE_ID_WINDOW_MAX_MS ||
        request->lease_ms < CERBERUS_REMOTE_ID_LEASE_MIN_MS ||
        request->lease_ms > CERBERUS_REMOTE_ID_LEASE_MAX_MS ||
        request->dwell_ms < CERBERUS_REMOTE_ID_DWELL_MIN_MS ||
        request->dwell_ms > CERBERUS_REMOTE_ID_DWELL_MAX_MS) return false;
    if (!request->flags) {
        for (size_t i = 0; i < sizeof(request->reserved); ++i)
            if (request->reserved[i]) return false;
        return true;
    }
    if (request->flags != CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON &&
        request->flags != CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN) return false;
    if (request->transmitter[0] & 1U) return false;
    uint8_t any = 0U;
    for (size_t i = 0; i < sizeof(request->transmitter); ++i)
        any |= request->transmitter[i];
    return any && (!request->follow_channel || remote_id_channel_valid(request->follow_channel));
}

/* Domain check only: the C5 still intersects this with its configured country
 * and SDK plan before tuning. Do not widen Packet Monitor's separate policy. */
static bool remote_id_channel_valid(uint8_t channel)
{
    return (channel >= 1U && channel <= 14U) ||
        (channel >= 36U && channel <= 64U && channel % 4U == 0U) ||
        (channel >= 100U && channel <= 144U && channel % 4U == 0U) ||
        (channel >= 149U && channel <= 177U && (channel - 149U) % 4U == 0U);
}

static bool remote_id_status_valid(const cerberus_remote_id_status_payload_t *status,
    uint64_t requested_id, uint32_t firmware_version)
{
    const uint8_t profile_flags = CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED |
        CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
    if (status->state > CERBERUS_REMOTE_ID_STATE_REJECTED ||
        (status->flags & ~(CERBERUS_REMOTE_ID_FLAG_OWNED |
            CERBERUS_REMOTE_ID_FLAG_RECEIVING | CERBERUS_REMOTE_ID_FLAG_DATA_READY | profile_flags)) ||
        ((status->flags & profile_flags) && firmware_version < CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE) ||
        status->lease_remaining_ms > CERBERUS_REMOTE_ID_LEASE_MAX_MS ||
        (requested_id && status->job_id != requested_id)) return false;
    if (status->state == CERBERUS_REMOTE_ID_STATE_IDLE)
        return !requested_id && !status->job_id && !status->error_code &&
            !status->elapsed_ms && !status->lease_remaining_ms &&
            !status->generation && !status->channel && !status->flags;
    if (!status->job_id) return false;
    if (status->state == CERBERUS_REMOTE_ID_STATE_REJECTED)
        return requested_id && status->error_code && !status->elapsed_ms &&
            !status->lease_remaining_ms && !status->generation &&
            !status->channel && !status->flags;
    if ((status->state == CERBERUS_REMOTE_ID_STATE_ERROR && !status->error_code) ||
        ((status->state == CERBERUS_REMOTE_ID_STATE_STARTING ||
          status->state == CERBERUS_REMOTE_ID_STATE_RUNNING ||
          status->state == CERBERUS_REMOTE_ID_STATE_COMPLETE ||
          status->state == CERBERUS_REMOTE_ID_STATE_STOPPED) && status->error_code)) return false;
    bool owned = (status->flags & CERBERUS_REMOTE_ID_FLAG_OWNED) != 0U;
    bool receiving = (status->flags & CERBERUS_REMOTE_ID_FLAG_RECEIVING) != 0U;
    bool data_ready = (status->flags & CERBERUS_REMOTE_ID_FLAG_DATA_READY) != 0U;
    bool follow_requested = (status->flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED) != 0U;
    bool follow_bound = (status->flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND) != 0U;
    /* Requested-only STARTING is pending proof, not a claim of SEARCH. Once
     * bound, the profile fact remains present through retune and cleanup. */
    if (follow_bound && (!follow_requested || status->state == CERBERUS_REMOTE_ID_STATE_STARTING))
        return false;
    if (!owned && status->lease_remaining_ms) return false;
    if ((status->state == CERBERUS_REMOTE_ID_STATE_STARTING ||
         status->state == CERBERUS_REMOTE_ID_STATE_RUNNING ||
         status->state == CERBERUS_REMOTE_ID_STATE_STOPPING) && !owned) return false;
    if ((status->state == CERBERUS_REMOTE_ID_STATE_COMPLETE ||
         status->state == CERBERUS_REMOTE_ID_STATE_STOPPED) && owned) return false;
    if (data_ready && (owned || status->state != CERBERUS_REMOTE_ID_STATE_COMPLETE)) return false;
    if (receiving)
        return owned && status->state == CERBERUS_REMOTE_ID_STATE_RUNNING &&
            remote_id_channel_valid(status->channel);
    return status->channel == 0U;
}

static bool remote_id_peer_equal(const cerberus_c5_status_t *a,
    const cerberus_c5_status_t *b)
{
    return a->firmware_version == b->firmware_version && a->capabilities == b->capabilities &&
        a->protocol_version == b->protocol_version && a->reset_reason == b->reset_reason &&
        a->boot_count == b->boot_count && b->uptime_ms >= a->uptime_ms;
}

/* NOT_SUPPORTED is reserved for the authoritative preflight below. In
 * particular, a failed delivered command must never select legacy fallback. */
static esp_err_t remote_id_transport_error(esp_err_t error)
{
    return error == ESP_ERR_NOT_SUPPORTED || error == CERBERUS_REMOTE_ID_ERR_PEER_CHANGED ?
        ESP_ERR_INVALID_RESPONSE : error;
}

static esp_err_t remote_id_command(cerberus_spi_command_t command,
    const void *request, size_t request_length, uint64_t requested_id,
    const cerberus_c5_status_t *expected_peer,
    cerberus_remote_id_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    cerberus_c5_status_t before = {0}, after = {0};
    cerberus_remote_id_status_payload_t pending = {0};
    const cerberus_remote_id_start_request_t *start =
        command == CERBERUS_SPI_COMMAND_REMOTE_ID_START ? request : NULL;
    esp_err_t error = remote_id_transport_error(get_status_v2_locked(++sequence, &before));
    const uint32_t required = CERBERUS_C5_CAP_WATCH_OBSERVATIONS | CERBERUS_C5_CAP_WIFI_2_4_GHZ;
    if (error == ESP_OK && expected_peer && !remote_id_peer_equal(expected_peer, &before))
        error = CERBERUS_REMOTE_ID_ERR_PEER_CHANGED;
    if (error == ESP_OK && before.protocol_version != CERBERUS_PROTOCOL_VERSION)
        error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK && (before.firmware_version < CERBERUS_REMOTE_ID_MIN_FIRMWARE ||
        (before.capabilities & required) != required)) error = ESP_ERR_NOT_SUPPORTED;
    if (error == ESP_OK && start && start->flags &&
        before.firmware_version < CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE)
        error = ESP_ERR_NOT_SUPPORTED;
    if (error == ESP_OK) error = remote_id_transport_error(exchange_v2_locked(command,
        request, request_length, &pending, sizeof(pending)));
    if (error == ESP_OK && !remote_id_status_valid(&pending, requested_id, before.firmware_version))
        error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK && start && pending.state != CERBERUS_REMOTE_ID_STATE_REJECTED &&
        (((start->flags != 0U) != ((pending.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED) != 0U)) ||
         (!start->follow_channel && (pending.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND))))
        error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK && pending.channel > 14U &&
        !(before.capabilities & CERBERUS_C5_CAP_WIFI_5_GHZ)) error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK) {
        error = remote_id_transport_error(get_status_v2_locked(++sequence, &after));
        if (error == ESP_OK && !remote_id_peer_equal(&before, &after))
            error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) *status = pending;
    give_link_mutex();
    return error;
}

esp_err_t c5_spi_link_remote_id_start(const cerberus_remote_id_start_request_t *request,
    cerberus_remote_id_status_payload_t *status)
{
    if (status) memset(status, 0, sizeof(*status));
    if (!status || !remote_id_start_valid(request)) return ESP_ERR_INVALID_ARG;
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_START, request,
        sizeof(*request), request->job_id, NULL, status);
}

esp_err_t c5_spi_link_remote_id_status(uint64_t job_id,
    cerberus_remote_id_status_payload_t *status)
{
    cerberus_remote_id_job_request_t request = {.job_id = job_id};
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS, &request,
        sizeof(request), job_id, NULL, status);
}

esp_err_t c5_spi_link_remote_id_stop(uint64_t job_id,
    cerberus_remote_id_status_payload_t *status)
{
    if (status) memset(status, 0, sizeof(*status));
    if (!status || !job_id) return ESP_ERR_INVALID_ARG;
    cerberus_remote_id_job_request_t request = {.job_id = job_id};
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_STOP, &request,
        sizeof(request), job_id, NULL, status);
}

esp_err_t c5_spi_link_remote_id_renew(uint64_t job_id,
    cerberus_remote_id_status_payload_t *status)
{
    if (status) memset(status, 0, sizeof(*status));
    if (!status || !job_id) return ESP_ERR_INVALID_ARG;
    cerberus_remote_id_job_request_t request = {.job_id = job_id};
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_RENEW, &request,
        sizeof(request), job_id, NULL, status);
}

esp_err_t c5_spi_link_remote_id_start_for_peer(const cerberus_remote_id_start_request_t *request,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status)
{
    if (status) memset(status, 0, sizeof(*status));
    if (!status || !expected_peer || !remote_id_start_valid(request)) return ESP_ERR_INVALID_ARG;
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_START, request,
        sizeof(*request), request->job_id, expected_peer, status);
}

esp_err_t c5_spi_link_remote_id_status_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status)
{
    if (status) memset(status, 0, sizeof(*status));
    if (!status || !expected_peer) return ESP_ERR_INVALID_ARG;
    cerberus_remote_id_job_request_t request = {.job_id = job_id};
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS, &request,
        sizeof(request), job_id, expected_peer, status);
}

esp_err_t c5_spi_link_remote_id_stop_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status)
{
    if (status) memset(status, 0, sizeof(*status));
    if (!status || !expected_peer || !job_id) return ESP_ERR_INVALID_ARG;
    cerberus_remote_id_job_request_t request = {.job_id = job_id};
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_STOP, &request,
        sizeof(request), job_id, expected_peer, status);
}

esp_err_t c5_spi_link_remote_id_renew_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status)
{
    if (status) memset(status, 0, sizeof(*status));
    if (!status || !expected_peer || !job_id) return ESP_ERR_INVALID_ARG;
    cerberus_remote_id_job_request_t request = {.job_id = job_id};
    return remote_id_command(CERBERUS_SPI_COMMAND_REMOTE_ID_RENEW, &request,
        sizeof(request), job_id, expected_peer, status);
}
