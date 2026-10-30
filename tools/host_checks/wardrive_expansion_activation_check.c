#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wardrive_plan.h"
#include "esp_err.h"
#include <assert.h>
#include <stdio.h>

static unsigned calls;
static esp_err_t injected_error;
static bool had_plan;
static cerberus_wardrive_plan_t received;

static esp_err_t field_survey_set_expansion_plan(const cerberus_wardrive_plan_t *plan)
{
    ++calls;
    had_plan = plan != NULL;
    if (plan != NULL) received = *plan;
    return injected_error;
}

#include "wardrive_expansion_activation_impl.h"

static void check(uint8_t present)
{
    calls = 0U;
    had_plan = false;
    injected_error = ESP_OK;
    assert(prepare_expansion_plan(present) == ESP_OK);
    assert(calls == 1U);
    assert(had_plan == ((present & 0x3fU) != 0U));
    if (!had_plan) return;
    assert(received.available_mask == (uint8_t)(1U | ((present & 0x3fU) << 1U)));
    assert(received.region == CERBERUS_REGION_US);
    assert(received.band == CERBERUS_WARDRIVE_BAND_MIXED);
    assert(received.timing == CERBERUS_WARDRIVE_TIMING_BALANCED);
    for (unsigned slot = 1U; slot <= 6U; ++slot)
        assert(received.radio[slot].available ==
            ((present & (1U << (slot - 1U))) != 0U));
}

int main(void)
{
    check(0U); /* no backboard: restore fallback discovery */
    check(1U);
    check(2U);
    check(0x3fU);
    check(0xffU); /* ignore bits outside R1-R6 */
    injected_error = ESP_ERR_INVALID_STATE;
    assert(prepare_expansion_plan(1U) == ESP_ERR_INVALID_STATE);
    assert(prepare_expansion_plan(0U) == ESP_ERR_INVALID_STATE);
    puts("wardrive expansion activation: present slots, fallback, and setter errors passed");
    return 0;
}
