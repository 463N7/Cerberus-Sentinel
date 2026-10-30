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
        watch->minimum_confidence = SENTINEL_WATCH_CONFIDENCE_MEDIUM;
    }
}

static bool same_device(const sentinel_watch_device_policy_t *p, const sentinel_watch_entry_t *entry)
{
    return p->radio == entry->radio && p->address_type == entry->address_type &&
        memcmp(p->address, entry->address, 6) == 0;
}
static bool alert_allowed(const sentinel_watch_t *watch, const sentinel_watch_entry_t *entry, uint32_t now)
{
    if (watch->muted || entry->match.confidence < watch->minimum_confidence) return false;
    for (unsigned i = 0; i < watch->policy_count; ++i) {
        const sentinel_watch_device_policy_t *p = &watch->policies[i];
        if (same_device(p, entry) && (p->ignored ||
            (uint32_t)(now - p->snooze_since_ms) < p->snooze_duration_ms)) return false;
    }
    return true;
}
static void prune_alerts(sentinel_watch_t *watch, uint32_t now)
{
    uint8_t kept = 0;
    for (uint8_t i = 0; i < watch->queue_count; ++i) {
        uint8_t slot = (watch->queue_head + i) % SENTINEL_WATCH_QUEUE_CAPACITY;
        if (!alert_allowed(watch, &watch->queue[slot], now)) continue;
        watch->queue[(watch->queue_head + kept++) % SENTINEL_WATCH_QUEUE_CAPACITY] = watch->queue[slot];
    }
    watch->queue_count = kept;
    if (watch->alert_active && !alert_allowed(watch, &watch->alert, now)) watch->alert_active = false;
}
static sentinel_watch_device_policy_t *device_policy(sentinel_watch_t *watch,
    const sentinel_watch_entry_t *entry, uint32_t now)
{
    if (!watch || !entry || entry->radio == SENTINEL_WATCH_RADIO_ENVIRONMENT) return NULL;
    for (unsigned i = 0; i < watch->policy_count; ++i)
        if (same_device(&watch->policies[i], entry)) return &watch->policies[i];
    unsigned slot = watch->policy_count;
    for (unsigned i = 0; i < watch->policy_count; ++i)
        if (!watch->policies[i].ignored &&
            (uint32_t)(now - watch->policies[i].snooze_since_ms) >= watch->policies[i].snooze_duration_ms) { slot = i; break; }
    if (slot >= SENTINEL_WATCH_POLICY_CAPACITY) return NULL;
    if (slot == watch->policy_count) ++watch->policy_count;
    sentinel_watch_device_policy_t *p = &watch->policies[slot];
    memset(p, 0, sizeof(*p));
    p->radio = entry->radio; p->address_type = entry->address_type;
    memcpy(p->address, entry->address, 6);
    return p;
}
void sentinel_watch_set_alert_filter(sentinel_watch_t *watch, sentinel_watch_confidence_t minimum,
    bool muted, uint32_t now_ms)
{
    if (!watch || minimum < SENTINEL_WATCH_CONFIDENCE_LOW || minimum > SENTINEL_WATCH_CONFIDENCE_HIGH) return;
    watch->minimum_confidence = minimum; watch->muted = muted; prune_alerts(watch, now_ms);
}
bool sentinel_watch_is_ignored(const sentinel_watch_t *watch, const sentinel_watch_entry_t *entry)
{
    if (!watch || !entry) return false;
    for (unsigned i = 0; i < watch->policy_count; ++i)
        if (same_device(&watch->policies[i], entry)) return watch->policies[i].ignored;
    return false;
}
bool sentinel_watch_set_ignored(sentinel_watch_t *watch, const sentinel_watch_entry_t *entry,
    bool ignored, uint32_t now_ms)
{
    sentinel_watch_device_policy_t *p = device_policy(watch, entry, now_ms);
    if (!p) return false;
    p->ignored = ignored; prune_alerts(watch, now_ms); return true;
}
bool sentinel_watch_snooze(sentinel_watch_t *watch, const sentinel_watch_entry_t *entry,
    uint32_t duration_ms, uint32_t now_ms)
{
    if (duration_ms > 4U * 60U * 60U * 1000U) return false;
    sentinel_watch_device_policy_t *p = device_policy(watch, entry, now_ms);
    if (!p) return false;
    p->snooze_since_ms = now_ms; p->snooze_duration_ms = duration_ms;
    prune_alerts(watch, now_ms); return true;
}
void sentinel_watch_clear_device_policies(sentinel_watch_t *watch)
{
    if (watch) { watch->policy_count = 0; memset(watch->policies, 0, sizeof(watch->policies)); }
}
void sentinel_watch_clear_history(sentinel_watch_t *watch)
{
    if (!watch) return;
    memset(watch->history, 0, sizeof(watch->history)); memset(watch->queue, 0, sizeof(watch->queue));
    memset(&watch->alert, 0, sizeof(watch->alert));
    watch->history_count = watch->history_next = watch->queue_count = watch->queue_head = 0;
    watch->alert_active = false; watch->matches = 0;
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
    bool was_suppressed = entry && entry->alerts_suppressed;
    bool notify = entry == NULL || (uint32_t)(now_ms - entry->last_ms) >= SENTINEL_WATCH_RETURN_MS ||
        match->confidence > entry->match.confidence;
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
    entry->alerts_suppressed = !alert_allowed(watch, entry, now_ms);
    /* New corroboration or an expired snooze may become alertable even when
     * this address has remained in view. Existing category cooldown still applies. */
    notify |= was_suppressed && !entry->alerts_suppressed;
    if (!notify || mode != SENTINEL_WATCH_MODE_ALERT || entry->alerts_suppressed) return false;
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
