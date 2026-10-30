#pragma once

/* Passive beacon pattern evidence only. Address prefixes and unauthenticated
 * beacon claims do not identify a physical device, owner, or intent. */
#include "wifi_beacon_analysis.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_WIFI_PINESCAN_DEFAULT_ROWS 32U
#define CERBERUS_WIFI_PINESCAN_MAX_ROWS 128U
enum {
    CERBERUS_WIFI_PINESCAN_REASON_OUI = 1U,
    CERBERUS_WIFI_PINESCAN_REASON_MINIMAL = 2U,
};
enum {
    CERBERUS_WIFI_PINESCAN_PROFILE_NONE = 0U,
    CERBERUS_WIFI_PINESCAN_PROFILE_ALFA,
    CERBERUS_WIFI_PINESCAN_PROFILE_ORIENT,
    CERBERUS_WIFI_PINESCAN_PROFILE_SHENZHEN,
    CERBERUS_WIFI_PINESCAN_PROFILE_IEEE,
    CERBERUS_WIFI_PINESCAN_PROFILE_LOCAL,
    CERBERUS_WIFI_PINESCAN_PROFILE_MEDIATEK,
    CERBERUS_WIFI_PINESCAN_PROFILE_PANDA,
    CERBERUS_WIFI_PINESCAN_PROFILE_DEADBE,
};
enum {
    CERBERUS_WIFI_PINESCAN_CONDITION_NONE = 0U,
    CERBERUS_WIFI_PINESCAN_CONDITION_ANY,
    CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR,
    CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_SET,
};

typedef struct {
    cerberus_wifi_beacon_observation_t beacon;
    uint16_t ie_count;
    uint8_t reasons; /* Current frame only. Zero means valid/nonmatching. */
    uint8_t profile; /* Exact addr2 prefix profile, even if its condition fails. */
    uint8_t condition; /* Rule requires privacy capability bit clear/set/either. */
    bool privacy_bit; /* Capability bit 0x0010; not verified WPA/RSN security. */
} cerberus_wifi_pinescan_evidence_t;

typedef struct {
    cerberus_wifi_beacon_observation_t latest; /* Latest valid beacon from this addr2. */
    cerberus_wifi_pinescan_evidence_t last_match; /* Latest qualifying frame. */
    uint64_t first_observed_us; /* First qualifying frame stored this session. */
    uint32_t frames; /* All admitted observations of this addr2 since insertion. */
    uint32_t matching_frames;
    int8_t peak_rssi;
    uint8_t reasons_seen; /* Historical OR; not a claim about latest/last_match. */
    bool multiple_bssids;
} cerberus_wifi_pinescan_record_t;

typedef struct {
    cerberus_wifi_pinescan_record_t *records; /* Caller-owned, preferably PSRAM. */
    uint16_t capacity,count;
    uint64_t latest_observed_us; /* Monotonic source high-water mark this session. */
    uint32_t valid_beacons,matching_beacons,rejected_frames,capacity_drops,stale_frames;
} cerberus_wifi_pinescan_t;

typedef enum {
    CERBERUS_WIFI_PINESCAN_INVALID = 0,
    CERBERUS_WIFI_PINESCAN_NONMATCH,
    CERBERUS_WIFI_PINESCAN_ADDED,
    CERBERUS_WIFI_PINESCAN_UPDATED,
    CERBERUS_WIFI_PINESCAN_FULL,
    CERBERUS_WIFI_PINESCAN_STALE,
} cerberus_wifi_pinescan_event_t;

/* Strict complete beacon (no FCS/radiotap). Returns false and clears output
 * for malformed input. True with reasons=0 is an ordinary valid beacon.
 * RX channel and C5 callback timestamp remain separate from advertised IEs. */
bool cerberus_wifi_pinescan_decode(const uint8_t *frame,size_t length,
    uint8_t rx_channel,int8_t rssi,uint64_t observed_us,
    cerberus_wifi_pinescan_evidence_t *out);

bool cerberus_wifi_pinescan_init(cerberus_wifi_pinescan_t *table,
    cerberus_wifi_pinescan_record_t *storage,uint16_t capacity);

/* Exact addr2 key. Store only matching identities; later valid nonmatches
 * update an existing row's latest/frame/RSSI fields but preserve last_match.
 * Older source timestamps cannot change evidence. Equal timestamps are valid.
 * A full table still updates existing rows and counts rejected new matches.
 * row_index is valid only for ADDED/UPDATED, otherwise UINT16_MAX.
 * Caller serializes access, deduplicates transport retries, resets per source
 * session/generation, and owns storage lifetime. No allocation or radio calls. */
cerberus_wifi_pinescan_event_t cerberus_wifi_pinescan_observe(
    cerberus_wifi_pinescan_t *table,const uint8_t *frame,size_t length,
    uint8_t rx_channel,int8_t rssi,uint64_t observed_us,uint16_t *row_index);

bool cerberus_wifi_pinescan_copy(const cerberus_wifi_pinescan_t *table,
    uint16_t index,cerberus_wifi_pinescan_record_t *out);
const char *cerberus_wifi_pinescan_profile_name(uint8_t profile);
const char *cerberus_wifi_pinescan_condition_name(uint8_t condition);

#ifdef __cplusplus
}
#endif
