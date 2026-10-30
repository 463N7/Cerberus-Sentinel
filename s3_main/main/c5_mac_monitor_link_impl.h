/* Included after c5_csi_link_impl.h: share its bounded, single-attempt READY
 * exchange and existing link mutex. MAC snapshots are drained incrementally;
 * no operation holds the SPI mutex while the UI processes a bank. */
#include "mac_monitor_link.h"

static uint64_t mac_reserved_boot;
static uint32_t mac_reserved_job;

static uint64_t mac_link_now(void *context)
{ (void)context; return (uint64_t)esp_timer_get_time(); }

static bool mac_channel_valid(uint8_t channel)
{
    return (channel >= 1U && channel <= 14U) ||
        (channel >= 36U && channel <= 64U && channel % 4U == 0U) ||
        (channel >= 100U && channel <= 144U && channel % 4U == 0U) ||
        (channel >= 149U && channel <= 177U && (channel - 149U) % 4U == 0U);
}

static bool mac_status_valid(const cerberus_mac_status_t *s)
{
    if (s->version != CERBERUS_MAC_VERSION || s->reserved || s->state > CERBERUS_MAC_ERROR ||
        (s->flags & ~(CERBERUS_MAC_OWNED | CERBERUS_MAC_RECEIVING))) return false;
    bool owned = (s->flags & CERBERUS_MAC_OWNED) != 0U;
    bool receiving = (s->flags & CERBERUS_MAC_RECEIVING) != 0U;
    if (s->state == CERBERUS_MAC_IDLE)
        return !s->job_id && !s->epoch && !s->error_code && !s->flags && !s->channel;
    if (!s->job_id || !s->epoch ||
        (receiving && (!owned || s->state != CERBERUS_MAC_RUNNING || !mac_channel_valid(s->channel))) ||
        (!receiving && s->channel) ||
        ((s->state == CERBERUS_MAC_STARTING || s->state == CERBERUS_MAC_RUNNING ||
            s->state == CERBERUS_MAC_STOPPING) && !owned) ||
        (s->state == CERBERUS_MAC_COMPLETE && owned)) return false;
    if (s->state == CERBERUS_MAC_ERROR) return s->error_code != 0;
    return s->state == CERBERUS_MAC_STOPPING || s->error_code == 0;
}

static esp_err_t mac_info(cerberus_mac_info_t *info, uint64_t deadline)
{
    cerberus_mac_status_request_t request = {.page = CERBERUS_MAC_PAGE_INFO};
    esp_err_t error = csi_link_exchange(CERBERUS_SPI_COMMAND_MAC_STATUS, &request,
        sizeof(request), info, sizeof(*info), deadline);
    if (!error && (!info->boot_id || info->version != CERBERUS_MAC_VERSION ||
        info->capacity != CERBERUS_MAC_CAPACITY || info->row_bytes != sizeof(cerberus_mac_row_t) ||
        !csi_link_zero(info->reserved, sizeof(info->reserved)))) error = ESP_ERR_INVALID_RESPONSE;
    if (error) memset(info, 0, sizeof(*info));
    return error;
}

static esp_err_t mac_page(const cerberus_mac_binding_t *binding, uint8_t page,
    void *output, size_t size, uint64_t deadline)
{
    cerberus_mac_status_request_t request = {.binding = *binding, .page = page};
    return csi_link_exchange(CERBERUS_SPI_COMMAND_MAC_STATUS, &request,
        sizeof(request), output, size, deadline);
}

static bool mac_profile_valid(const cerberus_mac_profile_t *profile, const cerberus_mac_status_t *status)
{
    return profile->epoch == status->epoch && profile->version == CERBERUS_MAC_VERSION &&
        profile->lease_ms >= CERBERUS_MAC_LEASE_MIN_MS && profile->lease_ms <= CERBERUS_MAC_LEASE_MAX_MS &&
        profile->dwell_ms >= CERBERUS_MAC_DWELL_MIN_MS && profile->dwell_ms <= CERBERUS_MAC_DWELL_MAX_MS &&
        profile->channel_count <= 42U &&
        (status->state != CERBERUS_MAC_RUNNING || profile->channel_count != 0U) &&
        csi_link_zero(profile->reserved, sizeof(profile->reserved));
}

static cerberus_mac_monitor_operation_t mac_observe(const cerberus_mac_binding_t *binding,
    cerberus_mac_monitor_observation_t *out, uint64_t deadline)
{
    memset(out, 0, sizeof(*out));
    cerberus_mac_info_t before, after;
    esp_err_t error = mac_info(&before, deadline);
    if (error) return (cerberus_mac_monitor_operation_t){error, false};
    if (before.boot_id != binding->boot_id)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE, true};
    error = mac_page(binding, CERBERUS_MAC_PAGE_CORE, &out->status, sizeof(out->status), deadline);
    if (error || !mac_status_valid(&out->status))
        return (cerberus_mac_monitor_operation_t){error ? error : ESP_ERR_INVALID_RESPONSE, false};
    if (out->status.job_id != binding->job_id ||
        (binding->epoch && out->status.epoch != binding->epoch))
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE, true};
    if (!out->status.epoch) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE, true};
    cerberus_mac_binding_t exact = *binding;
    exact.epoch = out->status.epoch;
    error = mac_page(&exact, CERBERUS_MAC_PAGE_PROFILE, &out->profile, sizeof(out->profile), deadline);
    cerberus_mac_status_t final = {0};
    if (!error) error = mac_page(&exact, CERBERUS_MAC_PAGE_CORE, &final, sizeof(final), deadline);
    if (!error) error = mac_info(&after, deadline);
    if (error) return (cerberus_mac_monitor_operation_t){error, false};
    if (after.boot_id != binding->boot_id)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE, true};
    if (!mac_status_valid(&final) || after.source_now_us < before.source_now_us)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_RESPONSE, false};
    if (final.job_id != exact.job_id || final.epoch != exact.epoch)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE, true};
    if (!mac_profile_valid(&out->profile, &final))
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_RESPONSE, false};
    out->status = final;
    out->valid = true;
    return (cerberus_mac_monitor_operation_t){ESP_OK, false};
}

static int32_t mac_link_reserve(void *context, uint8_t radio, cerberus_mac_binding_t *binding)
{
    (void)context;
    if (!binding || radio != CERBERUS_MAC_WIFI) return ESP_ERR_INVALID_ARG;
    memset(binding, 0, sizeof(*binding));
    cerberus_c5_status_t peer;
    esp_err_t error = c5_spi_link_get_status(&peer);
    if (error) return error;
    uint32_t required = CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ;
    if (peer.firmware_version < CERBERUS_MAC_MIN_FIRMWARE || (peer.capabilities & required) != required)
        return ESP_ERR_NOT_SUPPORTED;
    if (peer.protocol_version != CERBERUS_PROTOCOL_VERSION) return ESP_ERR_INVALID_RESPONSE;
    uint64_t deadline = mac_link_now(NULL) + CSI_LINK_BUDGET_US;
    cerberus_mac_info_t info;
    error = mac_info(&info, deadline);
    if (error) return error;
    cerberus_mac_binding_t query = {.boot_id = info.boot_id};
    cerberus_mac_status_t status;
    error = mac_page(&query, CERBERUS_MAC_PAGE_CORE, &status, sizeof(status), deadline);
    if (error) return error;
    if (!mac_status_valid(&status)) return ESP_ERR_INVALID_RESPONSE;
    if (status.flags & CERBERUS_MAC_OWNED) return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(20)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (mac_link_now(NULL) >= deadline) { give_link_mutex(); return ESP_ERR_TIMEOUT; }
    if (mac_reserved_boot != info.boot_id) { mac_reserved_boot = info.boot_id; mac_reserved_job = 0; }
    uint32_t floor = status.job_id > mac_reserved_job ? status.job_id : mac_reserved_job;
    if (floor == UINT32_MAX) { give_link_mutex(); return ESP_ERR_INVALID_STATE; }
    binding->boot_id = info.boot_id;
    binding->job_id = mac_reserved_job = floor + 1U;
    give_link_mutex();
    return ESP_OK;
}

static cerberus_mac_monitor_operation_t mac_link_poll(void *context, uint8_t radio,
    const cerberus_mac_binding_t *binding, cerberus_mac_monitor_observation_t *out)
{
    (void)context;
    if (radio != CERBERUS_MAC_WIFI) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    return mac_observe(binding, out, mac_link_now(NULL) + CSI_LINK_BUDGET_US);
}

static cerberus_mac_monitor_operation_t mac_command(cerberus_spi_command_t command,
    const void *request, size_t size, const cerberus_mac_binding_t *binding,
    cerberus_mac_monitor_observation_t *out)
{
    memset(out, 0, sizeof(*out));
    uint64_t deadline = mac_link_now(NULL) + CSI_LINK_BUDGET_US;
    cerberus_mac_status_t response;
    esp_err_t error = csi_link_exchange(command, request, size, &response, sizeof(response), deadline);
    if (error) return (cerberus_mac_monitor_operation_t){error, false};
    if (!mac_status_valid(&response)) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_RESPONSE, false};
    /* Poll authoritative boot/job/profile even after a command acknowledgement;
     * a missing reply remains ambiguous and never causes a new job ID here. */
    return mac_observe(binding, out, deadline);
}

static cerberus_mac_monitor_operation_t mac_link_start(void *context, uint8_t radio,
    const cerberus_mac_start_request_t *request, cerberus_mac_monitor_observation_t *out)
{
    if (!context || radio != CERBERUS_MAC_WIFI)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    memset(context, 0, sizeof(cerberus_mac_c5_link_t));
    cerberus_mac_binding_t binding = {.boot_id = request->boot_id, .job_id = request->job_id};
    return mac_command(CERBERUS_SPI_COMMAND_MAC_START, request, sizeof(*request), &binding, out);
}

static cerberus_mac_monitor_operation_t mac_link_renew(void *context, uint8_t radio,
    const cerberus_mac_renew_request_t *request, cerberus_mac_monitor_observation_t *out)
{
    (void)context;
    if (radio != CERBERUS_MAC_WIFI) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    return mac_command(CERBERUS_SPI_COMMAND_MAC_RENEW, request, sizeof(*request), &request->binding, out);
}

static cerberus_mac_monitor_operation_t mac_link_stop(void *context, uint8_t radio,
    const cerberus_mac_binding_t *binding, cerberus_mac_monitor_observation_t *out)
{
    if (!context || radio != CERBERUS_MAC_WIFI)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    cerberus_mac_monitor_operation_t result = mac_command(CERBERUS_SPI_COMMAND_MAC_STOP,
        binding, sizeof(*binding), binding, out);
    if (result.no_ownership || (!result.error && out->valid && !(out->status.flags & CERBERUS_MAC_OWNED)))
        memset(context, 0, sizeof(cerberus_mac_c5_link_t));
    return result;
}

static bool mac_peek_valid(const cerberus_mac_peek_t *peek, uint64_t epoch)
{
    return peek->epoch == epoch && peek->snapshot_id && peek->version == CERBERUS_MAC_VERSION &&
        peek->count <= CERBERUS_MAC_CAPACITY &&
        peek->length == sizeof(cerberus_mac_snapshot_header_t) + peek->count * sizeof(cerberus_mac_row_t) &&
        csi_link_zero(peek->reserved, sizeof(peek->reserved));
}

static cerberus_mac_monitor_operation_t mac_read_fail(cerberus_mac_c5_link_t *link,
    cerberus_mac_snapshot_t *snapshot, int32_t error, bool absent)
{
    memset(snapshot, 0, sizeof(*snapshot));
    link->offset = 0;
    link->ready = false;
    /* Keep the earliest possible pin time and known descriptor on transport
     * failure. An old immutable bank cannot be relabeled with a later anchor. */
    if (absent) memset(link, 0, sizeof(*link));
    return (cerberus_mac_monitor_operation_t){error, absent};
}

static cerberus_mac_monitor_operation_t mac_link_read_step(void *context, uint8_t radio,
    const cerberus_mac_binding_t *binding, cerberus_mac_snapshot_t *snapshot,
    cerberus_mac_monitor_transfer_t *transfer)
{
    cerberus_mac_c5_link_t *link = context;
    if (transfer) memset(transfer, 0, sizeof(*transfer));
    if (!link || !binding || !snapshot || !transfer || radio != CERBERUS_MAC_WIFI || !binding->boot_id ||
        !binding->job_id || !binding->epoch)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    if (!link->attempted || memcmp(&link->binding, binding, sizeof(*binding))) {
        memset(link, 0, sizeof(*link));
        memset(snapshot, 0, sizeof(*snapshot));
        link->binding = *binding;
        link->request_started_us = mac_link_now(NULL);
        link->attempted = true;
    }
    uint64_t deadline = mac_link_now(NULL) + CSI_LINK_BUDGET_US;
    cerberus_mac_info_t info;
    esp_err_t error = mac_info(&info, deadline);
    if (error) return mac_read_fail(link, snapshot, error, false);
    if (info.boot_id != binding->boot_id)
        return mac_read_fail(link, snapshot, ESP_ERR_INVALID_STATE, true);
    if (info.source_now_us < link->last_info_us)
        return mac_read_fail(link, snapshot, ESP_ERR_INVALID_RESPONSE, false);
    link->last_info_us = info.source_now_us;
    cerberus_mac_peek_t peek;
    error = csi_link_exchange(CERBERUS_SPI_COMMAND_MAC_PEEK, binding, sizeof(*binding),
        &peek, sizeof(peek), deadline);
    if (error) return mac_read_fail(link, snapshot, error, false);
    if (!mac_peek_valid(&peek, binding->epoch) ||
        (link->descriptor_valid && memcmp(&peek, &link->peek, sizeof(peek))))
        return mac_read_fail(link, snapshot, ESP_ERR_INVALID_RESPONSE, false);
    link->peek = peek;
    link->descriptor_valid = true;
    for (unsigned i = 0; i < 4U && link->offset < peek.length; ++i) {
        cerberus_mac_read_request_t request = {.job_id = binding->job_id, .epoch = binding->epoch,
            .snapshot_id = peek.snapshot_id, .offset = link->offset};
        cerberus_mac_chunk_t chunk;
        error = csi_link_exchange(CERBERUS_SPI_COMMAND_MAC_READ, &request, sizeof(request),
            &chunk, sizeof(chunk), deadline);
        size_t count = peek.length - link->offset;
        if (count > CERBERUS_MAC_CHUNK_BYTES) count = CERBERUS_MAC_CHUNK_BYTES;
        if (error) return mac_read_fail(link, snapshot, error, false);
        if (chunk.offset != link->offset ||
            !csi_link_zero(chunk.data + count, CERBERUS_MAC_CHUNK_BYTES - count))
            return mac_read_fail(link, snapshot, ESP_ERR_INVALID_RESPONSE, false);
        memcpy((uint8_t *)snapshot + link->offset, chunk.data, count);
        link->offset = (uint16_t)(link->offset + count);
    }
    if (link->offset < peek.length) return (cerberus_mac_monitor_operation_t){ESP_OK, false};
    cerberus_mac_peek_t after;
    error = csi_link_exchange(CERBERUS_SPI_COMMAND_MAC_PEEK, binding, sizeof(*binding),
        &after, sizeof(after), deadline);
    cerberus_mac_info_t final;
    if (!error) error = mac_info(&final, deadline);
    if (error) return mac_read_fail(link, snapshot, error, false);
    if (final.boot_id != binding->boot_id)
        return mac_read_fail(link, snapshot, ESP_ERR_INVALID_STATE, true);
    if (memcmp(&after, &peek, sizeof(peek)) || final.source_now_us < info.source_now_us ||
        snapshot->header.source_now_us > final.source_now_us ||
        snapshot->header.snapshot_id != peek.snapshot_id || snapshot->header.count != peek.count ||
        snapshot->header.radio != CERBERUS_MAC_WIFI ||
        cerberus_mac_crc32(snapshot, peek.length) != peek.crc32 || !cerberus_mac_snapshot_valid(snapshot))
        return mac_read_fail(link, snapshot, ESP_ERR_INVALID_RESPONSE, false);
    link->last_info_us = final.source_now_us;
    link->ready = true;
    transfer->complete = transfer->timing_valid = true;
    transfer->request_started_us = link->request_started_us;
    return (cerberus_mac_monitor_operation_t){ESP_OK, false};
}

static cerberus_mac_monitor_operation_t mac_link_ack(void *context, uint8_t radio,
    const cerberus_mac_ack_request_t *request)
{
    cerberus_mac_c5_link_t *link = context;
    if (!link || radio != CERBERUS_MAC_WIFI || !link->attempted || !link->ready ||
        request->job_id != link->binding.job_id || request->epoch != link->binding.epoch ||
        request->snapshot_id != link->peek.snapshot_id)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG, false};
    uint64_t deadline = mac_link_now(NULL) + CSI_LINK_BUDGET_US;
    cerberus_mac_info_t info;
    esp_err_t error = mac_info(&info, deadline);
    if (error) return (cerberus_mac_monitor_operation_t){error, false};
    if (info.boot_id != link->binding.boot_id) {
        memset(link, 0, sizeof(*link));
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE, true};
    }
    cerberus_mac_status_t status;
    error = csi_link_exchange(CERBERUS_SPI_COMMAND_MAC_ACK, request, sizeof(*request),
        &status, sizeof(status), deadline);
    if (error) return (cerberus_mac_monitor_operation_t){error, false};
    if (!mac_status_valid(&status)) return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_RESPONSE, false};
    if (status.job_id != request->job_id || status.epoch != request->epoch) {
        memset(link, 0, sizeof(*link));
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE, true};
    }
    memset(link, 0, sizeof(*link));
    return (cerberus_mac_monitor_operation_t){ESP_OK, false};
}

void cerberus_mac_c5_link_init(cerberus_mac_c5_link_t *link, cerberus_mac_monitor_ops_t *ops)
{
    if (!link || !ops) return;
    memset(link, 0, sizeof(*link));
    *ops = (cerberus_mac_monitor_ops_t){.context = link, .now_us = mac_link_now,
        .reserve = mac_link_reserve, .start = mac_link_start, .poll = mac_link_poll,
        .renew = mac_link_renew, .read_step = mac_link_read_step, .ack = mac_link_ack,
        .stop = mac_link_stop};
}

esp_err_t cerberus_mac_c5_diagnostics(cerberus_mac_info_t *info,
    cerberus_mac_status_t *status, cerberus_mac_counters_t *counters)
{
    if (!info || !status || !counters) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info)); memset(status, 0, sizeof(*status)); memset(counters, 0, sizeof(*counters));
    uint64_t deadline = mac_link_now(NULL) + CSI_LINK_BUDGET_US;
    esp_err_t error = mac_info(info, deadline);
    if (error) return error;
    cerberus_mac_binding_t binding = {.boot_id = info->boot_id};
    error = mac_page(&binding, CERBERUS_MAC_PAGE_CORE, status, sizeof(*status), deadline);
    if (error || !mac_status_valid(status)) return error ? error : ESP_ERR_INVALID_RESPONSE;
    if (!status->epoch) return ESP_OK;
    binding.job_id = status->job_id; binding.epoch = status->epoch;
    error = mac_page(&binding, CERBERUS_MAC_PAGE_COUNTERS, counters, sizeof(*counters), deadline);
    if (!error && (counters->epoch != binding.epoch || counters->version != CERBERUS_MAC_VERSION || counters->reserved))
        error = ESP_ERR_INVALID_RESPONSE;
    if (error) memset(counters, 0, sizeof(*counters));
    return error;
}
