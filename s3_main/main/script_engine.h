#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_SCRIPT_EXTENSION ".cbs"
#define CERBERUS_SCRIPT_MAX_FILE_BYTES 2048U
#define CERBERUS_SCRIPT_MAX_ACTIONS 24U
#define CERBERUS_SCRIPT_TEXT_MAX 180U
#define CERBERUS_SCRIPT_MAX_WAIT_MS 5000U
#define CERBERUS_SCRIPT_MAX_TOTAL_WAIT_MS 30000U

typedef enum {
    CERBERUS_SCRIPT_ACTION_TEXT = 1,
    CERBERUS_SCRIPT_ACTION_DIALOG = 2,
    CERBERUS_SCRIPT_ACTION_WAIT = 3,
    CERBERUS_SCRIPT_ACTION_TIMER = 4,
    CERBERUS_SCRIPT_ACTION_QR = 5,
} cerberus_script_action_type_t;

typedef struct {
    cerberus_script_action_type_t type;
    uint32_t value;
    char text[CERBERUS_SCRIPT_TEXT_MAX + 1U];
} cerberus_script_action_t;

typedef struct {
    char name[64];
    uint8_t action_count;
    uint32_t total_wait_ms;
    cerberus_script_action_t actions[CERBERUS_SCRIPT_MAX_ACTIONS];
} cerberus_script_t;

typedef struct {
    uint16_t line;
    char message[64];
} cerberus_script_error_t;

bool cerberus_script_file_name_is_valid(const char *file_name);
esp_err_t cerberus_script_load(
    const char *file_name,
    cerberus_script_t *script,
    cerberus_script_error_t *error);

#ifdef __cplusplus
}
#endif
