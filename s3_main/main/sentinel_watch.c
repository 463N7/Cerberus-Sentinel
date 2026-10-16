#include "sentinel_watch.h"
#include <limits.h>
#include <string.h>

static void increment(uint32_t *value) { if (*value < UINT32_MAX) ++*value; }
_Static_assert(SENTINEL_WATCH_CATEGORY_COUNT < 32, "category masks fit in 32 bits");

void sentinel_watch_init(sentinel_watch_t *watch)
{
    if (watch != NULL) {
        memset(watch, 0, sizeof(*watch));
        watch->enabled_mask = (1U << SENTINEL_WATCH_CATEGORY_COUNT) - 2U;
        watch->alert_mask = watch->enabled_mask & ~(1U << SENTINEL_WATCH_BEACON);
    }
}

bool sentinel_watch_observe(sentinel_watch_t *watch, sentinel_watch_radio_t radio,
    const uint8_t address[6], uint8_t address_type, const char *name, size_t name_length,
    int8_t rssi, const sentinel_watch_match_t *match, uint32_t now_ms)
{
    return sentinel_watch_observe_details(watch, radio, address, address_type, name,
        name_length, rssi, match, NULL, now_ms);
}

sentinel_watch_mode_t sentinel_watch_get_mode(const sentinel_watch_t *watch,
    sentinel_watch_category_t category)
{
    if (!watch || category <= SENTINEL_WATCH_NONE || category >= SENTINEL_WATCH_CATEGORY_COUNT)
        return SENTINEL_WATCH_MODE_OFF;
    uint32_t bit = 1U << category;
    return !(watch->enabled_mask & bit) ? SENTINEL_WATCH_MODE_OFF :
        (watch->alert_mask & bit) ? SENTINEL_WATCH_MODE_ALERT : SENTINEL_WATCH_MODE_HISTORY;
}

void sentinel_watch_set_mode(sentinel_watch_t *watch, sentinel_watch_category_t category,
    sentinel_watch_mode_t mode)
{
    if (!watch || category <= SENTINEL_WATCH_NONE || category >= SENTINEL_WATCH_CATEGORY_COUNT ||
        mode < SENTINEL_WATCH_MODE_OFF || mode > SENTINEL_WATCH_MODE_ALERT) return;
    uint32_t bit = 1U << category;
    watch->enabled_mask = (watch->enabled_mask & ~bit) | (mode != SENTINEL_WATCH_MODE_OFF ? bit : 0U);
    watch->alert_mask = (watch->alert_mask & ~bit) | (mode == SENTINEL_WATCH_MODE_ALERT ? bit : 0U);
    if (mode == SENTINEL_WATCH_MODE_ALERT) return;
    uint8_t kept = 0;
    for (uint8_t i = 0; i < watch->queue_count; ++i) {
        uint8_t slot = (watch->queue_head + i) % SENTINEL_WATCH_QUEUE_CAPACITY;
        if (watch->queue[slot].match.category == category) continue;
        watch->queue[(watch->queue_head + kept) % SENTINEL_WATCH_QUEUE_CAPACITY] = watch->queue[slot];
        ++kept;
    }
    watch->queue_count = kept;
    if (watch->alert_active && watch->alert.match.category == category) watch->alert_active = false;
}

bool sentinel_watch_observe_details(sentinel_watch_t *watch, sentinel_watch_radio_t radio,
    const uint8_t address[6], uint8_t address_type, const char *name, size_t name_length,
    int8_t rssi, const sentinel_watch_match_t *match, const sentinel_watch_details_t *details,
    uint32_t now_ms)
{
    if (watch == NULL || address == NULL || radio > SENTINEL_WATCH_RADIO_ENVIRONMENT ||
        radio < SENTINEL_WATCH_RADIO_WIFI || (name == NULL && name_length != 0)) return false;
    increment(&watch->observations);
    if (match == NULL || match->category <= SENTINEL_WATCH_NONE ||
        match->category >= SENTINEL_WATCH_CATEGORY_COUNT ||
        match->confidence <= SENTINEL_WATCH_CONFIDENCE_NONE ||
        match->confidence > SENTINEL_WATCH_CONFIDENCE_HIGH ||
        match->rule_id == NULL || match->label == NULL || match->evidence == NULL) return false;
    sentinel_watch_mode_t mode = sentinel_watch_get_mode(watch, match->category);
    if (mode == SENTINEL_WATCH_MODE_OFF) return false;

    sentinel_watch_entry_t *entry = NULL;
    for (size_t i = 0; i < watch->history_count; ++i) {
        sentinel_watch_entry_t *candidate = &watch->history[i];
        if (candidate->radio == radio && candidate->address_type == address_type &&
            candidate->match.category == match->category && memcmp(candidate->address, address, 6) == 0) {
            entry = candidate;
            break;
        }
    }
    bool notify = entry == NULL || (uint32_t)(now_ms - entry->last_ms) >= SENTINEL_WATCH_RETURN_MS;
    if (entry == NULL) {
        entry = &watch->history[watch->history_next];
        watch->history_next = (watch->history_next + 1U) % SENTINEL_WATCH_HISTORY_CAPACITY;
        if (watch->history_count < SENTINEL_WATCH_HISTORY_CAPACITY) ++watch->history_count;
        memset(entry, 0, sizeof(*entry));
        entry->radio = radio;
        entry->address_type = address_type;
        memcpy(entry->address, address, sizeof(entry->address));
        entry->first_ms = now_ms;
        increment(&watch->matches);
    }
    entry->match = *match;
    if (details) entry->details = *details;
    else memset(&entry->details, 0, sizeof(entry->details));
    entry->details.protocol.id[SENTINEL_WATCH_PROTOCOL_ID_MAX] = 0;
    entry->last_ms = now_ms;
    entry->rssi = rssi;
    if (entry->sightings < UINT16_MAX) ++entry->sightings;
    size_t length = name_length < sizeof(entry->name) - 1U ? name_length : sizeof(entry->name) - 1U;
    for (size_t i = 0; i < length; ++i)
        entry->name[i] = (unsigned char)name[i] >= 32 && (unsigned char)name[i] <= 126 ? name[i] : '?';
    entry->name[length] = 0;
    if (!notify || mode != SENTINEL_WATCH_MODE_ALERT) return false;
    uint32_t bit = 1U << match->category;
    if ((watch->category_alert_mask & bit) &&
        (uint32_t)(now_ms - watch->category_alert_ms[match->category]) < SENTINEL_WATCH_COOLDOWN_MS) {
        increment(&watch->suppressed);
        return false;
    }
    if (watch->queue_count == SENTINEL_WATCH_QUEUE_CAPACITY) {
        increment(&watch->queue_dropped);
        return false;
    }
    uint8_t slot = (watch->queue_head + watch->queue_count) % SENTINEL_WATCH_QUEUE_CAPACITY;
    watch->queue[slot] = *entry;
    ++watch->queue_count;
    watch->category_alert_ms[match->category] = now_ms;
    watch->category_alert_mask |= bit;
    return true;
}

bool sentinel_watch_tick(sentinel_watch_t *watch, uint32_t now_ms)
{
    if (watch == NULL) return false;
    bool changed = false;
    if (watch->alert_active && (uint32_t)(now_ms - watch->alert_since_ms) >= SENTINEL_WATCH_ALERT_MS) {
        watch->alert_active = false;
        changed = true;
    }
    if (!watch->alert_active && watch->queue_count) {
        watch->alert = watch->queue[watch->queue_head];
        watch->queue_head = (watch->queue_head + 1U) % SENTINEL_WATCH_QUEUE_CAPACITY;
        --watch->queue_count;
        watch->alert_since_ms = now_ms;
        watch->alert_active = true;
        changed = true;
    }
    return changed;
}

void sentinel_watch_dismiss(sentinel_watch_t *watch)
{
    if (watch != NULL) watch->alert_active = false;
}

const sentinel_watch_entry_t *sentinel_watch_history(const sentinel_watch_t *watch, size_t newest_offset)
{
    if (watch == NULL || newest_offset >= watch->history_count) return NULL;
    return &watch->history[(watch->history_next + SENTINEL_WATCH_HISTORY_CAPACITY - 1U - newest_offset) %
        SENTINEL_WATCH_HISTORY_CAPACITY];
}
