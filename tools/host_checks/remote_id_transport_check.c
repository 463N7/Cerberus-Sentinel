#undef NDEBUG
#include "c5_spi_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static int mutex_token;
static void *link_mutex = &mutex_token;
static uint8_t sequence;
static bool locked, lock_failure;
static unsigned takes, gives, calls, peer_reads, command_reads;
static unsigned fail_call, peer_defect;
static esp_err_t fail_error;
static cerberus_spi_command_t expected_command;
static uint64_t expected_id;
static cerberus_remote_id_start_request_t request;
static cerberus_c5_status_t peer;
static cerberus_remote_id_status_payload_t response;
static uint8_t sent[22];
static size_t sent_length;

static bool zero(const void *data, size_t length)
{ const uint8_t *p = data; for (size_t i = 0; i < length; ++i) if (p[i]) return false; return true; }
static int take_link_mutex(unsigned timeout)
{ CHECK(timeout == 250 && !locked); ++takes; if (lock_failure) return 0; locked = true; return pdTRUE; }
static void give_link_mutex(void) { CHECK(locked); locked = false; ++gives; }
static bool failure(void) { return ++calls == fail_call; }
static esp_err_t get_status_v2_locked(uint8_t seq, cerberus_c5_status_t *out)
{
    CHECK(locked && seq == sequence); ++peer_reads;
    if (failure()) return fail_error;
    *out = peer;
    if (peer_reads == 2) switch (peer_defect) {
    case 1: ++out->firmware_version; break;
    case 2: out->capabilities ^= CERBERUS_C5_CAP_WIFI_5_GHZ; break;
    case 3: ++out->boot_count; break;
    case 4: --out->uptime_ms; break;
    case 5: ++out->protocol_version; break;
    case 6: ++out->reset_reason; break;
    default: out->uptime_ms += 20; break;
    }
    return ESP_OK;
}
static esp_err_t exchange_v2_locked(cerberus_spi_command_t command, const void *data,
    size_t length, void *out, size_t out_length)
{
    CHECK(locked && data && out && command == expected_command && out_length == sizeof(response));
    ++command_reads;
    if (command == CERBERUS_SPI_COMMAND_REMOTE_ID_START) {
        CHECK(length == sizeof(request) && !memcmp(data, &request, sizeof(request)));
    } else {
        CHECK(length == sizeof(cerberus_remote_id_job_request_t));
        const cerberus_remote_id_job_request_t *query = data;
        CHECK(query->job_id == expected_id);
    }
    CHECK(length <= sizeof(sent)); memcpy(sent, data, length); sent_length = length;
    if (failure()) { memset(out, 0xA5, out_length); return fail_error; }
    memcpy(out, &response, out_length);
    return ESP_OK;
}
#include "c5_remote_id_link.h"

static void reset(cerberus_spi_command_t command)
{
    CHECK(!locked); link_mutex = &mutex_token; sequence = 0; lock_failure = false;
    takes = gives = calls = peer_reads = command_reads = fail_call = peer_defect = 0;
    fail_error = ESP_FAIL; expected_command = command;
    expected_id = UINT64_C(0xFEDCBA9876543210);
    memset(sent, 0, sizeof(sent)); sent_length = 0;
    request = (cerberus_remote_id_start_request_t){.job_id = expected_id,
        .duration_ms = 8000, .lease_ms = 3000, .dwell_ms = 250};
    peer = (cerberus_c5_status_t){.firmware_version = 54,
        .capabilities = CERBERUS_C5_CAP_WATCH_OBSERVATIONS | CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ,
        .protocol_version = CERBERUS_PROTOCOL_VERSION, .uptime_ms = 100, .boot_count = 7, .reset_reason = 3};
    response = (cerberus_remote_id_status_payload_t){.job_id = expected_id,
        .state = CERBERUS_REMOTE_ID_STATE_RUNNING, .generation = 255, .channel = 44,
        .elapsed_ms = 500, .lease_remaining_ms = 2500,
        .flags = CERBERUS_REMOTE_ID_FLAG_OWNED | CERBERUS_REMOTE_ID_FLAG_RECEIVING};
}
static esp_err_t invoke(cerberus_remote_id_status_payload_t *out)
{
    memset(out, 0xA5, sizeof(*out));
    esp_err_t error;
    switch (expected_command) {
    case CERBERUS_SPI_COMMAND_REMOTE_ID_START: error = c5_spi_link_remote_id_start(&request, out); break;
    case CERBERUS_SPI_COMMAND_REMOTE_ID_STOP: error = c5_spi_link_remote_id_stop(expected_id, out); break;
    case CERBERUS_SPI_COMMAND_REMOTE_ID_RENEW: error = c5_spi_link_remote_id_renew(expected_id, out); break;
    default: error = c5_spi_link_remote_id_status(expected_id, out); break;
    }
    CHECK(!locked && gives == (takes && !lock_failure ? 1U : 0U));
    if (error) CHECK(zero(out, sizeof(*out)));
    else CHECK(!memcmp(out, &response, sizeof(response)));
    return error;
}

static void command_success_and_retries(void)
{
    cerberus_remote_id_status_payload_t out;
    for (unsigned command = 90; command <= 93; ++command) {
        reset((cerberus_spi_command_t)command);
        CHECK(invoke(&out) == ESP_OK && peer_reads == 2 && command_reads == 1 && calls == 3);
        CHECK(out.job_id == UINT64_C(0xFEDCBA9876543210));
        /* Retried application requests preserve the full job/config bytes.
         * The producer suite separately proves same-ID idempotent delivery. */
        uint8_t first[22]; memcpy(first, sent, sizeof(first)); size_t first_length = sent_length;
        takes = gives = peer_reads = command_reads = calls = 0;
        CHECK(invoke(&out) == ESP_OK && sent_length == first_length && !memcmp(first, sent, first_length));
        reset((cerberus_spi_command_t)command);
        response = (cerberus_remote_id_status_payload_t){.job_id = expected_id,
            .state = CERBERUS_REMOTE_ID_STATE_REJECTED, .error_code = ESP_ERR_NOT_SUPPORTED};
        CHECK(invoke(&out) == ESP_OK && out.state == CERBERUS_REMOTE_ID_STATE_REJECTED &&
            out.error_code == ESP_ERR_NOT_SUPPORTED && peer_reads == 2);
        reset((cerberus_spi_command_t)command);
        response = (cerberus_remote_id_status_payload_t){.job_id = expected_id,
            .state = CERBERUS_REMOTE_ID_STATE_COMPLETE, .flags = CERBERUS_REMOTE_ID_FLAG_DATA_READY,
            .generation = 0, .elapsed_ms = 8000};
        CHECK(invoke(&out) == ESP_OK); /* Generation wrap is valid. */
    }
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); expected_id = 0;
    CHECK(invoke(&out) == ESP_OK); /* Discover retained nonzero high-water ID. */
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); expected_id = 0; memset(&response, 0, sizeof(response));
    CHECK(invoke(&out) == ESP_OK); /* Canonical never-started idle state. */
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); response.job_id = expected_id = UINT64_MAX;
    CHECK(invoke(&out) == ESP_OK); /* Exhausted ID can still be inspected/stopped. */
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STOP); response.job_id = expected_id = UINT64_MAX;
    CHECK(invoke(&out) == ESP_OK);
}

static void failures_and_legacy(void)
{
    cerberus_remote_id_status_payload_t out;
    for (unsigned command = 90; command <= 93; ++command) {
        for (unsigned stage = 1; stage <= 3; ++stage) {
            reset((cerberus_spi_command_t)command); fail_call = stage;
            CHECK(invoke(&out) == ESP_FAIL && calls == stage);
            reset((cerberus_spi_command_t)command); fail_call = stage; fail_error = ESP_ERR_NOT_SUPPORTED;
            CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE); /* Never fallback after transport failure. */
        }
        for (unsigned defect = 1; defect <= 6; ++defect) {
            reset((cerberus_spi_command_t)command); peer_defect = defect;
            CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE && calls == 3);
        }
        for (uint32_t version = 0; version < CERBERUS_REMOTE_ID_MIN_FIRMWARE; ++version) {
            reset((cerberus_spi_command_t)command); peer.firmware_version = version;
            CHECK(invoke(&out) == ESP_ERR_NOT_SUPPORTED && peer_reads == 1 && !command_reads);
        }
        for (unsigned missing = 0; missing < 2; ++missing) {
            reset((cerberus_spi_command_t)command);
            peer.capabilities &= ~(missing ? CERBERUS_C5_CAP_WATCH_OBSERVATIONS : CERBERUS_C5_CAP_WIFI_2_4_GHZ);
            CHECK(invoke(&out) == ESP_ERR_NOT_SUPPORTED && !command_reads);
        }
        reset((cerberus_spi_command_t)command); peer.protocol_version = 1;
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE && !command_reads);
        reset((cerberus_spi_command_t)command); peer.firmware_version = 55;
        CHECK(invoke(&out) == ESP_OK);
        reset((cerberus_spi_command_t)command); link_mutex = NULL;
        CHECK(invoke(&out) == ESP_ERR_INVALID_STATE && !takes && !calls);
        reset((cerberus_spi_command_t)command); lock_failure = true;
        CHECK(invoke(&out) == ESP_ERR_TIMEOUT && takes == 1 && !gives && !calls);
    }
}

static void request_guards(void)
{
    cerberus_remote_id_status_payload_t out;
    for (unsigned defect = 0; defect < 15; ++defect) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START);
        switch (defect) {
        case 0: request.job_id = 0; break;
        case 1: request.flags = 1; break;
        case 2: request.duration_ms = CERBERUS_REMOTE_ID_WINDOW_MIN_MS - 1; break;
        case 3: request.duration_ms = CERBERUS_REMOTE_ID_WINDOW_MAX_MS + 1; break;
        case 4: request.lease_ms = CERBERUS_REMOTE_ID_LEASE_MIN_MS - 1; break;
        case 5: request.lease_ms = CERBERUS_REMOTE_ID_LEASE_MAX_MS + 1; break;
        case 6: request.dwell_ms = CERBERUS_REMOTE_ID_DWELL_MIN_MS - 1; break;
        case 7: request.dwell_ms = CERBERUS_REMOTE_ID_DWELL_MAX_MS + 1; break;
        default: request.reserved[defect - 8] = 1; break;
        }
        CHECK(invoke(&out) == ESP_ERR_INVALID_ARG && !takes && !calls);
    }
    for (unsigned boundary = 0; boundary < 8; ++boundary) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START);
        request.duration_ms = boundary & 1 ? CERBERUS_REMOTE_ID_WINDOW_MAX_MS : CERBERUS_REMOTE_ID_WINDOW_MIN_MS;
        request.lease_ms = boundary & 2 ? CERBERUS_REMOTE_ID_LEASE_MAX_MS : CERBERUS_REMOTE_ID_LEASE_MIN_MS;
        request.dwell_ms = boundary & 4 ? CERBERUS_REMOTE_ID_DWELL_MAX_MS : CERBERUS_REMOTE_ID_DWELL_MIN_MS;
        CHECK(invoke(&out) == ESP_OK);
    }
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); memset(&out, 0xA5, sizeof(out));
    CHECK(c5_spi_link_remote_id_start(NULL, &out) == ESP_ERR_INVALID_ARG && zero(&out, sizeof(out)));
    CHECK(c5_spi_link_remote_id_start(&request, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_remote_id_status(0, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_remote_id_stop(1, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_remote_id_renew(1, NULL) == ESP_ERR_INVALID_ARG && !takes && !calls);
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STOP); expected_id = 0;
    CHECK(invoke(&out) == ESP_ERR_INVALID_ARG && !calls);
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_RENEW); expected_id = 0;
    CHECK(invoke(&out) == ESP_ERR_INVALID_ARG && !calls);
}

static void state_matrix(void)
{
    cerberus_remote_id_status_payload_t out;
    /* Independent public state/ownership truth table for a nonzero job. */
    const uint8_t masks[8] = {0, 1U << 1, (1U << 1) | (1U << 3),
        (1U << 0) | (1U << 4), 1U << 1, 1U << 0, (1U << 0) | (1U << 1), 1U << 0};
    for (unsigned state = 0; state < 8; ++state) for (unsigned flags = 0; flags < 8; ++flags) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS);
        response = (cerberus_remote_id_status_payload_t){.job_id = expected_id,
            .state = state, .flags = flags, .channel = flags & 2 ? 6 : 0,
            .error_code = state >= 6 ? ESP_FAIL : ESP_OK};
        bool expected = (masks[state] & (1U << flags)) != 0;
        CHECK(invoke(&out) == (expected ? ESP_OK : ESP_ERR_INVALID_RESPONSE));
    }
    for (unsigned state = 8; state <= 255; ++state) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); response.state = state;
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE);
    }
    for (unsigned flags = 8; flags <= 255; ++flags) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); response.flags = flags;
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE);
    }
    for (unsigned channel = 0; channel <= 255; ++channel) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); response.channel = channel;
        bool legal = (channel >= 1 && channel <= 14) || channel == 36 || channel == 40 ||
            channel == 44 || channel == 48 || channel == 52 || channel == 56 || channel == 60 ||
            channel == 64 || channel == 100 || channel == 104 || channel == 108 || channel == 112 ||
            channel == 116 || channel == 120 || channel == 124 || channel == 128 || channel == 132 ||
            channel == 136 || channel == 140 || channel == 144 || channel == 149 || channel == 153 ||
            channel == 157 || channel == 161 || channel == 165 || channel == 169 ||
            channel == 173 || channel == 177;
        CHECK(invoke(&out) == (legal ? ESP_OK : ESP_ERR_INVALID_RESPONSE));
    }
    for (unsigned defect = 0; defect < 9; ++defect) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS);
        switch (defect) {
        case 0: response.job_id = 0; break;
        case 1: response.job_id ^= UINT64_C(1) << 48; break;
        case 2: response.lease_remaining_ms = 5001; break;
        case 3: response.flags = CERBERUS_REMOTE_ID_FLAG_OWNED; break; /* Non-RX has channel. */
        case 4: response.error_code = ESP_FAIL; break;
        case 5: response.state = CERBERUS_REMOTE_ID_STATE_ERROR; response.flags = 1; response.channel = 0; break;
        case 6: peer.capabilities &= ~CERBERUS_C5_CAP_WIFI_5_GHZ; break;
        case 7: response.state = CERBERUS_REMOTE_ID_STATE_COMPLETE; response.flags = 4; response.channel = 0; break;
        default: expected_id = 0; response = (cerberus_remote_id_status_payload_t){
            .state = CERBERUS_REMOTE_ID_STATE_REJECTED, .job_id = 123, .error_code = ESP_FAIL}; break;
        }
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE);
    }
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS);
    peer.capabilities &= ~CERBERUS_C5_CAP_WIFI_5_GHZ; response.channel = 6;
    CHECK(invoke(&out) == ESP_OK);
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); response.channel = 0; response.flags = 1;
    response.lease_remaining_ms = 0; response.elapsed_ms = UINT32_MAX;
    CHECK(invoke(&out) == ESP_OK); /* Retune or lease-expiry cleanup can hold ownership. */
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STOP); response.channel = 0; response.flags = 1;
    response.state = CERBERUS_REMOTE_ID_STATE_ERROR; response.error_code = ESP_FAIL;
    response.lease_remaining_ms = 0;
    CHECK(invoke(&out) == ESP_OK && (out.flags & CERBERUS_REMOTE_ID_FLAG_OWNED));
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STOP); response = (cerberus_remote_id_status_payload_t){
        .job_id = expected_id, .state = CERBERUS_REMOTE_ID_STATE_ERROR, .error_code = ESP_FAIL};
    CHECK(invoke(&out) == ESP_OK && !out.flags); /* Error cause survives successful cleanup. */
    for (unsigned field = 0; field < 7; ++field) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); expected_id = 0; memset(&response, 0, sizeof(response));
        switch (field) {
        case 0: response.job_id = 1; break; case 1: response.error_code = 1; break;
        case 2: response.elapsed_ms = 1; break; case 3: response.lease_remaining_ms = 1; break;
        case 4: response.generation = 1; break; case 5: response.channel = 1; break;
        default: response.flags = 1; break;
        }
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE);
    }
    for (unsigned field = 0; field < 6; ++field) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); response = (cerberus_remote_id_status_payload_t){
            .job_id = expected_id, .state = CERBERUS_REMOTE_ID_STATE_REJECTED, .error_code = ESP_FAIL};
        switch (field) {
        case 0: response.error_code = 0; break; case 1: response.elapsed_ms = 1; break;
        case 2: response.lease_remaining_ms = 1; break; case 3: response.generation = 1; break;
        case 4: response.channel = 1; break; default: response.flags = 1; break;
        }
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE);
    }
}

static esp_err_t invoke_bound(const cerberus_c5_status_t *expected,
    cerberus_remote_id_status_payload_t *out)
{
    memset(out, 0xA5, sizeof(*out));
    esp_err_t error;
    switch (expected_command) {
    case CERBERUS_SPI_COMMAND_REMOTE_ID_START:
        error = c5_spi_link_remote_id_start_for_peer(&request, expected, out); break;
    case CERBERUS_SPI_COMMAND_REMOTE_ID_STOP:
        error = c5_spi_link_remote_id_stop_for_peer(expected_id, expected, out); break;
    case CERBERUS_SPI_COMMAND_REMOTE_ID_RENEW:
        error = c5_spi_link_remote_id_renew_for_peer(expected_id, expected, out); break;
    default: error = c5_spi_link_remote_id_status_for_peer(expected_id, expected, out); break;
    }
    CHECK(!locked && gives == (takes && !lock_failure ? 1U : 0U));
    if (error) CHECK(zero(out, sizeof(*out)));
    else CHECK(!memcmp(out, &response, sizeof(response)));
    return error;
}

static void expected_peer_boundary(void)
{
    cerberus_remote_id_status_payload_t out;
    for (unsigned command = 90; command <= 93; ++command) {
        for (unsigned defect = 0; defect < 8; ++defect) {
            reset((cerberus_spi_command_t)command);
            const cerberus_c5_status_t expected = peer;
            switch (defect) {
            case 0: ++peer.boot_count; break;
            case 1: --peer.uptime_ms; break;
            case 2: ++peer.firmware_version; break;
            case 3: peer.capabilities ^= CERBERUS_C5_CAP_WIFI_5_GHZ; break;
            case 4: ++peer.reset_reason; break;
            case 5: peer.protocol_version = 1; break;
            case 6: peer.firmware_version = 53; break;
            default: peer.capabilities = 0; break;
            }
            CHECK(invoke_bound(&expected, &out) == CERBERUS_REMOTE_ID_ERR_PEER_CHANGED);
            CHECK(peer_reads == 1 && !command_reads && calls == 1);
        }
        reset((cerberus_spi_command_t)command);
        cerberus_c5_status_t expected = peer; --expected.uptime_ms;
        /* Heap, phase and live busy status are not boot/firmware identity. */
        expected.free_heap_kib = 1; expected.minimum_free_heap_kib = 1;
        expected.ble_phase = 1; expected.radio_state = 1;
        CHECK(invoke_bound(&expected, &out) == ESP_OK && calls == 3);
        reset((cerberus_spi_command_t)command); expected = peer;
        CHECK(invoke_bound(&expected, &out) == ESP_OK); /* Equal clock is valid. */
        for (unsigned defect = 1; defect <= 6; ++defect) {
            reset((cerberus_spi_command_t)command); expected = peer; peer_defect = defect;
            CHECK(invoke_bound(&expected, &out) == ESP_ERR_INVALID_RESPONSE && command_reads == 1);
        }
        for (unsigned stage = 1; stage <= 3; ++stage) {
            reset((cerberus_spi_command_t)command); expected = peer;
            fail_call = stage; fail_error = ESP_ERR_TIMEOUT;
            CHECK(invoke_bound(&expected, &out) == ESP_ERR_TIMEOUT && calls == stage);
            CHECK(command_reads == (stage >= 2 ? 1U : 0U));
            reset((cerberus_spi_command_t)command); expected = peer;
            fail_call = stage; fail_error = CERBERUS_REMOTE_ID_ERR_PEER_CHANGED;
            CHECK(invoke_bound(&expected, &out) == ESP_ERR_INVALID_RESPONSE);
        }
        reset((cerberus_spi_command_t)command); peer.firmware_version = 53; expected = peer;
        CHECK(invoke_bound(&expected, &out) == ESP_ERR_NOT_SUPPORTED && !command_reads);
        reset((cerberus_spi_command_t)command);
        CHECK(invoke_bound(NULL, &out) == ESP_ERR_INVALID_ARG && !calls && !takes);
        reset((cerberus_spi_command_t)command); expected = peer; expected_id = 0; request.job_id = 0;
        if (command == CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS) {
            memset(&response, 0, sizeof(response));
            CHECK(invoke_bound(&expected, &out) == ESP_OK);
        } else CHECK(invoke_bound(&expected, &out) == ESP_ERR_INVALID_ARG && !calls);
    }
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START);
    CHECK(c5_spi_link_remote_id_start_for_peer(NULL, &peer, &out) == ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_remote_id_start_for_peer(&request, &peer, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_remote_id_status_for_peer(0, &peer, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_remote_id_stop_for_peer(1, &peer, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_remote_id_renew_for_peer(1, &peer, NULL) == ESP_ERR_INVALID_ARG && !calls && !takes);
}

static bool listed_channel(unsigned channel)
{
    static const uint8_t channels[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,
        149,153,157,161,165,169,173,177};
    for (size_t i = 0; i < sizeof(channels); ++i) if (channels[i] == channel) return true;
    return false;
}

static void follow_request(unsigned transport, uint8_t channel)
{
    peer.firmware_version = 55;
    request.flags = (uint8_t)transport;
    const uint8_t transmitter[6] = {0x02,0x31,0x42,0x53,0x64,0x75};
    memcpy(request.transmitter, transmitter, sizeof(transmitter));
    request.follow_channel = channel;
    response.flags |= CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED;
    if (channel) response.flags |= CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
}

static void follow_request_contract(void)
{
    cerberus_remote_id_status_payload_t out;
    CHECK(sizeof(request) == 22 && offsetof(cerberus_remote_id_start_request_t, transmitter) == 15 &&
        offsetof(cerberus_remote_id_start_request_t, follow_channel) == 21);
    /* The generic wire image remains the revision54 image on either peer. */
    for (unsigned version = 54; version <= 55; ++version) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); peer.firmware_version = version;
        CHECK(invoke(&out) == ESP_OK && sent_length == 22 && zero(sent + 14, 8));
    }
    for (unsigned transport = 1; transport <= 2; ++transport) {
        for (unsigned version = 0; version < 55; ++version) {
            reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 6);
            peer.firmware_version = version;
            cerberus_c5_status_t expected = peer;
            CHECK(invoke_bound(&expected, &out) == ESP_ERR_NOT_SUPPORTED &&
                peer_reads == 1 && !command_reads);
        }
        for (unsigned channel = 0; channel <= 255; ++channel) {
            reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, (uint8_t)channel);
            bool valid = !channel || listed_channel(channel);
            CHECK(invoke(&out) == (valid ? ESP_OK : ESP_ERR_INVALID_ARG));
            CHECK(valid ? calls == 3 : !takes && !calls);
            if (valid) CHECK(sent[14] == transport && !memcmp(sent + 15, request.transmitter, 6) &&
                sent[21] == channel);
        }
        for (unsigned first = 0; first <= 255; ++first) {
            reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 6);
            request.transmitter[0] = (uint8_t)first;
            CHECK(invoke(&out) == (first & 1U ? ESP_ERR_INVALID_ARG : ESP_OK));
            if (first & 1U) CHECK(!takes && !calls);
        }
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 6);
        memset(request.transmitter, 0, sizeof(request.transmitter));
        CHECK(invoke(&out) == ESP_ERR_INVALID_ARG && !calls && !takes);
        /* The capability is about actual receive status. An unsupported hint
         * can be explicitly SEARCHed by the producer, without fake 5GHz RX. */
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 149);
        peer.capabilities &= ~CERBERUS_C5_CAP_WIFI_5_GHZ;
        response.flags &= ~CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND; response.channel = 6;
        CHECK(invoke(&out) == ESP_OK);
    }
    for (unsigned flags = 0; flags <= 255; ++flags) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(flags, 6);
        bool valid = flags == 1 || flags == 2;
        CHECK(invoke(&out) == (valid ? ESP_OK : ESP_ERR_INVALID_ARG));
        if (!valid) CHECK(!takes && !calls);
    }
}

static void follow_status_matrix(void)
{
    cerberus_remote_id_status_payload_t out;
    /* Independent rev70 ownership table combined with the new profile facts.
     * IDLE requires job0; this matrix deliberately uses a nonzero scoped ID. */
    const uint8_t base_masks[8] = {0, 1U << 1, (1U << 1) | (1U << 3),
        (1U << 0) | (1U << 4), 1U << 1, 1U << 0, (1U << 0) | (1U << 1), 1U << 0};
    for (unsigned version = 54; version <= 56; ++version)
    for (unsigned state = 0; state < 8; ++state)
    for (unsigned flags = 0; flags <= 255; ++flags) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS); peer.firmware_version = version;
        response = (cerberus_remote_id_status_payload_t){.job_id = expected_id,
            .state = state, .flags = flags, .channel = flags & 2 ? 6 : 0,
            .error_code = state >= 6 ? ESP_FAIL : ESP_OK};
        unsigned base = flags & 7U, profile = (flags >> 3U) & 3U;
        bool valid = flags <= 31 && (base_masks[state] & (1U << base)) &&
            (profile == 0 || (version >= 55 && state != 7 &&
                (profile == 1 || (profile == 3 && state != 1))));
        CHECK(invoke(&out) == (valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE));
    }
    /* Read-only discovery can return retained Follow history; the exact-ID
     * stop/renew path retains the same facts without implying active RX. */
    for (unsigned command = 91; command <= 93; ++command) {
        reset((cerberus_spi_command_t)command); follow_request(1, 6);
        response = (cerberus_remote_id_status_payload_t){.job_id = expected_id,
            .state = CERBERUS_REMOTE_ID_STATE_COMPLETE, .generation = 0, .elapsed_ms = 8000,
            .flags = CERBERUS_REMOTE_ID_FLAG_DATA_READY | CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED |
                CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND};
        if (command == 91) expected_id = 0;
        CHECK(invoke(&out) == ESP_OK);
    }
}

static void follow_start_reply_and_peer_binding(void)
{
    cerberus_remote_id_status_payload_t out;
    for (unsigned transport = 1; transport <= 2; ++transport) {
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 6);
        response.state = CERBERUS_REMOTE_ID_STATE_STARTING; response.channel = 0;
        response.flags = CERBERUS_REMOTE_ID_FLAG_OWNED | CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED;
        CHECK(invoke(&out) == ESP_OK); /* Pending proof, not SEARCH evidence. */
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 6);
        response.flags &= ~CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
        CHECK(invoke(&out) == ESP_OK); /* Explicit supported SEARCH. */
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 0);
        response.flags |= CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE && command_reads == 1);
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 6);
        response.flags = CERBERUS_REMOTE_ID_FLAG_OWNED | CERBERUS_REMOTE_ID_FLAG_RECEIVING;
        CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE && command_reads == 1);
        reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); follow_request(transport, 6);
        response = (cerberus_remote_id_status_payload_t){.job_id = expected_id,
            .state = CERBERUS_REMOTE_ID_STATE_REJECTED, .error_code = ESP_ERR_NOT_SUPPORTED};
        CHECK(invoke(&out) == ESP_OK); /* Service rejection never means downgrade. */
    }
    reset(CERBERUS_SPI_COMMAND_REMOTE_ID_START); peer.firmware_version = 55;
    response.flags |= CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED;
    CHECK(invoke(&out) == ESP_ERR_INVALID_RESPONSE); /* Generic request cannot acquire Follow. */
    for (unsigned command = 90; command <= 93; ++command) {
        for (unsigned defect = 0; defect < 6; ++defect) {
            reset((cerberus_spi_command_t)command); follow_request(1, 6);
            cerberus_c5_status_t expected = peer;
            switch (defect) {
            case 0: ++peer.boot_count; break; case 1: --peer.uptime_ms; break;
            case 2: peer.firmware_version = 54; break; case 3: ++peer.reset_reason; break;
            case 4: peer.capabilities ^= CERBERUS_C5_CAP_WIFI_5_GHZ; break;
            default: ++peer.protocol_version; break;
            }
            CHECK(invoke_bound(&expected, &out) == CERBERUS_REMOTE_ID_ERR_PEER_CHANGED && !command_reads);
        }
        for (unsigned stage = 1; stage <= 3; ++stage) {
            reset((cerberus_spi_command_t)command); follow_request(2, 149);
            cerberus_c5_status_t expected = peer; fail_call = stage; fail_error = ESP_ERR_TIMEOUT;
            CHECK(invoke_bound(&expected, &out) == ESP_ERR_TIMEOUT);
            CHECK(command_reads == (stage >= 2 ? 1U : 0U));
            for (unsigned special = 0; special < 2; ++special) {
                reset((cerberus_spi_command_t)command); follow_request(2, 149); expected = peer;
                fail_call = stage; fail_error = special ? CERBERUS_REMOTE_ID_ERR_PEER_CHANGED : ESP_ERR_NOT_SUPPORTED;
                CHECK(invoke_bound(&expected, &out) == ESP_ERR_INVALID_RESPONSE);
            }
        }
        for (unsigned defect = 1; defect <= 6; ++defect) {
            reset((cerberus_spi_command_t)command); follow_request(1, 6);
            cerberus_c5_status_t expected = peer; peer_defect = defect;
            CHECK(invoke_bound(&expected, &out) == ESP_ERR_INVALID_RESPONSE && command_reads == 1);
        }
    }
}

int main(void)
{
    command_success_and_retries(); failures_and_legacy(); request_guards(); state_matrix();
    expected_peer_boundary(); follow_request_contract(); follow_status_matrix();
    follow_start_reply_and_peer_binding();
    printf("Remote ID production transport: %u checks passed\n", checks);
    return 0;
}
