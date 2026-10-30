#pragma once

/* Original fixed-point representation of advertised Remote ID Location values.
 * Reference semantics: OpenDroneID core C, commit 849c03adbe0cdd68997a462fafab397edcfe53f3,
 * libopendroneid/opendroneid.{c,h}. These are unauthenticated claims, not measured
 * motion. This header is independent of the SPI protocol and collector. */
#include <stdbool.h>
#include <stdint.h>

enum {
    SENTINEL_WATCH_MOTION_LOCATION_PRESENT = 1U,
    SENTINEL_WATCH_MOTION_SPEED = 2U,
    SENTINEL_WATCH_MOTION_DIRECTION = 4U,
    SENTINEL_WATCH_MOTION_HEIGHT = 8U,
    SENTINEL_WATCH_MOTION_HEIGHT_AGL = 16U,
};

typedef struct {
    uint16_t speed_qms; /* Quarter metres/second: low byte or 255 + 3 * high byte. */
    uint16_t direction_deg; /* Advertised 0..360; 360 is retained, not normalized. */
    uint16_t height_encoded; /* Metres = encoded / 2 - 1000; zero is unavailable. */
    uint8_t flags;
    uint8_t reserved;
} sentinel_watch_motion_t;

/* Absent components are zero. A valid Location may have every component
 * unavailable. AGL is meaningful only with HEIGHT; clear means above takeoff.
 * Zero speed/direction are valid when their respective flag is set. */
static inline bool sentinel_watch_motion_valid(const sentinel_watch_motion_t *m)
{
    if (!m || m->reserved || (m->flags & ~31U)) return false;
    if (!(m->flags & SENTINEL_WATCH_MOTION_LOCATION_PRESENT))
        return !m->flags && !m->speed_qms && !m->direction_deg && !m->height_encoded;
    if (m->flags & SENTINEL_WATCH_MOTION_SPEED) {
        if (m->speed_qms > 1017U || (m->speed_qms > 255U && m->speed_qms % 3U)) return false;
    } else if (m->speed_qms) return false;
    if (m->flags & SENTINEL_WATCH_MOTION_DIRECTION) {
        if (m->direction_deg > 360U) return false;
    } else if (m->direction_deg) return false;
    if (m->flags & SENTINEL_WATCH_MOTION_HEIGHT) {
        if (!m->height_encoded) return false;
    } else if (m->height_encoded || (m->flags & SENTINEL_WATCH_MOTION_HEIGHT_AGL)) return false;
    return true;
}
