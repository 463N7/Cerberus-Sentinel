#ifdef NDEBUG
#undef NDEBUG
#endif
#include "c5_spi_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)
static void *link_mutex;
static uint32_t peer_firmware_version;
static bool held, fail_lock;
static unsigned takes, releases, exchanges, clock_reads, fail_exchange;
static uint64_t clock_us, exchange_delay_us;
static cerberus_wifi_scan_result_payload_t reply;
static cerberus_wifi_scan_ssid_payload_t chunks[2];
static const uint8_t expected_generation=87U, expected_index=9U;
static uint8_t ssid[32];

static int take_link_mutex(unsigned ticks)
{
    CHECK(link_mutex && ticks==250U && !held); ++takes;
    if(fail_lock)return 0;
    held=true; return pdTRUE;
}
static void give_link_mutex(void)
{ CHECK(held); held=false; ++releases; }
static int64_t esp_timer_get_time(void)
{ CHECK(held && exchanges==0U); ++clock_reads; return (int64_t)clock_us; }
static esp_err_t exchange_v2_locked(cerberus_spi_command_t command,const void *request,
    size_t request_size,void *out,size_t size)
{
    CHECK(held); ++exchanges; clock_us+=exchange_delay_us;
    if(command==CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT){
        CHECK(exchanges==1U && request_size==sizeof(cerberus_wifi_scan_result_request_t));
        const cerberus_wifi_scan_result_request_t *q=request;
        CHECK(q->generation==expected_generation && q->index==expected_index);
        CHECK(size==sizeof(reply));
        if(fail_exchange==exchanges)return ESP_ERR_INVALID_CRC;
        memcpy(out,&reply,size);
    }else{
        /* Any status/timestamp/other extra SPI query is a contract violation. */
        CHECK(command==CERBERUS_SPI_COMMAND_WIFI_SCAN_SSID);
        CHECK(request_size==sizeof(cerberus_wifi_scan_ssid_request_t));
        const cerberus_wifi_scan_ssid_request_t *q=request;
        CHECK(q->generation==expected_generation && q->index==expected_index);
        CHECK(q->chunk<2U && exchanges==(unsigned)q->chunk+2U);
        CHECK(size==sizeof(chunks[0]));
        if(fail_exchange==exchanges)return ESP_ERR_INVALID_CRC;
        memcpy(out,&chunks[q->chunk],size);
    }
    return ESP_OK;
}
#include "s3_wifi_observation_link_impl.h"

static bool zero(const void *data,size_t length)
{ const uint8_t *p=data;for(size_t i=0;i<length;++i)if(p[i])return false;return true; }
static void reset(uint32_t firmware,size_t length)
{
    CHECK(!held && length<=sizeof(ssid));
    link_mutex=(void *)1;peer_firmware_version=firmware;
    takes=releases=exchanges=clock_reads=fail_exchange=0U;fail_lock=false;
    clock_us=UINT64_C(4294967296)*1000U+123456U;exchange_delay_us=1500U;
    reply=(cerberus_wifi_scan_result_payload_t){.generation=expected_generation,
        .index=expected_index,.rssi=-42,.channel=36U,.security=CERBERUS_WIFI_SECURITY_WPA2,
        .ssid_length=(uint8_t)length,.bssid={2,4,6,8,10,12},.observation_age_ms=123U,
        .observation_source_ms=4000000000U,.observation_channel=36U,
        .observation_flags=CERBERUS_WIFI_OBSERVATION_VALID|CERBERUS_WIFI_OBSERVATION_BEACON};
    for(size_t i=0;i<sizeof(ssid);++i)ssid[i]=(uint8_t)('A'+i%26U);
    memset(chunks,0,sizeof(chunks));
    for(size_t i=0;i<2U;++i){
        size_t offset=i*CERBERUS_WIFI_SSID_CHUNK_SIZE;
        size_t bytes=length>offset?length-offset:0U;
        if(bytes>CERBERUS_WIFI_SSID_CHUNK_SIZE)bytes=CERBERUS_WIFI_SSID_CHUNK_SIZE;
        chunks[i]=(cerberus_wifi_scan_ssid_payload_t){.generation=expected_generation,
            .index=expected_index,.chunk=(uint8_t)i,.byte_count=(uint8_t)bytes};
        if(bytes)memcpy(chunks[i].bytes,ssid+offset,bytes);
    }
}
static cerberus_s3_wifi_observation_t get(esp_err_t expected)
{
    cerberus_c5_wifi_result_t result;cerberus_s3_wifi_observation_t observed;
    memset(&result,0xa5,sizeof(result));memset(&observed,0xa5,sizeof(observed));
    CHECK(c5_spi_link_get_wifi_scan_observed_result(expected_generation,expected_index,
        &result,&observed)==expected);
    CHECK(!held);
    if(expected==ESP_OK){
        CHECK(!memcmp(result.ssid,ssid,reply.ssid_length));
        CHECK(result.ssid[reply.ssid_length]=='\0');
        CHECK(!memcmp(result.bssid,reply.bssid,6U));
        CHECK(result.channel==reply.channel && result.rssi==reply.rssi && result.security==reply.security);
    }else CHECK(zero(&observed,sizeof(observed)));
    CHECK(releases==takes-(fail_lock?1U:0U));
    return observed;
}
static void pure_decode_and_age(void)
{
    reset(51U,32U);
    cerberus_s3_wifi_observation_t observed;
    CHECK(!cerberus_s3_wifi_observation_decode(&reply,51U,0U,NULL));
    memset(&observed,0xa5,sizeof(observed));
    CHECK(!cerberus_s3_wifi_observation_decode(NULL,51U,0U,&observed));
    CHECK(zero(&observed,sizeof(observed)));
    for(unsigned flags=0;flags<=UINT8_MAX;++flags){
        reply.observation_flags=(uint8_t)flags;
        bool valid=flags==(CERBERUS_WIFI_OBSERVATION_VALID|CERBERUS_WIFI_OBSERVATION_BEACON)||
            flags==(CERBERUS_WIFI_OBSERVATION_VALID|CERBERUS_WIFI_OBSERVATION_PROBE_RESPONSE);
        memset(&observed,0xa5,sizeof(observed));
        CHECK(cerberus_s3_wifi_observation_decode(&reply,51U,0U,&observed)==valid);
        if(valid)CHECK(observed.valid && observed.request_started_us==0U && observed.source_age_ms==123U);
        else CHECK(zero(&observed,sizeof(observed)));
        /* Unknown/old firmware reserved bytes are not interpreted as evidence. */
        CHECK(cerberus_s3_wifi_observation_decode(&reply,0U,100U,&observed));
        CHECK(zero(&observed,sizeof(observed)));
        CHECK(cerberus_s3_wifi_observation_decode(&reply,50U,100U,&observed));
        CHECK(zero(&observed,sizeof(observed)));
    }
    reset(51U,0U);reply.observation_flags=0U;
    reply.observation_age_ms=reply.observation_source_ms=0U;reply.observation_channel=0U;
    CHECK(cerberus_s3_wifi_observation_decode(&reply,51U,10U,&observed));
    CHECK(zero(&observed,sizeof(observed)));
    for(unsigned field=0;field<3U;++field){
        if(field==0)reply.observation_age_ms=1U;
        if(field==1)reply.observation_source_ms=1U;
        if(field==2)reply.observation_channel=1U;
        CHECK(!cerberus_s3_wifi_observation_decode(&reply,51U,10U,&observed));
        CHECK(zero(&observed,sizeof(observed)));
        reply.observation_age_ms=reply.observation_source_ms=0U;reply.observation_channel=0U;
    }
    reset(51U,0U);
    for(unsigned channel=0;channel<=UINT8_MAX;++channel){
        reply.observation_channel=(uint8_t)channel;
        CHECK(cerberus_s3_wifi_observation_decode(&reply,51U,10U,&observed)==(channel==36U));
    }
    reply.channel=reply.observation_channel=0U;
    CHECK(!cerberus_s3_wifi_observation_decode(&reply,51U,10U,&observed));
    reset(51U,0U);reply.observation_age_ms=0U;reply.observation_source_ms=0U;
    CHECK(cerberus_s3_wifi_observation_decode(&reply,51U,0U,&observed));
    CHECK(observed.valid && cerberus_s3_wifi_observation_recent(&observed,0U));
    CHECK(cerberus_s3_wifi_observation_recent(&observed,30000000U));
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,30000001U));
    CHECK(!cerberus_s3_wifi_observation_recent(NULL,0U));
    observed=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=5000U,.source_age_ms=29999U};
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,4999U));
    CHECK(cerberus_s3_wifi_observation_recent(&observed,5999U));
    CHECK(cerberus_s3_wifi_observation_recent(&observed,6000U));
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,6001U));
    observed.source_age_ms=30000U;
    CHECK(cerberus_s3_wifi_observation_recent(&observed,5000U));
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,5001U));
    observed.source_age_ms=30001U;
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,5000U));
    observed.source_age_ms=UINT32_MAX;
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,5000U));
    observed.source_age_ms=0U;observed.request_started_us=(UINT64_C(1)<<32)*1000U-100U;
    CHECK(cerberus_s3_wifi_observation_recent(&observed,(UINT64_C(1)<<32)*1000U+100U));
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,observed.request_started_us+(UINT64_C(1)<<32)*1000U));
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,UINT64_MAX));
    observed.request_started_us=UINT64_MAX-1000U;
    CHECK(cerberus_s3_wifi_observation_recent(&observed,UINT64_MAX));
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,0U));
    observed.valid=false;CHECK(!cerberus_s3_wifi_observation_recent(&observed,UINT64_MAX));
}
static void pure_indexes(void)
{
    cerberus_s3_wifi_observation_t observations[32],saved[32];uint8_t indexes[40];
    for(unsigned mask=0;mask<256U;++mask){
        memset(observations,0,sizeof(observations));
        for(unsigned i=0;i<8U;++i)observations[i]=(cerberus_s3_wifi_observation_t){
            .valid=(mask&(1U<<i))!=0U,.request_started_us=100U,.source_age_ms=0U};
        memcpy(saved,observations,sizeof(saved));
        for(size_t count=0;count<=8U;++count)for(size_t capacity=0;capacity<=10U;++capacity){
            uint8_t expected[8];size_t expected_count=0U;
            for(unsigned i=0;i<count;++i)if(mask&(1U<<i))expected[expected_count++]=(uint8_t)i;
            memset(indexes,0xa5,sizeof(indexes));
            CHECK(cerberus_s3_wifi_observation_indexes(observations,count,true,1000U,indexes,capacity)==expected_count);
            size_t copied=expected_count<capacity?expected_count:capacity;
            CHECK(!memcmp(indexes,expected,copied));
            for(size_t i=copied;i<sizeof(indexes);++i)CHECK(indexes[i]==0xa5U);
            CHECK(cerberus_s3_wifi_observation_indexes(observations,count,true,1000U,NULL,capacity)==expected_count);
        }
        CHECK(!memcmp(observations,saved,sizeof(saved)));
    }
    memset(observations,0,sizeof(observations));
    observations[0]=(cerberus_s3_wifi_observation_t){.valid=true};observations[31]=observations[0];
    CHECK(cerberus_s3_wifi_observation_indexes(observations,32U,true,30000000U,indexes,40U)==2U);
    CHECK(indexes[0]==0U&&indexes[1]==31U);
    CHECK(cerberus_s3_wifi_observation_indexes(observations,32U,true,30000001U,indexes,40U)==0U);
    CHECK(cerberus_s3_wifi_observation_indexes(NULL,32U,true,0U,indexes,40U)==0U);
    CHECK(cerberus_s3_wifi_observation_indexes(NULL,32U,false,0U,indexes,40U)==32U);
    for(unsigned i=0;i<32U;++i)CHECK(indexes[i]==i);
    memset(indexes,0xa5,sizeof(indexes));
    CHECK(cerberus_s3_wifi_observation_indexes(observations,33U,false,0U,indexes,40U)==0U);
    for(size_t i=0;i<sizeof(indexes);++i)CHECK(indexes[i]==0xa5U);
}
static void link_success_and_legacy(void)
{
    const uint32_t versions[]={0U,50U,51U,52U};
    for(size_t v=0;v<sizeof(versions)/sizeof(versions[0]);++v)for(size_t length=0;length<=32U;++length){
        reset(versions[v],length);uint64_t start=clock_us;
        cerberus_s3_wifi_observation_t observed=get(ESP_OK);
        CHECK(exchanges==1U+(length+17U)/18U && takes==1U && clock_reads==1U);
        CHECK(observed.valid==(versions[v]>=51U));
        if(observed.valid){
            CHECK(observed.request_started_us==start && observed.source_age_ms==123U);
            CHECK(cerberus_s3_wifi_observation_recent(&observed,start+29877000U));
            CHECK(!cerberus_s3_wifi_observation_recent(&observed,start+29877001U));
        }else CHECK(zero(&observed,sizeof(observed)));
    }
    reset(51U,32U);ssid[0]=chunks[0].bytes[0]=0U;ssid[19]=chunks[1].bytes[1]=0xffU;
    (void)get(ESP_OK); /* Full binary/embedded-NUL SSID bytes survive, then trailing NUL. */
    reset(51U,32U);reply.observation_age_ms=29999U;
    cerberus_s3_wifi_observation_t observed=get(ESP_OK);
    CHECK(!cerberus_s3_wifi_observation_recent(&observed,clock_us)); /* All three bus delays count. */
    reset(51U,0U);reply.observation_flags=0U;
    reply.observation_age_ms=reply.observation_source_ms=0U;reply.observation_channel=0U;
    observed=get(ESP_OK);CHECK(zero(&observed,sizeof(observed)));
    reset(51U,32U);reply.observation_flags=0xffU;
    cerberus_c5_wifi_result_t result;
    CHECK(c5_spi_link_get_wifi_scan_result(expected_generation,expected_index,&result)==ESP_OK);
    CHECK(clock_reads==0U && exchanges==3U && takes==releases && !held);
    CHECK(!memcmp(result.ssid,ssid,32U) && result.ssid[32]=='\0');
}
static void link_failures(void)
{
    for(unsigned stage=1U;stage<=3U;++stage){
        reset(51U,32U);fail_exchange=stage;(void)get(ESP_ERR_INVALID_CRC);CHECK(exchanges==stage);
    }
    for(unsigned defect=0U;defect<7U;++defect){
        reset(51U,32U);
        if(defect==0U)++reply.generation;
        if(defect==1U)++reply.index;
        if(defect==2U)reply.ssid_length=33U;
        if(defect==3U)reply.security=CERBERUS_WIFI_SECURITY_OTHER+1U;
        if(defect==4U)reply.observation_flags=CERBERUS_WIFI_OBSERVATION_ALL;
        if(defect==5U)reply.observation_channel=1U;
        if(defect==6U)reply.channel=reply.observation_channel=0U;
        (void)get(ESP_ERR_INVALID_RESPONSE);CHECK(exchanges==1U);
    }
    for(unsigned chunk=0U;chunk<2U;++chunk)for(unsigned defect=0U;defect<6U;++defect){
        reset(51U,32U);
        if(defect==0U)++chunks[chunk].generation;
        if(defect==1U)++chunks[chunk].index;
        if(defect==2U)++chunks[chunk].chunk;
        if(defect==3U)chunks[chunk].byte_count=19U;
        if(defect==4U)--chunks[chunk].byte_count;
        if(defect==5U)chunks[chunk].byte_count=0U;
        (void)get(ESP_ERR_INVALID_RESPONSE);CHECK(exchanges==chunk+2U);
    }
    reset(51U,32U);chunks[0].byte_count=17U;chunks[1].byte_count=15U;
    (void)get(ESP_ERR_INVALID_RESPONSE);CHECK(exchanges==2U); /* Cannot compensate a skipped source byte. */
    reset(51U,32U);fail_lock=true;(void)get(ESP_ERR_TIMEOUT);
    CHECK(exchanges==0U && releases==0U && clock_reads==0U);
    reset(51U,32U);link_mutex=NULL;(void)get(ESP_ERR_INVALID_STATE);
    CHECK(takes==0U && exchanges==0U && clock_reads==0U);
    reset(51U,32U);cerberus_s3_wifi_observation_t observed;memset(&observed,0xa5,sizeof(observed));
    CHECK(c5_spi_link_get_wifi_scan_observed_result(expected_generation,expected_index,NULL,&observed)==ESP_ERR_INVALID_ARG);
    CHECK(zero(&observed,sizeof(observed)) && takes==0U && exchanges==0U && clock_reads==0U);
    CHECK(c5_spi_link_get_wifi_scan_result(expected_generation,expected_index,NULL)==ESP_ERR_INVALID_ARG);
}
int main(void)
{
    _Static_assert(sizeof(cerberus_wifi_scan_result_payload_t)==22U,"Wi-Fi result wire size unchanged");
    _Static_assert(CERBERUS_WIFI_OBSERVATION_MIN_FIRMWARE==51U,"Observation gate");
    pure_decode_and_age();pure_indexes();link_success_and_legacy();link_failures();
    printf("S3 Wi-Fi observation: %u checks passed; flags/channel/legacy gates, full-width source ages, stable bounded indexes, exact SSID chunks, transactional evidence, no extra queries and lock exits.\n",checks);
    return 0;
}
