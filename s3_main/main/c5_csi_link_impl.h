/* Included by c5_spi_link.c to reuse its READY transport and existing mutex.
 * CSI uses one physical attempt per command, with application-level retry and
 * a bounded per-operation deadline. No whole-record mutex is retained. */
#include "csi_link.h"

#define CSI_LINK_BUDGET_US 150000ULL
static uint64_t csi_reserved_boot;
static uint32_t csi_reserved_job;

static uint64_t csi_link_now(void *context)
{ (void)context; return (uint64_t)esp_timer_get_time(); }

static esp_err_t csi_link_exchange(cerberus_spi_command_t command,
    const void *request_data, size_t request_length, void *output,
    size_t output_length, uint64_t deadline)
{
    if (!output || !output_length) return ESP_ERR_INVALID_ARG;
    memset(output, 0, output_length);
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (csi_link_now(NULL) >= deadline) return ESP_ERR_TIMEOUT;
    if (take_link_mutex(pdMS_TO_TICKS(20)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t error = ESP_ERR_TIMEOUT;
    if (csi_link_now(NULL) < deadline) {
        cerberus_spi_frame_t request, response = {0};
        uint8_t discarded[CERBERUS_SPI_TRANSFER_SIZE] = {0};
        uint8_t token = ++sequence;
        if (!cerberus_protocol_prepare_frame(&request, command, token,
                request_data, request_length)) error = ESP_ERR_INVALID_ARG;
        else {
            spi_request_response(&request, discarded, &response, sizeof(request));
            error = ESP_ERR_INVALID_RESPONSE;
            if (cerberus_protocol_frame_is_valid(&response) &&
                response.command == command && response.sequence == token &&
                response.payload_length == output_length) {
                memcpy(output, response.payload, output_length);
                error = ESP_OK;
            }
        }
    }
    give_link_mutex();
    if (csi_link_now(NULL) >= deadline) error = ESP_ERR_TIMEOUT;
    if (error != ESP_OK) memset(output, 0, output_length);
    return error;
}

static bool csi_link_zero(const uint8_t *data, size_t size)
{ for (size_t i = 0; i < size; ++i) if (data[i]) return false; return true; }

static bool csi_link_status_valid(const cerberus_csi_status_t *s)
{
    if (s->version != CERBERUS_CSI_VERSION || s->reserved ||
        s->state > CERBERUS_CSI_ERROR || s->queued > CERBERUS_CSI_RING_CAPACITY ||
        (s->flags & ~(CERBERUS_CSI_FLAG_OWNED | CERBERUS_CSI_FLAG_RECEIVING))) return false;
    bool owned = (s->flags & CERBERUS_CSI_FLAG_OWNED) != 0;
    if (s->state == CERBERUS_CSI_IDLE) return !owned && !s->job_id && !s->epoch &&
        !s->queued && !s->channel && !s->error_code && !s->flags;
    if (!s->job_id || !s->epoch) return false;
    if (!(s->flags & CERBERUS_CSI_FLAG_RECEIVING) && s->channel) return false;
    if ((s->flags & CERBERUS_CSI_FLAG_RECEIVING) && !s->channel) return false;
    if ((s->state == CERBERUS_CSI_STARTING || s->state == CERBERUS_CSI_RUNNING ||
        s->state == CERBERUS_CSI_STOPPING) && !owned) return false;
    if (s->state == CERBERUS_CSI_COMPLETE && owned) return false;
    if (!owned && s->queued) return false;
    return !(s->flags & CERBERUS_CSI_FLAG_RECEIVING) ||
        (owned && s->state == CERBERUS_CSI_RUNNING);
}

static esp_err_t csi_link_info(cerberus_csi_info_t *info, uint64_t deadline)
{
    cerberus_csi_status_request_t request = {.page = CERBERUS_CSI_PAGE_INFO};
    esp_err_t error = csi_link_exchange(CERBERUS_SPI_COMMAND_CSI_STATUS,
        &request, sizeof(request), info, sizeof(*info), deadline);
    if (error == ESP_OK && (!info->boot_id ||
        info->version != CERBERUS_CSI_VERSION ||
        info->format != CERBERUS_CSI_FORMAT_LLTF_I8 ||
        info->capacity != CERBERUS_CSI_RING_CAPACITY ||
        !csi_link_zero(info->reserved, sizeof(info->reserved)))) error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) memset(info, 0, sizeof(*info));
    return error;
}

static esp_err_t csi_link_page(const cerberus_csi_binding_t *binding, uint8_t page,
    void *output, size_t size, uint64_t deadline)
{
    cerberus_csi_status_request_t request = {
        .boot_id = binding->boot_id, .job_id = binding->job_id,
        .epoch = binding->epoch, .page = page};
    return csi_link_exchange(CERBERUS_SPI_COMMAND_CSI_STATUS, &request,
        sizeof(request), output, size, deadline);
}

static cerberus_csi_operation_t csi_link_observe(const cerberus_csi_binding_t *binding,
    cerberus_csi_observation_t *out, uint64_t deadline)
{
    memset(out, 0, sizeof(*out));
    cerberus_csi_info_t info;
    esp_err_t error = csi_link_info(&info, deadline);
    if (error) return (cerberus_csi_operation_t){error, false};
    if (info.boot_id != binding->boot_id)
        return (cerberus_csi_operation_t){ESP_ERR_INVALID_STATE, true};
    error = csi_link_page(binding, CERBERUS_CSI_PAGE_STATUS,
        &out->status, sizeof(out->status), deadline);
    if (error || !csi_link_status_valid(&out->status)) {
        memset(out, 0, sizeof(*out));
        return (cerberus_csi_operation_t){error ? error : ESP_ERR_INVALID_RESPONSE, false};
    }
    out->status_valid = true;
    if (out->status.job_id != binding->job_id ||
        (binding->epoch && out->status.epoch != binding->epoch))
        return (cerberus_csi_operation_t){ESP_ERR_INVALID_STATE, true};
    if (!out->status.epoch) return (cerberus_csi_operation_t){ESP_OK, false};
    cerberus_csi_binding_t exact = *binding;
    exact.epoch = out->status.epoch;
    error = csi_link_page(&exact, CERBERUS_CSI_PAGE_TARGET,
        &out->target, sizeof(out->target), deadline);
    if (!error) error = csi_link_page(&exact, CERBERUS_CSI_PAGE_CLOCK,
        &out->clock, sizeof(out->clock), deadline);
    if (!error) error = csi_link_page(&exact, CERBERUS_CSI_PAGE_COUNTERS,
        &out->counters, sizeof(out->counters), deadline);
    cerberus_csi_status_t after = {0};
    if (!error) error = csi_link_page(&exact, CERBERUS_CSI_PAGE_STATUS,
        &after, sizeof(after), deadline);
    if (!error && (!csi_link_status_valid(&after) || after.job_id != exact.job_id ||
        after.epoch != exact.epoch || out->target.epoch != exact.epoch ||
        out->target.version != CERBERUS_CSI_VERSION ||
        !csi_link_zero(out->target.reserved, sizeof(out->target.reserved)) ||
        ((after.flags & CERBERUS_CSI_FLAG_RECEIVING) && out->target.channel != after.channel) ||
        out->target.lease_ms < CERBERUS_CSI_LEASE_MIN_MS ||
        out->target.lease_ms > CERBERUS_CSI_LEASE_MAX_MS ||
        out->clock.version != CERBERUS_CSI_VERSION || out->clock.epoch != exact.epoch ||
        out->clock.source_now_us < info.source_now_us ||
        out->counters.version != CERBERUS_CSI_VERSION ||
        out->counters.epoch != exact.epoch || out->counters.reserved)) error = ESP_ERR_INVALID_RESPONSE;
    if (error) {
        memset(&out->clock, 0, sizeof(out->clock));
        memset(&out->counters, 0, sizeof(out->counters));
        memset(&out->target, 0, sizeof(out->target));
        return (cerberus_csi_operation_t){error, false};
    }
    out->status = after;
    out->timing_valid = true;
    return (cerberus_csi_operation_t){ESP_OK, false};
}

static int32_t csi_link_reserve(void *context, const cerberus_csi_target_t *target,
    cerberus_csi_binding_t *binding)
{
    (void)context;
    memset(binding, 0, sizeof(*binding));
    if (!cerberus_csi_target_valid(target)) return ESP_ERR_INVALID_ARG;
    cerberus_c5_status_t peer;
    esp_err_t error = c5_spi_link_get_status(&peer);
    if (error) return error;
    if (peer.firmware_version < CERBERUS_CSI_MIN_FIRMWARE) return ESP_ERR_NOT_SUPPORTED;
    if (peer.protocol_version != CERBERUS_PROTOCOL_VERSION) return ESP_ERR_INVALID_RESPONSE;
    uint32_t capability = target->channel <= 14U ? CERBERUS_C5_CAP_WIFI_2_4_GHZ : CERBERUS_C5_CAP_WIFI_5_GHZ;
    if (!(peer.capabilities & capability)) return ESP_ERR_NOT_SUPPORTED;
    uint64_t deadline = csi_link_now(NULL) + CSI_LINK_BUDGET_US;
    cerberus_csi_info_t info;
    error = csi_link_info(&info, deadline);
    if (error) return error;
    cerberus_csi_binding_t query = {.boot_id = info.boot_id};
    cerberus_csi_status_t status;
    error = csi_link_page(&query, CERBERUS_CSI_PAGE_STATUS, &status, sizeof(status), deadline);
    if (error) return error;
    if (!csi_link_status_valid(&status)) return ESP_ERR_INVALID_RESPONSE;
    if ((status.flags & CERBERUS_CSI_FLAG_OWNED) || status.job_id == UINT32_MAX)
        return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(20)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (csi_link_now(NULL) >= deadline) { give_link_mutex(); return ESP_ERR_TIMEOUT; }
    if (csi_reserved_boot != info.boot_id) { csi_reserved_boot = info.boot_id; csi_reserved_job = 0U; }
    uint32_t floor = status.job_id > csi_reserved_job ? status.job_id : csi_reserved_job;
    if (floor == UINT32_MAX) { give_link_mutex(); return ESP_ERR_INVALID_STATE; }
    binding->boot_id = info.boot_id;
    binding->job_id = csi_reserved_job = floor + 1U;
    give_link_mutex();
    return ESP_OK;
}

static cerberus_csi_operation_t csi_link_poll(void *context,
    const cerberus_csi_binding_t *binding, cerberus_csi_observation_t *out)
{
    (void)context;
    return csi_link_observe(binding, out, csi_link_now(NULL) + CSI_LINK_BUDGET_US);
}

static cerberus_csi_operation_t csi_link_command(cerberus_spi_command_t command,
    const void *request, size_t size, const cerberus_csi_binding_t *binding,
    cerberus_csi_observation_t *out)
{
    memset(out, 0, sizeof(*out));
    uint64_t deadline = csi_link_now(NULL) + CSI_LINK_BUDGET_US;
    cerberus_csi_status_t response;
    esp_err_t error = csi_link_exchange(command, request, size, &response, sizeof(response), deadline);
    /* A missing/late reply is ambiguous. The controller polls the exact boot
     * and job before retrying or releasing its claim. Never adopt a zeroed ACK. */
    if (error) return (cerberus_csi_operation_t){error, false};
    if (!csi_link_status_valid(&response))
        return (cerberus_csi_operation_t){ESP_ERR_INVALID_RESPONSE, false};
    if (response.job_id != binding->job_id ||
        (binding->epoch && response.epoch != binding->epoch))
        return csi_link_observe(binding, out, deadline);
    out->status = response;
    out->status_valid = true;
    cerberus_csi_operation_t result = csi_link_observe(binding, out, deadline);
    if (result.error && !out->status_valid && !result.no_ownership) {
        out->status = response;
        out->status_valid = true;
    }
    return result;
}

static cerberus_csi_operation_t csi_link_start(void *context,
    const cerberus_csi_start_request_t *request, cerberus_csi_observation_t *out)
{
    (void)context;
    cerberus_csi_binding_t binding = {.boot_id=request->boot_id,.job_id=request->job_id};
    return csi_link_command(CERBERUS_SPI_COMMAND_CSI_START,request,sizeof(*request),&binding,out);
}
static cerberus_csi_operation_t csi_link_stop(void *context,
    const cerberus_csi_binding_t *binding, cerberus_csi_observation_t *out)
{ (void)context; return csi_link_command(CERBERUS_SPI_COMMAND_CSI_STOP,binding,sizeof(*binding),binding,out); }
static cerberus_csi_operation_t csi_link_renew(void *context,
    const cerberus_csi_renew_request_t *request, cerberus_csi_observation_t *out)
{
    (void)context;
    cerberus_csi_binding_t binding = {.boot_id=request->boot_id,.job_id=request->job_id,.epoch=request->epoch};
    return csi_link_command(CERBERUS_SPI_COMMAND_CSI_RENEW,request,sizeof(*request),&binding,out);
}

static cerberus_csi_operation_t csi_link_read(void *context,
    const cerberus_csi_binding_t *binding, cerberus_csi_sample_t *sample, bool *available)
{
    (void)context;
    memset(sample, 0, sizeof(*sample)); *available = false;
    if (!binding->epoch) return (cerberus_csi_operation_t){ESP_ERR_INVALID_ARG,false};
    uint64_t deadline=csi_link_now(NULL)+CSI_LINK_BUDGET_US;
    cerberus_csi_info_t info;
    esp_err_t error=csi_link_info(&info,deadline);
    if(error)return (cerberus_csi_operation_t){error,false};
    if(info.boot_id!=binding->boot_id)return (cerberus_csi_operation_t){ESP_ERR_INVALID_STATE,true};
    cerberus_csi_peek_t peek;
    error=csi_link_exchange(CERBERUS_SPI_COMMAND_CSI_PEEK,binding,sizeof(*binding),&peek,sizeof(peek),deadline);
    if(!error && (peek.epoch!=binding->epoch || peek.reserved || peek.flags ||
        peek.format!=CERBERUS_CSI_FORMAT_LLTF_I8 || peek.queued>CERBERUS_CSI_RING_CAPACITY ||
        (!peek.sequence && (peek.length||peek.crc32||peek.queued)) ||
        (peek.sequence && (peek.length!=sizeof(*sample)||!peek.queued)))) error=ESP_ERR_INVALID_RESPONSE;
    if(!error && !peek.sequence)return (cerberus_csi_operation_t){ESP_OK,false};
    for(size_t offset=0;!error && offset<sizeof(*sample);offset+=CERBERUS_CSI_CHUNK_BYTES){
        cerberus_csi_read_request_t request={.job_id=binding->job_id,.epoch=binding->epoch,
            .sequence=peek.sequence,.offset=(uint8_t)offset};
        cerberus_csi_chunk_t chunk;
        error=csi_link_exchange(CERBERUS_SPI_COMMAND_CSI_READ,&request,sizeof(request),&chunk,sizeof(chunk),deadline);
        size_t count=sizeof(*sample)-offset;if(count>CERBERUS_CSI_CHUNK_BYTES)count=CERBERUS_CSI_CHUNK_BYTES;
        if(!error && (chunk.offset!=offset || chunk.length!=count ||
            !csi_link_zero(chunk.data+count,CERBERUS_CSI_CHUNK_BYTES-count)))error=ESP_ERR_INVALID_RESPONSE;
        if(!error)memcpy((uint8_t *)sample+offset,chunk.data,count);
    }
    cerberus_csi_peek_t after;
    if(!error)error=csi_link_exchange(CERBERUS_SPI_COMMAND_CSI_PEEK,binding,sizeof(*binding),&after,sizeof(after),deadline);
    if(!error && (after.epoch!=peek.epoch||after.sequence!=peek.sequence||after.crc32!=peek.crc32||
        after.length!=peek.length||after.format!=peek.format||after.flags||after.reserved||
        !after.queued||after.queued>CERBERUS_CSI_RING_CAPACITY||
        sample->sequence!=peek.sequence||sample->format!=peek.format))error=ESP_ERR_INVALID_RESPONSE;
    if(!error && cerberus_csi_crc32(sample,sizeof(*sample))!=peek.crc32)error=ESP_ERR_INVALID_CRC;
    cerberus_csi_info_t final_info;
    if(!error)error=csi_link_info(&final_info,deadline);
    if(!error && (final_info.boot_id!=binding->boot_id || final_info.source_now_us<sample->source_us ||
        final_info.source_now_us<info.source_now_us ||
        !(sample->flags&CERBERUS_CSI_SAMPLE_ESTIMATE_VALID) ||
        (sample->flags&~(CERBERUS_CSI_SAMPLE_ESTIMATE_VALID|CERBERUS_CSI_SAMPLE_FIRST_INVALID)) ||
        sample->secondary>2U || sample->rssi < -127 || sample->rssi>20 ||
        !((sample->phy>=1U&&sample->phy<=7U)||sample->phy==11U)))error=ESP_ERR_INVALID_RESPONSE;
    cerberus_csi_target_t sample_target={.bssid={2U},.channel=sample->channel};
    if(!error && !cerberus_csi_target_valid(&sample_target))error=ESP_ERR_INVALID_RESPONSE;
    if(error)memset(sample,0,sizeof(*sample));else *available=true;
    return (cerberus_csi_operation_t){error,false};
}

static cerberus_csi_operation_t csi_link_ack(void *context,const cerberus_csi_ack_request_t *request)
{
    (void)context;
    cerberus_csi_status_t response;
    esp_err_t error=csi_link_exchange(CERBERUS_SPI_COMMAND_CSI_ACK,request,sizeof(*request),
        &response,sizeof(response),csi_link_now(NULL)+CSI_LINK_BUDGET_US);
    if(!error && (!csi_link_status_valid(&response)||response.job_id!=request->job_id||
        response.epoch!=request->epoch))error=ESP_ERR_INVALID_RESPONSE;
    return (cerberus_csi_operation_t){error,false};
}

void cerberus_csi_link_ops(cerberus_csi_ops_t *ops)
{
    if(!ops)return;
    *ops=(cerberus_csi_ops_t){.reserve=csi_link_reserve,.start=csi_link_start,.poll=csi_link_poll,
        .renew=csi_link_renew,.stop=csi_link_stop,.read=csi_link_read,.ack=csi_link_ack,.now_us=csi_link_now};
}

esp_err_t cerberus_csi_link_diagnostics(cerberus_csi_info_t *info,cerberus_csi_observation_t *out)
{
    if(!info||!out)return ESP_ERR_INVALID_ARG;
    memset(info,0,sizeof(*info));memset(out,0,sizeof(*out));
    cerberus_c5_status_t peer;
    esp_err_t error=c5_spi_link_get_status(&peer);
    if(error)return error;
    if(peer.firmware_version<CERBERUS_CSI_MIN_FIRMWARE)return ESP_ERR_NOT_SUPPORTED;
    uint64_t deadline=csi_link_now(NULL)+CSI_LINK_BUDGET_US;
    error=csi_link_info(info,deadline);if(error)return error;
    cerberus_csi_binding_t binding={.boot_id=info->boot_id};
    error=csi_link_page(&binding,CERBERUS_CSI_PAGE_STATUS,&out->status,sizeof(out->status),deadline);
    if(error||!csi_link_status_valid(&out->status)){memset(out,0,sizeof(*out));return error?error:ESP_ERR_INVALID_RESPONSE;}
    out->status_valid=true;
    if(!out->status.job_id)return ESP_OK;
    binding.job_id=out->status.job_id;binding.epoch=out->status.epoch;
    return csi_link_observe(&binding,out,deadline).error;
}
