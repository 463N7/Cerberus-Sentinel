#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CERBERUS_KEYBOARD_ACTION_NONE,
    CERBERUS_KEYBOARD_ACTION_CHANGED,
    CERBERUS_KEYBOARD_ACTION_SUBMIT,
} cerberus_keyboard_action_t;

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    bool uppercase;
    bool masked;
    bool mask_toggle_enabled;
} cerberus_keyboard_t;

esp_err_t cerberus_keyboard_begin(
    cerberus_keyboard_t *keyboard,
    char *buffer,
    size_t capacity,
    const char *initial_text);
void cerberus_keyboard_set_masked(cerberus_keyboard_t *keyboard, bool masked);
esp_err_t cerberus_keyboard_draw(const cerberus_keyboard_t *keyboard, const char *prompt);
esp_err_t cerberus_keyboard_handle_tap(
    cerberus_keyboard_t *keyboard,
    uint16_t x,
    uint16_t y,
    cerberus_keyboard_action_t *action);

#ifdef __cplusplus
}
#endif
