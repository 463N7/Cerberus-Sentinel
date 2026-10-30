#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "wardrive_core.h"
#include "wardrive_service.h"

#define CERBERUS_WIFI_SCAN_MAX_RESULTS 4U
#define CERBERUS_WIFI_SCAN_COMPLETE 2U
#define CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING 1U
#define WARDRIVE_C5_STATUS_GRACE_MS 12000U

typedef struct {
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    uint8_t security;
    char ssid[33];
} cerberus_c5_wifi_result_t;

typedef struct {
    uint8_t state, generation, stored_count;
    uint16_t total_count;
} cerberus_wifi_scan_status_payload_t;
typedef struct { uint8_t state; } cerberus_wifi_channel_survey_status_payload_t;
typedef struct { cerberus_wifi_probe_status_t status; uint32_t peer; } cerberus_c5_wifi_probe_bank_t;
typedef bool (*cerberus_c5_probe_continue_fn)(void *);
typedef struct { cerberus_wifi_relation_status_t status; uint32_t peer; } cerberus_c5_wifi_relation_bank_t;
typedef bool (*cerberus_c5_relation_continue_fn)(void *);

static cerberus_c5_wifi_result_t wifi_stage[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static cerberus_c5_wifi_result_t sample[2];
static cerberus_wardrive_dedupe_t dedupe;
static cerberus_wardrive_probe_dedupe_t probe_dedupe;
static cerberus_wardrive_relation_dedupe_t relation_dedupe;
static cerberus_wifi_relation_row_t relation_stage[CERBERUS_WIFI_RELATION_LIMIT], relation_sample[CERBERUS_WIFI_RELATION_LIMIT];
static cerberus_c5_wifi_relation_bank_t relation_bank;
static uint8_t relation_capture_next, relation_count;
static cerberus_wardrive_relation_state_t relation_capture_state;
static esp_err_t relation_copy_error, relation_fence_error;
static unsigned relation_copies, relation_fences, append_attempts, storage_fail_at;
static bool relation_unavailable, stop_during_relations;
static uint64_t relation_epoch;
static cerberus_wifi_probe_row_t probe_stage[CERBERUS_WIFI_PROBE_LIMIT], probe_sample[CERBERUS_WIFI_PROBE_LIMIT];
static cerberus_c5_wifi_probe_bank_t probe_bank;
static bool wifi_capture_ready;
static uint8_t wifi_capture_generation,wifi_capture_count,wifi_capture_next,probe_capture_next;
static cerberus_wardrive_probe_state_t probe_capture_state;
static cerberus_wardrive_status_t wardrive_status;
static uint8_t probe_count;
static esp_err_t probe_copy_error,probe_fence_error;
static unsigned probe_copies,probe_fences,stop_after_row;
static bool probe_unavailable,stop_during_copy;
static uint64_t probe_epoch;
static int wardrive_lock;
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
static uint32_t plus_one(uint32_t v){return v==UINT32_MAX?v:v+1U;}
static uint32_t plus_saturated(uint32_t v,uint32_t n){return n>UINT32_MAX-v?UINT32_MAX:v+n;}
static uint32_t clock_ms;
static int result_timeout_at, result_timeouts, status_timeouts, storage_timeouts;
static int survey_timeouts;
static unsigned result_calls, status_calls, survey_calls, rows, duplicates;
static bool stopping, stop_after_first_row;
static uint8_t status_generation, status_count;

static uint32_t now_ms(void) { return clock_ms; }
static bool wants_stop(void) { return stopping; }
static esp_err_t c5_spi_link_copy_wifi_probe_results(uint8_t generation,uint32_t deadline,
    cerberus_c5_wifi_probe_bank_t *bank,cerberus_wifi_probe_row_t *out,size_t capacity,
    cerberus_c5_probe_continue_fn keep_going,void *context)
{
    assert(generation==7U&&capacity==CERBERUS_WIFI_PROBE_LIMIT);
    assert((int32_t)(deadline-clock_ms)>0);
    ++probe_copies;++clock_ms;memset(bank,0,sizeof(*bank));memset(out,0,capacity*sizeof(*out));
    if(stop_during_copy){stopping=true;stop_during_copy=false;}
    if(!keep_going(context))return ESP_ERR_NOT_FINISHED;
    if(probe_copy_error)return probe_copy_error;
    bank->peer=42;bank->status=(cerberus_wifi_probe_status_t){.epoch=probe_epoch,.generation=generation,
        .count=probe_unavailable?0:probe_count,.state=CERBERUS_WIFI_PROBE_COMPLETE,
        .flags=probe_unavailable?0:CERBERUS_WIFI_PROBE_AVAILABLE};
    for(unsigned i=0;i<bank->status.count;++i){out[i]=probe_sample[i];bank->status.matched_frames+=out[i].frames;}
    return ESP_OK;
}
static esp_err_t c5_spi_link_check_wifi_probe_bank(const cerberus_c5_wifi_probe_bank_t *bank,uint32_t deadline)
{
    assert(bank->peer==42&&bank->status.epoch==probe_epoch);
    assert((int32_t)(deadline-clock_ms)>0);++probe_fences;++clock_ms;
    return probe_fence_error;
}
static esp_err_t c5_spi_link_copy_wifi_relations(uint8_t generation,uint32_t deadline,
    cerberus_c5_wifi_relation_bank_t *bank,cerberus_wifi_relation_row_t *out,size_t capacity,
    cerberus_c5_relation_continue_fn keep_going,void *context)
{
    assert(generation==7U&&capacity==CERBERUS_WIFI_RELATION_LIMIT);
    assert((int32_t)(deadline-clock_ms)>0);
    ++relation_copies;++clock_ms;memset(bank,0,sizeof(*bank));memset(out,0,capacity*sizeof(*out));
    if(stop_during_relations){stopping=true;stop_during_relations=false;}
    if(!keep_going(context))return ESP_ERR_NOT_FINISHED;
    if(relation_copy_error)return relation_copy_error;
    bank->peer=43;bank->status=(cerberus_wifi_relation_status_t){.epoch=relation_epoch,.generation=generation,
        .count=relation_unavailable?0:relation_count,.state=CERBERUS_WIFI_RELATION_COMPLETE,
        .flags=relation_unavailable?0:CERBERUS_WIFI_RELATION_AVAILABLE};
    for(unsigned i=0;i<bank->status.count;++i){
        out[i]=relation_sample[i];bank->status.matched_frames+=out[i].to_ap_frames+out[i].from_ap_frames;
    }
    return ESP_OK;
}
static esp_err_t c5_spi_link_check_wifi_relation_bank(const cerberus_c5_wifi_relation_bank_t *bank,uint32_t deadline)
{
    assert(bank->peer==43&&bank->status.epoch==relation_epoch);
    assert((int32_t)(deadline-clock_ms)>0);++relation_fences;++clock_ms;
    return relation_fence_error;
}
static esp_err_t c5_spi_link_get_wifi_scan_result(uint8_t generation,
    uint8_t index, cerberus_c5_wifi_result_t *out)
{
    assert(generation == 7U && index < 2U);
    ++result_calls;
    ++clock_ms;
    if ((int)index == result_timeout_at && result_timeouts != 0) {
        if (result_timeouts > 0) --result_timeouts;
        return ESP_ERR_TIMEOUT;
    }
    *out = sample[index];
    return ESP_OK;
}
static esp_err_t c5_spi_link_get_wifi_scan_status(
    cerberus_wifi_scan_status_payload_t *out)
{
    ++status_calls;
    ++clock_ms;
    if (status_timeouts != 0) {
        if (status_timeouts > 0) --status_timeouts;
        return ESP_ERR_TIMEOUT;
    }
    *out = (cerberus_wifi_scan_status_payload_t){
        .state = CERBERUS_WIFI_SCAN_COMPLETE,
        .generation = status_generation,
        .stored_count = status_count,
    };
    return ESP_OK;
}
static esp_err_t c5_spi_link_wifi_channel_survey_status(
    cerberus_wifi_channel_survey_status_payload_t *out)
{
    ++survey_calls;
    ++clock_ms;
    if (survey_timeouts != 0) {
        if (survey_timeouts > 0) --survey_timeouts;
        return ESP_ERR_TIMEOUT;
    }
    out->state = 0U;
    return ESP_OK;
}
static esp_err_t append_observation(const cerberus_wardrive_observation_t *observation)
{
    bool probe=observation->type==CERBERUS_WARDRIVE_WIFI_PROBE;
    bool relation=observation->type==CERBERUS_WARDRIVE_WIFI_RELATION;
    cerberus_wardrive_decision_t decision = relation?
        cerberus_wardrive_relation_decide(&relation_dedupe,observation,clock_ms):probe?
        cerberus_wardrive_probe_decide(&probe_dedupe,observation,clock_ms):
        cerberus_wardrive_decide(&dedupe, observation, clock_ms);
    assert(cerberus_wardrive_observation_valid(observation));
    ++append_attempts;
    if (!decision.should_log) { ++duplicates; return ESP_OK; }
    if(storage_fail_at&&append_attempts==storage_fail_at)return ESP_ERR_TIMEOUT;
    if (storage_timeouts != 0) {
        if (storage_timeouts > 0) --storage_timeouts;
        return ESP_ERR_TIMEOUT;
    }
    ++rows;
    if(relation)cerberus_wardrive_relation_commit(&relation_dedupe,observation,decision,clock_ms);
    else if(probe)cerberus_wardrive_probe_commit(&probe_dedupe,observation,decision,clock_ms);
    else cerberus_wardrive_commit(&dedupe, observation, decision, clock_ms);
    if ((stop_after_first_row && rows == 1U)||(stop_after_row&&rows==stop_after_row)) stopping = true;
    return ESP_OK;
}

#include "wardrive_wifi_capture_impl.h"

static void reset(void)
{
    memset(wifi_stage, 0, sizeof(wifi_stage));
    memset(sample, 0, sizeof(sample));
    cerberus_wardrive_dedupe_reset(&dedupe);
    cerberus_wardrive_probe_dedupe_reset(&probe_dedupe);
    cerberus_wardrive_relation_dedupe_reset(&relation_dedupe);
    memset(&wardrive_status,0,sizeof(wardrive_status));
    memset(probe_sample,0,sizeof(probe_sample));memset(probe_stage,0,sizeof(probe_stage));
    reset_wifi_capture();probe_count=0;probe_copy_error=probe_fence_error=ESP_OK;
    probe_copies=probe_fences=stop_after_row=0;probe_unavailable=stop_during_copy=false;probe_epoch=123;
    relation_copies=relation_fences=append_attempts=storage_fail_at=0;
    relation_count=0;relation_unavailable=stop_during_relations=false;relation_epoch=124;
    relation_copy_error=relation_fence_error=ESP_OK;
    memset(relation_stage,0,sizeof(relation_stage));memset(relation_sample,0,sizeof(relation_sample));
    sample[0].bssid[5] = 1U;
    sample[1].bssid[5] = 2U;
    for (unsigned i = 0U; i < 2U; ++i) {
        sample[i].channel = 6U;
        sample[i].rssi = -50;
        memcpy(sample[i].ssid, "Lab", 4U);
    }
    clock_ms = result_calls = status_calls = survey_calls = rows = duplicates = 0U;
    result_timeout_at = -1;
    result_timeouts = status_timeouts = storage_timeouts = 0;
    survey_timeouts = 0;
    stopping = stop_after_first_row = false;
    status_generation = 7U;
    status_count = 2U;
}

static void probes(unsigned count)
{
    assert(count<=CERBERUS_WIFI_PROBE_LIMIT);probe_count=(uint8_t)count;
    for(unsigned i=0;i<count;++i){
        probe_sample[i]=(cerberus_wifi_probe_row_t){.transmitter={0x10,2,3,4,5,(uint8_t)i},
            .ssid={'F','l','o','c','k'},.ssid_length=5,.channel=6,.rssi=-60,
            .name_bits=CERBERUS_WIFI_PROBE_NAME_FLOCK,.callback_ms=i,.frames=2};
        assert(cerberus_wifi_probe_row_valid(&probe_sample[i]));
    }
}

static void relations(unsigned count)
{
    assert(count<=CERBERUS_WIFI_RELATION_LIMIT);relation_count=(uint8_t)count;
    for(unsigned i=0;i<count;++i){
        relation_sample[i]=(cerberus_wifi_relation_row_t){.bssid={0x10,1,2,3,4,5},
            .station={2,6,7,8,9,(uint8_t)i},.channel=6,.directions=3,.latest_direction=2,
            .rssi_valid=3,.to_ap_frames=2,.from_ap_frames=3,.to_ap_callback_us=i,
            .from_ap_callback_us=UINT64_C(4294967296000)+i,.to_ap_rssi=-80,.from_ap_rssi=-30};
        assert(cerberus_wifi_relation_row_valid(&relation_sample[i]));
    }
}

int main(void)
{
    bool captured = false;
    char radio = '?';
    esp_err_t error;

    reset();
    result_timeout_at = 1;
    result_timeouts = 1;
    error = capture_wifi_with_retry(7U, 2U, false, 12000U, &captured, &radio);
    assert(error == ESP_OK && !captured && radio == 'C' && rows == 0U);
    error = capture_wifi_with_retry(7U, 2U, false, 12000U, &captured, &radio);
    assert(error == ESP_OK && captured && rows == 2U);

    reset();
    status_timeouts = 1;
    error = capture_wifi_with_retry(7U, 2U, false, 12000U, &captured, &radio);
    assert(error == ESP_OK && !captured && rows == 0U);
    error = capture_wifi_with_retry(7U, 2U, false, 12000U, &captured, &radio);
    assert(error == ESP_OK && captured && rows == 2U);

    reset();
    storage_timeouts = 1;
    error = capture_wifi_with_retry(7U, 2U, false, 12000U, &captured, &radio);
    assert(error == ESP_ERR_TIMEOUT && !captured && radio == 'S' && rows == 0U);

    reset();
    stop_after_first_row = true;
    error = capture_wifi_with_retry(7U, 2U, false, 12000U, &captured, &radio);
    assert(error == ESP_OK && !captured && stopping && rows == 1U);
    error = capture_wifi_with_retry(7U, 2U, true, 12000U, &captured, &radio);
    assert(error == ESP_OK && captured && rows == 2U && duplicates == 0U&&probe_copies==1U);

    reset();
    result_timeout_at = 0;
    result_timeouts = -1;
    for (unsigned attempts = 0U; attempts < 121U; ++attempts) {
        error = capture_wifi_with_retry(7U, 2U, false, 12000U,
            &captured, &radio);
        if (error != ESP_OK) break;
        assert(!captured);
        clock_ms += 100U;
    }
    assert(error == ESP_ERR_TIMEOUT && radio == 'C' && rows == 0U);
    assert(clock_ms >= 12000U && clock_ms <= 12100U);

    reset();
    status_generation = 8U;
    error = capture_wifi_with_retry(7U, 2U, false, 12000U,
        &captured, &radio);
    assert(error == ESP_ERR_INVALID_STATE && radio == 'C' && rows == 0U);

    reset();
    bool ready = false, pending = false;
    uint32_t deadline = 0U;
    survey_timeouts = 1;
    error = wifi_survey_ready(&ready, &pending, &deadline);
    assert(error == ESP_OK && !ready && pending && deadline > clock_ms);
    error = wifi_survey_ready(&ready, &pending, &deadline);
    assert(error == ESP_OK && ready && !pending && survey_calls == 2U);
    reset();
    ready = pending = false;
    deadline = 0U;
    survey_timeouts = -1;
    for (unsigned attempts = 0U; attempts < 121U; ++attempts) {
        error = wifi_survey_ready(&ready, &pending, &deadline);
        if (error != ESP_OK) break;
        assert(!ready && pending);
        clock_ms += 100U;
    }
    assert(error == ESP_ERR_TIMEOUT && !ready && clock_ms >= deadline);

    /* No AP/probe write is possible before the final bank/peer proof. */
    reset();probes(2);probe_fence_error=ESP_ERR_INVALID_RESPONSE;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_ERR_INVALID_RESPONSE);
    assert(!captured&&!rows&&!wifi_capture_ready&&radio=='C'&&probe_fences==1);
    probe_fence_error=ESP_OK;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==4);
    assert(probe_copies==2&&probe_capture_next==2&&wifi_capture_next==2);

    /* Stop in either AP or probe output resumes the exact unprocessed suffix. */
    for(unsigned boundary=1;boundary<=3;++boundary){
        reset();probes(2);stop_after_row=boundary;
        assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&!captured);
        assert(rows==boundary&&stopping&&probe_copies==1);
        /* A later producer bank cannot change the already-fenced suffix. */
        memset(probe_sample,0,sizeof(probe_sample));
        memset(sample,0,sizeof(sample));
        ++probe_epoch;
        cerberus_wifi_scan_status_payload_t completed={.state=2,.generation=7,.stored_count=2,.total_count=3};
        assert(drain_completed_wifi(&completed,12000,&captured,&radio)==ESP_OK&&captured);
        assert(rows==4&&!duplicates&&probe_copies==1&&wardrive_status.c5_wifi_sweeps==1);
        assert(wardrive_status.c5_probe_state==CERBERUS_WARDRIVE_PROBE_AVAILABLE&&
            wardrive_status.c5_probe_frames==4&&wardrive_status.c5_wifi_scan_dropped==1);
    }
    reset();probes(2);stop_during_copy=true;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&!captured&&!rows&&!wifi_capture_ready);
    assert(capture_wifi_with_retry(7,2,true,12000,&captured,&radio)==ESP_OK&&captured&&rows==4&&probe_copies==2);

    reset();probes(2);status_count=0;
    assert(capture_wifi_with_retry(7,0,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==2);
    assert(result_calls==0&&probe_capture_next==2&&wifi_capture_next==0);
    reset();probes(2);probe_copy_error=ESP_ERR_INVALID_RESPONSE;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_ERR_INVALID_RESPONSE);
    assert(!captured&&!rows&&!wifi_capture_ready&&!result_calls&&radio=='C');

    /* Old or unavailable collection does not fabricate zero-detection support. */
    for(unsigned unavailable=0;unavailable<2;++unavailable){
        reset();probes(2);
        if(unavailable)probe_unavailable=true;else probe_copy_error=ESP_ERR_NOT_SUPPORTED;
        assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==2);
        cerberus_wifi_scan_status_payload_t completed={.state=2,.generation=7,.stored_count=2};
        publish_completed_wifi(&completed);
        assert(wardrive_status.c5_probe_state==(unavailable?CERBERUS_WARDRIVE_PROBE_UNAVAILABLE:CERBERUS_WARDRIVE_PROBE_UNSUPPORTED));
        assert(wardrive_status.c5_probe_unavailable_sweeps==1&&!wardrive_status.c5_probe_frames);
    }
    reset();probes(16);
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==18);
    assert(probe_dedupe.used==16&&probe_copies==1);
    /* An accepted new START resets the bank even if the public generation wraps. */
    reset_wifi_capture();++probe_epoch;probe_sample[0].rssi=-50;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&captured&&probe_copies==2&&rows==19);
    wardrive_status.c5_probe_frames=UINT32_MAX-1U;
    probe_bank.status.dropped_frames=4;wardrive_status.c5_probe_dropped=UINT32_MAX-1U;
    cerberus_wifi_scan_status_payload_t completed={.state=2,.generation=7,.stored_count=2};
    publish_completed_wifi(&completed);
    assert(wardrive_status.c5_probe_frames==UINT32_MAX&&wardrive_status.c5_probe_dropped==UINT32_MAX);

    /* All three source banks must validate before the first SD row. */
    reset();probes(2);relations(3);relation_fence_error=ESP_ERR_INVALID_RESPONSE;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_ERR_INVALID_RESPONSE);
    assert(!captured&&!rows&&!wifi_capture_ready&&radio=='C'&&result_calls==2&&probe_fences==1);
    relation_fence_error=ESP_OK;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==7);
    assert(relation_copies==2&&relation_fences==2&&relation_dedupe.used==3);
    reset();relations(2);relation_sample[1].reserved[0]=1;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_ERR_INVALID_RESPONSE);
    assert(!rows&&!result_calls&&!wifi_capture_ready&&radio=='C');
    for(unsigned prefix=1;prefix<7;++prefix){
        reset();probes(2);relations(3);stop_after_row=prefix;
        assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&!captured);
        assert(rows==prefix&&!duplicates&&wifi_capture_ready);
        cerberus_wifi_relation_row_t frozen=relation_stage[2];
        relation_sample[2].from_ap_rssi=-5; /* Later producer change cannot replace staged evidence. */
        assert(drain_completed_wifi(&completed,12000,&captured,&radio)==ESP_OK&&captured);
        assert(rows==7&&!duplicates&&probe_copies==1&&relation_copies==1&&relation_fences==1);
        assert(!memcmp(&frozen,&relation_stage[2],sizeof(frozen)));
        assert(wardrive_status.c5_wifi_sweeps==1&&wardrive_status.c5_relation_frames==15&&
            wardrive_status.c5_relation_state==CERBERUS_WARDRIVE_RELATION_AVAILABLE);
    }
    reset();probes(2);relations(2);stop_during_relations=true;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&!captured&&!rows&&!wifi_capture_ready);
    assert(capture_wifi_with_retry(7,2,true,12000,&captured,&radio)==ESP_OK&&captured&&rows==6&&relation_copies==2);
    reset();relations(2);storage_fail_at=3;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_ERR_TIMEOUT&&radio=='S');
    assert(rows==2&&!relation_capture_next&&!relation_dedupe.used&&wifi_capture_ready);
    assert(capture_wifi_with_retry(7,2,true,12000,&captured,&radio)==ESP_OK&&captured&&rows==4);
    assert(!duplicates&&relation_dedupe.used==2&&relation_copies==1);
    reset();relations(1);relation_copy_error=ESP_ERR_TIMEOUT;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&!captured&&!rows&&radio=='C');
    relation_copy_error=ESP_OK;
    assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==3);
    for(unsigned unavailable=0;unavailable<2;++unavailable){
        reset();relations(2);
        if(unavailable)relation_unavailable=true;else relation_copy_error=ESP_ERR_NOT_SUPPORTED;
        assert(capture_wifi_with_retry(7,2,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==2);
        publish_completed_wifi(&completed);
        assert(wardrive_status.c5_relation_state==(unavailable?CERBERUS_WARDRIVE_RELATION_UNAVAILABLE:CERBERUS_WARDRIVE_RELATION_UNSUPPORTED));
        assert(wardrive_status.c5_relation_unavailable_sweeps==1&&!wardrive_status.c5_relation_frames);
    }
    reset();relations(32);status_count=0;
    assert(capture_wifi_with_retry(7,0,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==32);
    assert(!result_calls&&relation_dedupe.used==32);
    reset_wifi_capture();++relation_epoch;relation_sample[0].from_ap_rssi=-27;
    assert(capture_wifi_with_retry(7,0,false,12000,&captured,&radio)==ESP_OK&&captured&&rows==33&&relation_copies==2);
    relation_bank.status.dropped_frames=9;
    wardrive_status.c5_relation_frames=wardrive_status.c5_relation_dropped=UINT32_MAX-1U;
    publish_completed_wifi(&completed);
    assert(wardrive_status.c5_relation_frames==UINT32_MAX&&wardrive_status.c5_relation_dropped==UINT32_MAX);

    puts("wardrive AP/probe/relation capture: whole-bank staging/fences, retry, availability, Stop suffix, append-only commit and bounded metrics pass");
    return 0;
}
