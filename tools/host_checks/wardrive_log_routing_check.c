#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wardrive_core.h"
#include "wardrive_wigle.h"
#include "esp_err.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(ms) (ms)
static int wardrive_lock;
static uint64_t clock_ms = 10000U, started_ms = 10000U;
static uint64_t now_ms64(void) { return clock_ms; }
static void vTaskDelay(unsigned ms) { clock_ms += ms; }
static uint32_t plus_one(uint32_t v) { return v == UINT32_MAX ? v : v + 1U; }
static cerberus_wardrive_dedupe_t dedupe, wigle_dedupe;
static cerberus_wardrive_probe_dedupe_t probe_dedupe;
static cerberus_wardrive_relation_dedupe_t relation_dedupe;
static cerberus_wardrive_gps_snapshot_t gps_fixture;
static const char wardrive_header[] = CERBERUS_WARDRIVE_UNLOCATED_HEADER;
static const char wigle_header[] = CERBERUS_WIGLE_METADATA_HEADER CERBERUS_WIGLE_COLUMN_HEADER;
static char row_buffer[384], last_row[2][384];
static bool wardrive_file_open, wigle_file_open, storage_open[2];
static uint32_t open_file_rows, wigle_file_rows;
static unsigned appends[2], begins[2], closes[2], errors, invalid, duplicates;
static esp_err_t fail_append;
static struct {
    bool gps_requested;
    uint32_t files_saved, geotagged_rows, unlocated_rows, gps_fallback_rows;
    uint64_t sd_bytes;
    char last_file_name[24];
} wardrive_status;
static void publish_error(esp_err_t error, char source)
{ assert(error != ESP_OK && source == 'S'); ++errors; }
static void publish_invalid(void) { ++invalid; }
static void publish_sighting(const cerberus_wardrive_observation_t *o, bool logged)
{ (void)o; if (!logged) ++duplicates; }
static void snapshot_optional_gps(bool enabled,
    cerberus_wardrive_gps_snapshot_t *gps, esp_err_t *error)
{ *gps = enabled ? gps_fixture : (cerberus_wardrive_gps_snapshot_t){0}; if (error) *error = ESP_OK; }
static esp_err_t begin(unsigned which, const char *header, size_t length)
{ assert(!storage_open[which] && length == strlen(header)); storage_open[which] = true; ++begins[which]; return ESP_OK; }
static esp_err_t append(unsigned which, const char *row, size_t length)
{
    assert(storage_open[which] && length == strlen(row));
    if (fail_append) { esp_err_t error = fail_append; fail_append = ESP_OK; return error; }
    memcpy(last_row[which], row, length + 1U); ++appends[which]; return ESP_OK;
}
static esp_err_t finish(unsigned which, bool keep, char *name, size_t capacity, uint64_t *bytes)
{
    assert(storage_open[which] && keep && capacity >= 13U);
    storage_open[which] = false; ++closes[which];
    snprintf(name, capacity, "%s001.CSV", which ? "WIGLE" : "UNLOC");
    if (bytes) *bytes = 1U;
    return ESP_OK;
}
static esp_err_t cerberus_storage_wardrive_begin(const char *h, size_t n) { return begin(0U,h,n); }
static esp_err_t cerberus_storage_wardrive_wigle_begin(const char *h, size_t n) { return begin(1U,h,n); }
static esp_err_t cerberus_storage_wardrive_append(const char *h, size_t n) { return append(0U,h,n); }
static esp_err_t cerberus_storage_wardrive_wigle_append(const char *h, size_t n) { return append(1U,h,n); }
static esp_err_t cerberus_storage_wardrive_finish(bool keep, char *n, size_t c, uint64_t *b) { return finish(0U,keep,n,c,b); }
static esp_err_t cerberus_storage_wardrive_wigle_finish(bool keep, char *n, size_t c, uint64_t *b) { return finish(1U,keep,n,c,b); }
static esp_err_t cerberus_storage_wardrive_session_finish(void)
{ assert(!storage_open[0] && !storage_open[1]); return ESP_OK; }

#include "wardrive_log_routing_impl.h"

int main(void)
{
    cerberus_wardrive_observation_t ap = {
        .type = CERBERUS_WARDRIVE_WIFI, .radio = CERBERUS_WARDRIVE_RADIO_C5,
        .address = {2,1,2,3,4,5}, .channel = 6, .rssi_dbm = -50,
        .label = {'L','a','b'}, .label_length = 3,
    };
    assert(append_observation(&ap) == ESP_OK);
    assert(appends[0] == 1U && appends[1] == 0U && begins[1] == 0U);
    assert(append_observation(&ap) == ESP_OK && appends[0] == 1U);
    wardrive_status.gps_requested = true;
    gps_fixture = (cerberus_wardrive_gps_snapshot_t){
        .state = CERBERUS_WARDRIVE_GPS_FRESH, .coordinates_valid = true,
        .latitude = 31.0, .longitude = -80.0, .utc_valid = true,
        .utc_from_rmc = true, .utc = "2026-10-30T03:00:00.000Z",
    };
    assert(append_observation(&ap) == ESP_OK); /* first fix bypasses unlocated history */
    assert(appends[0] == 1U && appends[1] == 1U);
    assert(strstr(last_row[1], ",31.0000000,-80.0000000,") != NULL);
    gps_fixture.state = CERBERUS_WARDRIVE_GPS_STALE;
    ap.address[5] = 6;
    assert(append_observation(&ap) == ESP_OK && appends[0] == 2U && appends[1] == 1U);
    assert(strstr(last_row[0], "31.0000000") == NULL);
    gps_fixture.state = CERBERUS_WARDRIVE_GPS_FRESH;
    assert(append_observation(&ap) == ESP_OK && appends[1] == 2U);
    ap.address[5] = 7; gps_fixture.utc_valid = false;
    assert(append_observation(&ap) == ESP_OK && appends[0] == 3U);
    gps_fixture.utc_valid = true; ap.address[5] = 8; ap.label[0] = 0xffU;
    assert(append_observation(&ap) == ESP_OK && appends[0] == 4U); /* raw SSID preserved */
    ap.label[0] = 'L'; ap.address[5] = 9; fail_append = ESP_FAIL;
    assert(append_observation(&ap) == ESP_FAIL);
    assert(append_observation(&ap) == ESP_OK && appends[1] == 3U); /* no false dedupe */
    ap.address[5] = 10; fail_append = ESP_ERR_INVALID_SIZE;
    assert(append_observation(&ap) == ESP_OK && begins[1] == 2U && closes[1] == 1U);
    ap.type = CERBERUS_WARDRIVE_BLE; ap.radio = CERBERUS_WARDRIVE_RADIO_S3;
    ap.channel = 0; ap.manufacturer_id = 76U; ap.address[5] = 11;
    assert(append_observation(&ap) == ESP_OK);
    assert(strstr(last_row[1], ",[BLE],") && strstr(last_row[1], ",0,,-50,") &&
        strstr(last_row[1], ",76,BLE\n"));
    assert(wardrive_status.gps_fallback_rows == 3U &&
        wardrive_status.geotagged_rows == 5U && wardrive_status.unlocated_rows == 4U);
    assert(save_open_file(true) == ESP_OK);
    assert(save_log_stream(true, true) == ESP_OK);
    finish_storage_session();
    assert(errors == 1U && invalid == 0U && duplicates == 1U);
    /* A new unlocated hint must survive an unchanged RSSI and a failed write;
     * the located stream retains its existing WiGLE schema and dedupe policy. */
    wardrive_status.gps_requested=false;ap.address[5]=22;
    assert(append_observation(&ap)==ESP_OK);
    unsigned original=appends[0];
    ap.ble_evidence=(cerberus_s3_ble_wardrive_evidence_t){.flags=1,
        .advertisement_sequence=1,.advertisement_observed_ms=123,
        .advertisement={.facts=CERBERUS_BLE_FLOCK_FS_BATTERY}};
    fail_append=ESP_FAIL;assert(append_observation(&ap)==ESP_FAIL&&appends[0]==original);
    assert(append_observation(&ap)==ESP_OK&&appends[0]==original+1);
    assert(strstr(last_row[0],",0,2,01000000,1,123,,,,,,,,,,,,,,,\n"));
    ap.ble_evidence.advertisement_observed_ms=999;
    assert(append_observation(&ap)==ESP_OK&&appends[0]==original+1);
    wardrive_status.gps_requested=true;
    assert(append_observation(&ap)==ESP_OK);
    unsigned located=appends[1];
    ap.ble_evidence.advertisement.facts |= CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
    assert(append_observation(&ap)==ESP_OK&&appends[1]==located);
    assert(strstr(last_row[1],",76,BLE\n"));
    assert(save_open_file(true)==ESP_OK&&save_log_stream(true,true)==ESP_OK);
    finish_storage_session();
    /* Probes always take the evidence stream, including a valid receiver GPS
     * fix. Their exact requested-name history is independent of AP/BLE banks. */
    cerberus_wifi_probe_row_t source={.transmitter={2,1,2,3,4,22},
        .ssid={'F','l','o','c','k'},.ssid_length=5,.channel=6,.rssi=-55,
        .name_bits=CERBERUS_WIFI_PROBE_NAME_FLOCK,.frames=4,.callback_ms=UINT64_C(4294967297)};
    cerberus_wardrive_observation_t probe;
    assert(cerberus_wardrive_observation_from_probe(&source,&probe));
    cerberus_wardrive_dedupe_t old_plain=dedupe,old_located=wigle_dedupe;
    cerberus_wardrive_probe_dedupe_t before=probe_dedupe;
    const unsigned unlocated_before=appends[0],located_before=appends[1],
        fallback_before=wardrive_status.gps_fallback_rows,located_begins=begins[1];
    unsigned errors_before=errors;
    assert(wardrive_status.gps_requested&&gps_fixture.state==CERBERUS_WARDRIVE_GPS_FRESH);
    fail_append=ESP_FAIL;
    assert(append_observation(&probe)==ESP_FAIL&&errors==errors_before+1);
    assert(appends[0]==unlocated_before&&!memcmp(&probe_dedupe,&before,sizeof(before)));
    assert(append_observation(&probe)==ESP_OK&&appends[0]==unlocated_before+1&&probe_dedupe.used==1);
    assert(appends[1]==located_before&&begins[1]==located_begins&&wardrive_status.gps_fallback_rows==fallback_before);
    assert(!memcmp(&dedupe,&old_plain,sizeof(dedupe))&&!memcmp(&wigle_dedupe,&old_located,sizeof(wigle_dedupe)));
    assert(strstr(last_row[0],",wifi_probe,C5,02:01:02:03:04:16,,6,-55,,,,,fresh,")&&
        strstr(last_row[0],",466C6F636B,1,0,4294967297,4,,,,,\n")&&!strstr(last_row[0],"31.0000000"));
    unsigned duplicates_before=duplicates;
    assert(append_observation(&probe)==ESP_OK&&appends[0]==unlocated_before+1&&duplicates==duplicates_before+1);
    probe.probe_evidence.observed_ms_low++;probe.probe_evidence.count++;
    assert(append_observation(&probe)==ESP_OK&&appends[0]==unlocated_before+1);
    /* A different requested SSID, including its embedded NUL, is a new key. */
    probe.label[5]=0;probe.label_length=6;
    assert(append_observation(&probe)==ESP_OK&&appends[0]==unlocated_before+2&&probe_dedupe.used==2);
    assert(strstr(last_row[0],",466C6F636B00,1,0,4294967298,5,,,,,\n"));
    before=probe_dedupe;probe.label[6]='X';probe.label_length=7;fail_append=ESP_ERR_INVALID_SIZE;
    assert(append_observation(&probe)==ESP_OK&&probe_dedupe.used==before.used+1&&appends[0]==unlocated_before+3);
    assert(appends[1]==located_before&&wardrive_status.gps_fallback_rows==fallback_before);
    before=probe_dedupe;unsigned invalid_before=invalid;probe.probe_evidence.oui_clue=1;
    assert(append_observation(&probe)==ESP_OK&&invalid==invalid_before+1&&
        !memcmp(&before,&probe_dedupe,sizeof(before))&&appends[0]==unlocated_before+3);
    assert(save_open_file(true)==ESP_OK);finish_storage_session();
    {
    cerberus_wifi_relation_row_t relation_source={.bssid={0x10,1,2,3,4,5},.station={2,6,7,8,9,10},
        .channel=6,.directions=3,.latest_direction=2,.rssi_valid=3,
        .to_ap_frames=3,.from_ap_frames=4,.to_ap_callback_us=123,
        .from_ap_callback_us=UINT64_C(4294967297001),.to_ap_rssi=-80,.from_ap_rssi=-30};
    cerberus_wardrive_observation_t relation;
    assert(cerberus_wardrive_observation_from_relation(&relation_source,&relation));
    assert(wardrive_status.gps_requested&&gps_fixture.state==CERBERUS_WARDRIVE_GPS_FRESH);
    unsigned plain_before=appends[0],located_before=appends[1],fallback_before=wardrive_status.gps_fallback_rows;
    cerberus_wardrive_relation_dedupe_t relation_before=relation_dedupe;
    fail_append=ESP_FAIL;
    assert(append_observation(&relation)==ESP_FAIL&&!memcmp(&relation_before,&relation_dedupe,sizeof(relation_before)));
    assert(appends[0]==plain_before&&appends[1]==located_before);
    assert(append_observation(&relation)==ESP_OK&&appends[0]==plain_before+1&&appends[1]==located_before);
    assert(wardrive_status.gps_fallback_rows==fallback_before&&relation_dedupe.used==1);
    assert(strstr(last_row[0],",wifi_relation,C5,02:06:07:08:09:0A,,6,-30,,,,,fresh,")&&
        strstr(last_row[0],",10:01:02:03:04:05,FROM_AP,4294967297001,3,4\n")&&!strstr(last_row[0],"31.0000000"));
    assert(append_observation(&relation)==ESP_OK&&appends[0]==plain_before+1);
    relation.relation_evidence.callback_us_low++;relation.relation_evidence.from_ap_frames++;
    assert(append_observation(&relation)==ESP_OK&&appends[0]==plain_before+1);
    relation.channel=11;fail_append=ESP_ERR_INVALID_SIZE;
    assert(append_observation(&relation)==ESP_OK&&appends[0]==plain_before+2&&relation_dedupe.used==2);
    assert(appends[1]==located_before&&wardrive_status.gps_fallback_rows==fallback_before);
    char untouched[384]="unchanged";size_t written=123;
    cerberus_wigle_time_provenance_t timing={.source=CERBERUS_WIGLE_TIME_RESULT_RECEIPT,
        .result_received_monotonic_ms=clock_ms,.gps_snapshot_monotonic_ms=clock_ms};
    assert(cerberus_wardrive_wigle_format_observation(&relation,&gps_fixture,&timing,
        untouched,sizeof(untouched),&written)==CERBERUS_WIGLE_INVALID_ARG);
    assert(!strcmp(untouched,"unchanged")&&written==123);
    }
    puts("wardrive routing: GPS recovery, separate probe/relation histories, unlocated-only evidence, SD retry and rotation passed");
    return 0;
}
