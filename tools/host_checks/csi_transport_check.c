#ifdef NDEBUG
#undef NDEBUG
#endif
#include "c5_spi_link.h"
#include "csi_link.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)

static int mutex;
static void *link_mutex=&mutex;
static uint8_t sequence;
static bool held, deny_lock, start_delivery, consume_ack;
static uint64_t now_us, exchange_us, lock_wait_us;
static unsigned calls,takes,gives,peer_calls,fail_at,delay_at,reboot_at,mutate_at,lock_delay_at;
static unsigned mutation,read_calls,peek_calls,start_calls,stop_calls,renew_calls,ack_calls;
static int bit_corrupt;
static uint32_t last_ack;
static esp_err_t peer_error;
static cerberus_c5_status_t peer;
static cerberus_csi_info_t info;
static cerberus_csi_status_t status;
static cerberus_csi_clock_t source_clock;
static cerberus_csi_counters_t counters;
static cerberus_csi_target_payload_t target;
static cerberus_csi_sample_t record;
static cerberus_csi_ops_t ops;
static cerberus_csi_binding_t binding;
static uint8_t sent_offsets[16];
static unsigned bytes_sent;

static bool zero(const void *p,size_t n)
{const uint8_t *b=p;for(size_t i=0;i<n;++i)if(b[i])return false;return true;}
static int64_t esp_timer_get_time(void){return (int64_t)now_us;}
static int take_link_mutex(unsigned timeout)
{CHECK(timeout==20&&!held);++takes;now_us+=takes==lock_delay_at?200000:lock_wait_us;if(deny_lock)return 0;held=true;return pdTRUE;}
static void give_link_mutex(void){CHECK(held);held=false;++gives;}
esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *out)
{CHECK(!held);++peer_calls;*out=peer;return peer_error;}

static void mutate_payload(uint8_t command,uint8_t page,void *payload)
{
    if(command==CERBERUS_SPI_COMMAND_CSI_STATUS&&page==CERBERUS_CSI_PAGE_INFO){
        cerberus_csi_info_t *p=payload;
        switch(mutation){case 0:p->boot_id=0;break;case 1:p->version++;break;case 2:p->format++;break;
        case 3:p->capacity++;break;case 4:p->reserved[0]=1;break;case 5:p->reserved[1]=1;break;case 6:p->reserved[2]=1;break;
        case 7:p->source_now_us=record.source_us?record.source_us-1:0;break;
        case 8:p->source_now_us=info.source_now_us-1;break;}
    }else if(command==CERBERUS_SPI_COMMAND_CSI_STATUS&&page==CERBERUS_CSI_PAGE_TARGET){
        cerberus_csi_target_payload_t *p=payload;
        switch(mutation){case 0:p->epoch++;break;case 1:p->version++;break;case 2:p->channel++;break;
        case 3:p->lease_ms=999;break;case 4:p->lease_ms=10001;break;
        default:p->reserved[mutation-5]=1;break;}
    }else if(command==CERBERUS_SPI_COMMAND_CSI_STATUS&&page==CERBERUS_CSI_PAGE_CLOCK){
        cerberus_csi_clock_t *p=payload;
        switch(mutation){case 0:p->epoch++;break;case 1:p->version++;break;case 2:p->source_now_us=info.source_now_us-1;break;}
    }else if(command==CERBERUS_SPI_COMMAND_CSI_STATUS&&page==CERBERUS_CSI_PAGE_COUNTERS){
        cerberus_csi_counters_t *p=payload;
        switch(mutation){case 0:p->epoch++;break;case 1:p->version++;break;case 2:p->reserved=1;break;}
    }else if(command==CERBERUS_SPI_COMMAND_CSI_PEEK){
        cerberus_csi_peek_t *p=payload;
        switch(mutation){case 0:p->epoch++;break;case 1:p->sequence++;break;case 2:p->crc32++;break;
        case 3:p->length--;break;case 4:p->format++;break;case 5:p->flags=1;break;
        case 6:p->reserved=1;break;case 7:p->queued=0;break;case 8:p->queued=17;break;}
    }else if(command==CERBERUS_SPI_COMMAND_CSI_READ){
        cerberus_csi_chunk_t *p=payload;
        if(mutation==0)p->offset++;
        else if(mutation==1)p->length++;
        else if(mutation==2)p->data[19]=1;
        else p->data[mutation-3]^=1;
    }else{
        cerberus_csi_status_t *p=payload;
        switch(mutation){case 0:p->version++;break;case 1:p->reserved=1;break;case 2:p->state=99;break;
        case 3:p->flags|=4;break;case 4:p->queued=17;break;case 5:p->job_id++;break;case 6:p->epoch++;break;
        case 7:p->flags=0;break;case 8:p->channel=0;break;}
    }
}

static void spi_request_response(const void *request_data,void *discarded,void *response_data,size_t length)
{
    (void)discarded;
    const cerberus_spi_frame_t *request=request_data;cerberus_spi_frame_t *response=response_data;
    CHECK(held&&length==sizeof(*request)&&cerberus_protocol_frame_is_valid(request));
    ++calls;now_us+=calls==delay_at?200000:exchange_us;
    if(calls==reboot_at){info.boot_id++;status.epoch++;source_clock.epoch=status.epoch;target.epoch=status.epoch;counters.epoch=status.epoch;}
    memset(response,0,sizeof(*response));
    uint8_t payload[22]={0},page=255;bool valid=true;
    const uint8_t command=request->command;
    if(command==CERBERUS_SPI_COMMAND_CSI_STATUS){
        CHECK(request->payload_length==sizeof(cerberus_csi_status_request_t));
        cerberus_csi_status_request_t q;memcpy(&q,request->payload,sizeof(q));page=q.page;
        if(page==CERBERUS_CSI_PAGE_INFO){CHECK(!q.boot_id&&!q.job_id&&!q.epoch);memcpy(payload,&info,22);}
        else{
            valid=q.boot_id==info.boot_id;
            if(page!=CERBERUS_CSI_PAGE_STATUS)valid=valid&&q.job_id==status.job_id&&q.epoch==status.epoch;
            if(page==CERBERUS_CSI_PAGE_STATUS)memcpy(payload,&status,22);
            else if(page==CERBERUS_CSI_PAGE_CLOCK)memcpy(payload,&source_clock,22);
            else if(page==CERBERUS_CSI_PAGE_COUNTERS)memcpy(payload,&counters,22);
            else if(page==CERBERUS_CSI_PAGE_TARGET)memcpy(payload,&target,22);
            else CHECK(false);
        }
    }else if(command==CERBERUS_SPI_COMMAND_CSI_START){
        ++start_calls;CHECK(request->payload_length==sizeof(cerberus_csi_start_request_t));
        cerberus_csi_start_request_t q;memcpy(&q,request->payload,sizeof(q));
        CHECK(q.boot_id==binding.boot_id&&q.job_id==binding.job_id&&q.version==1&&q.channel==6&&q.lease_ms==5000);
        if(start_delivery&&q.boot_id==info.boot_id){status.job_id=q.job_id;status.epoch=binding.epoch?binding.epoch:UINT64_C(0x1234567800000001);
            status.state=CERBERUS_CSI_RUNNING;status.flags=3;status.channel=q.channel;status.version=1;
            target.epoch=source_clock.epoch=counters.epoch=status.epoch;}
        valid=start_delivery&&q.boot_id==info.boot_id;memcpy(payload,&status,22);
    }else if(command==CERBERUS_SPI_COMMAND_CSI_STOP||command==CERBERUS_SPI_COMMAND_CSI_RENEW){
        cerberus_csi_binding_t q;memcpy(&q,request->payload,sizeof(q));
        CHECK(request->payload_length==(command==CERBERUS_SPI_COMMAND_CSI_STOP?sizeof(q):sizeof(cerberus_csi_renew_request_t)));
        valid=q.boot_id==info.boot_id&&q.job_id==status.job_id&&(!q.epoch||q.epoch==status.epoch);
        if(command==CERBERUS_SPI_COMMAND_CSI_STOP)++stop_calls;else ++renew_calls;
        memcpy(payload,&status,22);
    }else if(command==CERBERUS_SPI_COMMAND_CSI_PEEK){
        ++peek_calls;CHECK(request->payload_length==sizeof(cerberus_csi_binding_t));
        cerberus_csi_binding_t q;memcpy(&q,request->payload,sizeof(q));
        valid=q.boot_id==info.boot_id&&q.job_id==status.job_id&&q.epoch==status.epoch;
        cerberus_csi_peek_t p={.epoch=status.epoch,.format=1,.queued=status.queued};
        if(p.queued){p.sequence=record.sequence;p.crc32=cerberus_csi_crc32(&record,sizeof(record));p.length=sizeof(record);}
        memcpy(payload,&p,22);
    }else if(command==CERBERUS_SPI_COMMAND_CSI_READ){
        CHECK(request->payload_length==sizeof(cerberus_csi_read_request_t));
        cerberus_csi_read_request_t q;memcpy(&q,request->payload,sizeof(q));
        valid=q.job_id==status.job_id&&q.epoch==status.epoch&&q.sequence==record.sequence&&status.queued;
        CHECK(q.offset<sizeof(record)&&q.offset%20==0);CHECK(read_calls<sizeof(sent_offsets));sent_offsets[read_calls++]=q.offset;
        cerberus_csi_chunk_t p={.offset=q.offset,.length=(uint8_t)(sizeof(record)-q.offset)};
        if(p.length>20)p.length=20;memcpy(p.data,(uint8_t *)&record+q.offset,p.length);bytes_sent+=p.length;
        memcpy(payload,&p,22);
    }else if(command==CERBERUS_SPI_COMMAND_CSI_ACK){
        ++ack_calls;CHECK(request->payload_length==sizeof(cerberus_csi_ack_request_t));
        cerberus_csi_ack_request_t q;memcpy(&q,request->payload,sizeof(q));
        valid=q.job_id==status.job_id&&q.epoch==status.epoch&&(q.sequence==last_ack||(status.queued&&q.sequence==record.sequence));
        if(valid&&consume_ack&&q.sequence!=last_ack){last_ack=q.sequence;--status.queued;++counters.acked;}
        memcpy(payload,&status,22);
    }else CHECK(false);
    if(calls==mutate_at)mutate_payload(command,page,payload);
    if(!valid||calls==fail_at)return;
    CHECK(cerberus_protocol_prepare_frame(response,(cerberus_spi_command_t)command,request->sequence,payload,22));
    if(bit_corrupt>=0&&calls==1)((uint8_t *)response)[(unsigned)bit_corrupt/8]^=(uint8_t)(1U<<((unsigned)bit_corrupt%8));
}

#include "c5_csi_link_impl.h"

static void reset(void)
{
    CHECK(!held);link_mutex=&mutex;sequence=0;now_us=1000000;exchange_us=100;lock_wait_us=0;
    csi_reserved_boot=0;csi_reserved_job=0;lock_delay_at=0;
    deny_lock=false;start_delivery=true;consume_ack=true;calls=takes=gives=peer_calls=fail_at=delay_at=reboot_at=mutate_at=mutation=0;
    read_calls=peek_calls=start_calls=stop_calls=renew_calls=ack_calls=bytes_sent=last_ack=0;bit_corrupt=-1;peer_error=ESP_OK;
    memset(sent_offsets,255,sizeof(sent_offsets));
    peer=(cerberus_c5_status_t){.firmware_version=58,.protocol_version=2,.capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ|CERBERUS_C5_CAP_WIFI_5_GHZ};
    info=(cerberus_csi_info_t){.boot_id=UINT64_C(0xfedcba9876543210),.source_now_us=10000000,.version=1,.format=1,.capacity=16};
    binding=(cerberus_csi_binding_t){.boot_id=info.boot_id,.job_id=0xf1234567,.epoch=UINT64_C(0x8765432100000007)};
    status=(cerberus_csi_status_t){.job_id=binding.job_id,.epoch=binding.epoch,.state=CERBERUS_CSI_RUNNING,
        .channel=6,.flags=3,.queued=1,.version=1};
    source_clock=(cerberus_csi_clock_t){.epoch=binding.epoch,.source_now_us=10000100,.accepted=1,.version=1,.pending=1};
    counters=(cerberus_csi_counters_t){.epoch=binding.epoch,.version=1};
    target=(cerberus_csi_target_payload_t){.epoch=binding.epoch,.bssid={2,3,4,5,6,7},.channel=6,.version=1,.lease_ms=5000};
    record=(cerberus_csi_sample_t){.source_us=9999900,.sequence=0xa1234567,.rssi=-40,.channel=6,.phy=1,.flags=1,.format=1};
    for(unsigned i=0;i<106;++i)record.data[i]=(int8_t)(i-53);
    cerberus_csi_link_ops(&ops);CHECK(ops.context==NULL);
}

static cerberus_csi_operation_t read_record(cerberus_csi_sample_t *out,bool *available)
{
    memset(out,0xa5,sizeof(*out));*available=true;
    cerberus_csi_operation_t result=ops.read(NULL,&binding,out,available);
    CHECK(!held&&takes==gives+(deny_lock?1U:0U));
    if(result.error)CHECK(!*available&&zero(out,sizeof(*out)));
    else if(*available)CHECK(!memcmp(out,&record,sizeof(record)));
    return result;
}

static void check_read(void)
{
    cerberus_csi_sample_t out;bool available;
    reset();CHECK(read_record(&out,&available).error==ESP_OK&&available);
    CHECK(calls==11&&takes==11&&gives==11&&read_calls==7&&peek_calls==2&&bytes_sent==124&&!ack_calls);
    for(unsigned i=0;i<7;++i)CHECK(sent_offsets[i]==i*20);
    /* Full CRC covers metadata as well as the106 coefficient bytes. */
    for(unsigned byte=0;byte<124;++byte){
        reset();mutate_at=3+byte/20;mutation=3+byte%20;
        CHECK(read_record(&out,&available).error!=ESP_OK&&!available);
    }
    for(unsigned chunk=0;chunk<7;++chunk)for(unsigned defect=0;defect<2;++defect){
        reset();mutate_at=3+chunk;mutation=defect;
        CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_RESPONSE);
        CHECK(read_calls==chunk+1);
    }
    reset();mutate_at=9;mutation=2;CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_RESPONSE);
    for(unsigned stage=1;stage<=11;++stage){
        reset();fail_at=stage;cerberus_csi_operation_t r=read_record(&out,&available);
        CHECK(r.error==ESP_ERR_INVALID_RESPONSE&&!r.no_ownership&&calls==stage);
        reset();delay_at=stage;r=read_record(&out,&available);
        CHECK(r.error==ESP_ERR_TIMEOUT&&!r.no_ownership&&calls==stage);
        reset();reboot_at=stage;r=read_record(&out,&available);
        CHECK(r.error!=ESP_OK&&r.no_ownership==(stage==1));
    }
    for(unsigned which=0;which<2;++which)for(unsigned defect=0;defect<9;++defect){
        reset();mutate_at=which?10:2;mutation=defect;
        CHECK(read_record(&out,&available).error!=ESP_OK);
    }
    for(unsigned defect=0;defect<9;++defect){
        reset();mutate_at=11;mutation=defect;
        CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_RESPONSE&&!available&&calls==11);
    }
    reset();record.source_us=0;info.source_now_us=0;source_clock.source_now_us=1;
    CHECK(read_record(&out,&available).error==ESP_OK&&available&&out.source_us==0);
    reset();status.queued=0;CHECK(read_record(&out,&available).error==ESP_OK&&!available&&zero(&out,sizeof(out))&&calls==2);
    reset();binding.epoch=0;CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_ARG&&!calls);
    reset();link_mutex=NULL;CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_STATE&&!calls&&!takes);
    reset();deny_lock=true;CHECK(read_record(&out,&available).error==ESP_ERR_TIMEOUT&&!calls&&takes==1&&!gives);
    reset();lock_wait_us=200000;CHECK(read_record(&out,&available).error==ESP_ERR_TIMEOUT&&!calls&&takes==1&&gives==1);
}

static void check_sample_metadata(void)
{
    cerberus_csi_sample_t out;bool available;
    /* Correct CRC is generated after mutation. These cases test canonical
     * declared metadata, not corruption already detected by the checksum. */
    for(unsigned flags=0;flags<256;++flags){
        reset();record.flags=(uint8_t)flags;
        bool valid=flags==CERBERUS_CSI_SAMPLE_ESTIMATE_VALID||flags==(CERBERUS_CSI_SAMPLE_ESTIMATE_VALID|CERBERUS_CSI_SAMPLE_FIRST_INVALID);
        CHECK((read_record(&out,&available).error==ESP_OK)==valid&&available==valid);
    }
    for(unsigned phy=0;phy<256;++phy){
        reset();record.phy=(uint8_t)phy;bool valid=(phy>=1&&phy<=7)||phy==11;
        CHECK((read_record(&out,&available).error==ESP_OK)==valid&&available==valid);
    }
    for(unsigned secondary=0;secondary<256;++secondary){
        reset();record.secondary=(uint8_t)secondary;
        CHECK((read_record(&out,&available).error==ESP_OK)==(secondary<=2)&&available==(secondary<=2));
    }
    for(int rssi=-128;rssi<=127;++rssi){
        reset();record.rssi=(int8_t)rssi;bool valid=rssi>=-127&&rssi<=20;
        CHECK((read_record(&out,&available).error==ESP_OK)==valid&&available==valid);
    }
    for(unsigned channel=0;channel<256;++channel){
        reset();record.channel=(uint8_t)channel;
        bool valid=(channel>=1&&channel<=14)||(channel>=36&&channel<=64&&channel%4==0)||
            (channel>=100&&channel<=144&&channel%4==0)||(channel>=149&&channel<=177&&(channel-149)%4==0);
        CHECK((read_record(&out,&available).error==ESP_OK)==valid&&available==valid);
    }
    reset();record.source_us=info.source_now_us+1;
    CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_RESPONSE&&!available);
    reset();record.source_us=info.source_now_us=UINT64_MAX;
    CHECK(read_record(&out,&available).error==ESP_OK&&available&&out.source_us==UINT64_MAX);
    reset();record.source_us=info.source_now_us=UINT64_C(0x100000000)*1000+7;
    CHECK(read_record(&out,&available).error==ESP_OK&&available);
    reset();record.format=2;CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_RESPONSE);
    reset();record.sequence=0;CHECK(read_record(&out,&available).error==ESP_ERR_INVALID_RESPONSE);
}

static void check_poll(void)
{
    cerberus_csi_observation_t out;
    reset();cerberus_csi_operation_t r=ops.poll(NULL,&binding,&out);
    CHECK(!r.error&&!r.no_ownership&&out.status_valid&&out.timing_valid&&calls==6);
    CHECK(out.target.epoch==binding.epoch&&out.clock.source_now_us==source_clock.source_now_us);
    for(unsigned stage=1;stage<=6;++stage){
        reset();fail_at=stage;r=ops.poll(NULL,&binding,&out);
        CHECK(r.error&&!r.no_ownership&&!out.timing_valid);
        CHECK(zero(&out.target,sizeof(out.target))&&zero(&out.clock,sizeof(out.clock))&&zero(&out.counters,sizeof(out.counters)));
    }
    for(unsigned defect=0;defect<7;++defect){reset();mutate_at=1;mutation=defect;r=ops.poll(NULL,&binding,&out);CHECK(r.error&&!r.no_ownership&&!out.status_valid);}
    for(unsigned defect=0;defect<9;++defect){reset();mutate_at=3;mutation=defect;r=ops.poll(NULL,&binding,&out);CHECK(r.error&&!r.no_ownership&&!out.timing_valid);}
    for(unsigned stage=4;stage<=5;++stage)for(unsigned defect=0;defect<3;++defect){reset();mutate_at=stage;mutation=defect;r=ops.poll(NULL,&binding,&out);CHECK(r.error&&!r.no_ownership&&!out.timing_valid);}
    for(unsigned stage=2;stage<=6;stage+=4)for(unsigned defect=0;defect<9;++defect){
        reset();mutate_at=stage;mutation=defect;r=ops.poll(NULL,&binding,&out);
        CHECK(r.error&&!out.timing_valid);
        CHECK(r.no_ownership==(stage==2&&(defect==5||defect==6)));
    }
    reset();info.boot_id++;r=ops.poll(NULL,&binding,&out);CHECK(r.error&&r.no_ownership&&!out.status_valid&&calls==1);
    reset();binding.epoch=0;r=ops.poll(NULL,&binding,&out);CHECK(!r.error&&out.status.epoch==status.epoch&&out.timing_valid);
    reset();status.state=CERBERUS_CSI_COMPLETE;status.flags=0;status.channel=0;status.queued=0;source_clock.pending=0;
    r=ops.poll(NULL,&binding,&out);CHECK(!r.error&&out.status_valid&&out.timing_valid&&out.target.channel==6);
}

static void check_reserve_and_commands(void)
{
    cerberus_csi_target_t selected={.bssid={2,3,4,5,6,7},.channel=6};
    cerberus_csi_binding_t reserved;
    for(unsigned fw=0;fw<58;++fw){reset();peer.firmware_version=fw;CHECK(ops.reserve(NULL,&selected,&reserved)==ESP_ERR_NOT_SUPPORTED&&zero(&reserved,sizeof(reserved))&&peer_calls==1&&!calls);}
    reset();status=(cerberus_csi_status_t){.version=1};
    CHECK(ops.reserve(NULL,&selected,&reserved)==ESP_OK&&reserved.boot_id==info.boot_id&&reserved.job_id==1&&!reserved.epoch&&calls==2&&!start_calls);
    reset();status.state=CERBERUS_CSI_COMPLETE;status.flags=status.channel=status.queued=0;
    CHECK(ops.reserve(NULL,&selected,&reserved)==ESP_OK&&reserved.job_id==binding.job_id+1&&!reserved.epoch);
    reset();status.state=CERBERUS_CSI_COMPLETE;status.flags=status.channel=status.queued=0;status.job_id=UINT32_MAX;
    CHECK(ops.reserve(NULL,&selected,&reserved)==ESP_ERR_INVALID_STATE&&zero(&reserved,sizeof(reserved)));
    reset();CHECK(ops.reserve(NULL,&selected,&reserved)==ESP_ERR_INVALID_STATE&&zero(&reserved,sizeof(reserved)));
    reset();selected.bssid[0]|=1;CHECK(ops.reserve(NULL,&selected,&reserved)==ESP_ERR_INVALID_ARG&&!peer_calls&&!calls);selected.bssid[0]&=254;
    cerberus_csi_observation_t out;
    for(unsigned delivered=0;delivered<2;++delivered){
        reset();binding.job_id++;binding.epoch=0;start_delivery=delivered;fail_at=1;
        cerberus_csi_start_request_t start={.boot_id=binding.boot_id,.job_id=binding.job_id,.channel=6,.version=1,.lease_ms=5000};memcpy(start.bssid,selected.bssid,6);
        cerberus_csi_operation_t r=ops.start(NULL,&start,&out);CHECK(r.error&&!r.no_ownership&&!out.status_valid&&start_calls==1&&calls==1);
        fail_at=0;r=ops.poll(NULL,&binding,&out);
        CHECK(delivered?(!r.error&&out.timing_valid&&out.status.job_id==binding.job_id):(r.error&&r.no_ownership));
    }
    reset();cerberus_csi_renew_request_t renew={.boot_id=binding.boot_id,.job_id=binding.job_id,.epoch=binding.epoch,.lease_ms=5000};
    CHECK(ops.renew(NULL,&renew,&out).error==ESP_OK&&renew_calls==1);
    reset();status.state=CERBERUS_CSI_STOPPING;status.flags=1;status.channel=0;status.queued=0;
    CHECK(ops.stop(NULL,&binding,&out).error==ESP_OK&&stop_calls==1&&out.status.flags==1);
    reset();status.state=CERBERUS_CSI_COMPLETE;status.flags=0;status.channel=0;status.queued=0;
    fail_at=2;cerberus_csi_operation_t r=ops.stop(NULL,&binding,&out);
    CHECK(r.error&&!r.no_ownership&&out.status_valid&&out.status.state==CERBERUS_CSI_COMPLETE&&!out.timing_valid);
    reset();cerberus_csi_ack_request_t ack={.job_id=binding.job_id,.epoch=binding.epoch,.sequence=record.sequence};
    CHECK(!ops.ack(NULL,&ack).error&&counters.acked==1&&status.queued==0);
    CHECK(!ops.ack(NULL,&ack).error&&counters.acked==1&&status.queued==0&&ack_calls==2&&!read_calls);
    ack.sequence++;CHECK(ops.ack(NULL,&ack).error&&!status.queued&&counters.acked==1);
    reset();ack=(cerberus_csi_ack_request_t){.job_id=binding.job_id,.epoch=binding.epoch,.sequence=record.sequence};fail_at=1;
    r=ops.ack(NULL,&ack);CHECK(r.error&&!r.no_ownership&&ack_calls==1&&counters.acked==1);
    fail_at=0;CHECK(!ops.ack(NULL,&ack).error&&counters.acked==1);
}

static void check_reservation_floor(void)
{
    cerberus_csi_target_t selected={.bssid={2,3,4,5,6,7},.channel=6};
    cerberus_csi_binding_t a,b;
    reset();status=(cerberus_csi_status_t){.version=1};
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_OK&&a.job_id==1);
    /* A pre-radio rejected/undelivered START leaves the remote status idle.
     * Reopening and reserving again still cannot reuse the local attempted ID. */
    CHECK(ops.reserve(NULL,&selected,&b)==ESP_OK&&b.job_id==2&&a.boot_id==b.boot_id);
    CHECK(!start_calls&&csi_reserved_job==2&&csi_reserved_boot==info.boot_id);
    status=(cerberus_csi_status_t){.version=1,.job_id=50,.epoch=11,.state=CERBERUS_CSI_COMPLETE};
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_OK&&a.job_id==51);
    status.job_id=3;
    CHECK(ops.reserve(NULL,&selected,&b)==ESP_OK&&b.job_id==52);
    /* Transient failed reservations neither overwrite a prior floor nor burn
     * another ID. Changing peer boot legitimately starts a fresh namespace. */
    uint64_t saved_boot=csi_reserved_boot;uint32_t saved_job=csi_reserved_job;
    fail_at=calls+1;
    CHECK(ops.reserve(NULL,&selected,&a)!=ESP_OK&&zero(&a,sizeof(a)));
    CHECK(csi_reserved_boot==saved_boot&&csi_reserved_job==saved_job);
    fail_at=0;info.boot_id++;status=(cerberus_csi_status_t){.version=1};
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_OK&&a.job_id==1&&a.boot_id!=saved_boot);
    reset();status=(cerberus_csi_status_t){.version=1};
    csi_reserved_boot=info.boot_id;csi_reserved_job=UINT32_MAX-1;
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_OK&&a.job_id==UINT32_MAX);
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_ERR_INVALID_STATE&&zero(&a,sizeof(a))&&csi_reserved_job==UINT32_MAX);
    info.boot_id++;
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_OK&&a.job_id==1);
    reset();status=(cerberus_csi_status_t){.version=1};lock_delay_at=3;
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_ERR_TIMEOUT&&zero(&a,sizeof(a))&&!csi_reserved_job&&!csi_reserved_boot);
    CHECK(calls==2&&takes==3&&gives==3&&!held);
    for(unsigned protocol=0;protocol<256;++protocol){
        if(protocol==CERBERUS_PROTOCOL_VERSION)continue;
        reset();peer.protocol_version=(uint8_t)protocol;
        CHECK(ops.reserve(NULL,&selected,&a)==ESP_ERR_INVALID_RESPONSE&&zero(&a,sizeof(a))&&!calls);
    }
    reset();peer.capabilities=CERBERUS_C5_CAP_WIFI_5_GHZ;
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_ERR_NOT_SUPPORTED&&zero(&a,sizeof(a))&&!calls);
    reset();peer.capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ;selected.channel=149;
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_ERR_NOT_SUPPORTED&&zero(&a,sizeof(a))&&!calls);
    reset();peer.capabilities=CERBERUS_C5_CAP_WIFI_5_GHZ;status=(cerberus_csi_status_t){.version=1};
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_OK&&a.job_id==1);
    reset();peer_error=ESP_ERR_TIMEOUT;
    CHECK(ops.reserve(NULL,&selected,&a)==ESP_ERR_TIMEOUT&&zero(&a,sizeof(a))&&!calls);
}

static void check_frames_and_diagnostics(void)
{
    cerberus_csi_observation_t out;cerberus_csi_info_t details;
    for(unsigned bit=0;bit<sizeof(cerberus_spi_frame_t)*8;++bit){reset();bit_corrupt=(int)bit;cerberus_csi_operation_t r=ops.poll(NULL,&binding,&out);CHECK(r.error&&!r.no_ownership&&!out.status_valid&&calls==1);}
    reset();CHECK(cerberus_csi_link_diagnostics(&details,&out)==ESP_OK&&out.timing_valid&&!start_calls&&!stop_calls&&!renew_calls&&!ack_calls&&!read_calls);
    reset();peer.firmware_version=57;CHECK(cerberus_csi_link_diagnostics(&details,&out)==ESP_ERR_NOT_SUPPORTED&&!calls&&!out.status_valid&&zero(&details,sizeof(details)));
    reset();status=(cerberus_csi_status_t){.version=1};CHECK(cerberus_csi_link_diagnostics(&details,&out)==ESP_OK&&out.status_valid&&!out.timing_valid&&calls==2);
    reset();CHECK(cerberus_csi_link_diagnostics(NULL,&out)==ESP_ERR_INVALID_ARG&&!calls);
    cerberus_csi_link_ops(NULL);
}

int main(void)
{
    check_read();check_sample_metadata();check_poll();check_reserve_and_commands();check_reservation_floor();check_frames_and_diagnostics();
    printf("CSI production transport: %u checks passed; exact frame/record fences, deadlines, ownership and no RF initialization.\n",checks);
    return 0;
}
