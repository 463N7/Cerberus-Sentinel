#pragma once

#include "esp_err.h"
#include "sentinel_watch_detect.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Scene identifiers are independent of classifier enums. Art never determines
 * a detection or implies that a broadcast hint proves a device's identity. */
typedef enum {
    SENTINEL_WATCH_SCENE_GUARDIAN = 0,
    SENTINEL_WATCH_SCENE_AXON,
    SENTINEL_WATCH_SCENE_FLOCK,
    SENTINEL_WATCH_SCENE_WELLNESS,
    SENTINEL_WATCH_SCENE_FINDMY,
    SENTINEL_WATCH_SCENE_TRACKER,
    SENTINEL_WATCH_SCENE_CAMERA,
    SENTINEL_WATCH_SCENE_SMART_GLASSES,
    SENTINEL_WATCH_SCENE_FLIPPER,
    SENTINEL_WATCH_SCENE_MESHTASTIC,
    SENTINEL_WATCH_SCENE_SAMSUNG,
    SENTINEL_WATCH_SCENE_BEACON,
    SENTINEL_WATCH_SCENE_PWNAGOTCHI,
    SENTINEL_WATCH_SCENE_DRONE,
    SENTINEL_WATCH_SCENE_BLE_ACTIVITY,
    SENTINEL_WATCH_SCENE_COUNT,
} sentinel_watch_scene_t;

typedef struct {
    sentinel_watch_scene_t scene;
    sentinel_watch_confidence_t confidence;
    bool demo;
    bool paused;
    bool radio_online;
    bool has_details;
    uint16_t observation_count;
    uint16_t match_count;
    const char *phase;    /* Actual operation, e.g. "BLE LISTENING / 4S LEFT". */
    const char *title;    /* Optional; at most 24 displayed ASCII characters. */
    const char *evidence; /* Actual broadcast evidence; at most 48 characters. */
    const char *detail;   /* Second evidence/context line; at most 48 characters. */
} sentinel_watch_view_t;

typedef enum {
    SENTINEL_WATCH_ACTION_NONE = 0,
    SENTINEL_WATCH_ACTION_BACK,
    SENTINEL_WATCH_ACTION_PAUSE, /* Resume when paused. Disabled in demo. */
    SENTINEL_WATCH_ACTION_HISTORY,
    SENTINEL_WATCH_ACTION_DEMO,  /* Next scene when demo. */
    SENTINEL_WATCH_ACTION_DETAILS,
} sentinel_watch_action_t;

/* Owns the complete 320x480 screen. Call draw on entry, update on UI ticks.
 * now_ms uses the monotonic clock. Art animates at at most 2.5 fps; paused art
 * stays still. update owns bounded text copies, uses no heap, and does no
 * drawing for an unchanged frame. Failure invalidates the cache for repair.
 * Rendering is synchronous on the UI task; caller presents the composed frame.
 * Evidence/Details target: x8,y348,w304,h64, enabled only with has_details.
 * Buttons y426,h44: Back x8,w68; Pause/Resume x84,w68; History x160,w68;
 * Options/Next x236,w76 (legacy ACTION_DEMO id). Demo disables Pause and
 * History. Gaps never activate. Live Recon uses original pixel art; the
 * separate scene gallery retains the original illustrated Sentinel scenes.
 */
esp_err_t sentinel_watch_view_draw(const sentinel_watch_view_t *view, uint32_t now_ms);
esp_err_t sentinel_watch_view_update(const sentinel_watch_view_t *view, uint32_t now_ms);
void sentinel_watch_view_invalidate(void);
sentinel_watch_action_t sentinel_watch_view_hit_test(const sentinel_watch_view_t *view,
                                                   uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
