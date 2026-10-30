#include "field_survey_view.h"
#include "field_survey.h"
#include "display.h"
#include "esp_check.h"
#include <stdio.h>
#include <string.h>
static field_survey_snapshot_t snapshot;
static unsigned page;
static char message[48] = "GPS unavailable / local snapshot only";
esp_err_t field_survey_view_draw(const cerberus_ui_widget_theme_t *t)
{
    field_survey_snapshot(&snapshot);
    char line[64];
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 54, "FIELD SURVEY / US / R1-R6", t->text, t->background, 1), "survey", "title");
    snprintf(line, sizeof(line), "%s  %u/32 cached  %lu seen",
        snapshot.requested ? (snapshot.scanning_mask ? "SCANNING" : "STARTING") :
            snapshot.scanning_mask || snapshot.uncertain_mask ? "STOPPING" : "IDLE",
        snapshot.count, (unsigned long)snapshot.observations);
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 72, line, t->text, t->background, 1), "survey", "status");
    for (unsigned i = 0; i < 6; ++i) {
        unsigned bit = 1U << i;
        snprintf(line, sizeof(line), "R%u %s", i + 1,
            snapshot.uncertain_mask & bit ? (snapshot.requested ? "LINK LOST" : "STOP UNCONFIRMED") : snapshot.scanning_mask & bit ? "SCANNING" :
            snapshot.present_mask & bit ? "READY" : "UNSEEN");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14 + (i % 2) * 150, 94 + (i / 2) * 18,
            line, t->muted, t->background, 1), "survey", "radio");
    }
    if (page * 3 >= snapshot.count) page = 0;
    for (unsigned i = 0; i < 3 && page * 3 + i < snapshot.count; ++i) {
        expansion_record_t *r = &snapshot.records[page * 3 + i];
        char name[33]; memcpy(name, r->ssid, 33);
        for (unsigned j = 0; j < r->ssid_length; ++j)
            if ((unsigned char)name[j] < 32 || (unsigned char)name[j] > 126) name[j] = '?';
        snprintf(line, sizeof(line), "%.24s", name[0] ? name : "[hidden SSID]");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 158 + i * 52, line, t->text, t->background, 2), "survey", "ssid");
        snprintf(line, sizeof(line), "R%u  CH %u  %d dBm  SEC %u", r->radio, r->channel, r->rssi, r->security);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 180 + i * 52, line, t->muted, t->background, 1), "survey", "record");
        snprintf(line, sizeof(line), "Vendor: %.38s", r->vendor[0] ? r->vendor : "unknown / SD lookup unavailable");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 194 + i * 52, line, t->muted, t->background, 1), "survey", "vendor");
    }
    snprintf(line, sizeof(line), "Evicted %lu / radio drops %lu", (unsigned long)snapshot.evicted,
        (unsigned long)snapshot.radio_dropped);
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 322, line, t->muted, t->background, 1), "survey", "limits");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 340, message, t->muted, t->background, 1), "survey", "message");
    const char *labels[] = {snapshot.requested ? "STOP" : "START", "PREV", "NEXT", "SAVE"};
    for (unsigned i = 0; i < 4; ++i)
        ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8 + i * 76, 364, 70, 42},
            labels[i], 1, true, t), "survey", "button");
    return ESP_OK;
}
void field_survey_view_tap(uint16_t x, uint16_t y)
{
    if (y < 364 || y >= 406 || x < 8 || x >= 306 || (x - 8) % 76 >= 70) return;
    field_survey_snapshot(&snapshot);
    unsigned action = (x - 8) / 76;
    if (action == 0) {
        esp_err_t err = field_survey_set_running(!snapshot.requested);
        snprintf(message, sizeof(message), "%s", err == ESP_OK ? "GPS unavailable / local snapshot only" : esp_err_to_name(err));
    } else if (action == 1 && page) --page;
    else if (action == 2 && (page + 1) * 3 < snapshot.count) ++page;
    else if (action == 3) {
        char file[32]; esp_err_t err = field_survey_save(file, sizeof(file));
        snprintf(message, sizeof(message), err == ESP_OK ? "Saved LOGS/%s" : "Save failed: %s", err == ESP_OK ? file : esp_err_to_name(err));
    }
}
