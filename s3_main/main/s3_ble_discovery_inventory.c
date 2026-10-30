#include "s3_ble_discovery_inventory.h"

#include <string.h>

static bool family_valid(cerberus_s3_ble_filter_t family)
{
    return (unsigned)family < CERBERUS_S3_BLE_FILTER_COUNT;
}

static bool key_equal(const cerberus_s3_ble_discovery_record_t *a,
    const cerberus_s3_ble_discovery_record_t *b)
{
    return a->peer.address_type == b->peer.address_type &&
        memcmp(a->peer.address, b->peer.address, sizeof(a->peer.address)) == 0;
}

/* Unique lift into a producer's bounded full-width clock interval. The copy
 * time is only an upper bound; it never replaces the original source time. */
static bool lift(uint64_t start, uint64_t end, uint32_t low, uint64_t *value)
{
    if (end < start || end - start > UINT32_MAX) return false;
    const uint32_t delta = (uint32_t)end - low;
    if (end < delta || end - delta < start) return false;
    if (value) *value = end - delta;
    return true;
}

static bool record_valid(const cerberus_s3_ble_discovery_record_t *r,
    const cerberus_s3_ble_discovery_meta_t *m, uint8_t index)
{
    const cerberus_s3_ble_discovery_evidence_t *d = &r->focus.discovery;
    if (r->index != index || r->generation != m->status.generation ||
        r->peer.address_type > 3U ||
        r->advertisement.address_type != r->peer.address_type ||
        memcmp(r->advertisement.address, r->peer.address, 6U) ||
        !memchr(r->peer.name, 0, sizeof(r->peer.name)) ||
        (r->peer.uuid_length != 0U && r->peer.uuid_length != 2U &&
         r->peer.uuid_length != 4U && r->peer.uuid_length != 16U) ||
        (r->peer.flags & ~CERBERUS_BLE_RESULT_CONNECTABLE) ||
        r->advertisement.length > sizeof(r->advertisement.data) ||
        (r->advertisement.flags & ~(CERBERUS_BLE_ADVERTISEMENT_TRUNCATED |
            CERBERUS_BLE_ADVERTISEMENT_MALFORMED)) ||
        (r->focus.category_bits & ~0x3fU) || (d->flags & ~0x3fU) ||
        d->address_type != r->peer.address_type || d->generation != r->generation ||
        memcmp(d->address, r->peer.address, 6U) ||
        !lift(m->started_ms, m->source_now_ms, r->observed_ms, NULL)) return false;

    /* Invalid/incomplete originals deliberately retire current hints. Their
     * old raw packet can remain intact, so do not equate these two clocks. */
    if (d->advertisement_sequence) {
        if (!lift(m->started_ms, m->source_now_ms, d->advertisement_observed_ms, NULL))
            return false;
    } else if (d->flags & (CERBERUS_S3_BLE_EVIDENCE_ADV_VALID |
            CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID)) return false;
    if ((d->flags & CERBERUS_S3_BLE_EVIDENCE_ADV_VALID) &&
        !(d->flags & CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE)) return false;
    if ((d->flags & CERBERUS_S3_BLE_EVIDENCE_SID_VALID) &&
        ((d->flags & CERBERUS_S3_BLE_EVIDENCE_SID_INVALID) || d->sid > 15U)) return false;
    if (d->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) {
        uint64_t original, response;
        if (!(d->flags & CERBERUS_S3_BLE_EVIDENCE_ADV_VALID) ||
            (d->flags & CERBERUS_S3_BLE_EVIDENCE_SID_INVALID) ||
            d->scan_response_sequence <= d->advertisement_sequence ||
            !lift(m->started_ms, m->source_now_ms, d->advertisement_observed_ms, &original) ||
            !lift(m->started_ms, m->source_now_ms, d->scan_response_observed_ms, &response) ||
            response < original || response - original > 2000U) return false;
    }
    if (r->focus.ibeacon_valid && (!r->focus.ibeacon_sequence ||
            !lift(m->started_ms, m->source_now_ms, r->focus.ibeacon_observed_ms, NULL)))
        return false;
    return true;
}

bool cerberus_s3_ble_discovery_bank_valid(const cerberus_s3_ble_discovery_bank_t *bank)
{
    if (!bank) return false;
    const cerberus_s3_ble_discovery_meta_t *m = &bank->meta;
    if (!m->scan_epoch || !m->revision || !m->attached || !family_valid(m->family) ||
        (m->status.state != CERBERUS_BLE_SCAN_RUNNING &&
         m->status.state != CERBERUS_BLE_SCAN_COMPLETE) ||
        m->status.stored_count > CERBERUS_BLE_SCAN_MAX_RESULTS ||
        m->status.unnamed_count > m->status.stored_count ||
        m->source_now_ms < m->started_ms ||
        m->source_now_ms - m->started_ms > UINT32_MAX) return false;
    for (uint8_t i = 0; i < m->status.stored_count; ++i) {
        if (!record_valid(&bank->records[i], m, i)) return false;
        for (uint8_t j = 0; j < i; ++j)
            if (key_equal(&bank->records[i], &bank->records[j])) return false;
    }
    return true;
}

bool cerberus_s3_ble_discovery_inventory_init(cerberus_s3_ble_discovery_inventory_t *inventory,
    cerberus_s3_ble_filter_t family)
{
    if (!inventory || !family_valid(family)) return false;
    memset(inventory, 0, sizeof(*inventory));
    inventory->family = family;
    inventory->initialized = true;
    return true;
}

static bool matches(const cerberus_s3_ble_discovery_record_t *record,
    cerberus_s3_ble_filter_t family)
{
    return family == CERBERUS_S3_BLE_FILTER_ALL ||
        (record->focus.category_bits & (1U << ((unsigned)family - 1U)));
}

static int find_entry(const cerberus_s3_ble_discovery_inventory_t *inventory,
    const cerberus_s3_ble_discovery_record_t *record)
{
    for (uint8_t i = 0; i < inventory->count; ++i)
        if (key_equal(&inventory->entries[i].record, record)) return i;
    return -1;
}

static void replace_entry(cerberus_s3_ble_discovery_inventory_t *inventory, uint8_t index,
    const cerberus_s3_ble_discovery_record_t *record,
    const cerberus_s3_ble_discovery_meta_t *meta)
{
    cerberus_s3_ble_discovery_entry_t *entry = &inventory->entries[index];
    entry->record = *record;
    entry->epoch = meta->scan_epoch;
    entry->source_now_ms = meta->source_now_ms;
    (void)lift(meta->started_ms, meta->source_now_ms, record->observed_ms,
        &entry->original_source_ms);
    inventory->current_mask |= UINT32_C(1) << index;
}

cerberus_s3_ble_discovery_apply_t cerberus_s3_ble_discovery_inventory_apply(
    cerberus_s3_ble_discovery_inventory_t *inventory,
    const cerberus_s3_ble_discovery_bank_t *bank)
{
    if (!inventory || !inventory->initialized || inventory->count > CERBERUS_BLE_SCAN_MAX_RESULTS ||
        !cerberus_s3_ble_discovery_bank_valid(bank) || inventory->family != bank->meta.family)
        return CERBERUS_BLE_INVENTORY_INVALID;
    const cerberus_s3_ble_discovery_meta_t *m = &bank->meta;
    if (m->scan_epoch < inventory->epoch || m->source_now_ms < inventory->source_now_ms ||
        (inventory->epoch && m->started_ms < inventory->started_ms))
        return CERBERUS_BLE_INVENTORY_INVALID;
    if (m->scan_epoch == inventory->epoch) {
        if (m->revision < inventory->revision || m->started_ms != inventory->started_ms ||
            m->status.generation != inventory->generation) return CERBERUS_BLE_INVENTORY_INVALID;
        if (m->revision == inventory->revision) return CERBERUS_BLE_INVENTORY_DUPLICATE;
    }
    /* Preflight all source regressions before mutating even one entry. */
    for (uint8_t i = 0; i < m->status.stored_count; ++i) {
        int previous = find_entry(inventory, &bank->records[i]);
        uint64_t original = 0;
        (void)lift(m->started_ms, m->source_now_ms, bank->records[i].observed_ms, &original);
        if (previous >= 0 && original < inventory->entries[previous].original_source_ms)
            return CERBERUS_BLE_INVENTORY_INVALID;
    }
    inventory->current_mask = 0;
    /* Removal precedes insertion, so a dequalified key cannot evict a useful
     * earlier match merely because of its position in the incoming bank. */
    for (uint8_t i = 0; i < m->status.stored_count; ++i) {
        if (matches(&bank->records[i], m->family)) continue;
        int previous = find_entry(inventory, &bank->records[i]);
        if (previous < 0) continue;
        const size_t remaining = inventory->count - (size_t)previous - 1U;
        memmove(&inventory->entries[previous], &inventory->entries[previous + 1],
            remaining * sizeof(inventory->entries[0]));
        --inventory->count;
        memset(&inventory->entries[inventory->count], 0, sizeof(inventory->entries[0]));
    }
    /* Existing current keys are protected before filling missing keys. With
     * at most 32 unique input rows there is always a noncurrent victim. */
    for (uint8_t i = 0; i < m->status.stored_count; ++i) {
        if (!matches(&bank->records[i], m->family)) continue;
        int previous = find_entry(inventory, &bank->records[i]);
        if (previous >= 0) replace_entry(inventory, (uint8_t)previous, &bank->records[i], m);
    }
    for (uint8_t i = 0; i < m->status.stored_count; ++i) {
        if (!matches(&bank->records[i], m->family) || find_entry(inventory, &bank->records[i]) >= 0)
            continue;
        uint8_t destination = inventory->count;
        if (destination == CERBERUS_BLE_SCAN_MAX_RESULTS) {
            destination = 0;
            while (inventory->current_mask & (UINT32_C(1) << destination)) ++destination;
            for (uint8_t j = (uint8_t)(destination + 1U); j < inventory->count; ++j)
                if (!(inventory->current_mask & (UINT32_C(1) << j)) &&
                    inventory->entries[j].original_source_ms < inventory->entries[destination].original_source_ms)
                    destination = j;
            if (inventory->replacements != UINT32_MAX) ++inventory->replacements;
        } else ++inventory->count;
        replace_entry(inventory, destination, &bank->records[i], m);
    }
    inventory->epoch = m->scan_epoch;
    inventory->revision = m->revision;
    inventory->started_ms = m->started_ms;
    inventory->source_now_ms = m->source_now_ms;
    inventory->generation = m->status.generation;
    return CERBERUS_BLE_INVENTORY_APPLIED;
}

bool cerberus_s3_ble_discovery_entry_age_ms(const cerberus_s3_ble_discovery_entry_t *entry,
    uint64_t now_ms, uint64_t *age_ms)
{
    if (age_ms) *age_ms = 0;
    if (!entry || !age_ms || !entry->epoch || now_ms < entry->source_now_ms ||
        entry->source_now_ms < entry->original_source_ms) return false;
    *age_ms = now_ms - entry->original_source_ms;
    return true;
}

bool cerberus_s3_ble_discovery_entry_recent(const cerberus_s3_ble_discovery_entry_t *entry,
    cerberus_s3_ble_filter_t family, uint64_t now_ms)
{
    uint64_t unused;
    if (!cerberus_s3_ble_recent_supported(family) ||
        !cerberus_s3_ble_discovery_entry_age_ms(entry, now_ms, &unused) ||
        now_ms - entry->source_now_ms > CERBERUS_S3_BLE_RECENT_WINDOW_MS) return false;
    /* Use the existing source-admission predicate, but remove any source that
     * has expired in full-width time. This also handles a very late first copy
     * near a complete 32-bit revolution, not just an old publication. */
    cerberus_s3_ble_focus_t focus = entry->record.focus;
    const uint64_t elapsed = now_ms - entry->source_now_ms;
    if ((uint64_t)((uint32_t)entry->source_now_ms - focus.ibeacon_observed_ms) + elapsed >
            CERBERUS_S3_BLE_RECENT_WINDOW_MS) focus.ibeacon_valid = false;
    if ((uint64_t)((uint32_t)entry->source_now_ms - focus.discovery.advertisement_observed_ms) + elapsed >
            CERBERUS_S3_BLE_RECENT_WINDOW_MS) focus.discovery.findmy_advertisement.bits = 0;
    if ((uint64_t)((uint32_t)entry->source_now_ms - focus.discovery.scan_response_observed_ms) + elapsed >
            CERBERUS_S3_BLE_RECENT_WINDOW_MS) focus.discovery.findmy_scan_response.bits = 0;
    return cerberus_s3_ble_focus_recent(&focus, family, (uint32_t)now_ms);
}

bool cerberus_s3_ble_discovery_inventory_current(
    const cerberus_s3_ble_discovery_inventory_t *inventory, uint8_t index)
{
    return inventory && inventory->initialized && inventory->count <= CERBERUS_BLE_SCAN_MAX_RESULTS &&
        index < inventory->count && (inventory->current_mask & (UINT32_C(1) << index));
}

bool cerberus_s3_ble_discovery_inventory_copy(const cerberus_s3_ble_discovery_inventory_t *inventory,
    uint8_t index, cerberus_s3_ble_discovery_entry_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    if (!out || !inventory || !inventory->initialized ||
        inventory->count > CERBERUS_BLE_SCAN_MAX_RESULTS || index >= inventory->count) return false;
    *out = inventory->entries[index];
    return true;
}

size_t cerberus_s3_ble_discovery_inventory_page_sorted(
    const cerberus_s3_ble_discovery_inventory_t *inventory, cerberus_s3_ble_sort_t sort,
    uint32_t visible_mask, size_t offset, uint8_t *indexes, size_t capacity)
{
    if (!inventory || !inventory->initialized || inventory->count > CERBERUS_BLE_SCAN_MAX_RESULTS ||
        (unsigned)sort >= CERBERUS_S3_BLE_SORT_COUNT || (capacity && !indexes)) return 0;
    uint8_t ordered[CERBERUS_BLE_SCAN_MAX_RESULTS];
    size_t matched = 0;
    for (uint8_t i = 0; i < inventory->count; ++i) {
        if (!(visible_mask & (UINT32_C(1) << i))) continue;
        size_t position = matched;
        while (position && cerberus_s3_ble_list_before(&inventory->entries[i].record.peer,
                &inventory->entries[ordered[position - 1U]].record.peer, sort)) {
            ordered[position] = ordered[position - 1U];
            --position;
        }
        ordered[position] = i;
        ++matched;
    }
    if (offset < matched) {
        size_t copied = matched - offset < capacity ? matched - offset : capacity;
        for (size_t i = 0; i < copied; ++i) indexes[i] = ordered[offset + i];
    }
    return matched;
}
