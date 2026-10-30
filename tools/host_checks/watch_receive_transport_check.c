#undef NDEBUG
#include "c5_spi_link.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static int mutex_token;
static void *link_mutex=&mutex_token;
static uint8_t sequence;
static bool locked,lock_failure;
static unsigned takes,gives,calls,peer_reads,status_reads,data_reads,timing_reads;
static unsigned motion_reads,motion_timing_reads,motion_defect,motion_timing_defect,motion_timing_defect_at;
static unsigned fail_call,peer_defect,status_defect,chunk_defect,timing_defect,timing_defect_at;
static int64_t clock_us,first_anchor;
static int64_t motion_anchor;
static cerberus_c5_status_t peer;
static cerberus_watch_scan_status_payload_t status;
static cerberus_watch_observation_t record;
static uint64_t first_age,second_age,capture_epoch;
static uint8_t timing_flags;
static sentinel_watch_motion_t motion_record;
static uint64_t motion_first_age,motion_second_age,motion_epoch;
static uint8_t motion_timing_flags;
static bool zero(const void *data,size_t length)
{const uint8_t *p=data;for(size_t i=0;i<length;++i)if(p[i])return false;return true;}
static int take_link_mutex(unsigned timeout)
{CHECK(timeout==250&&!locked);++takes;if(lock_failure)return 0;locked=true;return pdTRUE;}
static void give_link_mutex(void){CHECK(locked);locked=false;++gives;}
static int64_t esp_timer_get_time(void){CHECK(locked);return clock_us;}
static bool failure(void){++calls;clock_us+=1111;return calls==fail_call;}
static esp_err_t get_status_v2_locked(uint8_t seq,cerberus_c5_status_t *out)
{
    CHECK(locked&&seq==sequence);++peer_reads;if(failure())return ESP_FAIL;*out=peer;
    if(peer_reads==2)switch(peer_defect){
    case 1:++out->firmware_version;break;case 2:out->capabilities^=1;break;
    case 3:++out->boot_count;break;case 4:--out->uptime_ms;break;
    case 5:++out->protocol_version;break;case 6:++out->reset_reason;break;
    default:out->uptime_ms+=20;break;
    }
    return ESP_OK;
}
static esp_err_t exchange_v2_locked(uint8_t command,const void *query,size_t query_size,
    void *reply,size_t reply_size)
{
    CHECK(locked&&query&&reply);
    if(command==CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS){
        CHECK(query_size==sizeof(cerberus_watch_scan_request_t)&&reply_size==sizeof(status));
        const cerberus_watch_scan_request_t *q=query;CHECK(q->radio==status.radio&&q->generation==status.generation);
        ++status_reads;if(failure())return ESP_FAIL;
        cerberus_watch_scan_status_payload_t *out=reply;*out=status;
        if(status_reads==2)switch(status_defect){
        case 1:++out->generation;break;case 2:out->state=CERBERUS_WATCH_RUNNING;break;
        case 3:++out->reports;break;case 4:++out->stored_count;break;
        case 5:++out->duration_ms;break;default:break;
        }
        return ESP_OK;
    }
    if(command==CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA){
        CHECK(query_size==sizeof(cerberus_watch_scan_data_request_t)&&reply_size==sizeof(cerberus_watch_scan_data_payload_t));
        const cerberus_watch_scan_data_request_t *q=query;
        CHECK(q->radio==status.radio&&q->generation==status.generation&&q->index==0&&q->chunk==data_reads);
        ++data_reads;if(failure())return ESP_FAIL;
        cerberus_watch_scan_data_payload_t *out=reply;memset(out,0,sizeof(*out));
        out->radio=q->radio;out->generation=q->generation;out->index=q->index;out->chunk=q->chunk;
        size_t offset=(size_t)q->chunk*17,count=sizeof(record)-offset;if(count>17)count=17;
        out->byte_count=(uint8_t)count;memcpy(out->bytes,(const uint8_t *)&record+offset,count);
        if(chunk_defect&&data_reads==chunk_defect)--out->byte_count;
        return ESP_OK;
    }
    if(command==CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION){
        CHECK(query_size==sizeof(cerberus_watch_scan_data_request_t)&&reply_size==22U);
        const cerberus_watch_scan_data_request_t *q=query;
        CHECK(q->radio==status.radio&&q->generation==status.generation&&q->index==0&&!q->chunk);
        ++motion_reads;if(failure())return ESP_FAIL;
        cerberus_watch_scan_motion_payload_t *out=reply;memset(out,0,sizeof(*out));
        out->radio=q->radio;out->generation=q->generation;out->index=q->index;
        out->capture_epoch=motion_epoch;out->motion=motion_record;
        switch(motion_defect){
        case 1:out->radio^=3;break;case 2:++out->generation;break;case 3:++out->index;break;
        case 4:out->reserved0=1;break;case 5:out->reserved[0]=1;break;case 6:out->reserved[1]=1;break;
        case 7:out->capture_epoch=0;break;case 8:++out->capture_epoch;break;
        case 9:out->motion.flags|=32;break;case 10:out->motion.reserved=1;break;
        case 11:out->motion.direction_deg=361;break;case 12:out->motion.speed_qms=256;break;
        case 13:out->motion.height_encoded=0;break;case 14:out->motion.flags&=~SENTINEL_WATCH_MOTION_LOCATION_PRESENT;break;
        default:break;
        }
        return ESP_OK;
    }
    const bool location=command==CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION_TIMING;
    CHECK(location||command==CERBERUS_SPI_COMMAND_WATCH_SCAN_TIMING);
    CHECK(query_size==(location?sizeof(cerberus_watch_scan_data_request_t):sizeof(cerberus_watch_scan_timing_request_t))&&
        reply_size==sizeof(cerberus_watch_scan_timing_payload_t));
    uint8_t radio,generation,index;
    if(location){
        const cerberus_watch_scan_data_request_t *q=query;
        radio=q->radio;generation=q->generation;index=q->index;CHECK(!q->chunk);
        if(!motion_timing_reads)motion_anchor=clock_us;
        ++motion_timing_reads;
    }else{
        const cerberus_watch_scan_timing_request_t *q=query;
        radio=q->radio;generation=q->generation;index=q->index;
        if(!timing_reads)first_anchor=clock_us;
        ++timing_reads;
    }
    CHECK(radio==status.radio&&generation==status.generation&&index==0);
    if(failure())return ESP_FAIL;
    cerberus_watch_scan_timing_payload_t *out=reply;memset(out,0,sizeof(*out));
    out->radio=radio;out->generation=generation;out->index=index;
    out->flags=location?motion_timing_flags:timing_flags;
    unsigned reads=location?motion_timing_reads:timing_reads;
    uint64_t start_age=location?motion_first_age:first_age;
    out->source_age_ms=reads==1?start_age:(location?motion_second_age:second_age);
    out->capture_epoch=location?motion_epoch:capture_epoch;
    if(reads==(location?motion_timing_defect_at:timing_defect_at))switch(location?motion_timing_defect:timing_defect){
    case 1:out->radio^=3;break;case 2:++out->generation;break;case 3:++out->index;break;
    case 4:out->flags|=2;break;case 5:out->capture_epoch=0;break;case 6:out->reserved[0]=1;break;
    case 7:out->reserved[1]=1;break;case 8:out->flags=0;out->source_age_ms=1;break;
    case 9:out->source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS+1;break;
    case 10:++out->capture_epoch;break;case 11:out->flags=0;out->source_age_ms=0;break;
    case 12:out->source_age_ms=start_age-1;break;default:break;
    }
    return ESP_OK;
}
#include "c5_watch_link.h"

static void reset(uint8_t radio)
{
    CHECK(!locked);link_mutex=&mutex_token;lock_failure=false;sequence=0;
    takes=gives=calls=peer_reads=status_reads=data_reads=timing_reads=0;
    motion_reads=motion_timing_reads=motion_defect=motion_timing_defect=0;motion_timing_defect_at=1;
    fail_call=peer_defect=status_defect=chunk_defect=timing_defect=0;timing_defect_at=1;
    clock_us=INT64_C(5000000999);first_anchor=-1;
    peer=(cerberus_c5_status_t){.firmware_version=53,.capabilities=CERBERUS_C5_CAP_WATCH_OBSERVATIONS,
        .uptime_ms=100,.protocol_version=2,.reset_reason=3,.boot_count=4};
    status=(cerberus_watch_scan_status_payload_t){.radio=radio,.generation=255,.state=CERBERUS_WATCH_COMPLETE,
        .stored_count=1,.reports=1,.duration_ms=8000};
    record=(cerberus_watch_observation_t){.address={2,3,4,5,6,7},.address_type=radio==1?1:0,
        .channel=radio==1?0:6,.rssi=-70,.kind=SENTINEL_WATCH_PROTOCOL_DRONE,
        .transport=radio==1?SENTINEL_WATCH_TRANSPORT_BLE:SENTINEL_WATCH_TRANSPORT_WIFI_NAN,
        .flags=SENTINEL_WATCH_PROTOCOL_HAS_ID};strcpy(record.id,"CLAIM-A");
    first_age=(uint64_t)UINT32_MAX+1;second_age=first_age+5;capture_epoch=UINT64_C(0x100000123);
    timing_flags=CERBERUS_WATCH_TIMING_VALID;
    motion_record=(sentinel_watch_motion_t){.speed_qms=258,.direction_deg=360,.height_encoded=2001,.flags=31};
    motion_first_age=first_age+100;motion_second_age=motion_first_age+8;
    motion_epoch=capture_epoch;motion_timing_flags=CERBERUS_WATCH_TIMING_VALID;motion_anchor=-1;
}
static esp_err_t read_result(cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing);
static esp_err_t read_detailed(cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion,cerberus_watch_receive_timing_t *motion_timing)
{
    memset(out,0xa5,sizeof(*out));memset(timing,0xa5,sizeof(*timing));
    memset(motion,0xa5,sizeof(*motion));memset(motion_timing,0xa5,sizeof(*motion_timing));
    esp_err_t result=c5_spi_link_get_watch_detailed_result(status.radio,status.generation,0,out,timing,motion,motion_timing);
    CHECK(!locked&&gives==(lock_failure||!link_mutex?0:1));
    if(result!=ESP_OK)CHECK(zero(out,sizeof(*out))&&zero(timing,sizeof(*timing))&&
        zero(motion,sizeof(*motion))&&zero(motion_timing,sizeof(*motion_timing)));
    return result;
}

static void detailed_motion(void)
{
    cerberus_watch_observation_t out;cerberus_watch_receive_timing_t timing,location;
    sentinel_watch_motion_t motion;
    for(uint8_t radio=1;radio<=2;++radio){
        reset(radio);peer.firmware_version=56;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_OK);
        CHECK(!memcmp(&out,&record,sizeof(out))&&!memcmp(&motion,&motion_record,sizeof(motion)));
        CHECK(timing.valid&&location.valid&&timing.source_age_ms==first_age&&location.source_age_ms==motion_first_age);
        CHECK(timing.request_started_us==(uint64_t)first_anchor&&location.request_started_us==(uint64_t)motion_anchor);
        CHECK(location.request_started_us>timing.request_started_us);
        CHECK(calls==12&&peer_reads==2&&status_reads==2&&timing_reads==2&&motion_reads==1&&motion_timing_reads==2&&data_reads==3);
        uint64_t source_age,location_age;
        CHECK(cerberus_watch_receive_age_ms(&timing,(uint64_t)clock_us,&source_age));
        CHECK(cerberus_watch_receive_age_ms(&location,(uint64_t)clock_us,&location_age));
        CHECK(source_age==first_age+12&&location_age==motion_first_age+10);
    }
    for(uint32_t fw=31;fw<=55;++fw){
        reset(2);peer.firmware_version=fw;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_OK&&!motion_reads&&!motion_timing_reads);
        CHECK(zero(&motion,sizeof(motion))&&zero(&location,sizeof(location))&&calls==(fw>=53?9U:7U));
    }
    /* Adding support must not increase either old API's transaction count. */
    reset(2);peer.firmware_version=56;CHECK(read_result(&out,&timing)==ESP_OK&&calls==9&&!motion_reads&&!motion_timing_reads);
    reset(2);peer.firmware_version=56;CHECK(c5_spi_link_get_watch_result(2,255,0,&out)==ESP_OK&&calls==6&&!motion_reads&&!motion_timing_reads);
    reset(2);peer.firmware_version=56;memset(&motion_record,0,sizeof(motion_record));motion_timing_flags=0;motion_first_age=motion_second_age=0;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_OK&&timing.valid&&zero(&motion,sizeof(motion))&&zero(&location,sizeof(location)));
    /* A present Location with unavailable component values/time remains explicit. */
    reset(2);peer.firmware_version=56;motion_record=(sentinel_watch_motion_t){.flags=1};
    motion_timing_flags=0;motion_first_age=motion_second_age=0;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_OK&&motion.flags==1&&zero(&location,sizeof(location)));
    /* Untimed newer Basic ID may clear only source timing, retaining old Location timing. */
    reset(2);peer.firmware_version=56;timing_flags=0;first_age=second_age=0;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_OK&&!timing.valid&&location.valid);
    reset(2);peer.firmware_version=56;clock_us=-2222;first_age=second_age=0;motion_first_age=motion_second_age=0;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_OK&&timing.valid&&timing.request_started_us==0&&location.valid);
    for(unsigned stage=1;stage<=12;++stage){reset(2);peer.firmware_version=56;fail_call=stage;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_FAIL&&calls==stage);}
    for(unsigned defect=1;defect<=14;++defect){reset(2);peer.firmware_version=56;motion_defect=defect;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE&&!data_reads);}
    for(unsigned at=1;at<=2;++at)for(unsigned defect=1;defect<=12;++defect){
        if(at==1&&defect==12)continue; /* A smaller first age is still valid. */
        reset(2);peer.firmware_version=56;motion_timing_defect=defect;motion_timing_defect_at=at;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE);
    }
    for(unsigned at=1;at<=2;++at)for(unsigned defect=1;defect<=12;++defect){
        if(at==1&&defect==12)continue;
        reset(2);peer.firmware_version=56;timing_defect=defect;timing_defect_at=at;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE);
    }
    for(unsigned defect=1;defect<=6;++defect){reset(2);peer.firmware_version=56;peer_defect=defect;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE);}
    for(unsigned defect=1;defect<=5;++defect){reset(2);peer.firmware_version=56;status_defect=defect;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE);}
    for(unsigned chunk=1;chunk<=3;++chunk){reset(2);peer.firmware_version=56;chunk_defect=chunk;
        CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE);}
    reset(2);peer.firmware_version=56;motion_epoch++;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE&&!motion_reads);
    reset(2);peer.firmware_version=56;memset(&motion_record,0,sizeof(motion_record));
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE); /* Timing without Location. */
    reset(2);peer.firmware_version=56;record.kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;record.transport=SENTINEL_WATCH_TRANSPORT_WIFI_BEACON;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_RESPONSE);
    reset(2);peer.firmware_version=56;lock_failure=true;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_TIMEOUT&&!calls);
    reset(2);peer.firmware_version=56;link_mutex=NULL;
    CHECK(read_detailed(&out,&timing,&motion,&location)==ESP_ERR_INVALID_STATE&&!calls);
    for(unsigned missing=0;missing<4;++missing){
        reset(2);memset(&out,0xa5,sizeof(out));memset(&timing,0xa5,sizeof(timing));
        memset(&motion,0xa5,sizeof(motion));memset(&location,0xa5,sizeof(location));
        CHECK(c5_spi_link_get_watch_detailed_result(2,255,0,missing==0?NULL:&out,missing==1?NULL:&timing,
            missing==2?NULL:&motion,missing==3?NULL:&location)==ESP_ERR_INVALID_ARG&&!takes);
        CHECK((missing==0||zero(&out,sizeof(out)))&&(missing==1||zero(&timing,sizeof(timing)))&&
            (missing==2||zero(&motion,sizeof(motion)))&&(missing==3||zero(&location,sizeof(location))));
    }
}
static esp_err_t read_result(cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing)
{
    memset(out,0xa5,sizeof(*out));memset(timing,0xa5,sizeof(*timing));
    esp_err_t result=c5_spi_link_get_watch_observed_result(status.radio,status.generation,0,out,timing);
    CHECK(!locked&&gives==(lock_failure||!link_mutex?0:1));
    if(result!=ESP_OK)CHECK(zero(out,sizeof(*out))&&zero(timing,sizeof(*timing)));
    return result;
}
static void success_and_compatibility(void)
{
    cerberus_watch_observation_t out;cerberus_watch_receive_timing_t timing;
    for(uint8_t radio=1;radio<=2;++radio){
        reset(radio);CHECK(read_result(&out,&timing)==ESP_OK);
        CHECK(!memcmp(&out,&record,sizeof(out))&&timing.valid);
        CHECK(timing.request_started_us==(uint64_t)first_anchor&&timing.source_age_ms==first_age);
        CHECK(timing_reads==2&&data_reads==3&&peer_reads==2&&status_reads==2&&calls==9);
        uint64_t age;CHECK(cerberus_watch_receive_age_ms(&timing,(uint64_t)clock_us,&age));
        CHECK(age==first_age+8); /* 7 delayed operations since the first request. */
        CHECK(cerberus_watch_receive_age_ms(&timing,(uint64_t)clock_us+1000001,&age)&&age==first_age+1008);
    }
    for(uint32_t firmware=31;firmware<53;++firmware){
        reset(2);peer.firmware_version=firmware;
        CHECK(read_result(&out,&timing)==ESP_OK&&zero(&timing,sizeof(timing)));
        CHECK(timing_reads==0&&data_reads==3&&peer_reads==2&&calls==7);
    }
    reset(2);peer.firmware_version=0;CHECK(read_result(&out,&timing)==ESP_ERR_NOT_SUPPORTED&&!status_reads);
    reset(2);peer.capabilities=0;CHECK(read_result(&out,&timing)==ESP_ERR_NOT_SUPPORTED&&!status_reads);
    reset(2);CHECK(c5_spi_link_get_watch_result(2,255,0,&out)==ESP_OK);
    CHECK(!locked&&gives==1&&peer_reads==1&&!timing_reads&&calls==6&&!memcmp(&out,&record,sizeof(out)));
    reset(2);timing_flags=0;first_age=second_age=0;
    CHECK(read_result(&out,&timing)==ESP_OK&&zero(&timing,sizeof(timing))&&timing_reads==2);
    reset(2);clock_us=-2222;first_age=second_age=0;
    CHECK(read_result(&out,&timing)==ESP_OK&&timing.valid&&timing.request_started_us==0);
    reset(2);first_age=second_age=CERBERUS_WATCH_MAX_SOURCE_AGE_MS;
    CHECK(read_result(&out,&timing)==ESP_OK&&timing.source_age_ms==CERBERUS_WATCH_MAX_SOURCE_AGE_MS);
}
static void failures_are_atomic(void)
{
    cerberus_watch_observation_t out;cerberus_watch_receive_timing_t timing;
    for(unsigned stage=1;stage<=9;++stage){reset(2);fail_call=stage;CHECK(read_result(&out,&timing)==ESP_FAIL&&calls==stage);}
    for(unsigned which=1;which<=2;++which)for(unsigned defect=1;defect<=9;++defect){
        reset(2);timing_defect_at=which;timing_defect=defect;
        CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_RESPONSE);
        CHECK(data_reads==(which==1?0:3));
    }
    for(unsigned defect=10;defect<=12;++defect){reset(2);timing_defect_at=2;timing_defect=defect;
        CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_RESPONSE&&data_reads==3);}
    for(unsigned defect=1;defect<=6;++defect){reset(2);peer_defect=defect;CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_RESPONSE);}
    for(unsigned defect=1;defect<=5;++defect){reset(2);status_defect=defect;CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_RESPONSE);}
    for(unsigned chunk=1;chunk<=3;++chunk){reset(2);chunk_defect=chunk;
        CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_RESPONSE&&data_reads==chunk&&timing_reads==1);}
    reset(2);record.flags=0xff;CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_RESPONSE);
    reset(2);clock_us=-100000;CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_STATE&&!timing_reads);
    reset(2);lock_failure=true;CHECK(read_result(&out,&timing)==ESP_ERR_TIMEOUT&&!calls);
    reset(2);link_mutex=NULL;CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_STATE&&!takes);
    reset(2);status.stored_count=0;CHECK(read_result(&out,&timing)==ESP_ERR_INVALID_ARG&&!timing_reads);
    reset(2);memset(&timing,0xa5,sizeof(timing));
    CHECK(c5_spi_link_get_watch_observed_result(2,255,0,NULL,&timing)==ESP_ERR_INVALID_ARG&&zero(&timing,sizeof(timing))&&!takes);
    memset(&out,0xa5,sizeof(out));memset(&timing,0xa5,sizeof(timing));
    CHECK(c5_spi_link_get_watch_observed_result(2,255,12,&out,&timing)==ESP_ERR_INVALID_ARG&&zero(&out,sizeof(out))&&zero(&timing,sizeof(timing))&&!takes);
}
int main(void)
{
    success_and_compatibility();failures_are_atomic();detailed_motion();
    printf("Production Watch observed transport: %u checks passed; coherent epoch-bracketed rows, full64 age/request bounds, legacy no-query fallback, exact peer/status/chunk/timing rejection and atomic failure outputs.\n",checks);
    return 0;
}
