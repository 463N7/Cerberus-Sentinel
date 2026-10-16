#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool touch_available;
    bool c5_link_available;
    bool settings_available;
    bool storage_available;
} cerberus_ui_config_t;

void cerberus_ui_run(const cerberus_ui_config_t *config);

#ifdef __cplusplus
}
#endif
