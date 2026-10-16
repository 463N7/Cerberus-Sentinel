#pragma once

/* Passive, allocation-free broadcast classification. No radio/ESP dependencies.
 * A match is an unauthenticated broadcast hint, never proof of device identity,
 * ownership, recording, or tracking a person. See docs/WATCH_SIGNATURES.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENTINEL_WATCH_MAX_AD_LENGTH 31U
#define SENTINEL_WATCH_MAX_NAME_LENGTH 32U
#define SENTINEL_WATCH_MAX_SSID_LENGTH 32U

typedef enum {
    SENTINEL_WATCH_NONE = 0,
    SENTINEL_WATCH_AXON,
    SENTINEL_WATCH_FLOCK,
    SENTINEL_WATCH_WELLNESS,
    SENTINEL_WATCH_FINDMY,
    SENTINEL_WATCH_TRACKER,
    SENTINEL_WATCH_CAMERA,
    SENTINEL_WATCH_SMART_GLASSES,
    SENTINEL_WATCH_FLIPPER,
    SENTINEL_WATCH_MESHTASTIC,
    SENTINEL_WATCH_SAMSUNG,
    SENTINEL_WATCH_BEACON,
    SENTINEL_WATCH_PWNAGOTCHI,
    SENTINEL_WATCH_DRONE,
    SENTINEL_WATCH_BLE_ACTIVITY,
    SENTINEL_WATCH_CATEGORY_COUNT,
} sentinel_watch_category_t;

/* Evidence strength, not a probability or authenticated identity. */
typedef enum {
    SENTINEL_WATCH_CONFIDENCE_NONE = 0,
    SENTINEL_WATCH_CONFIDENCE_LOW,
    SENTINEL_WATCH_CONFIDENCE_MEDIUM,
    SENTINEL_WATCH_CONFIDENCE_HIGH,
} sentinel_watch_confidence_t;

typedef struct {
    sentinel_watch_category_t category;
    sentinel_watch_confidence_t confidence;
    /* Static printable strings owned by the classifier, never broadcast text. */
    const char *rule_id;
    const char *label;
    const char *evidence;
} sentinel_watch_match_t;

typedef struct {
    const uint8_t *name;
    size_t name_length; /* optional bounded local name, no NUL required */
    const uint8_t *advertisement;
    size_t advertisement_length; /* one packet only; never join AD + scan reply */
    uint8_t advertisement_flags; /* nonzero means truncated/malformed: reject */
    uint8_t address[6];
    uint8_t address_type;
} sentinel_watch_ble_observation_t;

typedef struct {
    const uint8_t *ssid;
    size_t ssid_length; /* <=32, no NUL required */
    uint8_t bssid[6];
} sentinel_watch_wifi_observation_t;

/* Invalid input or no supported signature returns false and clears match.
 * Strongest documented match wins; no allocations, connections or probing. */
bool sentinel_watch_classify_ble(const sentinel_watch_ble_observation_t *observation,
    sentinel_watch_match_t *match);
bool sentinel_watch_classify_wifi(const sentinel_watch_wifi_observation_t *observation,
    sentinel_watch_match_t *match);
const char *sentinel_watch_category_name(sentinel_watch_category_t category);
const char *sentinel_watch_confidence_name(sentinel_watch_confidence_t confidence);

#ifdef __cplusplus
}
#endif
