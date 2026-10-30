#include "remote_id_inventory.h"
#include <limits.h>
#include <string.h>

static int bank_index(uint8_t radio)
{
    return radio == CERBERUS_WATCH_RADIO_WIFI ? 0 :
        radio == CERBERUS_WATCH_RADIO_BLE ? 1 : -1;
}

static bool status_valid(const cerberus_watch_scan_status_payload_t *status)
{
    if (!status || bank_index(status->radio) < 0 || status->state != CERBERUS_WATCH_COMPLETE ||
        status->stored_count > CERBERUS_WATCH_MAX_OBSERVATIONS ||
        (status->flags & ~(CERBERUS_WATCH_CAPTURE_UNAVAILABLE | CERBERUS_WATCH_CAPTURE_OVERFLOW)) ||
        (uint64_t)status->stored_count + status->dropped_records > status->reports) return false;
    bool overflow = (status->flags & CERBERUS_WATCH_CAPTURE_OVERFLOW) != 0U;
    if (overflow != (status->dropped_records != 0U) ||
        (overflow && status->stored_count != CERBERUS_WATCH_MAX_OBSERVATIONS)) return false;
    if ((status->flags & CERBERUS_WATCH_CAPTURE_UNAVAILABLE) &&
        (status->stored_count || status->reports || status->dropped_records || overflow)) return false;
    return true;
}

static bool same_observation_source(const cerberus_watch_observation_t *a,
    const cerberus_watch_observation_t *b)
{
    return a->transport == b->transport && a->address_type == b->address_type &&
        !memcmp(a->address, b->address, sizeof(a->address));
}

static bool row_valid(uint8_t radio, const cerberus_watch_observation_t *row)
{
    if (!row || row->address_type > 3U || (row->flags & ~7U) ||
        (row->kind != SENTINEL_WATCH_PROTOCOL_DRONE &&
         row->kind != SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI)) return false;
    if (radio == CERBERUS_WATCH_RADIO_BLE) {
        if (row->kind != SENTINEL_WATCH_PROTOCOL_DRONE ||
            row->transport != SENTINEL_WATCH_TRANSPORT_BLE || row->channel) return false;
    } else if (radio == CERBERUS_WATCH_RADIO_WIFI) {
        bool any_address = false;
        for (size_t i = 0U; i < sizeof(row->address); ++i) any_address |= row->address[i] != 0U;
        if (!any_address || (row->address[0] & 1U) || row->address_type || !row->channel ||
            (row->transport != SENTINEL_WATCH_TRANSPORT_WIFI_BEACON &&
             row->transport != SENTINEL_WATCH_TRANSPORT_WIFI_NAN)) return false;
    } else return false;
    if (row->kind == SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI &&
        (row->transport != SENTINEL_WATCH_TRANSPORT_WIFI_BEACON ||
         row->flags != SENTINEL_WATCH_PROTOCOL_HAS_ID)) return false;
    bool terminated = false;
    for (size_t i = 0U; i < sizeof(row->id); ++i) {
        uint8_t c = (uint8_t)row->id[i];
        if (!c) terminated = true;
        else if (terminated || c < 32U || c > 126U) return false;
    }
    if (!terminated || ((row->flags & SENTINEL_WATCH_PROTOCOL_HAS_ID) ?
        row->id[0] == '\0' : row->id[0] != '\0')) return false;
    if (row->flags & SENTINEL_WATCH_PROTOCOL_HAS_POSITION) {
        if (row->latitude_e7 < -900000000 || row->latitude_e7 > 900000000 ||
            row->longitude_e7 < -1800000000 || row->longitude_e7 > 1800000000) return false;
    } else if (row->latitude_e7 || row->longitude_e7) return false;
    if (row->flags & SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE) {
        if (row->altitude_dm <= -10000 || row->altitude_dm > 317675 ||
            (row->altitude_kind != SENTINEL_WATCH_ALTITUDE_GEODETIC &&
             row->altitude_kind != SENTINEL_WATCH_ALTITUDE_PRESSURE)) return false;
    } else if (row->altitude_dm || row->altitude_kind) return false;
    return true;
}

static bool timing_valid(const cerberus_watch_receive_timing_t *timing, uint64_t receipt_ms)
{
    return !timing || cerberus_watch_receive_timing_valid(timing, receipt_ms);
}

static bool motion_valid(const cerberus_watch_observation_t *row,
    const sentinel_watch_motion_t *motion, const cerberus_watch_receive_timing_t *timing,
    uint64_t receipt_ms)
{
    if (!motion && !timing) return true;
    if (!motion || !timing || !sentinel_watch_motion_valid(motion) ||
        !cerberus_watch_receive_timing_valid(timing, receipt_ms)) return false;
    bool present = (motion->flags & SENTINEL_WATCH_MOTION_LOCATION_PRESENT) != 0U;
    if ((present && row->kind != SENTINEL_WATCH_PROTOCOL_DRONE) || (!present && timing->valid)) return false;
    return true;
}

static void copy_timing(cerberus_watch_receive_timing_t *out,
    const cerberus_watch_receive_timing_t *timing)
{
    /* Copy through a local value so in-place compaction is safe and padding is
     * canonical even when the caller supplied an independently initialized row. */
    cerberus_watch_receive_timing_t value = {0};
    if (timing) {
        value.request_started_us = timing->request_started_us;
        value.source_age_ms = timing->source_age_ms;
        value.valid = timing->valid;
    }
    *out = value;
}

static void make_record(const cerberus_remote_id_bank_t *bank, uint8_t index,
    cerberus_remote_id_record_t *record)
{
    memset(record, 0, sizeof(*record));
    record->sweep_token = bank->sweep_token;
    record->receipt_ms = bank->receipt_ms;
    record->radio = bank->status.radio;
    record->observation = bank->rows[index];
    copy_timing(&record->timing, &bank->timings[index]);
    record->motion = bank->motions[index];
    copy_timing(&record->motion_timing, &bank->motion_timings[index]);
}

static void refresh_selection(cerberus_remote_id_inventory_t *inventory,
    const cerberus_remote_id_bank_t *bank)
{
    cerberus_remote_id_selection_t *selection = &inventory->selected;
    if (!selection->valid || selection->frozen.radio != bank->status.radio) return;
    selection->checked_sweep_token = bank->sweep_token;
    selection->checked_receipt_ms = bank->receipt_ms;
    selection->radio_unavailable = (bank->status.flags & CERBERUS_WATCH_CAPTURE_UNAVAILABLE) != 0U;
    selection->present = false;
    for (uint8_t i = 0U; i < bank->count; ++i) {
        if (!same_observation_source(&selection->frozen.observation, &bank->rows[i])) continue;
        if (selection->latest.sweep_token != bank->sweep_token &&
            selection->matching_sweeps != UINT32_MAX) ++selection->matching_sweeps;
        make_record(bank, i, &selection->latest);
        selection->present = true;
        break;
    }
}

void cerberus_remote_id_inventory_init(cerberus_remote_id_inventory_t *inventory)
{
    if (inventory) memset(inventory, 0, sizeof(*inventory));
}

const cerberus_remote_id_bank_t *cerberus_remote_id_inventory_bank(
    const cerberus_remote_id_inventory_t *inventory, uint8_t radio)
{
    int index = bank_index(radio);
    return inventory && index >= 0 ? &inventory->banks[index] : NULL;
}

cerberus_remote_id_publish_result_t cerberus_remote_id_inventory_publish(
    cerberus_remote_id_inventory_t *inventory, uint64_t sweep_token,
    const cerberus_watch_scan_status_payload_t *before,
    const cerberus_watch_scan_status_payload_t *after,
    const cerberus_watch_observation_t *rows, size_t count, uint64_t receipt_ms)
{
    return cerberus_remote_id_inventory_publish_observed(inventory, sweep_token,
        before, after, rows, NULL, count, receipt_ms);
}

cerberus_remote_id_publish_result_t cerberus_remote_id_inventory_publish_observed(
    cerberus_remote_id_inventory_t *inventory, uint64_t sweep_token,
    const cerberus_watch_scan_status_payload_t *before,
    const cerberus_watch_scan_status_payload_t *after,
    const cerberus_watch_observation_t *rows,
    const cerberus_watch_receive_timing_t *timings, size_t count, uint64_t receipt_ms)
{
    return cerberus_remote_id_inventory_publish_detailed(inventory, sweep_token,
        before, after, rows, timings, NULL, NULL, count, receipt_ms);
}

cerberus_remote_id_publish_result_t cerberus_remote_id_inventory_publish_detailed(
    cerberus_remote_id_inventory_t *inventory, uint64_t sweep_token,
    const cerberus_watch_scan_status_payload_t *before,
    const cerberus_watch_scan_status_payload_t *after,
    const cerberus_watch_observation_t *rows,
    const cerberus_watch_receive_timing_t *timings,
    const sentinel_watch_motion_t *motions,
    const cerberus_watch_receive_timing_t *motion_timings, size_t count, uint64_t receipt_ms)
{
    if (!inventory || !sweep_token || !status_valid(before) || !after ||
        memcmp(before, after, sizeof(*before)) || count != before->stored_count ||
        (count && !rows) || ((motions == NULL) != (motion_timings == NULL))) return CERBERUS_REMOTE_ID_INVALID;
    uint8_t filtered = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (!row_valid(before->radio, &rows[i]) ||
            !timing_valid(timings ? &timings[i] : NULL, receipt_ms) ||
            !motion_valid(&rows[i], motions ? &motions[i] : NULL,
                motion_timings ? &motion_timings[i] : NULL, receipt_ms)) return CERBERUS_REMOTE_ID_INVALID;
        for (size_t j = 0U; j < i; ++j) {
            if (rows[i].kind == rows[j].kind && same_observation_source(&rows[i], &rows[j]))
                return CERBERUS_REMOTE_ID_INVALID;
        }
        if (rows[i].kind == SENTINEL_WATCH_PROTOCOL_DRONE) ++filtered;
    }
    cerberus_remote_id_bank_t *bank = &inventory->banks[bank_index(before->radio)];
    if (bank->published) {
        if (sweep_token < bank->sweep_token || receipt_ms < bank->receipt_ms)
            return CERBERUS_REMOTE_ID_STALE;
        if (sweep_token == bank->sweep_token) {
            if (memcmp(before, &bank->status, sizeof(*before)) || filtered != bank->count)
                return CERBERUS_REMOTE_ID_INVALID;
            uint8_t index = 0U;
            for (size_t i = 0U; i < count; ++i) {
                if (rows[i].kind == SENTINEL_WATCH_PROTOCOL_DRONE) {
                    if (memcmp(&rows[i], &bank->rows[index], sizeof(rows[i])) ||
                        (motions && memcmp(&motions[i], &bank->motions[index], sizeof(motions[i]))))
                        return CERBERUS_REMOTE_ID_INVALID;
                    ++index;
                }
            }
            return CERBERUS_REMOTE_ID_DUPLICATE;
        }
    }
    /* Validation is complete. Forward compaction also permits rows==bank->rows;
     * callers normally use the separate reusable staging array. */
    uint8_t index = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (rows[i].kind == SENTINEL_WATCH_PROTOCOL_DRONE) {
            bank->rows[index] = rows[i];
            copy_timing(&bank->timings[index], timings ? &timings[i] : NULL);
            bank->motions[index] = motions ? motions[i] : (sentinel_watch_motion_t){0};
            copy_timing(&bank->motion_timings[index], motion_timings ? &motion_timings[i] : NULL);
            ++index;
        }
    }
    memset(bank->rows + index, 0, (CERBERUS_WATCH_MAX_OBSERVATIONS - index) * sizeof(bank->rows[0]));
    memset(bank->timings + index, 0, (CERBERUS_WATCH_MAX_OBSERVATIONS - index) * sizeof(bank->timings[0]));
    memset(bank->motions + index, 0, (CERBERUS_WATCH_MAX_OBSERVATIONS - index) * sizeof(bank->motions[0]));
    memset(bank->motion_timings + index, 0, (CERBERUS_WATCH_MAX_OBSERVATIONS - index) * sizeof(bank->motion_timings[0]));
    bank->status = *before;
    bank->sweep_token = sweep_token;
    bank->receipt_ms = receipt_ms;
    bank->count = filtered;
    bank->published = true;
    refresh_selection(inventory, bank);
    return CERBERUS_REMOTE_ID_PUBLISHED;
}

bool cerberus_remote_id_inventory_page(const cerberus_remote_id_inventory_t *inventory,
    size_t offset, cerberus_remote_id_page_t *page)
{
    if (!page) return false;
    memset(page, 0, sizeof(*page));
    if (!inventory) return false;
    page->offset = offset;
    for (size_t b = 0U; b < CERBERUS_REMOTE_ID_RADIOS; ++b)
        if (inventory->banks[b].published) page->total += inventory->banks[b].count;
    size_t original = 0U;
    for (size_t b = 0U; b < CERBERUS_REMOTE_ID_RADIOS; ++b) {
        const cerberus_remote_id_bank_t *bank = &inventory->banks[b];
        if (!bank->published) continue;
        for (uint8_t i = 0U; i < bank->count; ++i, ++original) {
            if (original < offset || page->copied == CERBERUS_REMOTE_ID_PAGE_ROWS) continue;
            make_record(bank, i, &page->rows[page->copied++]);
        }
    }
    return true;
}

bool cerberus_remote_id_source_equal(const cerberus_remote_id_record_t *a,
    const cerberus_remote_id_record_t *b)
{
    return a && b && a->radio == b->radio &&
        same_observation_source(&a->observation, &b->observation);
}

bool cerberus_remote_id_inventory_select(cerberus_remote_id_inventory_t *inventory,
    const cerberus_remote_id_record_t *painted)
{
    if (!inventory || !painted || !painted->sweep_token ||
        painted->observation.kind != SENTINEL_WATCH_PROTOCOL_DRONE ||
        !row_valid(painted->radio, &painted->observation) ||
        !timing_valid(&painted->timing, painted->receipt_ms) ||
        !motion_valid(&painted->observation, &painted->motion, &painted->motion_timing, painted->receipt_ms)) return false;
    const cerberus_remote_id_bank_t *bank = cerberus_remote_id_inventory_bank(inventory, painted->radio);
    if (!bank || !bank->published || painted->sweep_token > bank->sweep_token ||
        painted->receipt_ms > bank->receipt_ms) return false;
    if (painted->sweep_token == bank->sweep_token) {
        bool found = false;
        for (uint8_t i = 0U; i < bank->count; ++i)
            if (!memcmp(&painted->observation, &bank->rows[i], sizeof(painted->observation)) &&
                cerberus_watch_receive_timing_equal(&painted->timing, &bank->timings[i]) &&
                !memcmp(&painted->motion, &bank->motions[i], sizeof(painted->motion)) &&
                cerberus_watch_receive_timing_equal(&painted->motion_timing, &bank->motion_timings[i])) found = true;
        if (!found || painted->receipt_ms != bank->receipt_ms) return false;
    }
    cerberus_remote_id_record_t frozen = *painted; /* May alias the previous selection. */
    memset(&inventory->selected, 0, sizeof(inventory->selected));
    inventory->selected.frozen = inventory->selected.latest = frozen;
    inventory->selected.matching_sweeps = 1U;
    inventory->selected.valid = true;
    refresh_selection(inventory, bank);
    return true;
}

void cerberus_remote_id_inventory_clear_selection(cerberus_remote_id_inventory_t *inventory)
{
    if (inventory) memset(&inventory->selected, 0, sizeof(inventory->selected));
}

bool cerberus_remote_id_inventory_follow_hint(const cerberus_remote_id_inventory_t *inventory,
    uint64_t now_us, cerberus_remote_id_follow_hint_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!inventory) return false;
    const cerberus_remote_id_selection_t *selection = &inventory->selected;
    const cerberus_remote_id_record_t *frozen = &selection->frozen;
    if (!selection->valid || !frozen->sweep_token || frozen->radio != CERBERUS_WATCH_RADIO_WIFI ||
        frozen->observation.kind != SENTINEL_WATCH_PROTOCOL_DRONE ||
        !row_valid(frozen->radio, &frozen->observation)) return false;
    memcpy(out->transmitter, frozen->observation.address, sizeof(out->transmitter));
    out->transport = frozen->observation.transport;
    const cerberus_remote_id_record_t *latest = &selection->latest;
    uint64_t age_ms;
    uint8_t channel = latest->observation.channel;
    bool channel_valid = (channel >= 1U && channel <= 14U) ||
        (channel >= 36U && channel <= 64U && channel % 4U == 0U) ||
        (channel >= 100U && channel <= 144U && channel % 4U == 0U) ||
        (channel >= 149U && channel <= 177U && (channel - 149U) % 4U == 0U);
    if (selection->present && !selection->radio_unavailable && latest->sweep_token && channel_valid &&
        cerberus_remote_id_source_equal(frozen, latest) &&
        row_valid(latest->radio, &latest->observation) &&
        cerberus_watch_receive_age_ms(&latest->timing, now_us, &age_ms) &&
        age_ms < CERBERUS_REMOTE_ID_FOLLOW_MAX_AGE_MS)
        out->channel = channel;
    return true;
}
