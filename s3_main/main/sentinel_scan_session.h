#pragma once

/* Pure scheduler: no ESP-IDF, transport, radio, heap, or clock dependencies.
 * One UI task owns a session and executes each returned command exactly once. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENTINEL_SCAN_NONE = 0,
    SENTINEL_SCAN_WIFI,
    SENTINEL_SCAN_BLE,
    SENTINEL_SCAN_ENERGY,
    SENTINEL_SCAN_BEACON,
    SENTINEL_SCAN_ACTIVITY,
    SENTINEL_SCAN_DEFENSE,
    SENTINEL_SCAN_TOOL_COUNT,
} sentinel_scan_tool_t;

typedef enum {
    SENTINEL_SCAN_IDLE = 0,
    SENTINEL_SCAN_RUNNING,
    SENTINEL_SCAN_COMPLETE,
    SENTINEL_SCAN_ERROR,
} sentinel_scan_state_t;

typedef struct {
    /* False if the status query failed: no idle assumption is made. */
    bool valid;
    /* Other scans/connections/radio work block START. The adapter must observe
     * them, not disconnect them. Exclude the session's own active scan here. */
    bool radio_busy;
    /* Query session.active. With no active tool, any valid global idle snapshot
     * permits a start; tool/state are ignored. Always use fresh observations
     * made AFTER acknowledgement of the last command, never old cached state. */
    sentinel_scan_tool_t tool;
    sentinel_scan_state_t state;
} sentinel_scan_observation_t;

typedef enum {
    SENTINEL_SCAN_COMMAND_NONE = 0,
    SENTINEL_SCAN_COMMAND_START,
    SENTINEL_SCAN_COMMAND_STOP,
} sentinel_scan_command_kind_t;

typedef struct {
    sentinel_scan_command_kind_t kind;
    sentinel_scan_tool_t tool;
    uint32_t token;
} sentinel_scan_command_t;

typedef enum {
    SENTINEL_SCAN_ACCEPTED = 0,
    SENTINEL_SCAN_BUSY,
    SENTINEL_SCAN_FAILED,
} sentinel_scan_result_t;

#define SENTINEL_SCAN_RETRY_BASE_MS 1000U
#define SENTINEL_SCAN_STOP_WAIT_MS 3000U
#define SENTINEL_SCAN_FAILURE_LIMIT 3U

typedef struct {
    /* Read these fields to poll active status and render selection/state. */
    sentinel_scan_tool_t desired;
    sentinel_scan_tool_t active;
    bool paused;
    /* A completed sweep or caller-confirmed snapshot satisfies the selection. */
    bool completed;
    bool faulted;
    uint8_t failure_count;
    /* Remaining fields are scheduler-owned. */
    uint32_t stoppable_mask;
    uint32_t wait_since_ms;
    uint32_t wait_duration_ms;
    uint32_t stop_since_ms;
    uint32_t next_token;
    uint32_t arm_generation;
    uint32_t active_arm_generation;
    sentinel_scan_command_t pending;
    bool waiting;
    bool stop_requested;
    bool command_failed;
} sentinel_scan_session_t;

void sentinel_scan_session_init(sentinel_scan_session_t *session);
/* A new tool selection arms one sweep. Repeated selection of the same tool is
 * a no-op: both pause and completion remain sticky. Selecting NONE stops/drains
 * the current task before going idle. */
void sentinel_scan_session_select(sentinel_scan_session_t *session, sentinel_scan_tool_t tool);
/* Select a derived view backed by an already captured snapshot, without arming
 * another sweep. The caller must have real completed data for this tool.
 * Applies only to a NEW non-NONE selection; repeated calls cannot defeat Stop
 * or Scan Again. Any previous active operation still stops/drains safely. */
void sentinel_scan_session_select_snapshot(sentinel_scan_session_t *session, sentinel_scan_tool_t tool);
/* pause(false) explicitly arms ONE new sweep and clears retry exhaustion. If an
 * older sweep/STOP is still draining, it cannot satisfy this fresh request.
 * Pausing schedules a supported STOP or drains a bounded scan. Neither action
 * initiates a network connection. */
void sentinel_scan_session_pause(sentinel_scan_session_t *session, bool paused);
/* Defaults: WiFi, BLE and defense support STOP. Existing 802.15.4 scans must
 * finish naturally. Set support true only when the C5 has a real stop API. */
void sentinel_scan_session_set_stop_supported(sentinel_scan_session_t *session, sentinel_scan_tool_t tool, bool supported);
sentinel_scan_command_t sentinel_scan_session_step(sentinel_scan_session_t *session, uint32_t now_ms, sentinel_scan_observation_t observation);
/* Must be called for every emitted command, including transport failures. Stale
 * or duplicate acknowledgements are ignored and return false. A failed START
 * remains tracked until a fresh status query rules out an ambiguous delivery. */
bool sentinel_scan_session_ack(sentinel_scan_session_t *session, sentinel_scan_command_t command, sentinel_scan_result_t result, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
