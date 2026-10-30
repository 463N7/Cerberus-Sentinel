#pragma once

#include "csi_session.h"
#include "display.h"

/* Pure presentation and painted-control hit testing. Storage belongs beside
 * the session in the caller's single PSRAM allocation, never a static worker. */
typedef struct {
    const cerberus_csi_session_view_t *session;
    const char *source_label; /* Caller supplies privacy-formatted identity. */
} cerberus_csi_ui_view_t;

typedef enum {
    CERBERUS_CSI_UI_NONE,
    CERBERUS_CSI_UI_START,
    CERBERUS_CSI_UI_STOP,
    CERBERUS_CSI_UI_BACK,
    CERBERUS_CSI_UI_RECALIBRATE,
} cerberus_csi_ui_action_t;

typedef struct {
    char lines[12][49];
    uint8_t history[CERBERUS_CSI_MOTION_HISTORY];
    uint16_t history_count, state_color;
    bool start, stop, recalibrate;
} cerberus_csi_ui_model_t;

typedef struct {
    cerberus_csi_ui_model_t painted, staging;
    bool valid;
} cerberus_csi_ui_cache_t;

esp_err_t cerberus_csi_ui_update(cerberus_csi_ui_cache_t *cache,
    const cerberus_csi_ui_view_t *view, bool force);
void cerberus_csi_ui_invalidate(cerberus_csi_ui_cache_t *cache);
cerberus_csi_ui_action_t cerberus_csi_ui_tap(const cerberus_csi_ui_cache_t *cache,
    uint16_t x, uint16_t y);
