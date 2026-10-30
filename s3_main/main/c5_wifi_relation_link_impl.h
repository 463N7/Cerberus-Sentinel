/* Included by c5_spi_link.c. One physical attempt per step, no hidden retry,
 * allocation, RF operation or access to the independent active station bank. */
enum {
    RELATION_PEER = 1, RELATION_STATUS, RELATION_CLOCK, RELATION_ROWS,
    RELATION_FINAL_STATUS, RELATION_FINAL_CLOCK, RELATION_FINAL_PEER,
    RELATION_DONE, RELATION_FAILED
};

static esp_err_t relation_guard(cerberus_c5_wifi_relation_reader_t *reader,
    cerberus_c5_relation_continue_fn proceed, void *context, uint64_t *now,
    uint32_t *remaining)
{
    if (proceed && !proceed(context)) return ESP_ERR_NOT_FINISHED;
    int64_t sampled = esp_timer_get_time();
    if (sampled < 0) return ESP_ERR_INVALID_STATE;
    if (reader->anchored && (uint64_t)sampled < reader->last_local_us) return ESP_ERR_INVALID_STATE;
    uint32_t left = reader->deadline_ms - (uint32_t)((uint64_t)sampled / 1000U);
    if (!left || left > INT32_MAX) return ESP_ERR_TIMEOUT;
    reader->last_local_us = (uint64_t)sampled;
    if (now) *now = (uint64_t)sampled;
    if (remaining) *remaining = left;
    return ESP_OK;
}

static esp_err_t relation_exchange(cerberus_c5_wifi_relation_reader_t *reader,
    bool peer, cerberus_spi_command_t command, const void *input, size_t input_size,
    void *output, size_t output_size, cerberus_c5_relation_continue_fn proceed, void *context)
{
    memset(output, 0, output_size);
    uint32_t remaining = 0U;
    uint64_t now = 0U;
    esp_err_t error = relation_guard(reader, proceed, context, &now, &remaining);
    if (error != ESP_OK) return error;
    if (!reader->anchored) {
        reader->pending.request_started_us = now;
        reader->anchored = true;
    }
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (remaining > 20U) remaining = 20U;
    if (take_link_mutex(pdMS_TO_TICKS(remaining)) != pdTRUE) return ESP_ERR_TIMEOUT;
    error = relation_guard(reader, NULL, NULL, NULL, NULL);
    if (error == ESP_OK) {
        if (peer) error = get_status_v2_locked(++sequence, output);
        else {
            cerberus_spi_frame_t request, response = {0};
            uint8_t discarded[CERBERUS_SPI_TRANSFER_SIZE] = {0};
            uint8_t token = ++sequence;
            if (!cerberus_protocol_prepare_frame(&request, command, token, input, input_size))
                error = ESP_ERR_INVALID_ARG;
            else {
                spi_request_response(&request, discarded, &response, sizeof(request));
                error = ESP_ERR_INVALID_RESPONSE;
                if (cerberus_protocol_frame_is_valid(&response) && response.command == command &&
                    response.sequence == token && response.payload_length == output_size) {
                    memcpy(output, response.payload, output_size);
                    error = ESP_OK;
                }
            }
        }
    }
    give_link_mutex();
    if (error == ESP_OK) error = relation_guard(reader, proceed, context, NULL, NULL);
    if (error != ESP_OK) memset(output, 0, output_size);
    return error;
}

static void relation_clear_rows(cerberus_c5_wifi_relation_reader_t *reader)
{
    if (reader->rows && reader->capacity <= CERBERUS_WIFI_RELATION_LIMIT)
        memset(reader->rows, 0, reader->capacity * sizeof(*reader->rows));
}

static esp_err_t relation_fail(cerberus_c5_wifi_relation_reader_t *reader, esp_err_t error)
{
    relation_clear_rows(reader);
    memset(&reader->pending, 0, sizeof(reader->pending));
    memset(&reader->row, 0, sizeof(reader->row));
    reader->phase = RELATION_FAILED;
    reader->error = error;
    return error;
}

void c5_spi_link_wifi_relation_cancel(cerberus_c5_wifi_relation_reader_t *reader)
{
    if (reader) (void)relation_fail(reader, ESP_ERR_NOT_FINISHED);
}

esp_err_t c5_spi_link_wifi_relation_begin(cerberus_c5_wifi_relation_reader_t *reader,
    uint8_t generation, uint32_t deadline_ms, cerberus_wifi_relation_row_t *rows, size_t capacity)
{
    if (!reader) return ESP_ERR_INVALID_ARG;
    if (reader->phase >= RELATION_PEER && reader->phase <= RELATION_FINAL_PEER)
        relation_clear_rows(reader);
    memset(reader, 0, sizeof(*reader));
    size_t clear_count = capacity < CERBERUS_WIFI_RELATION_LIMIT ? capacity : CERBERUS_WIFI_RELATION_LIMIT;
    if (rows) memset(rows, 0, clear_count * sizeof(*rows));
    if (capacity > CERBERUS_WIFI_RELATION_LIMIT || (!rows && capacity))
        return relation_fail(reader, ESP_ERR_INVALID_ARG);
    reader->rows = rows;
    reader->capacity = (uint8_t)capacity;
    reader->generation = generation;
    reader->deadline_ms = deadline_ms;
    reader->phase = RELATION_PEER;
    return ESP_OK;
}

esp_err_t c5_spi_link_wifi_relation_begin_expected(cerberus_c5_wifi_relation_reader_t *reader,
    const cerberus_c5_wifi_relation_bank_t *expected, uint32_t deadline_ms,
    cerberus_wifi_relation_row_t *rows, size_t capacity)
{
    if (!reader) return ESP_ERR_INVALID_ARG;
    cerberus_c5_wifi_relation_bank_t copy = {0};
    bool valid = cerberus_s3_wifi_relation_meta_valid(expected);
    if (valid) copy = *expected;
    esp_err_t error = c5_spi_link_wifi_relation_begin(reader, copy.status.generation,
        deadline_ms, rows, capacity);
    if (error != ESP_OK) return error;
    if (!valid) return relation_fail(reader, ESP_ERR_INVALID_ARG);
    reader->expected = copy;
    reader->expect_bank = true;
    return ESP_OK;
}

static bool relation_peer_matches(const cerberus_c5_wifi_relation_bank_t *proof,
    const cerberus_c5_status_t *peer)
{
    return proof->firmware_version == peer->firmware_version &&
        proof->capabilities == peer->capabilities && proof->protocol_version == peer->protocol_version &&
        proof->reset_reason == peer->reset_reason && proof->boot_count == peer->boot_count &&
        peer->uptime_ms >= proof->peer_uptime_ms;
}

static bool relation_clock_valid(const cerberus_wifi_relation_clock_t *clock,
    const cerberus_c5_wifi_relation_bank_t *proof)
{
    return clock->epoch == proof->status.epoch && clock->generation == proof->status.generation &&
        clock->count == proof->status.count && clock->source_now_us <= INT64_MAX &&
        !clock->reserved[0] && !clock->reserved[1] && !clock->reserved[2] && !clock->reserved[3];
}

static bool relation_key_equal(const cerberus_wifi_relation_row_t *a, const cerberus_wifi_relation_row_t *b)
{
    return a->channel == b->channel && !memcmp(a->bssid, b->bssid, 6U) && !memcmp(a->station, b->station, 6U);
}

esp_err_t c5_spi_link_wifi_relation_step(cerberus_c5_wifi_relation_reader_t *reader,
    cerberus_c5_wifi_relation_bank_t *proof, bool *complete,
    cerberus_c5_relation_continue_fn proceed, void *context)
{
    if (proof) memset(proof, 0, sizeof(*proof));
    if (complete) *complete = false;
    if (!reader || !proof || !complete) {
        if (reader) (void)relation_fail(reader, ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }
    if (reader->phase == RELATION_FAILED) return reader->error;
    if (reader->phase == RELATION_DONE) {
        *proof = reader->pending;
        *complete = true;
        return ESP_OK;
    }
    esp_err_t error = ESP_ERR_INVALID_STATE;
    cerberus_c5_wifi_relation_bank_t *pending = &reader->pending;
    if (reader->phase == RELATION_PEER || reader->phase == RELATION_FINAL_PEER) {
        cerberus_c5_status_t peer = {0};
        error = relation_exchange(reader, true, CERBERUS_SPI_COMMAND_STATUS, NULL, 0U,
            &peer, sizeof(peer), proceed, context);
        if (error == ESP_OK && reader->phase == RELATION_PEER) {
            if (peer.firmware_version < CERBERUS_WIFI_RELATION_MIN_FIRMWARE)
                error = reader->expect_bank ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_NOT_SUPPORTED;
            else if (peer.protocol_version != CERBERUS_PROTOCOL_VERSION || !(peer.capabilities &
                (CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ))) error = ESP_ERR_INVALID_RESPONSE;
            else if (reader->expect_bank && !relation_peer_matches(&reader->expected, &peer))
                error = ESP_ERR_INVALID_RESPONSE;
            else {
                pending->firmware_version = peer.firmware_version;
                pending->capabilities = peer.capabilities;
                pending->protocol_version = peer.protocol_version;
                pending->reset_reason = peer.reset_reason;
                pending->boot_count = peer.boot_count;
                pending->peer_uptime_ms = peer.uptime_ms;
                reader->phase = RELATION_STATUS;
            }
        } else if (error == ESP_OK) {
            if (!relation_peer_matches(pending, &peer)) error = ESP_ERR_INVALID_RESPONSE;
            else {
                pending->peer_uptime_ms = peer.uptime_ms;
                if (!cerberus_s3_wifi_relation_meta_valid(pending)) error = ESP_ERR_INVALID_RESPONSE;
                else reader->phase = RELATION_DONE;
            }
        }
    } else if (reader->phase == RELATION_STATUS || reader->phase == RELATION_FINAL_STATUS) {
        cerberus_wifi_relation_status_query_t query = {reader->generation};
        cerberus_wifi_relation_status_t status = {0};
        error = relation_exchange(reader, false, CERBERUS_SPI_COMMAND_WIFI_RELATION_STATUS,
            &query, sizeof(query), &status, sizeof(status), proceed, context);
        if (error == ESP_OK && (!cerberus_wifi_relation_status_valid(&status) ||
            status.generation != reader->generation))
            error = ESP_ERR_INVALID_RESPONSE;
        if (error == ESP_OK && status.state != CERBERUS_WIFI_RELATION_COMPLETE)
            error = reader->phase == RELATION_STATUS && !reader->expect_bank ?
                ESP_ERR_NOT_FINISHED : ESP_ERR_INVALID_RESPONSE;
        if (error == ESP_OK && reader->expect_bank &&
            memcmp(&status, &reader->expected.status, sizeof(status))) error = ESP_ERR_INVALID_RESPONSE;
        if (error == ESP_OK && reader->phase == RELATION_STATUS) {
            pending->status = status;
            if (!reader->check_only && status.count > reader->capacity) error = ESP_ERR_INVALID_SIZE;
            else if (status.flags & CERBERUS_WIFI_RELATION_AVAILABLE) reader->phase = RELATION_CLOCK;
            else {
                pending->request_started_us = 0U;
                reader->phase = RELATION_FINAL_STATUS;
            }
        } else if (error == ESP_OK) {
            if (memcmp(&status, &pending->status, sizeof(status))) error = ESP_ERR_INVALID_RESPONSE;
            else reader->phase = pending->clock_valid ? RELATION_FINAL_CLOCK : RELATION_FINAL_PEER;
        }
    } else if (reader->phase == RELATION_CLOCK || reader->phase == RELATION_FINAL_CLOCK) {
        cerberus_wifi_relation_clock_query_t query = {pending->status.epoch, reader->generation};
        cerberus_wifi_relation_clock_t clock = {0};
        error = relation_exchange(reader, false, CERBERUS_SPI_COMMAND_WIFI_RELATION_CLOCK,
            &query, sizeof(query), &clock, sizeof(clock), proceed, context);
        if (error == ESP_OK && !relation_clock_valid(&clock, pending)) error = ESP_ERR_INVALID_RESPONSE;
        if (error == ESP_OK && reader->phase == RELATION_CLOCK) {
            reader->read_clock_us = clock.source_now_us;
            if (reader->expect_bank && (clock.source_now_us < reader->expected.source_now_us ||
                pending->request_started_us < reader->expected.request_started_us))
                error = ESP_ERR_INVALID_RESPONSE;
            pending->source_now_us = reader->expect_bank ? reader->expected.source_now_us : clock.source_now_us;
            if (reader->expect_bank) pending->request_started_us = reader->expected.request_started_us;
            pending->clock_valid = true;
            reader->frames = pending->status.dropped_frames;
            reader->phase = !reader->check_only && pending->status.count ? RELATION_ROWS : RELATION_FINAL_STATUS;
        } else if (error == ESP_OK) {
            if (clock.source_now_us < reader->read_clock_us) error = ESP_ERR_INVALID_RESPONSE;
            else reader->phase = RELATION_FINAL_PEER;
        }
    } else if (reader->phase == RELATION_ROWS) {
        cerberus_wifi_relation_query_t query = {pending->status.epoch, reader->generation, reader->index, reader->chunk};
        cerberus_wifi_relation_chunk_t chunk = {0};
        error = relation_exchange(reader, false, CERBERUS_SPI_COMMAND_WIFI_RELATION_CHUNK,
            &query, sizeof(query), &chunk, sizeof(chunk), proceed, context);
        size_t offset = (size_t)reader->chunk * CERBERUS_WIFI_RELATION_CHUNK_BYTES;
        size_t length = sizeof(reader->row) - offset;
        if (length > CERBERUS_WIFI_RELATION_CHUNK_BYTES) length = CERBERUS_WIFI_RELATION_CHUNK_BYTES;
        if (error == ESP_OK && (chunk.epoch != pending->status.epoch || chunk.generation != reader->generation ||
            chunk.index != reader->index || chunk.chunk != reader->chunk || chunk.length != length))
            error = ESP_ERR_INVALID_RESPONSE;
        for (size_t i = length; error == ESP_OK && i < sizeof(chunk.data); ++i)
            if (chunk.data[i]) error = ESP_ERR_INVALID_RESPONSE;
        if (error == ESP_OK) {
            memcpy((uint8_t *)&reader->row + offset, chunk.data, length);
            if (++reader->chunk == CERBERUS_WIFI_RELATION_CHUNKS) {
                const cerberus_wifi_relation_row_t *row = &reader->row;
                if (!cerberus_wifi_relation_row_valid(row) ||
                    cerberus_wifi_relation_latest_us(row) > pending->source_now_us ||
                    !(pending->capabilities & (row->channel <= 14U ? CERBERUS_C5_CAP_WIFI_2_4_GHZ : CERBERUS_C5_CAP_WIFI_5_GHZ)))
                    error = ESP_ERR_INVALID_RESPONSE;
                for (size_t i = 0U; error == ESP_OK && i < reader->index; ++i)
                    if (relation_key_equal(row, &reader->rows[i])) error = ESP_ERR_INVALID_RESPONSE;
                if (error == ESP_OK) {
                    reader->rows[reader->index++] = *row;
                    reader->frames += (uint64_t)row->to_ap_frames + row->from_ap_frames;
                    reader->chunk = 0U;
                    memset(&reader->row, 0, sizeof(reader->row));
                    if (reader->index == pending->status.count) {
                        uint32_t total = reader->frames > UINT32_MAX ? UINT32_MAX : (uint32_t)reader->frames;
                        if (total != pending->status.matched_frames) error = ESP_ERR_INVALID_RESPONSE;
                        else reader->phase = RELATION_FINAL_STATUS;
                    }
                }
            }
        }
    }
    if (error != ESP_OK) return relation_fail(reader, error);
    if (reader->phase == RELATION_DONE) {
        *proof = *pending;
        *complete = true;
    }
    return ESP_OK;
}

esp_err_t c5_spi_link_copy_wifi_relations(uint8_t generation, uint32_t deadline_ms,
    cerberus_c5_wifi_relation_bank_t *proof, cerberus_wifi_relation_row_t *rows, size_t capacity,
    cerberus_c5_relation_continue_fn proceed, void *context)
{
    if (proof) memset(proof, 0, sizeof(*proof));
    cerberus_c5_wifi_relation_reader_t reader = {0};
    esp_err_t error = c5_spi_link_wifi_relation_begin(&reader, generation, deadline_ms, rows, capacity);
    if (!proof) return relation_fail(&reader, ESP_ERR_INVALID_ARG);
    bool complete = false;
    while (error == ESP_OK && !complete)
        error = c5_spi_link_wifi_relation_step(&reader, proof, &complete, proceed, context);
    return error;
}

esp_err_t c5_spi_link_get_wifi_relation_bank(uint8_t generation, uint32_t deadline_ms,
    cerberus_c5_wifi_relation_bank_t *proof)
{
    if (!proof) return ESP_ERR_INVALID_ARG;
    memset(proof, 0, sizeof(*proof));
    cerberus_c5_wifi_relation_reader_t reader = {0};
    esp_err_t error = c5_spi_link_wifi_relation_begin(&reader, generation, deadline_ms, NULL, 0U);
    reader.check_only = true;
    bool complete = false;
    while (error == ESP_OK && !complete)
        error = c5_spi_link_wifi_relation_step(&reader, proof, &complete, NULL, NULL);
    return error;
}

esp_err_t c5_spi_link_check_wifi_relation_bank(const cerberus_c5_wifi_relation_bank_t *proof,
    uint32_t deadline_ms)
{
    if (!cerberus_s3_wifi_relation_meta_valid(proof)) return ESP_ERR_INVALID_ARG;
    cerberus_c5_wifi_relation_bank_t current = {0};
    esp_err_t error = c5_spi_link_get_wifi_relation_bank(proof->status.generation, deadline_ms, &current);
    if (error == ESP_ERR_NOT_SUPPORTED) error = ESP_ERR_INVALID_RESPONSE;
    if (error == ESP_OK && (memcmp(&current.status, &proof->status, sizeof(proof->status)) ||
        current.firmware_version != proof->firmware_version || current.capabilities != proof->capabilities ||
        current.protocol_version != proof->protocol_version || current.reset_reason != proof->reset_reason ||
        current.boot_count != proof->boot_count || current.peer_uptime_ms < proof->peer_uptime_ms ||
        current.clock_valid != proof->clock_valid || current.source_now_us < proof->source_now_us ||
        current.request_started_us < proof->request_started_us)) error = ESP_ERR_INVALID_RESPONSE;
    return error;
}
