#include "remote_id_diagnostics.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static portMUX_TYPE diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static cerberus_remote_id_diagnostics_t diagnostics;

void cerberus_remote_id_diagnostics_publish(const cerberus_remote_id_diagnostics_t *snapshot)
{
    portENTER_CRITICAL(&diagnostics_lock);
    if (snapshot) diagnostics = *snapshot;
    else memset(&diagnostics, 0, sizeof(diagnostics));
    portEXIT_CRITICAL(&diagnostics_lock);
}

void cerberus_remote_id_diagnostics_get(cerberus_remote_id_diagnostics_t *snapshot)
{
    if (!snapshot) return;
    portENTER_CRITICAL(&diagnostics_lock);
    *snapshot = diagnostics;
    portEXIT_CRITICAL(&diagnostics_lock);
}
