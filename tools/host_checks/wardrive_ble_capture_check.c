#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wardrive_core.h"
#include "wardrive_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CERBERUS_BLE_SCAN_MAX_RESULTS 32U
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef struct { uint8_t address[6],address_type;int8_t rssi;uint16_t manufacturer_id;char name[33]; } cerberus_c5_ble_result_t;
static cerberus_c5_ble_result_t samples[32],ble_stage[32];
static cerberus_s3_ble_wardrive_evidence_t evidence[32],ble_evidence_stage[32];
static cerberus_wardrive_dedupe_t dedupe,wigle_dedupe;
static cerberus_wardrive_probe_dedupe_t probe_dedupe;
static cerberus_wardrive_relation_dedupe_t relation_dedupe;
static cerberus_wardrive_status_t wardrive_status;
static cerberus_wardrive_observation_t saved[32];
static unsigned bank_calls,append_calls,saved_count;
static bool stop,stop_after_first;
static bool ble_capture_ready;
static uint8_t ble_capture_generation,ble_capture_count,ble_capture_next;
static esp_err_t bank_error,sd_error;
static int wardrive_lock;
static uint32_t plus_one(uint32_t v){return v==UINT32_MAX?v:v+1;}
static uint32_t plus_saturated(uint32_t v,uint32_t n){return n>UINT32_MAX-v?UINT32_MAX:v+n;}
static bool wants_stop(void){return stop;}
static void publish_sighting(const cerberus_wardrive_observation_t *o,bool logged);
static esp_err_t cerberus_s3_ble_scanner_copy_wardrive(uint8_t generation,uint8_t count,
    cerberus_c5_ble_result_t *rows,cerberus_s3_ble_wardrive_evidence_t *hints,size_t capacity)
{
    ++bank_calls;assert(generation==7&&count<=32&&capacity==32);
    memset(rows,0,capacity*sizeof(*rows));memset(hints,0,capacity*sizeof(*hints));
    /* Caller must not publish even if a failing dependency touched output. */
    memcpy(rows,samples,count*sizeof(*rows));memcpy(hints,evidence,count*sizeof(*hints));
    return bank_error;
}
static esp_err_t append_observation(const cerberus_wardrive_observation_t *o)
{
    ++append_calls;
    if(!cerberus_wardrive_observation_valid(o))return ESP_ERR_INVALID_ARG;
    cerberus_wardrive_decision_t decision=cerberus_wardrive_decide_evidence(&dedupe,o,1234);
    if(decision.should_log){
        if(sd_error)return sd_error;
        assert(saved_count<32);saved[saved_count++]=*o;
        cerberus_wardrive_commit(&dedupe,o,decision,1234);
    }
    publish_sighting(o,decision.should_log);
    if(stop_after_first)stop=true;
    return ESP_OK;
}
#include "wardrive_ble_impl.h"
static void reset(void)
{
    memset(samples,0,sizeof(samples));memset(evidence,0,sizeof(evidence));
    memset(&dedupe,0,sizeof(dedupe));memset(&wigle_dedupe,0,sizeof(wigle_dedupe));
    memset(&probe_dedupe,0,sizeof(probe_dedupe));
    memset(&relation_dedupe,0,sizeof(relation_dedupe));
    memset(&wardrive_status,0,sizeof(wardrive_status));
    bank_calls=append_calls=saved_count=0;stop=stop_after_first=false;bank_error=sd_error=ESP_OK;
    ble_capture_ready=false;ble_capture_next=ble_capture_generation=ble_capture_count=0;
    for(unsigned i=0;i<32;++i){
        samples[i]=(cerberus_c5_ble_result_t){.address={1,2,3,4,5,(uint8_t)(6+i)},.address_type=1,.rssi=-70,.manufacturer_id=2504,.name="Test"};
        evidence[i]=(cerberus_s3_ble_wardrive_evidence_t){.flags=1,.advertisement_sequence=i+1,
            .advertisement_observed_ms=100+i,.advertisement={.facts=CERBERUS_BLE_FLOCK_FS_BATTERY}};
    }
}
int main(void)
{
    reset();assert(capture_ble(7,33,false)==ESP_ERR_INVALID_SIZE&&!bank_calls);
    stop=true;assert(capture_ble(7,2,false)==ESP_OK&&!bank_calls);
    stop=false;bank_error=ESP_ERR_INVALID_STATE;
    assert(capture_ble(7,2,false)==bank_error&&bank_calls==1&&!append_calls&&!saved_count);
    bank_error=ESP_OK;assert(capture_ble(7,2,false)==ESP_OK&&bank_calls==2&&saved_count==2);
    assert(saved[0].address[0]==6&&saved[0].address[5]==1&&saved[0].address_type==1);
    assert(saved[0].ble_evidence.advertisement_observed_ms==100&&!strcmp((char*)saved[0].label,"Test"));
    assert(wardrive_status.s3_ble_flock_observations==2&&wardrive_status.s3_ble_flock_signature==2);
    assert(wardrive_status.s3_ble_observations==2&&wardrive_status.s3_ble_logged==2&&wardrive_status.sd_rows==2);
    assert(capture_ble(7,2,false)==ESP_OK&&saved_count==2&&wardrive_status.s3_ble_flock_observations==2);
    ble_capture_ready=false; /* A new accepted START, including generation rollover. */
    assert(capture_ble(7,2,false)==ESP_OK&&saved_count==2);
    assert(wardrive_status.s3_ble_flock_observations==4&&wardrive_status.duplicate_sightings==2);
    evidence[0].advertisement_observed_ms=999;
    ble_capture_ready=false;
    assert(capture_ble(7,2,false)==ESP_OK&&saved_count==2); /* fresh callback alone is not new facts */
    evidence[0].advertisement.facts|=CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
    ble_capture_ready=false;
    assert(capture_ble(7,2,false)==ESP_OK&&saved_count==3&&wardrive_status.s3_ble_flock_corroborated==1);
    reset();sd_error=ESP_FAIL;assert(capture_ble(7,2,false)==ESP_FAIL&&append_calls==1&&!saved_count);
    assert(!wardrive_status.s3_ble_observations&&!wardrive_status.s3_ble_flock_observations);
    sd_error=ESP_OK;assert(capture_ble(7,2,false)==ESP_OK&&saved_count==2);
    reset();stop_after_first=true;assert(capture_ble(7,2,false)==ESP_OK&&saved_count==1);
    assert(capture_ble(7,1,true)==ESP_ERR_INVALID_STATE);
    assert(capture_ble(8,2,true)==ESP_ERR_INVALID_STATE);
    assert(capture_ble(7,2,true)==ESP_OK&&saved_count==2&&bank_calls==1);
    assert(wardrive_status.s3_ble_flock_observations==2&&wardrive_status.s3_ble_observations==2);
    assert(!wardrive_status.duplicate_sightings); /* final drain never replays a successful prefix */
    reset();evidence[0]=(cerberus_s3_ble_wardrive_evidence_t){.flags=4};
    evidence[1]=(cerberus_s3_ble_wardrive_evidence_t){0};
    assert(capture_ble(7,2,false)==ESP_OK&&wardrive_status.s3_ble_flock_clue==1&&wardrive_status.s3_ble_flock_observations==1);
    reset();assert(capture_ble(7,0,false)==ESP_OK&&bank_calls==1&&!append_calls);
    ble_capture_ready=false;
    assert(capture_ble(7,32,false)==ESP_OK&&saved_count==32&&wardrive_status.s3_ble_flock_observations==32);
    wardrive_status.s3_ble_flock_observations=wardrive_status.s3_ble_flock_signature=UINT32_MAX;
    ble_capture_ready=false;
    assert(capture_ble(7,32,false)==ESP_OK&&wardrive_status.s3_ble_flock_observations==UINT32_MAX&&wardrive_status.s3_ble_flock_signature==UINT32_MAX);
    reset();
    cerberus_wardrive_observation_t relation={.type=CERBERUS_WARDRIVE_WIFI_RELATION,
        .radio=CERBERUS_WARDRIVE_RADIO_C5};
    publish_sighting(&relation,true);publish_sighting(&relation,false);
    assert(wardrive_status.c5_relation_observations==2&&wardrive_status.c5_relation_logged==1);
    assert(!wardrive_status.c5_wifi_observations&&!wardrive_status.c5_probe_observations&&!wardrive_status.s3_ble_observations);
    assert(wardrive_status.sd_rows==1&&wardrive_status.duplicate_sightings==1);
    wardrive_status.c5_relation_observations=wardrive_status.c5_relation_logged=UINT32_MAX;
    relation_dedupe.evicted=UINT32_MAX;probe_dedupe.evicted=2;
    publish_sighting(&relation,true);
    assert(wardrive_status.c5_relation_observations==UINT32_MAX&&wardrive_status.c5_relation_logged==UINT32_MAX&&
        wardrive_status.dedupe_evicted==UINT32_MAX);
    puts("Wardriving BLE: atomic banks, source evidence, independent relation counters, SD retry and Stop drain pass");
}
