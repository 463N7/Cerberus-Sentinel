#include "field_survey.h"
#include "field_survey_stream.h"
#include "radio_bus.h"
#include "storage.h"
#include "recon_store.h"
#include "../../expansion/shared/cerberus_config_proof.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SemaphoreHandle_t state_lock;
static field_survey_snapshot_t state;
static uint8_t sequence, replace_at;
static bool may_run[6];
static bool worker_busy, profile_active, profile_attempted[6];
static bool profile_proof_seen[6];
static bool idle_probe_enabled = true;
static cerberus_scan_config_t profile_config[6];
static TickType_t next_idle_probe[6], profile_attempt_tick[6];
static uint32_t scan_rejection_baseline[6];
static bool scan_rejection_baseline_known[6];
static uint32_t dropped[6];
static field_survey_stream_t *wardrive_stream;

static esp_err_t transfer(unsigned slot, const uint8_t *tx, uint8_t *rx)
{
    vTaskDelay(pdMS_TO_TICKS(3));
    return radio_bus_expansion_transfer(slot, tx, rx, 64);
}
/* Complete exchange under the SAME lock used by the onboard C5. */
static bool exchange(unsigned slot, unsigned command, bool enabled,
                     const cerberus_scan_config_t *profile, expansion_status_t *status,
                     expansion_record_t records[3], uint8_t *count)
{
    SemaphoreHandle_t bus = radio_bus_mutex();
    if (!bus || xSemaphoreTake(bus, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    uint8_t tx[64], rx[64], seq = ++sequence;
    bool ok = false;
    if (command == EXP_CONFIG) {
        expansion_request(tx, EXP_ARM, seq);
        if (transfer(slot, tx, rx) != ESP_OK) goto done;
        if (profile) expansion_config_profile(tx, seq, profile);
        else expansion_config(tx, seq, slot, enabled);
    } else expansion_request(tx, command, seq);
    if (transfer(slot, tx, rx) != ESP_OK) goto done;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(5));
        expansion_request(tx, 0, seq);
        if (transfer(slot, tx, rx) != ESP_OK) break;
        ok = command == EXP_READ ? expansion_parse_records(rx, seq, slot, records, count) :
            expansion_parse_status(rx, seq, status);
        if (ok) break;
    }
done:
    xSemaphoreGive(bus);
    return ok;
}
static void add(const expansion_record_t *record)
{
    unsigned i;
    for (i = 0; i < state.count; ++i)
        if (!memcmp(state.records[i].bssid, record->bssid, 6)) break;
    if (i == state.count) {
        if (state.count < FIELD_SURVEY_CAPACITY) ++state.count;
        else { i = replace_at++ % FIELD_SURVEY_CAPACITY; ++state.evicted; }
    }
    state.records[i] = *record;
    state.record_update[i] = ++state.observations;
}
static void worker(void *unused)
{
    (void)unused;
    unsigned slot = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60));
        xSemaphoreTake(state_lock, portMAX_DELAY);
        bool planned = profile_active;
        uint8_t bit = 1U << slot;
        bool assigned = planned && (state.profile_mask & bit);
        bool wanted = state.requested && (!planned || assigned);
        cerberus_scan_config_t config = profile_config[slot];
        uint32_t epoch = state.request_epoch;
        TickType_t now = xTaskGetTickCount();
        bool due = next_idle_probe[slot] == 0 ||
            (int32_t)(now - next_idle_probe[slot]) >= 0;
        bool probe = idle_probe_enabled && !wanted && !may_run[slot] && due;
        bool absent_backoff = wanted && !(state.present_mask & bit) &&
            !may_run[slot] && !due;
        if ((!wanted && !may_run[slot] && !probe) || absent_backoff) {
            state.worker_seen_epoch = epoch;
            xSemaphoreGive(state_lock);
            slot = (slot + 1) % 6;
            continue;
        }
        worker_busy = true;
        xSemaphoreGive(state_lock);
        if (probe) next_idle_probe[slot] = now + pdMS_TO_TICKS(15000);
        /* A failed STOP remains pending and is retried ahead of idle probes. */
        expansion_status_t status = {0};
        expansion_record_t records[3]; uint8_t count = 0;
        bool ok = exchange(slot, EXP_STATUS, false, NULL, &status, records, &count);
        bool profile_config_failed = false;
        if (ok) {
            may_run[slot] |= status.enabled;
            dropped[slot] = status.dropped;
            if (planned && assigned && status.config_proof_supported)
                profile_proof_seen[slot] = true;
            if (planned && wanted) {
                if (!profile_proof_seen[slot] &&
                    (!profile_attempted[slot] || !status.enabled) &&
                    (!profile_attempted[slot] ||
                     (TickType_t)(now - profile_attempt_tick[slot]) >= pdMS_TO_TICKS(10000))) {
                    /* Older v3 images omit admission proof. Apply the planned
                     * CONFIG once (or retry a disabled node), then label its
                     * reports unverified rather than leaving it permanently off. */
                    may_run[slot] = true;
                    profile_attempted[slot] = true;
                    profile_attempt_tick[slot] = now;
                    ok = exchange(slot, EXP_CONFIG, true, &config,
                        &status, records, &count);
                } else if (profile_proof_seen[slot] && status.enabled &&
                    !expansion_status_matches_profile(&status, &config)) {
                    /* Stop a stale allocation before applying a new profile. */
                    ok = exchange(slot, EXP_CONFIG, false, NULL,
                        &status, records, &count);
                } else if (profile_proof_seen[slot] &&
                    !expansion_status_matches_profile(&status, &config) &&
                    (!profile_attempted[slot] ||
                     (TickType_t)(now - profile_attempt_tick[slot]) >= pdMS_TO_TICKS(10000))) {
                    /* A lost ACK may have started scanning: hold the STOP obligation. */
                    may_run[slot] = true;
                    profile_attempted[slot] = true;
                    profile_attempt_tick[slot] = now;
                    ok = exchange(slot, EXP_CONFIG, true, &config,
                        &status, records, &count);
                    profile_config_failed = ok &&
                        !expansion_status_matches_profile(&status, &config);
                }
            } else if (status.enabled != wanted) {
                if (wanted) may_run[slot] = true;
                if (planned && assigned && status.config_proof_supported) {
                    config.scan_enabled = 0U;
                    ok = exchange(slot, EXP_CONFIG, false, &config,
                        &status, records, &count);
                } else {
                    ok = exchange(slot, EXP_CONFIG, wanted, NULL,
                        &status, records, &count);
                }
            }
            if (ok && planned && assigned && status.config_proof_supported)
                profile_proof_seen[slot] = true;
            if (ok && planned && wanted && profile_proof_seen[slot] &&
                status.enabled &&
                !expansion_status_matches_profile(&status, &config)) {
                /* An old or mismatched allocation must not keep scanning. */
                ok = exchange(slot, EXP_CONFIG, false, NULL,
                    &status, records, &count);
            }
            if (ok) may_run[slot] = status.enabled;
        }
        xSemaphoreTake(state_lock, portMAX_DELAY);
        if (ok) {
            state.present_mask |= bit;
            state.uncertain_mask &= ~bit;
            if (wanted) next_idle_probe[slot] = 0;
            if (may_run[slot]) state.scanning_mask |= bit;
            else state.scanning_mask &= ~bit;
        } else {
            bool was_present = (state.present_mask & bit) != 0U;
            state.present_mask &= ~bit;
            /* A replacement on this CS may be an older image. Reapply the
             * profile when communication returns instead of trusting the
             * previous node's proof or CONFIG attempt. */
            profile_proof_seen[slot] = false;
            profile_attempted[slot] = false;
            profile_attempt_tick[slot] = 0;
            if (may_run[slot]) state.uncertain_mask |= bit;
            else next_idle_probe[slot] = now + pdMS_TO_TICKS(15000);
            if (may_run[slot] || was_present) ++state.errors;
        }
        if (planned && assigned && wanted && ok) {
            if (!profile_proof_seen[slot]) state.profile_unverified_mask |= bit;
            else state.profile_unverified_mask &= (uint8_t)~bit;
            if (expansion_status_matches_profile(&status, &config))
                state.profile_verified_mask |= bit;
            else state.profile_verified_mask &= (uint8_t)~bit;
            if (status.config_proof_supported && profile_config_failed &&
                !expansion_status_matches_profile(&status, &config))
                state.profile_rejected_mask |= bit;
            else if (state.profile_verified_mask & bit)
                state.profile_rejected_mask &= (uint8_t)~bit;
        } else {
            state.profile_verified_mask &= (uint8_t)~bit;
            if (!ok) state.profile_unverified_mask &= (uint8_t)~bit;
            if (!wanted) {
                state.profile_unverified_mask &= (uint8_t)~bit;
                state.profile_rejected_mask &= (uint8_t)~bit;
            }
        }
        if (ok && status.config_proof_supported) {
            state.scan_rejections_by_slot[slot] = status.scan_rejections;
            state.last_rejected_channel[slot] = status.last_rejected_channel;
            state.last_scan_error_by_slot[slot] = status.last_scan_error;
            if (!scan_rejection_baseline_known[slot] ||
                status.scan_rejections < scan_rejection_baseline[slot]) {
                scan_rejection_baseline[slot] = status.scan_rejections;
                scan_rejection_baseline_known[slot] = true;
            }
            if (status.scan_rejections > scan_rejection_baseline[slot] &&
                planned && assigned) state.profile_driver_rejected_mask |= bit;
            else state.profile_driver_rejected_mask &= (uint8_t)~bit;
        }
        state.radio_dropped = 0;
        for (unsigned i = 0; i < 6; ++i) {
            state.radio_dropped_by_slot[i] = dropped[i];
            state.radio_dropped += dropped[i];
        }
        bool read = ok && wanted && state.requested && may_run[slot] &&
            (!planned || ((state.profile_verified_mask |
                state.profile_unverified_mask) & bit) != 0U);
        state.worker_seen_epoch = epoch;
        xSemaphoreGive(state_lock);
        if (read && exchange(slot, EXP_READ, false, NULL, &status, records, &count)) {
            /* Reuse labels already resolved for this exact BSSID. Most
             * expansion batches repeat nearby APs, and an SD OUI lookup
             * otherwise opens the index for every result. */
            xSemaphoreTake(state_lock, portMAX_DELAY);
            for (unsigned i = 0; i < count; ++i) {
                for (unsigned j = 0; j < state.count; ++j) {
                    if (state.records[j].vendor[0] != '\0' &&
                        memcmp(state.records[j].bssid, records[i].bssid, 6U) == 0) {
                        memcpy(records[i].vendor, state.records[j].vendor,
                            sizeof(records[i].vendor));
                        break;
                    }
                }
            }
            xSemaphoreGive(state_lock);
            for (unsigned i = 0; i < count; ++i) {
                if (records[i].vendor[0] != '\0') continue;
                for (unsigned earlier = 0; earlier < i; ++earlier) {
                    if (records[earlier].vendor[0] != '\0' &&
                        memcmp(records[earlier].bssid, records[i].bssid, 6U) == 0) {
                        memcpy(records[i].vendor, records[earlier].vendor,
                            sizeof(records[i].vendor));
                        break;
                    }
                }
                if (records[i].vendor[0] == '\0')
                    (void)recon_store_vendor(records[i].bssid, records[i].vendor);
            }
            xSemaphoreTake(state_lock, portMAX_DELAY);
            for (unsigned i = 0; i < count; ++i) {
                add(&records[i]);
                if (wardrive_stream != NULL)
                    (void)field_survey_stream_push(wardrive_stream, &records[i]);
            }
            xSemaphoreGive(state_lock);
        }
        xSemaphoreTake(state_lock, portMAX_DELAY);
        worker_busy = false;
        xSemaphoreGive(state_lock);
        slot = (slot + 1) % 6;
    }
}
esp_err_t field_survey_init(void)
{
    if (state_lock) return ESP_OK;
    esp_err_t err = radio_bus_init();
    if (err != ESP_OK) return err;
    state_lock = xSemaphoreCreateMutex();
    if (!state_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(worker, "field_survey", 4096, NULL, 2, NULL) != pdPASS) {
        vSemaphoreDelete(state_lock); state_lock = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t field_survey_set_running(bool run)
{
    if (!state_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    if (state.requested != run) {
        state.requested = run;
        ++state.request_epoch;
    }
    xSemaphoreGive(state_lock);
    return ESP_OK;
}
esp_err_t field_survey_set_idle_probe_enabled(bool enabled)
{
    if (!state_lock) return ESP_ERR_INVALID_STATE;
    for (unsigned attempt = 0; attempt < 100U; ++attempt) {
        xSemaphoreTake(state_lock, portMAX_DELAY);
        if (enabled) {
            idle_probe_enabled = true;
            memset(next_idle_probe, 0, sizeof(next_idle_probe));
            xSemaphoreGive(state_lock);
            return ESP_OK;
        }
        bool pending_stop = false;
        if (!worker_busy)
            for (unsigned i = 0; i < 6U; ++i) pending_stop |= may_run[i];
        if (state.requested || state.scanning_mask || state.uncertain_mask ||
            pending_stop) {
            xSemaphoreGive(state_lock);
            return ESP_ERR_INVALID_STATE;
        }
        idle_probe_enabled = false;
        bool busy = worker_busy;
        xSemaphoreGive(state_lock);
        if (!busy) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    /* The gate remains closed. A caller may retry waiting for a slow exchange. */
    return ESP_ERR_TIMEOUT;
}
esp_err_t field_survey_set_expansion_plan(const cerberus_wardrive_plan_t *plan)
{
    if (!state_lock) return ESP_ERR_INVALID_STATE;
    cerberus_scan_config_t next[6] = {0};
    uint8_t mask = 0U;
    if (plan) {
        if (!(plan->available_mask & 1U) || (plan->available_mask & 0x80U))
            return ESP_ERR_INVALID_ARG;
        for (unsigned slot = 1U; slot <= 6U; ++slot) {
            if (!(plan->available_mask & (1U << slot))) continue;
            if (!cerberus_wardrive_plan_expansion_config(plan, slot, true,
                    &next[slot - 1U]) ||
                !cerberus_config_region_masks_valid(&next[slot - 1U]))
                return ESP_ERR_INVALID_ARG;
            mask |= (uint8_t)(1U << (slot - 1U));
        }
    }
    /* Presence polling is brief but may be in progress. Wait for that exchange
     * so the plan swap is atomic with respect to the worker's local copy. */
    for (unsigned attempt = 0; attempt < 30U; ++attempt) {
        xSemaphoreTake(state_lock, portMAX_DELAY);
        if (worker_busy) {
            xSemaphoreGive(state_lock);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        bool pending_stop = false;
        for (unsigned i = 0; i < 6U; ++i) pending_stop |= may_run[i];
        bool busy = state.requested || state.scanning_mask ||
            state.uncertain_mask || pending_stop || wardrive_stream != NULL;
        if (busy) {
            xSemaphoreGive(state_lock);
            return ESP_ERR_INVALID_STATE;
        }
        memcpy(profile_config, next, sizeof(next));
        profile_active = plan != NULL;
        memset(profile_attempted, 0, sizeof(profile_attempted));
        memset(profile_proof_seen, 0, sizeof(profile_proof_seen));
        memset(profile_attempt_tick, 0, sizeof(profile_attempt_tick));
        memset(next_idle_probe, 0, sizeof(next_idle_probe));
        memset(scan_rejection_baseline, 0,
            sizeof(scan_rejection_baseline));
        memset(scan_rejection_baseline_known, 0,
            sizeof(scan_rejection_baseline_known));
        state.profile_mask = mask;
        state.profile_verified_mask = 0U;
        state.profile_unverified_mask = 0U;
        state.profile_rejected_mask = 0U;
        state.profile_driver_rejected_mask = 0U;
        memset(state.scan_rejections_by_slot, 0, sizeof(state.scan_rejections_by_slot));
        memset(state.last_rejected_channel, 0, sizeof(state.last_rejected_channel));
        memset(state.last_scan_error_by_slot, 0,
            sizeof(state.last_scan_error_by_slot));
        ++state.profile_generation;
        xSemaphoreGive(state_lock);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}
void field_survey_snapshot(field_survey_snapshot_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!state_lock) return;
    xSemaphoreTake(state_lock, portMAX_DELAY); *out = state; xSemaphoreGive(state_lock);
}
esp_err_t field_survey_stream_open(void)
{
    if (!state_lock) return ESP_ERR_INVALID_STATE;
    field_survey_stream_t *new_stream = heap_caps_calloc(1U,
        sizeof(*new_stream), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new_stream == NULL) return ESP_ERR_NO_MEM;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    bool busy = wardrive_stream != NULL || state.requested ||
        state.scanning_mask != 0U || state.uncertain_mask != 0U;
    if (!busy) wardrive_stream = new_stream;
    xSemaphoreGive(state_lock);
    if (busy) heap_caps_free(new_stream);
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}
void field_survey_stream_close(void)
{
    if (!state_lock) return;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    field_survey_stream_t *old_stream = wardrive_stream;
    wardrive_stream = NULL;
    xSemaphoreGive(state_lock);
    heap_caps_free(old_stream);
}
size_t field_survey_stream_read(expansion_record_t *out, size_t capacity,
    uint32_t *overflow_dropped, uint32_t *invalid_dropped, uint16_t *queued,
    uint32_t overflow_by_radio[6])
{
    if (!state_lock || out == NULL || capacity == 0U) return 0U;
    xSemaphoreTake(state_lock, portMAX_DELAY);
    field_survey_stream_t *stream = wardrive_stream;
    size_t read = field_survey_stream_pop(stream, out, capacity);
    if (overflow_dropped) *overflow_dropped = stream ? stream->overflow_dropped : 0U;
    if (invalid_dropped) *invalid_dropped = stream ? stream->invalid_dropped : 0U;
    if (queued) *queued = stream ? stream->count : 0U;
    if (overflow_by_radio) {
        if (stream) memcpy(overflow_by_radio, stream->overflow_by_radio,
            sizeof(stream->overflow_by_radio));
        else memset(overflow_by_radio, 0, 6U * sizeof(uint32_t));
    }
    xSemaphoreGive(state_lock);
    return read;
}
esp_err_t field_survey_save(char *name, size_t capacity)
{
    /* Export a bounded snapshot through the existing SD/USB ownership service.
     * Hex SSID preserves arbitrary bytes and cannot create spreadsheet formulas. */
    field_survey_snapshot_t *copy = malloc(sizeof(*copy));
    char *csv = malloc(4096);
    if (!copy || !csv) { free(copy); free(csv); return ESP_ERR_NO_MEM; }
    field_survey_snapshot(copy);
    size_t used = (size_t)snprintf(csv, 4096, "# Cerberus field survey; GPS unavailable; bounded snapshot\nradio,bssid,channel,rssi,security,ssid_hex\n");
    esp_err_t err = ESP_OK;
    for (unsigned i = 0; i < copy->count; ++i) {
        expansion_record_t *r = &copy->records[i]; char hex[65];
        unsigned j;
        for (j = 0; j < r->ssid_length; ++j) snprintf(hex + j * 2, 3, "%02X", (unsigned char)r->ssid[j]);
        hex[j * 2] = 0;
        int n = snprintf(csv + used, 4096 - used, "%u,%02X:%02X:%02X:%02X:%02X:%02X,%u,%d,%u,%s\n",
            r->radio, r->bssid[0], r->bssid[1], r->bssid[2], r->bssid[3], r->bssid[4], r->bssid[5],
            r->channel, r->rssi, r->security, hex);
        if (n < 0 || (size_t)n >= 4096 - used) { err = ESP_ERR_INVALID_SIZE; break; }
        used += n;
    }
    if (err == ESP_OK) err = cerberus_storage_write_wifi_report(csv, used, name, capacity);
    free(csv); free(copy); return err;
}
