#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_WARDRIVE_GPS_LINE_CAPACITY 128U
#define CERBERUS_WARDRIVE_GPS_FIX_MAX_AGE_MS 5000U
#define CERBERUS_WARDRIVE_GPS_SENTENCE_MAX_AGE_MS 10000U

typedef enum {
    CERBERUS_WARDRIVE_GPS_ABSENT = 0,
    CERBERUS_WARDRIVE_GPS_NO_FIX,
    CERBERUS_WARDRIVE_GPS_STALE,
    CERBERUS_WARDRIVE_GPS_FRESH,
} cerberus_wardrive_gps_state_t;

/* Pure parser state. The UART/PPS adapter owns synchronization when feeding it
 * from a task. No hardware or ESP-IDF dependency is required for host tests. */
typedef struct {
    char line[CERBERUS_WARDRIVE_GPS_LINE_CAPACITY];
    uint8_t line_length;
    bool discarding_line;
    bool pending_cr;
    bool sentence_seen;
    bool fix_live;
    bool utc_seen;
    bool altitude_valid;
    uint64_t last_sentence_ms;
    uint64_t last_fix_ms;
    uint64_t last_utc_ms;
    double latitude;
    double longitude;
    float altitude_m;
    float hdop;
    uint8_t satellites;
    char utc[25]; /* YYYY-MM-DDTHH:MM:SS.mmmZ from a valid RMC date/time. */
    char utc_second[7];
    uint32_t valid_sentences;
    uint32_t bad_checksums;
    uint32_t malformed_sentences;
    uint32_t overflow_lines;
    uint32_t fix_updates;
} cerberus_wardrive_gps_core_t;

typedef struct {
    cerberus_wardrive_gps_state_t state;
    uint32_t sentence_age_ms;
    uint32_t fix_age_ms;
    uint32_t utc_age_ms; /* UINT32_MAX until a checksum-validated RMC time exists. */
    bool coordinates_valid;
    double latitude;
    double longitude;
    bool altitude_valid;
    float altitude_m;
    float hdop;
    uint8_t satellites;
    bool utc_valid;
    bool utc_from_rmc;
    char utc[25];
    uint32_t valid_sentences;
    uint32_t bad_checksums;
    uint32_t malformed_sentences;
    uint32_t overflow_lines;
    uint32_t fix_updates;
} cerberus_wardrive_gps_snapshot_t;

void cerberus_wardrive_gps_core_reset(cerberus_wardrive_gps_core_t *core);

/* Feed arbitrary UART chunks. A line with an invalid/missing checksum, an
 * embedded control byte, or more than 127 bytes is discarded. */
void cerberus_wardrive_gps_core_feed(cerberus_wardrive_gps_core_t *core,
    const uint8_t *bytes, size_t count, uint64_t monotonic_ms);

/* For complete NMEA lines without CR/LF. Returns true only when a strict
 * checksum passes; recognized sentences may still have malformed fields. */
bool cerberus_wardrive_gps_core_ingest_line(cerberus_wardrive_gps_core_t *core,
    const char *line, size_t length, uint64_t monotonic_ms);

/* Coordinates and UTC are zero/empty unless the fix is fresh. UTC is separately
 * valid only when a checksum-checked RMC gave a valid date and time. */
void cerberus_wardrive_gps_core_snapshot(const cerberus_wardrive_gps_core_t *core,
    uint64_t monotonic_ms, cerberus_wardrive_gps_snapshot_t *out);
