#ifdef NDEBUG
#undef NDEBUG
#endif
#include "c5_spi_link.h"
#include "csi_link.h"
#include "mac_monitor_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static int mutex;
static void *link_mutex = &mutex;
static uint8_t sequence;
static uint64_t clock_us, exchange_us;
static unsigned calls, takes, gives, reads, peeks, acks, fail_at, reboot_at, corrupt_at;
static bool held, deny_lock, pinned, deliver_start, corrupt_row;
static uint32_t last_ack;
static cerberus_c5_status_t peer;
static cerberus_mac_info_t info;
static cerberus_mac_status_t status;
static cerberus_mac_profile_t profile;
static cerberus_mac_observation_t collector;
static cerberus_mac_snapshot_t frozen, received;
static cerberus_mac_c5_link_t link;
static cerberus_mac_monitor_ops_t ops;
static cerberus_mac_binding_t binding;

static int64_t esp_timer_get_time(void) { return (int64_t)clock_us; }
static int take_link_mutex(unsigned timeout)
{ CHECK(timeout == 20U && !held); ++takes; if (deny_lock) return 0; held = true; return pdTRUE; }
static void give_link_mutex(void) { CHECK(held); held = false; ++gives; }
esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *out) { CHECK(!held); *out = peer; return ESP_OK; }

static void spi_request_response(const void *bytes, void *discarded, void *response_bytes, size_t length)
{
    (void)discarded;
    const cerberus_spi_frame_t *request = bytes;
    cerberus_spi_frame_t *response = response_bytes;
    CHECK(held && length == sizeof(*request) && cerberus_protocol_frame_is_valid(request));
    ++calls; clock_us += exchange_us;
    info.source_now_us = UINT64_C(1000000) + clock_us;
    if (calls == reboot_at) { ++info.boot_id; ++status.epoch; profile.epoch = status.epoch; }
    uint8_t payload[22] = {0}; bool valid = true;
    if (request->command == CERBERUS_SPI_COMMAND_MAC_STATUS) {
        cerberus_mac_status_request_t q; memcpy(&q, request->payload, sizeof(q));
        CHECK(request->payload_length == sizeof(q));
        if (q.page == CERBERUS_MAC_PAGE_INFO) {
            CHECK(!q.binding.boot_id && !q.binding.job_id && !q.binding.epoch);
            memcpy(payload, &info, sizeof(info));
        } else {
            valid = q.binding.boot_id == info.boot_id;
            if (q.page != CERBERUS_MAC_PAGE_CORE)
                valid &= q.binding.job_id == status.job_id && q.binding.epoch == status.epoch;
            if (q.page == CERBERUS_MAC_PAGE_CORE) memcpy(payload, &status, sizeof(status));
            else if (q.page == CERBERUS_MAC_PAGE_PROFILE) memcpy(payload, &profile, sizeof(profile));
            else if (q.page == CERBERUS_MAC_PAGE_COUNTERS) {
                cerberus_mac_counters_t counters = {.epoch = status.epoch, .version = 1,
                    .observations = collector.observations, .invalid = collector.invalid, .replacements = collector.replacements};
                memcpy(payload, &counters, sizeof(counters));
            } else CHECK(false);
        }
    } else if (request->command == CERBERUS_SPI_COMMAND_MAC_START) {
        cerberus_mac_start_request_t q; memcpy(&q, request->payload, sizeof(q));
        CHECK(request->payload_length == sizeof(q));
        if (deliver_start && q.boot_id == info.boot_id) {
            status.job_id = q.job_id; status.epoch = UINT64_C(0x1234567800000001);
            status.state = CERBERUS_MAC_RUNNING; status.flags = 3; status.channel = 6;
            profile.epoch = status.epoch;
        }
        memcpy(payload, &status, sizeof(status));
    } else if (request->command == CERBERUS_SPI_COMMAND_MAC_STOP || request->command == CERBERUS_SPI_COMMAND_MAC_RENEW) {
        cerberus_mac_binding_t q; memcpy(&q, request->payload, sizeof(q));
        valid = q.boot_id == info.boot_id && q.job_id == status.job_id && (!q.epoch || q.epoch == status.epoch);
        if (request->command == CERBERUS_SPI_COMMAND_MAC_STOP && valid) {
            status.state = CERBERUS_MAC_COMPLETE; status.flags = status.channel = 0;
        }
        memcpy(payload, &status, sizeof(status));
    } else if (request->command == CERBERUS_SPI_COMMAND_MAC_PEEK) {
        ++peeks;
        cerberus_mac_binding_t q; memcpy(&q, request->payload, sizeof(q));
        valid = q.boot_id == info.boot_id && q.job_id == status.job_id && q.epoch == status.epoch;
        if (!pinned) {
            CHECK(cerberus_mac_observation_snapshot(&collector, info.source_now_us, &frozen));
            if (corrupt_row && frozen.header.count) frozen.rows[0].radio = CERBERUS_MAC_BLE;
            pinned = true;
        }
        size_t size = sizeof(frozen.header) + frozen.header.count * sizeof(frozen.rows[0]);
        cerberus_mac_peek_t p = {.epoch = status.epoch, .snapshot_id = frozen.header.snapshot_id,
            .crc32 = cerberus_mac_crc32(&frozen, size), .length = (uint16_t)size, .count = frozen.header.count, .version = 1};
        memcpy(payload, &p, sizeof(p));
    } else if (request->command == CERBERUS_SPI_COMMAND_MAC_READ) {
        ++reads;
        cerberus_mac_read_request_t q; memcpy(&q, request->payload, sizeof(q));
        CHECK(request->payload_length == sizeof(q));
        size_t size = sizeof(frozen.header) + frozen.header.count * sizeof(frozen.rows[0]);
        valid = pinned && q.job_id == status.job_id && q.epoch == status.epoch &&
            q.snapshot_id == frozen.header.snapshot_id && q.offset < size && q.offset % 20U == 0U;
        if (valid) {
            cerberus_mac_chunk_t chunk = {.offset = q.offset};
            size_t count = size - q.offset; if (count > 20U) count = 20U;
            memcpy(chunk.data, (uint8_t *)&frozen + q.offset, count);
            if (calls == corrupt_at) chunk.data[0] ^= 1U;
            memcpy(payload, &chunk, sizeof(chunk));
        }
    } else if (request->command == CERBERUS_SPI_COMMAND_MAC_ACK) {
        ++acks;
        cerberus_mac_ack_request_t q; memcpy(&q, request->payload, sizeof(q));
        valid = q.job_id == status.job_id && q.epoch == status.epoch &&
            ((pinned && q.snapshot_id == frozen.header.snapshot_id) || q.snapshot_id == last_ack);
        if (valid) { last_ack = q.snapshot_id; pinned = false; }
        memcpy(payload, &status, sizeof(status));
    } else CHECK(false);
    memset(response, 0, sizeof(*response));
    if (valid) CHECK(cerberus_protocol_prepare_frame(response, request->command, request->sequence, payload, sizeof(payload)));
    if (calls == fail_at) memset(response, 0, sizeof(*response));
}

#include "c5_csi_link_impl.h"
#include "c5_mac_monitor_link_impl.h"

static void setup(unsigned count)
{
    clock_us = 0; exchange_us = 100; calls = takes = gives = reads = peeks = acks = 0;
    fail_at = reboot_at = corrupt_at = 0; last_ack = 0; held = deny_lock = pinned = corrupt_row = false;
    deliver_start = true;
    peer = (cerberus_c5_status_t){.firmware_version = 59, .protocol_version = CERBERUS_PROTOCOL_VERSION,
        .capabilities = CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ};
    info = (cerberus_mac_info_t){.boot_id = 123, .source_now_us = 1000000, .capacity = 32, .row_bytes = 40, .version = 1};
    status = (cerberus_mac_status_t){.job_id = 7, .epoch = UINT64_C(0x1234567800000001),
        .state = CERBERUS_MAC_RUNNING, .channel = 6, .flags = 3, .version = 1};
    profile = (cerberus_mac_profile_t){.epoch = status.epoch, .lease_ms = 5000, .dwell_ms = 250, .channel_count = 39, .version = 1};
    binding = (cerberus_mac_binding_t){.boot_id = info.boot_id, .job_id = status.job_id, .epoch = status.epoch};
    CHECK(cerberus_mac_observation_init(&collector, CERBERUS_MAC_WIFI, 0));
    for (unsigned i = 0; i < count; ++i) {
        uint8_t address[6] = {2,4,6,8,(uint8_t)i,10};
        CHECK(cerberus_mac_observation_observe(&collector, address, 0, -40, 6, CERBERUS_MAC_WIFI_DATA, i));
    }
    memset(&received, 0, sizeof(received));
    cerberus_mac_c5_link_init(&link, &ops);
}

static cerberus_mac_monitor_operation_t step(cerberus_mac_monitor_transfer_t *transfer)
{
    unsigned before = reads;
    cerberus_mac_monitor_operation_t r = ops.read_step(ops.context, 0, &binding, &received, transfer);
    CHECK(reads - before <= 4U && !held && gives <= takes);
    return r;
}

static unsigned drain(cerberus_mac_monitor_transfer_t *transfer)
{
    for (unsigned i = 0; i < 40U; ++i) {
        cerberus_mac_monitor_operation_t r = step(transfer);
        CHECK(!r.error && !r.no_ownership);
        if (transfer->complete) return i + 1U;
    }
    CHECK(false); return 0;
}

int main(void)
{
    cerberus_mac_monitor_transfer_t transfer;
    for (unsigned count = 0; count <= 32; ++count) {
        setup(count);
        unsigned n = drain(&transfer);
        CHECK(n == (sizeof(frozen.header) + count * sizeof(frozen.rows[0]) + 79U) / 80U);
        CHECK(transfer.timing_valid && transfer.request_started_us == 0 && !memcmp(&received, &frozen, sizeof(frozen)));
        CHECK(reads == (28U + count * 40U + 19U) / 20U && takes == gives);
        cerberus_mac_ack_request_t ack = {.job_id = binding.job_id, .epoch = binding.epoch, .snapshot_id = received.header.snapshot_id};
        CHECK(!ops.ack(ops.context, 0, &ack).error && !link.attempted && !pinned);
    }
    /* Lost first PEEK pins a bank despite the missing response. Its first
     * request anchor survives a long delay and full incremental retry. */
    setup(32); fail_at = 2;
    CHECK(step(&transfer).error && pinned && !transfer.complete && link.attempted);
    clock_us += 900000; fail_at = 0;
    drain(&transfer); CHECK(transfer.request_started_us == 0);
    uint32_t snapshot_id = received.header.snapshot_id;
    cerberus_mac_ack_request_t ack = {.job_id = binding.job_id, .epoch = binding.epoch, .snapshot_id = snapshot_id};
    fail_at = calls + 2;
    CHECK(ops.ack(ops.context, 0, &ack).error && link.ready && !pinned);
    fail_at = 0; CHECK(!ops.ack(ops.context, 0, &ack).error && !link.attempted);
    uint64_t next_anchor = clock_us; drain(&transfer);
    CHECK(transfer.request_started_us == next_anchor && received.header.snapshot_id == snapshot_id + 1U);

    setup(32); corrupt_at = 3;
    bool rejected = false;
    for (unsigned i = 0; i < 20; ++i) {
        cerberus_mac_monitor_operation_t r = step(&transfer);
        CHECK(!transfer.complete);
        if (r.error) { rejected = true; break; }
    }
    CHECK(rejected && link.offset == 0 && link.attempted);
    corrupt_at = 0; drain(&transfer); CHECK(transfer.request_started_us == 0);
    setup(1); corrupt_row = true; CHECK(step(&transfer).error && !transfer.complete);
    setup(32); CHECK(!step(&transfer).error && !transfer.complete);
    ++info.boot_id; CHECK(step(&transfer).no_ownership && !link.attempted);
    setup(0); reboot_at = 6; CHECK(step(&transfer).no_ownership && !transfer.complete);
    setup(32); exchange_us = 200000; CHECK(step(&transfer).error && calls == 1 && !held);
    setup(0); deny_lock = true; CHECK(step(&transfer).error && calls == 0 && !held);

    setup(0); cerberus_mac_monitor_observation_t observation;
    CHECK(!ops.poll(ops.context, 0, &binding, &observation).error && observation.valid);
    profile.dwell_ms = 25; CHECK(ops.poll(ops.context, 0, &binding, &observation).error && !observation.valid);
    setup(0); status.channel = 0; CHECK(ops.poll(ops.context, 0, &binding, &observation).error);
    setup(0); --status.job_id; CHECK(ops.poll(ops.context, 0, &binding, &observation).no_ownership);
    setup(0); info.row_bytes = 39; CHECK(ops.poll(ops.context, 0, &binding, &observation).error);
    setup(0); CHECK(!ops.stop(ops.context, 0, &binding, &observation).error && observation.valid && !(observation.status.flags & 1U));

    setup(0); status.state = CERBERUS_MAC_COMPLETE; status.flags = status.channel = 0;
    mac_reserved_boot = mac_reserved_job = 0;
    CHECK(!ops.reserve(ops.context, 0, &binding) && binding.job_id == 8 && !binding.epoch);
    CHECK(!ops.reserve(ops.context, 0, &binding) && binding.job_id == 9);
    status.job_id = UINT32_MAX; CHECK(ops.reserve(ops.context, 0, &binding));
    setup(0); peer.firmware_version = 58; CHECK(ops.reserve(ops.context, 0, &binding) == ESP_ERR_NOT_SUPPORTED && !calls);
    printf("MAC transport: %u checks passed\n", checks);
    return 0;
}
