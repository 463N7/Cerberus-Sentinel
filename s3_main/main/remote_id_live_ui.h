/* Private integration after the ordinary live/Watch helpers in ui.c. A single
 * on-demand PSRAM object owns presentation and both passive sweep sessions. */
#include "remote_id_session.h"
#include "remote_id_ui.h"
#include "remote_id_diagnostics.h"

typedef struct {
    cerberus_remote_id_inventory_t inventory;
    cerberus_remote_id_session_t session;
    cerberus_remote_id_ui_view_t view;
    cerberus_remote_id_ui_cache_t cache;
    uint64_t c5_epoch;
    uint64_t c5_job_id;
    cerberus_remote_id_status_payload_t c5_profile;
    cerberus_c5_status_t c5_peer;
    cerberus_remote_id_follow_hint_t c5_follow_hint; /* Immutable attempted job profile. */
    uint32_t c5_uptime, c5_boot, c5_firmware, last_tick;
    size_t offset;
    char identifiers[5][24], phases[2][40], message[49];
    uint8_t c5_before_generation;
    bool c5_seen, c5_priority, c5_profile_valid, detail, exit_pending, follow_enabled, motion_detail;
} remote_id_live_store_t;
static remote_id_live_store_t *remote_id_live;
static esp_err_t remote_id_open_error;
/* The C5's retained high-water ID makes page/S3 restarts safe. This local
 * high-water also prevents reuse of an attempted ID whose reply was lost. */
static uint64_t remote_id_last_job_id;

static void remote_id_publish_diagnostics(void)
{
    if (!remote_id_live) { cerberus_remote_id_diagnostics_publish(NULL); return; }
    remote_id_live_store_t *store = remote_id_live;
    cerberus_remote_id_diagnostics_t snapshot = {.open = true,
        .running = store->session.running,
        .stopping = !store->session.running && !cerberus_remote_id_session_released(&store->session),
        .selected = store->inventory.selected.valid,
        .present = store->inventory.selected.valid && store->inventory.selected.present};
    snapshot.wifi_priority = store->c5_priority;
    snapshot.wifi_profile_valid = store->c5_priority && store->c5_profile_valid;
    if (store->follow_enabled) snapshot.wifi_follow_flags |= CERBERUS_REMOTE_ID_DIAG_FOLLOW_ENABLED;
    if (snapshot.wifi_profile_valid) {
        snapshot.wifi_job_id = store->c5_profile.job_id;
        snapshot.wifi_window_elapsed_ms = store->c5_profile.elapsed_ms;
        snapshot.wifi_lease_remaining_ms = store->c5_profile.lease_remaining_ms;
        snapshot.wifi_job_state = store->c5_profile.state;
        snapshot.wifi_channel = store->c5_profile.channel;
        if (store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED)
            snapshot.wifi_follow_flags |= CERBERUS_REMOTE_ID_DIAG_FOLLOW_REQUESTED;
        if (store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND)
            snapshot.wifi_follow_flags |= CERBERUS_REMOTE_ID_DIAG_FOLLOW_BOUND;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const cerberus_remote_id_radio_session_t *r = &store->session.radios[i];
        snapshot.radios[i].completed_sweeps = r->published_sweeps;
        snapshot.radios[i].error = r->error ? r->error : (r->faulted || r->scan.faulted) ? ESP_FAIL : ESP_OK;
        snapshot.radios[i].dropped_reports = store->inventory.banks[i].status.dropped_records;
        snapshot.radios[i].retained_sources = store->inventory.banks[i].count;
        snapshot.radios[i].owned = r->owned;
    }
    cerberus_remote_id_diagnostics_publish(&snapshot);
}

static bool remote_id_takeover_ready(void)
{
    if (!recon_sessions_idle_for_takeover() || wardrive_start_pending || wardrive_return_pending) return false;
    cerberus_wardrive_status_t wardrive;
    cerberus_wardrive_get_status(&wardrive);
    return wardrive.state != CERBERUS_WARDRIVE_STARTING &&
        wardrive.state != CERBERUS_WARDRIVE_RUNNING && wardrive.state != CERBERUS_WARDRIVE_STOPPING;
}

static uint64_t remote_id_clock(void *context)
{ (void)context; return (uint64_t)(esp_timer_get_time() / 1000LL); }

static int32_t remote_id_priority_poll(remote_id_live_store_t *store,
    const cerberus_c5_status_t *peer, bool owned,
    cerberus_remote_id_radio_observation_t *out)
{
    bool scoped = owned || store->session.radios[0].capture_pending;
    if (scoped && !store->c5_job_id) return ESP_ERR_INVALID_STATE;
    cerberus_remote_id_status_payload_t status = {0};
    esp_err_t error = c5_spi_link_remote_id_status_for_peer(scoped ? store->c5_job_id : 0U, peer, &status);
    if (error != ESP_OK) return error;
    /* Reading status never extends a lease. Explicit renewal stops as soon as
     * the user stops/exits, and never renews a foreign or cleanup-only job. */
    const cerberus_remote_id_radio_session_t *session = &store->session.radios[0];
    if (owned && !session->identity_pending && out->epoch == session->expected_epoch &&
        status.generation == session->expected_generation &&
        store->session.running && !store->exit_pending &&
        (status.flags & CERBERUS_REMOTE_ID_FLAG_OWNED) &&
        (status.state == CERBERUS_REMOTE_ID_STATE_STARTING ||
         status.state == CERBERUS_REMOTE_ID_STATE_RUNNING) && status.lease_remaining_ms < 1500U) {
        error = c5_spi_link_remote_id_renew_for_peer(store->c5_job_id, peer, &status);
        if (error != ESP_OK) return error;
    }
    store->c5_profile = status;
    store->c5_profile_valid = true;
    out->identity_verified = scoped && status.job_id == store->c5_job_id &&
        status.state != CERBERUS_REMOTE_ID_STATE_REJECTED;
    out->generation = status.generation;
    out->radio_error = status.error_code;
    out->released = (status.flags & CERBERUS_REMOTE_ID_FLAG_OWNED) == 0U;
    switch (status.state) {
    case CERBERUS_REMOTE_ID_STATE_STARTING:
    case CERBERUS_REMOTE_ID_STATE_RUNNING:
    case CERBERUS_REMOTE_ID_STATE_STOPPING:
        out->state = SENTINEL_SCAN_RUNNING; break;
    case CERBERUS_REMOTE_ID_STATE_COMPLETE:
        out->state = SENTINEL_SCAN_COMPLETE;
        if (scoped && !(status.flags & CERBERUS_REMOTE_ID_FLAG_DATA_READY))
            out->radio_error = CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED;
        break;
    case CERBERUS_REMOTE_ID_STATE_ERROR:
        out->state = out->released ? SENTINEL_SCAN_ERROR : SENTINEL_SCAN_RUNNING;
        break;
    case CERBERUS_REMOTE_ID_STATE_REJECTED:
        /* A scoped no-match is authoritative absence of OUR attempted job.
         * Use the saved pre-start generation, never a foreign job's byte. */
        out->generation = store->c5_before_generation;
        out->state = SENTINEL_SCAN_IDLE; break;
    default: out->state = SENTINEL_SCAN_IDLE; break;
    }
    out->busy = (peer->radio_state & (owned ? (uint8_t)~CERBERUS_C5_RADIO_WIFI_SCAN : UINT8_MAX)) != 0U ||
        (!owned && (!out->released || !remote_id_takeover_ready()));
    return ESP_OK;
}

static int32_t remote_id_poll(void *context, uint8_t radio, bool owned,
    cerberus_remote_id_radio_observation_t *out)
{
    remote_id_live_store_t *store = context;
    memset(out, 0, sizeof(*out));
    if (radio == CERBERUS_WATCH_RADIO_BLE) {
        cerberus_ble_scan_status_payload_t status = {0};
        esp_err_t error = cerberus_s3_ble_scanner_get_status(&status);
        if (error == ESP_ERR_NOT_SUPPORTED) { out->released = true; return ESP_OK; }
        if (error != ESP_OK) return error;
        cerberus_s3_ble_owner_t owner = cerberus_s3_ble_host_owner();
        out->supported = true;
        out->epoch = cerberus_s3_ble_host_epoch();
        out->generation = status.generation; out->state = status.state;
        out->radio_error = status.error_code;
        out->released = status.state != CERBERUS_BLE_SCAN_RUNNING && owner != CERBERUS_S3_BLE_OWNER_SCANNER;
        out->busy = owner != CERBERUS_S3_BLE_OWNER_NONE || status.state == CERBERUS_BLE_SCAN_RUNNING ||
            cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER || (!owned && !remote_id_takeover_ready());
        if (owned && owner == CERBERUS_S3_BLE_OWNER_SCANNER) out->busy = false;
        return ESP_OK;
    }
    cerberus_c5_status_t peer = {0};
    store->c5_profile_valid = false;
    esp_err_t error = c5_spi_link_get_status(&peer);
    if (error != ESP_OK) return error;
    if (!store->c5_seen || peer.boot_count != store->c5_boot || peer.uptime_ms < store->c5_uptime ||
        peer.firmware_version != store->c5_firmware || peer.reset_reason != store->c5_peer.reset_reason ||
        peer.capabilities != store->c5_peer.capabilities || peer.protocol_version != store->c5_peer.protocol_version) {
        if (store->c5_epoch == UINT64_MAX) return ESP_ERR_INVALID_STATE;
        ++store->c5_epoch;
    }
    store->c5_seen = true; store->c5_uptime = peer.uptime_ms;
    store->c5_boot = peer.boot_count; store->c5_firmware = peer.firmware_version;
    store->c5_peer = peer;
    out->epoch = store->c5_epoch;
    out->supported = peer.firmware_version >= CERBERUS_WATCH_MIN_FIRMWARE &&
        (peer.capabilities & CERBERUS_C5_CAP_WATCH_OBSERVATIONS) &&
        (peer.capabilities & CERBERUS_C5_CAP_WIFI_2_4_GHZ);
    if (!out->supported) { store->c5_priority = false; out->released = true; return ESP_OK; }
    store->c5_priority = peer.firmware_version >= CERBERUS_REMOTE_ID_MIN_FIRMWARE;
    if (store->c5_priority) {
        error = remote_id_priority_poll(store, &peer, owned, out);
        if (error != ESP_OK) return error; /* Supported failures never downgrade. */
    } else {
        cerberus_wifi_scan_status_payload_t status = {0};
        error = c5_spi_link_get_wifi_scan_status(&status);
        if (error != ESP_OK) return error;
        out->generation = status.generation; out->state = status.state;
        out->radio_error = status.error_code;
        out->released = status.state != CERBERUS_WIFI_SCAN_RUNNING;
        out->busy = (peer.radio_state & (owned ? (uint8_t)~CERBERUS_C5_RADIO_WIFI_SCAN : UINT8_MAX)) != 0U ||
            (!owned && (status.state == CERBERUS_WIFI_SCAN_RUNNING || !remote_id_takeover_ready()));
    }
    if (!owned) {
        /* C5's ordinary Start can preempt channel survey. Detect foreign work
         * before calling it; this page never asks that service to surrender. */
        if (peer.firmware_version >= CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE) {
            cerberus_wifi_channel_survey_status_payload_t survey = {0};
            error = c5_spi_link_wifi_channel_survey_status(&survey);
            if (error != ESP_OK) return error;
            out->busy |= survey.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING;
        }
        if (peer.capabilities & CERBERUS_C5_CAP_WIFI_STATION) {
            cerberus_wifi_connection_status_payload_t connection = {0};
            error = c5_spi_link_get_wifi_connection_status(&connection);
            if (error != ESP_OK) return error;
            out->busy |= connection.state != CERBERUS_WIFI_CONNECTION_IDLE &&
                connection.state != CERBERUS_WIFI_CONNECTION_ERROR;
        }
    }
    return ESP_OK;
}

static cerberus_remote_id_command_result_t remote_id_command(void *context, uint8_t radio,
    bool start, const cerberus_remote_id_radio_observation_t *before)
{
    cerberus_remote_id_command_result_t result = {0};
    remote_id_live_store_t *store = context;
    if (start && radio == CERBERUS_WATCH_RADIO_WIFI && store->follow_enabled &&
        store->c5_firmware < CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE) {
        result.error = ESP_ERR_NOT_SUPPORTED; result.rejected = true; return result;
    }
    /* All ordinary scan callers except Wardriving run on this same UI task.
     * Its background service is excluded by the takeover gate. Recheck a STOP
     * immediately: normal scan APIs have no generation-scoped stop argument. */
    if (!start) {
        cerberus_remote_id_radio_observation_t fresh = {0};
        result.error = remote_id_poll(context, radio, true, &fresh);
        if (result.error) return result;
        if (fresh.epoch != before->epoch || fresh.generation != before->generation) {
            result.error = CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED; return result;
        }
        if (fresh.state != SENTINEL_SCAN_RUNNING) return result;
    }
    if (radio == CERBERUS_WATCH_RADIO_BLE) {
        cerberus_ble_scan_status_payload_t status = {0};
        result.error = start ? cerberus_s3_ble_scanner_start(&status) : cerberus_s3_ble_scanner_stop(&status);
        result.rejected = start && result.error != ESP_OK; /* Local enqueue is authoritative. */
        if (!result.error) {
            result.identity_valid = true; result.generation = status.generation;
            result.epoch = cerberus_s3_ble_host_epoch();
        }
    } else if (store->c5_priority) {
        cerberus_remote_id_status_payload_t status = {0};
        store->c5_profile_valid = false;
        if (start) {
            uint64_t high = store->c5_profile.job_id > remote_id_last_job_id ?
                store->c5_profile.job_id : remote_id_last_job_id;
            if (high == UINT64_MAX) {
                result.error = CERBERUS_REMOTE_ID_ERROR_TOKEN_EXHAUSTED;
                result.rejected = true; return result;
            }
            remote_id_last_job_id = store->c5_job_id = high + 1U;
            store->c5_before_generation = before->generation;
            cerberus_remote_id_start_request_t request = {
                .job_id = store->c5_job_id, .duration_ms = 8000U,
                .lease_ms = 3000U, .dwell_ms = 250U,
            };
            memset(&store->c5_follow_hint, 0, sizeof(store->c5_follow_hint));
            if (store->follow_enabled) {
                cerberus_remote_id_follow_hint_t hint;
                int64_t now = esp_timer_get_time();
                if (!cerberus_remote_id_inventory_follow_hint(&store->inventory,
                    now >= 0 ? (uint64_t)now : UINT64_MAX, &hint)) {
                    result.error = ESP_ERR_INVALID_STATE; result.rejected = true; return result;
                }
                if (now < 0 || (hint.channel > 14U && !(store->c5_peer.capabilities & CERBERUS_C5_CAP_WIFI_5_GHZ)))
                    hint.channel = 0U;
                store->c5_follow_hint = hint;
                request.flags = hint.transport == SENTINEL_WATCH_TRANSPORT_WIFI_NAN ?
                    CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN : CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON;
                memcpy(request.transmitter, hint.transmitter, sizeof(request.transmitter));
                request.follow_channel = hint.channel;
            }
            result.error = c5_spi_link_remote_id_start_for_peer(&request, &store->c5_peer, &status);
            /* NOT_SUPPORTED is reserved by the transport for no-delivery
             * capability preflight. A later poll may select legacy behavior. */
            result.rejected = result.error == ESP_ERR_NOT_SUPPORTED ||
                result.error == CERBERUS_REMOTE_ID_ERR_PEER_CHANGED;
            result.identity_pending = result.error != ESP_OK && !result.rejected;
        } else result.error = c5_spi_link_remote_id_stop_for_peer(store->c5_job_id, &store->c5_peer, &status);
        if (!result.error) {
            store->c5_profile = status;
            store->c5_profile_valid = true;
            if (status.state == CERBERUS_REMOTE_ID_STATE_REJECTED) {
                result.error = status.error_code ? status.error_code : ESP_ERR_INVALID_STATE;
                result.rejected = start;
            } else {
                result.identity_valid = true; result.generation = status.generation;
                result.epoch = before->epoch;
            }
        }
    } else {
        cerberus_wifi_scan_status_payload_t status = {0};
        result.error = start ? c5_spi_link_start_wifi_scan(&status) : c5_spi_link_stop_wifi_scan(&status);
        if (!result.error && start && (status.generation != (uint8_t)(before->generation + 1U) ||
            (status.state != CERBERUS_WIFI_SCAN_RUNNING && status.state != CERBERUS_WIFI_SCAN_COMPLETE))) {
            /* The legacy C5 handler returns current status even on rejected
             * START. An unchanged snapshot does not acknowledge a new job. */
            result.error = status.error_code ? status.error_code : ESP_ERR_INVALID_STATE;
            result.rejected = true;
        } else if (!result.error) {
            result.identity_valid = true; result.generation = status.generation;
            result.epoch = before->epoch;
        }
    }
    if (start && !result.error && !(radio == CERBERUS_WATCH_RADIO_WIFI && store->c5_priority))
        live_capture_pending |= UINT32_C(1) << (radio == CERBERUS_WATCH_RADIO_BLE ? SENTINEL_SCAN_BLE : SENTINEL_SCAN_WIFI);
    return result;
}

static int32_t remote_id_watch_status(void *context, uint8_t radio, uint8_t generation,
    cerberus_watch_scan_status_payload_t *status)
{
    remote_id_live_store_t *store = context;
    memset(status, 0, sizeof(*status));
    if (radio == CERBERUS_WATCH_RADIO_WIFI && store->c5_priority) {
        cerberus_remote_id_status_payload_t job = {0};
        esp_err_t error = c5_spi_link_remote_id_status_for_peer(store->c5_job_id, &store->c5_peer, &job);
        if (error != ESP_OK) return error;
        if (job.state != CERBERUS_REMOTE_ID_STATE_COMPLETE || job.generation != generation ||
            job.error_code || !(job.flags & CERBERUS_REMOTE_ID_FLAG_DATA_READY) ||
            (job.flags & (CERBERUS_REMOTE_ID_FLAG_OWNED | CERBERUS_REMOTE_ID_FLAG_RECEIVING)))
            return CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED;
    }
    return radio == CERBERUS_WATCH_RADIO_BLE ?
        cerberus_s3_ble_scanner_get_watch_status(generation, status) :
        c5_spi_link_get_watch_status(radio, generation, status);
}
static int32_t remote_id_watch_row(void *context, uint8_t radio, uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *row)
{
    (void)context; memset(row, 0, sizeof(*row));
    return radio == CERBERUS_WATCH_RADIO_BLE ?
        cerberus_s3_ble_scanner_get_watch_result(generation, index, row) :
        c5_spi_link_get_watch_result(radio, generation, index, row);
}

static int32_t remote_id_watch_row_observed(void *context, uint8_t radio, uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *row, cerberus_watch_receive_timing_t *timing)
{
    (void)context;
    memset(row, 0, sizeof(*row));
    memset(timing, 0, sizeof(*timing));
    return radio == CERBERUS_WATCH_RADIO_BLE ?
        cerberus_s3_ble_scanner_get_watch_observed_result(generation, index, row, timing) :
        c5_spi_link_get_watch_observed_result(radio, generation, index, row, timing);
}

static int32_t remote_id_watch_row_detailed(void *context, uint8_t radio, uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *row, cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion, cerberus_watch_receive_timing_t *motion_timing)
{
    (void)context;
    memset(row, 0, sizeof(*row)); memset(timing, 0, sizeof(*timing));
    memset(motion, 0, sizeof(*motion)); memset(motion_timing, 0, sizeof(*motion_timing));
    return radio == CERBERUS_WATCH_RADIO_BLE ?
        cerberus_s3_ble_scanner_get_watch_detailed_result(generation, index, row, timing, motion, motion_timing) :
        c5_spi_link_get_watch_detailed_result(radio, generation, index, row, timing, motion, motion_timing);
}

static const char *remote_id_error_name(int32_t error)
{
    if (error == CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED) return "SOURCE CHANGED";
    if (error == CERBERUS_REMOTE_ID_ERROR_SNAPSHOT) return "SNAPSHOT INVALID";
    if (error == CERBERUS_REMOTE_ID_ERROR_TOKEN_EXHAUSTED) return "SESSION LIMIT";
    return esp_err_to_name(error);
}

static void remote_id_phase(unsigned slot)
{
    remote_id_live_store_t *store = remote_id_live;
    const cerberus_remote_id_radio_session_t *r = &store->session.radios[slot];
    const cerberus_remote_id_bank_t *bank = cerberus_remote_id_inventory_bank(&store->inventory,
        slot ? CERBERUS_WATCH_RADIO_BLE : CERBERUS_WATCH_RADIO_WIFI);
    const char *phase = !r->observed ? (store->session.running ? "WAITING FOR STATUS" : "READY") : !r->observation.supported ? "UNAVAILABLE" :
        r->owned ? (store->session.running ? "LISTENING" : "STOPPING") : r->capture_pending ? "READING SWEEP" :
        store->session.running && r->observation.busy ? "WAITING FOR RADIO" : store->session.running ? "NEXT SWEEP" : "STOPPED";
    if (r->error) snprintf(store->phases[slot], sizeof(store->phases[slot]), "%.33s", remote_id_error_name(r->error));
    else if (r->faulted || r->scan.faulted) snprintf(store->phases[slot], sizeof(store->phases[slot]), "RETRY EXHAUSTED");
    else if (bank->published && (bank->status.flags & CERBERUS_WATCH_CAPTURE_UNAVAILABLE))
        snprintf(store->phases[slot], sizeof(store->phases[slot]), "CAPTURE UNAVAILABLE");
    else if (!slot && store->c5_priority && store->c5_profile_valid && r->owned &&
        store->c5_profile.job_id == store->c5_job_id &&
        (store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_RECEIVING))
        snprintf(store->phases[slot], sizeof(store->phases[slot]), "%s CH%u / %lu",
            (store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND) ? "FOLLOW RX" :
            (store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED) ? "SEARCH RX" : "PRIORITY",
            store->c5_profile.channel, (unsigned long)r->published_sweeps);
    else snprintf(store->phases[slot], sizeof(store->phases[slot]), "%s / %lu", phase, (unsigned long)r->published_sweeps);
}

static void remote_id_source_label(const cerberus_remote_id_record_t *row, char *label, size_t length)
{
    if (row->radio == CERBERUS_WATCH_RADIO_BLE) {
        char address[24];
        cerberus_privacy_format_ble_identifier(row->observation.address, privacy_addresses_masked(), address, sizeof(address));
        snprintf(label, length, "%.17s / T%c", address, (char)('0' + row->observation.address_type));
    }
    else cerberus_privacy_format_wifi_identifier(row->observation.address, privacy_addresses_masked(), label, length);
}

static esp_err_t remote_id_draw(bool full)
{
    remote_id_live_store_t *store = remote_id_live;
    if (!store) {
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "remote clear");
        ESP_RETURN_ON_ERROR(draw_header("REMOTE ID"), TAG, "remote title");
        ESP_RETURN_ON_ERROR(draw_centered(170, esp_err_to_name(remote_id_open_error), UI_BAD, 1), TAG, "remote memory");
        return draw_button(64, 420, 192, 44, "BACK");
    }
    cerberus_remote_id_ui_view_t *view = &store->view;
    memset(view, 0, sizeof(*view));
    view->now_ms = remote_id_clock(store); view->detail = store->detail;
    view->motion_detail = store->detail && store->motion_detail;
    bool released = cerberus_remote_id_session_released(&store->session);
    bool fault = store->session.radios[0].faulted || store->session.radios[1].faulted ||
        store->session.radios[0].scan.faulted || store->session.radios[1].scan.faulted;
    view->state = !released && !store->session.running ? CERBERUS_REMOTE_ID_UI_STOPPING :
        fault ? CERBERUS_REMOTE_ID_UI_ERROR : store->session.running ? CERBERUS_REMOTE_ID_UI_RUNNING :
        (store->session.radios[0].published_sweeps || store->session.radios[1].published_sweeps) ?
        CERBERUS_REMOTE_ID_UI_STOPPED : CERBERUS_REMOTE_ID_UI_IDLE;
    view->start_available = !store->exit_pending && released && !store->session.running;
    view->stop_available = store->session.running || !released;
    view->back_available = !store->exit_pending;
    view->follow_enabled = store->follow_enabled;
    view->follow_available = !store->exit_pending && (store->follow_enabled ||
        ((released || store->session.running) && store->c5_seen &&
         store->c5_firmware >= CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE));
    if (!cerberus_remote_id_inventory_page(&store->inventory, store->offset, &view->page)) {
        store->offset = 0;
        (void)cerberus_remote_id_inventory_page(&store->inventory, 0, &view->page);
    }
    if (!view->page.copied && store->offset) {
        store->offset = 0; (void)cerberus_remote_id_inventory_page(&store->inventory, 0, &view->page);
    }
    for (unsigned i = 0; i < view->page.copied; ++i) {
        remote_id_source_label(&view->page.rows[i], store->identifiers[i], sizeof(store->identifiers[i]));
        view->source_labels[i] = store->identifiers[i];
    }
    view->selection = store->inventory.selected;
    if (view->selection.valid) {
        remote_id_source_label(&view->selection.frozen, store->identifiers[4], sizeof(store->identifiers[4]));
        view->selected_source_label = store->identifiers[4];
    }
    remote_id_phase(0); remote_id_phase(1);
    view->wifi_status = store->phases[0]; view->ble_status = store->phases[1];
    view->wifi_dropped_reports = store->inventory.banks[0].status.dropped_records;
    view->ble_dropped_reports = store->inventory.banks[1].status.dropped_records;
    store->message[0] = '\0';
    if (store->follow_enabled) {
        const cerberus_remote_id_record_t *selected = &store->inventory.selected.frozen;
        bool same_follow = store->inventory.selected.valid && selected->radio == CERBERUS_WATCH_RADIO_WIFI &&
            store->session.radios[0].owned && store->c5_profile.job_id == store->c5_job_id &&
            store->c5_follow_hint.transport == selected->observation.transport &&
            !memcmp(store->c5_follow_hint.transmitter, selected->observation.address, sizeof(store->c5_follow_hint.transmitter));
        const char *message = !store->session.running && !released ? "STOPPING FOLLOW / WAIT FOR RELEASE" :
            same_follow && store->c5_profile_valid &&
            (store->c5_profile.state == CERBERUS_REMOTE_ID_STATE_ERROR ||
             store->c5_profile.state == CERBERUS_REMOTE_ID_STATE_STOPPING) ? "FOLLOW CLEANUP / WAIT FOR RELEASE" :
            store->c5_firmware < CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE ? "FOLLOW REQUIRES C5 UPDATE" :
            !store->session.running ? "FOLLOW READY / START TO RESUME" :
            !store->c5_profile_valid ? "FOLLOW WAITING FOR RADIO STATUS" :
            !same_follow || !(store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED) ? "FOLLOW STARTS AT NEXT WI-FI WINDOW" :
            store->c5_profile.state == CERBERUS_REMOTE_ID_STATE_STARTING ? "CHECKING SELECTED SOURCE CHANNEL" :
            store->c5_profile.state != CERBERUS_REMOTE_ID_STATE_RUNNING ? "FOLLOW STARTS AT NEXT WI-FI WINDOW" :
            (store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND) ? "FOLLOWING WITH BACKGROUND COVERAGE" :
            "SEARCHING FOR SELECTED WI-FI SOURCE";
        snprintf(store->message, sizeof(store->message), "%s", message);
    } else if (store->c5_profile_valid && store->session.radios[0].owned &&
        (store->c5_profile.flags & CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED))
        snprintf(store->message, sizeof(store->message), "FINISHING CURRENT FOLLOW WINDOW");
    view->message = store->exit_pending ? "STOPPING BOTH RADIOS BEFORE EXIT" :
        store->message[0] ? store->message : NULL;
    return cerberus_remote_id_ui_update(&store->cache, view, full);
}

static void remote_id_open(void)
{
    current_page = UI_PAGE_REMOTE_ID;
    ui_capability_blocked = false; ui_capability_blocked_reason = NULL;
    if (!remote_id_live) {
        remote_id_live = heap_caps_calloc(1, sizeof(*remote_id_live), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (remote_id_live) {
            cerberus_remote_id_inventory_init(&remote_id_live->inventory);
            cerberus_remote_id_session_init(&remote_id_live->session);
        }
    }
    remote_id_open_error = remote_id_live ? ESP_OK : ESP_ERR_NO_MEM;
    remote_id_publish_diagnostics();
    ESP_ERROR_CHECK(remote_id_draw(true));
}

static bool remote_id_finish_exit(void)
{
    if (remote_id_live && !cerberus_remote_id_session_released(&remote_id_live->session)) return false;
    heap_caps_free(remote_id_live); remote_id_live = NULL;
    remote_id_publish_diagnostics();
    if (current_page == UI_PAGE_REMOTE_ID) {
        current_page = UI_PAGE_FIELD_MENU; field_menu.attack = false; field_menu.page = 0;
        ESP_ERROR_CHECK(draw_current_page());
    }
    return true;
}

static void remote_id_handle_tap(uint16_t x, uint16_t y)
{
    if (!remote_id_live) {
        if (x >= 64 && x < 256 && y >= 420 && y < 464) (void)remote_id_finish_exit();
        return;
    }
    remote_id_live_store_t *store = remote_id_live;
    cerberus_remote_id_ui_event_t event = cerberus_remote_id_ui_tap(&store->cache, x, y);
    switch (event.action) {
    case CERBERUS_REMOTE_ID_UI_ACTION_START:
        (void)cerberus_remote_id_session_start(&store->session); break;
    case CERBERUS_REMOTE_ID_UI_ACTION_STOP:
        cerberus_remote_id_session_stop(&store->session); break;
    case CERBERUS_REMOTE_ID_UI_ACTION_BACK:
        if (store->detail && store->motion_detail) {
            store->motion_detail = false;
        } else if (store->detail) {
            store->detail = false; store->follow_enabled = false;
            cerberus_remote_id_inventory_clear_selection(&store->inventory);
        } else {
            store->exit_pending = true; cerberus_remote_id_session_stop(&store->session);
            if (remote_id_finish_exit()) return;
        }
        break;
    case CERBERUS_REMOTE_ID_UI_ACTION_SELECT:
        if (cerberus_remote_id_inventory_select(&store->inventory, &event.selected)) {
            store->detail = true; store->motion_detail = false; store->follow_enabled = false;
        }
        break;
    case CERBERUS_REMOTE_ID_UI_ACTION_MOTION:
    case CERBERUS_REMOTE_ID_UI_ACTION_DETAILS:
        if (!store->detail || !store->inventory.selected.valid || store->exit_pending ||
            !cerberus_remote_id_source_equal(&event.selected, &store->inventory.selected.frozen)) return;
        store->motion_detail = event.action == CERBERUS_REMOTE_ID_UI_ACTION_MOTION;
        break;
    case CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW:
    case CERBERUS_REMOTE_ID_UI_ACTION_UNFOLLOW:
        if (!store->detail || !store->inventory.selected.valid || store->exit_pending ||
            (event.action == CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW &&
             (store->c5_firmware < CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE ||
              (!store->session.running && !cerberus_remote_id_session_released(&store->session)))) ||
            event.selected.radio != CERBERUS_WATCH_RADIO_WIFI ||
            !cerberus_remote_id_source_equal(&event.selected, &store->inventory.selected.frozen)) return;
        store->follow_enabled = event.action == CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW;
        if (store->follow_enabled && !store->session.running)
            (void)cerberus_remote_id_session_start(&store->session);
        break;
    case CERBERUS_REMOTE_ID_UI_ACTION_PREV: if (store->offset >= 4) store->offset -= 4; break;
    case CERBERUS_REMOTE_ID_UI_ACTION_NEXT: store->offset += 4; break;
    default: return;
    }
    store->last_tick = (uint32_t)remote_id_clock(store) - 500U;
    remote_id_publish_diagnostics();
    ESP_ERROR_CHECK(remote_id_draw(false));
}

static bool remote_id_keeps_awake(void)
{
    return remote_id_live && (remote_id_live->session.running ||
        !cerberus_remote_id_session_released(&remote_id_live->session));
}

static void remote_id_tick(uint32_t now, bool asleep)
{
    remote_id_live_store_t *store = remote_id_live;
    if (!store) return;
    if (current_page != UI_PAGE_REMOTE_ID && !store->exit_pending) {
        store->exit_pending = true; cerberus_remote_id_session_stop(&store->session);
    }
    if ((uint32_t)(now - store->last_tick) < 500U) return;
    store->last_tick = now;
    cerberus_remote_id_ops_t ops = {.context = store, .poll = remote_id_poll,
        .command = remote_id_command, .status = remote_id_watch_status, .row = remote_id_watch_row,
        .now_ms = remote_id_clock, .row_observed = remote_id_watch_row_observed,
        .row_detailed = remote_id_watch_row_detailed};
    cerberus_remote_id_session_tick(&store->session, &store->inventory, &ops);
    remote_id_publish_diagnostics();
    if (!store->c5_priority && store->session.radios[0].published_sweeps)
        live_capture_pending |= UINT32_C(1) << SENTINEL_SCAN_WIFI;
    if (store->session.radios[1].published_sweeps) live_capture_pending |= UINT32_C(1) << SENTINEL_SCAN_BLE;
    if (store->exit_pending && remote_id_finish_exit()) return;
    if (current_page == UI_PAGE_REMOTE_ID && !asleep) ESP_ERROR_CHECK(remote_id_draw(false));
}
