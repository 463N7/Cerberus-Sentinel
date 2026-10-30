#include "s3_wifi_relation.h"
#include "cerberus_protocol.h"

static bool same_peer(const cerberus_s3_wifi_relation_meta_t *a,
    const cerberus_s3_wifi_relation_meta_t *b)
{
    return a->firmware_version == b->firmware_version && a->capabilities == b->capabilities &&
        a->protocol_version == b->protocol_version && a->reset_reason == b->reset_reason &&
        a->boot_count == b->boot_count;
}

bool cerberus_s3_wifi_relation_meta_valid(const cerberus_s3_wifi_relation_meta_t *m)
{
    if (!m || !cerberus_wifi_relation_status_valid(&m->status) ||
        m->status.state != CERBERUS_WIFI_RELATION_COMPLETE ||
        m->firmware_version < CERBERUS_WIFI_RELATION_MIN_FIRMWARE ||
        m->protocol_version != CERBERUS_PROTOCOL_VERSION || !(m->capabilities &
            (CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ))) return false;
    if (!(m->status.flags & CERBERUS_WIFI_RELATION_AVAILABLE))
        return !m->clock_valid && !m->source_now_us && !m->request_started_us;
    return m->clock_valid && m->source_now_us <= INT64_MAX && m->request_started_us <= INT64_MAX;
}

static bool same_key(const cerberus_wifi_relation_row_t *a, const cerberus_wifi_relation_row_t *b)
{
    return a->channel == b->channel && !memcmp(a->bssid, b->bssid, 6U) &&
        !memcmp(a->station, b->station, 6U);
}

cerberus_s3_wifi_relation_apply_t cerberus_s3_wifi_relation_apply(
    cerberus_s3_wifi_relation_bank_t *bank, uint64_t publication,
    const cerberus_s3_wifi_relation_meta_t *meta, const cerberus_wifi_relation_row_t *rows,
    size_t count, uint64_t receipt_us)
{
    if (!bank || !publication || !cerberus_s3_wifi_relation_meta_valid(meta) ||
        count != meta->status.count || count > CERBERUS_WIFI_RELATION_LIMIT || (!rows && count) ||
        receipt_us > INT64_MAX || (meta->clock_valid && receipt_us < meta->request_started_us) ||
        publication < bank->ap_publication ||
        (publication == bank->ap_publication && bank->generation != meta->status.generation))
        return CERBERUS_S3_WIFI_RELATION_REJECTED;
    uint32_t frames = meta->status.dropped_frames;
    for (size_t i = 0U; i < count; ++i) {
        const cerberus_wifi_relation_row_t *row = &rows[i];
        if (!cerberus_wifi_relation_row_valid(row) ||
            cerberus_wifi_relation_latest_us(row) > meta->source_now_us ||
            !(meta->capabilities & (row->channel <= 14U ?
                CERBERUS_C5_CAP_WIFI_2_4_GHZ : CERBERUS_C5_CAP_WIFI_5_GHZ)))
            return CERBERUS_S3_WIFI_RELATION_REJECTED;
        for (size_t j = 0U; j < i; ++j)
            if (same_key(row, &rows[j])) return CERBERUS_S3_WIFI_RELATION_REJECTED;
        frames = cerberus_wifi_relation_add(frames,
            cerberus_wifi_relation_add(row->to_ap_frames, row->from_ap_frames));
    }
    if (frames != meta->status.matched_frames) return CERBERUS_S3_WIFI_RELATION_REJECTED;
    if (publication == bank->ap_publication &&
        (bank->outcome == CERBERUS_S3_WIFI_RELATION_READY ||
         bank->outcome == CERBERUS_S3_WIFI_RELATION_UNAVAILABLE)) {
        if (!same_peer(meta, &bank->meta) ||
            memcmp(&meta->status, &bank->meta.status, sizeof(meta->status)) ||
            meta->clock_valid != bank->meta.clock_valid ||
            meta->source_now_us < bank->meta.source_now_us ||
            (count && memcmp(rows, bank->rows, count * sizeof(*rows))))
            return CERBERUS_S3_WIFI_RELATION_REJECTED;
        return CERBERUS_S3_WIFI_RELATION_DUPLICATE;
    }
    /* Input validation is complete before touching the retained bank. */
    cerberus_s3_wifi_relation_meta_t copy = *meta;
    if (count) memmove(bank->rows, rows, count * sizeof(*rows));
    memset(bank->rows + count, 0, (CERBERUS_WIFI_RELATION_LIMIT - count) * sizeof(bank->rows[0]));
    bank->meta = copy;
    bank->ap_publication = publication;
    bank->receipt_us = receipt_us;
    bank->generation = meta->status.generation;
    bank->error = 0;
    bank->outcome = (meta->status.flags & CERBERUS_WIFI_RELATION_AVAILABLE) ?
        CERBERUS_S3_WIFI_RELATION_READY : CERBERUS_S3_WIFI_RELATION_UNAVAILABLE;
    return CERBERUS_S3_WIFI_RELATION_APPLIED;
}

bool cerberus_s3_wifi_relation_set_outcome(cerberus_s3_wifi_relation_bank_t *bank,
    uint64_t publication, uint8_t generation, cerberus_s3_wifi_relation_outcome_t outcome,
    int32_t error)
{
    if (!bank || !publication || publication < bank->ap_publication ||
        outcome < CERBERUS_S3_WIFI_RELATION_UNKNOWN || outcome > CERBERUS_S3_WIFI_RELATION_CANCELLED ||
        outcome == CERBERUS_S3_WIFI_RELATION_READY ||
        (outcome == CERBERUS_S3_WIFI_RELATION_FAILED && !error)) return false;
    memset(bank, 0, sizeof(*bank));
    bank->ap_publication = publication;
    bank->generation = generation;
    bank->outcome = outcome;
    bank->error = error;
    return true;
}

bool cerberus_s3_wifi_relation_select(const cerberus_s3_wifi_relation_bank_t *bank,
    uint64_t publication, const uint8_t bssid[6], uint8_t channel,
    cerberus_s3_wifi_relation_selection_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!publication || !cerberus_wifi_relation_mac_valid(bssid) ||
        !cerberus_wifi_relation_channel_valid(channel)) return false;
    memcpy(out->bssid, bssid, 6U);
    out->channel = channel;
    out->valid = true;
    out->bank.ap_publication = publication;
    if (!bank || publication != bank->ap_publication) return true;
    out->bank.meta = bank->meta;
    out->bank.receipt_us = bank->receipt_us;
    out->bank.error = bank->error;
    out->bank.outcome = bank->outcome;
    out->bank.generation = bank->generation;
    if (bank->outcome != CERBERUS_S3_WIFI_RELATION_READY) return true;
    if (!cerberus_s3_wifi_relation_meta_valid(&bank->meta)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    for (size_t i = 0U; i < bank->meta.status.count; ++i) {
        const cerberus_wifi_relation_row_t *row = &bank->rows[i];
        if (row->channel == channel && !memcmp(row->bssid, bssid, 6U))
            out->bank.rows[out->count++] = *row;
    }
    return true;
}

bool cerberus_s3_wifi_relation_age_ms(const cerberus_s3_wifi_relation_selection_t *selection,
    size_t index, uint8_t direction, uint64_t now_us, uint64_t *age_ms)
{
    if (!age_ms) return false;
    *age_ms = 0U;
    if (!selection || !selection->valid || selection->bank.outcome != CERBERUS_S3_WIFI_RELATION_READY ||
        index >= selection->count || (direction != CERBERUS_WIFI_RELATION_TO_AP &&
        direction != CERBERUS_WIFI_RELATION_FROM_AP) ||
        !cerberus_s3_wifi_relation_meta_valid(&selection->bank.meta)) return false;
    const cerberus_s3_wifi_relation_meta_t *meta = &selection->bank.meta;
    const cerberus_wifi_relation_row_t *row = &selection->bank.rows[index];
    if (!cerberus_wifi_relation_row_valid(row) || !(row->directions & direction) ||
        now_us < meta->request_started_us) return false;
    uint64_t source = direction == CERBERUS_WIFI_RELATION_TO_AP ?
        row->to_ap_callback_us : row->from_ap_callback_us;
    if (source > meta->source_now_us) return false;
    uint64_t source_age = meta->source_now_us - source;
    uint64_t elapsed = now_us - meta->request_started_us;
    if (elapsed > UINT64_MAX - source_age) return false;
    uint64_t age = source_age + elapsed;
    *age_ms = age / 1000U + (age % 1000U != 0U);
    return true;
}
