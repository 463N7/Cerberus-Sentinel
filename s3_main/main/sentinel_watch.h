#pragma once

#include "sentinel_watch_detect.h"
#include "sentinel_watch_activity.h"
#include "sentinel_watch_protocol.h"

#define SENTINEL_WATCH_HISTORY_CAPACITY 32U
#define SENTINEL_WATCH_QUEUE_CAPACITY 8U
#define SENTINEL_WATCH_RETURN_MS 300000U
#define SENTINEL_WATCH_COOLDOWN_MS 20000U
#define SENTINEL_WATCH_ALERT_MS 8000U

typedef enum { SENTINEL_WATCH_RADIO_WIFI, SENTINEL_WATCH_RADIO_BLE,
    SENTINEL_WATCH_RADIO_ENVIRONMENT } sentinel_watch_radio_t;

typedef enum { SENTINEL_WATCH_MODE_OFF, SENTINEL_WATCH_MODE_HISTORY,
    SENTINEL_WATCH_MODE_ALERT } sentinel_watch_mode_t;

typedef struct {
    sentinel_watch_protocol_record_t protocol;
    sentinel_watch_activity_sample_t activity;
    bool has_activity;
} sentinel_watch_details_t;

typedef struct {
    sentinel_watch_match_t match;
    sentinel_watch_radio_t radio;
    uint8_t address[6], address_type;
    char name[33]; /* Printable copy; never interpret broadcast text as markup. */
    int8_t rssi;
    uint16_t sightings;
    uint32_t first_ms, last_ms;
    sentinel_watch_details_t details;
} sentinel_watch_entry_t;

/* Volatile session only. No addresses/names are written to storage. Rotating
 * addresses are different broadcast identities, not a count of physical devices. */
typedef struct {
    sentinel_watch_entry_t history[SENTINEL_WATCH_HISTORY_CAPACITY];
    sentinel_watch_entry_t queue[SENTINEL_WATCH_QUEUE_CAPACITY];
    sentinel_watch_entry_t alert;
    uint8_t history_count, history_next, queue_count, queue_head;
    uint32_t category_alert_ms[SENTINEL_WATCH_CATEGORY_COUNT];
    uint32_t category_alert_mask;
    uint32_t enabled_mask, alert_mask;
    uint32_t observations, matches, sweeps, suppressed, queue_dropped;
    uint32_t alert_since_ms;
    bool alert_active;
} sentinel_watch_t;

void sentinel_watch_init(sentinel_watch_t *watch);
/* One call per completed, validated record per sweep, including nonmatches.
 * Returns true only if an alert was queued. match==NULL means no signature. */
bool sentinel_watch_observe(sentinel_watch_t *watch, sentinel_watch_radio_t radio,
    const uint8_t address[6], uint8_t address_type, const char *name, size_t name_length,
    int8_t rssi, const sentinel_watch_match_t *match, uint32_t now_ms);
bool sentinel_watch_observe_details(sentinel_watch_t *watch, sentinel_watch_radio_t radio,
    const uint8_t address[6], uint8_t address_type, const char *name, size_t name_length,
    int8_t rssi, const sentinel_watch_match_t *match, const sentinel_watch_details_t *details,
    uint32_t now_ms);
sentinel_watch_mode_t sentinel_watch_get_mode(const sentinel_watch_t *watch,
    sentinel_watch_category_t category);
/* Session preferences; existing history remains. Disabling alerts immediately
 * removes queued scenes of this category and dismisses its current scene. */
void sentinel_watch_set_mode(sentinel_watch_t *watch, sentinel_watch_category_t category,
    sentinel_watch_mode_t mode);
/* Pop queued scenes at a measured pace; no screen blinking/strobe. */
bool sentinel_watch_tick(sentinel_watch_t *watch, uint32_t now_ms);
void sentinel_watch_dismiss(sentinel_watch_t *watch);
const sentinel_watch_entry_t *sentinel_watch_history(const sentinel_watch_t *watch, size_t newest_offset);
