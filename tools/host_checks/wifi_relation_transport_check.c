#include "c5_spi_link.h"
#include "wardrive_service.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"Relation transport line %u: %s\n",__LINE__,#x); exit(1); } } while(0)
typedef unsigned TickType_t;
typedef int BaseType_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static void *link_mutex=(void *)1;
static uint8_t sequence;
static bool held,fail_mutex,delay_mutex;
static int64_t now_us;
static unsigned exchanges,takes,gives,fail_at,delay_at,cancel_at,mutate_at,mutation;
static cerberus_status_payload_t peer;
static cerberus_wifi_relation_status_t status;
static cerberus_wifi_relation_row_t remote_rows[32];
static uint64_t source_now;
static unsigned chunk_requests, scan_requests;
static esp_err_t scan_error;
static uint8_t scan_state;
static bool scan_malformed, scan_delayed;
static char console_output[4096];
static size_t console_used;

static int64_t esp_timer_get_time(void) { return now_us; }
static BaseType_t take_link_mutex(TickType_t ticks)
{
    CHECK(!held&&ticks<=20U);++takes;
    if(fail_mutex)return 0;
    held=true;
    if(delay_mutex)now_us+=6000000;
    return pdTRUE;
}
static void give_link_mutex(void) { CHECK(held);held=false;++gives; }
static bool proceed(void *context)
{
    CHECK(context==(void *)7&&!held);
    return !cancel_at||exchanges<cancel_at;
}

static void spi_request_response(const cerberus_spi_frame_t *request,void *discarded,
    cerberus_spi_frame_t *response,size_t size)
{
    (void)discarded;CHECK(held&&size==sizeof(*request));
    CHECK(cerberus_protocol_frame_is_valid(request));++exchanges;
    uint8_t bytes[22]={0};size_t length=0;
    cerberus_spi_command_t command=(cerberus_spi_command_t)request->command;
    if(command==CERBERUS_SPI_COMMAND_STATUS){
        CHECK(request->payload_length==0);memcpy(bytes,&peer,sizeof(peer));length=sizeof(peer);
    }else if(command==CERBERUS_SPI_COMMAND_WIFI_RELATION_STATUS){
        CHECK(request->payload_length==1&&request->payload[0]==status.generation);
        memcpy(bytes,&status,sizeof(status));length=sizeof(status);
    }else if(command==CERBERUS_SPI_COMMAND_WIFI_RELATION_CLOCK){
        cerberus_wifi_relation_clock_query_t q;memcpy(&q,request->payload,sizeof(q));
        CHECK(request->payload_length==sizeof(q));
        if(q.epoch!=status.epoch||q.generation!=status.generation){memset(response,0,sizeof(*response));return;}
        cerberus_wifi_relation_clock_t clock={.epoch=status.epoch,.source_now_us=source_now,
            .generation=status.generation,.count=status.count};
        memcpy(bytes,&clock,sizeof(clock));length=sizeof(clock);
    }else if(command==CERBERUS_SPI_COMMAND_WIFI_RELATION_CHUNK){
        ++chunk_requests;
        cerberus_wifi_relation_query_t q;memcpy(&q,request->payload,sizeof(q));
        CHECK(request->payload_length==sizeof(q));
        if(q.epoch!=status.epoch||q.generation!=status.generation){memset(response,0,sizeof(*response));return;}
        CHECK(q.index<status.count&&q.chunk<5);
        size_t offset=(size_t)q.chunk*10U,n=sizeof(remote_rows[0])-offset;if(n>10)n=10;
        cerberus_wifi_relation_chunk_t chunk={.epoch=status.epoch,.generation=status.generation,
            .index=q.index,.chunk=q.chunk,.length=(uint8_t)n};
        memcpy(chunk.data,(uint8_t *)&remote_rows[q.index]+offset,n);
        memcpy(bytes,&chunk,sizeof(chunk));length=sizeof(chunk);
    }else CHECK(false);
    if(exchanges==mutate_at){
        switch(mutation){
            case 0:bytes[0]^=1;break; /* epoch or peer readiness */
            case 1:bytes[8]^=1;break; /* generation */
            case 2:bytes[9]^=1;break; /* index/count */
            case 3:bytes[10]^=1;break; /* chunk/state */
            case 4:bytes[11]^=1;break; /* declared length/flags */
            case 5:bytes[21]=1;break; /* canonical tail padding */
            case 6:peer.boot_count++;break;
            case 7:peer.firmware_version=59;break;
            case 8:peer.capabilities=0;break;
            case 9:peer.reset_reason++;break;
            case 10:peer.uptime_ms=0;break;
            case 11:source_now=0;break;
            case 12:status.epoch++;break;
            case 13:status.state=CERBERUS_WIFI_RELATION_RUNNING;break;
            case 17:--source_now;break;
            default:break;
        }
    }
    CHECK(cerberus_protocol_prepare_frame(response,command,request->sequence,bytes,length));
    if(exchanges==fail_at)response->payload[0]^=0x40; /* CRC fails, no retry. */
    if(exchanges==mutate_at&&mutation==14)response->sequence++;
    if(exchanges==mutate_at&&mutation==15)response->command++;
    if(exchanges==mutate_at&&mutation==16)response->payload_length--;
    if(exchanges==delay_at)now_us+=INT64_C(6000000);
    now_us+=1000;
}

#include "relation_peer_impl.h"
#include "c5_wifi_relation_link_impl.h"

static const char *esp_err_to_name(esp_err_t error)
{
    switch(error){
        case ESP_OK:return "ESP_OK";
        case ESP_ERR_NOT_SUPPORTED:return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_NOT_FINISHED:return "ESP_ERR_NOT_FINISHED";
        case ESP_ERR_TIMEOUT:return "ESP_ERR_TIMEOUT";
        default:return "ERROR";
    }
}
static void console_printf(const char *format,...)
{
    CHECK(!held);
    va_list args;va_start(args,format);
    int added=vsnprintf(console_output+console_used,sizeof(console_output)-console_used,format,args);
    va_end(args);
    CHECK(added>=0&&(size_t)added<sizeof(console_output)-console_used);
    console_used+=(size_t)added;
}
static void console_write(const char *value) { console_printf("%s",value); }
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *out)
{
    CHECK(!held);++scan_requests;
    *out=(cerberus_wifi_scan_status_payload_t){.generation=status.generation,.state=scan_state,
        .stored_count=scan_malformed?1:0};
    if(scan_delayed)now_us+=301000;
    return scan_error;
}
#include "relation_console_impl.h"

static void reset(unsigned count)
{
    CHECK(!held);exchanges=takes=gives=0;fail_at=delay_at=cancel_at=mutate_at=mutation=0;
    chunk_requests=scan_requests=0;scan_error=ESP_OK;scan_state=CERBERUS_WIFI_SCAN_COMPLETE;
    scan_malformed=scan_delayed=false;console_used=0;memset(console_output,0,sizeof(console_output));
    fail_mutex=delay_mutex=false;link_mutex=(void *)1;now_us=1000000;sequence=0;source_now=500000;
    peer=(cerberus_status_payload_t){.ready=1,.firmware_version=60,.uptime_ms=500,
        .capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ|CERBERUS_C5_CAP_WIFI_5_GHZ,.boot_count=2};
    status=(cerberus_wifi_relation_status_t){.epoch=UINT64_C(0x123456789abcdef0),.generation=255,
        .count=(uint8_t)count,.state=CERBERUS_WIFI_RELATION_COMPLETE,
        .flags=CERBERUS_WIFI_RELATION_AVAILABLE,.matched_frames=count*3U};
    memset(remote_rows,0,sizeof(remote_rows));
    for(unsigned i=0;i<count;++i){
        remote_rows[i]=(cerberus_wifi_relation_row_t){.bssid={2,2,3,4,5,6},.station={4,2,3,4,5,(uint8_t)i},
            .channel=i%2?149:6,.directions=3,.latest_direction=2,.rssi_valid=3,
            .to_ap_frames=1,.from_ap_frames=2,.to_ap_callback_us=100000,.from_ap_callback_us=200000,
            .to_ap_rssi=-45,.from_ap_rssi=-80};
    }
}

static bool zero(const void *data,size_t size)
{const uint8_t *p=data;for(size_t i=0;i<size;++i)if(p[i])return false;return true;}

static esp_err_t drive(cerberus_c5_wifi_relation_reader_t *reader,cerberus_c5_wifi_relation_bank_t *proof)
{
    bool complete=false;esp_err_t error=ESP_OK;
    for(unsigned tick=0;error==ESP_OK&&!complete&&tick<200;++tick){
        unsigned before=exchanges;
        error=c5_spi_link_wifi_relation_step(reader,proof,&complete,proceed,(void *)7);
        CHECK(exchanges-before<=1U&&!held);
        if(!complete)CHECK(zero(proof,sizeof(*proof)));
    }
    CHECK(error!=ESP_OK||complete);
    return error;
}

static esp_err_t read_bank(cerberus_c5_wifi_relation_reader_t *reader,
    cerberus_wifi_relation_row_t *rows,size_t capacity,cerberus_c5_wifi_relation_bank_t *proof)
{
    CHECK(c5_spi_link_wifi_relation_begin(reader,status.generation,(uint32_t)(now_us/1000)+5000,rows,capacity)==ESP_OK);
    CHECK(exchanges==0);bool done=false;esp_err_t error=ESP_OK;
    for(unsigned tick=0;error==ESP_OK&&!done&&tick<200;++tick){
        unsigned before=exchanges;
        error=c5_spi_link_wifi_relation_step(reader,proof,&done,proceed,(void *)7);
        CHECK(exchanges-before<=1U&&!held);
        if(!done)CHECK(zero(proof,sizeof(*proof)));
    }
    CHECK(error!=ESP_OK||done);
    CHECK(takes==gives||fail_mutex);
    return error;
}

static void happy_and_bounds(void)
{
    for(unsigned count=0;count<=32;++count){
        reset(count);cerberus_c5_wifi_relation_reader_t reader={0};
        cerberus_wifi_relation_row_t rows[33];memset(rows,0x5a,sizeof(rows));
        cerberus_c5_wifi_relation_bank_t proof;
        CHECK(read_bank(&reader,rows,32,&proof)==ESP_OK);
        CHECK(exchanges==6U+count*5U);
        CHECK(!memcmp(rows,remote_rows,count*sizeof(rows[0])));
        CHECK(zero(rows+count,(32-count)*sizeof(rows[0])));
        CHECK(((uint8_t *)&rows[32])[0]==0x5a);
        CHECK(proof.clock_valid&&proof.request_started_us==1000000&&proof.source_now_us==500000);
        CHECK(proof.status.epoch==status.epoch&&proof.status.count==count);
        unsigned before=exchanges;now_us+=10000000;bool done=false;
        CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&done,NULL,NULL)==ESP_OK&&done&&exchanges==before);
        cerberus_s3_wifi_relation_bank_t model={0};
        CHECK(cerberus_s3_wifi_relation_apply(&model,1,&proof,rows,count,(uint64_t)now_us)==1);
    }
    reset(0);status.flags=0;status.epoch=0;
    cerberus_c5_wifi_relation_reader_t reader={0};cerberus_c5_wifi_relation_bank_t proof;
    CHECK(read_bank(&reader,NULL,0,&proof)==ESP_OK&&exchanges==4&&!proof.clock_valid&&!proof.request_started_us);
    reset(1);cerberus_wifi_relation_row_t rows[32];
    CHECK(read_bank(&reader,rows,0,&proof)==ESP_ERR_INVALID_SIZE&&zero(&proof,sizeof(proof)));
    reset(1);memset(rows,0x55,sizeof(rows));
    CHECK(c5_spi_link_wifi_relation_begin(&reader,255,6000,rows,33)==ESP_ERR_INVALID_ARG);
    CHECK(zero(rows,sizeof(rows))&&exchanges==0);
}

static void every_boundary_failure(void)
{
    for(unsigned kind=0;kind<3;++kind)for(unsigned stage=1;stage<=166;++stage){
        reset(32);if(kind==0)fail_at=stage;else if(kind==1)delay_at=stage;else cancel_at=stage;
        cerberus_c5_wifi_relation_reader_t reader={0};cerberus_wifi_relation_row_t rows[32];
        cerberus_c5_wifi_relation_bank_t proof;
        esp_err_t error=read_bank(&reader,rows,32,&proof);
        CHECK(error==(kind==0?ESP_ERR_INVALID_RESPONSE:kind==1?ESP_ERR_TIMEOUT:ESP_ERR_NOT_FINISHED));
        CHECK(zero(rows,sizeof(rows))&&zero(&proof,sizeof(proof))&&exchanges==stage);
        unsigned before=exchanges;bool complete=true;
        CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&complete,NULL,NULL)==error&&!complete&&exchanges==before);
    }
}

static void malformed_identity_and_fields(void)
{
    for(unsigned kind=0;kind<=16;++kind){
        reset(1);mutate_at=kind<=5?8:4;mutation=kind; /* Last row chunk for header/padding. */
        cerberus_c5_wifi_relation_reader_t reader={0};cerberus_wifi_relation_row_t rows[32];
        cerberus_c5_wifi_relation_bank_t proof;
        CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_INVALID_RESPONSE);
        CHECK(zero(rows,sizeof(rows))&&zero(&proof,sizeof(proof)));
    }
    for(unsigned kind=0;kind<14;++kind){
        reset(2);
        switch(kind){
            case 0:remote_rows[1]=remote_rows[0];break;
            case 1:remote_rows[1].reserved[0]=1;break;
            case 2:remote_rows[1].bssid[0]|=1;break;
            case 3:remote_rows[1].station[0]|=1;break;
            case 4:remote_rows[1].channel=200;break;
            case 5:remote_rows[1].directions=4;break;
            case 6:remote_rows[1].latest_direction=1;break;
            case 7:remote_rows[1].rssi_valid=0;break;
            case 8:remote_rows[1].to_ap_rssi=1;break;
            case 9:remote_rows[1].to_ap_frames=0;break;
            case 10:remote_rows[1].from_ap_callback_us=UINT64_MAX;break;
            case 11:remote_rows[1].from_ap_callback_us=500001;break;
            case 12:status.matched_frames++;break;
            default:peer.capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ;break;
        }
        cerberus_c5_wifi_relation_reader_t reader={0};cerberus_wifi_relation_row_t rows[32];
        cerberus_c5_wifi_relation_bank_t proof;
        CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_INVALID_RESPONSE);
        CHECK(zero(rows,sizeof(rows))&&zero(&proof,sizeof(proof)));
    }
}

static void lifecycle_and_wrappers(void)
{
    cerberus_c5_wifi_relation_reader_t reader={0};cerberus_wifi_relation_row_t rows[32];
    cerberus_c5_wifi_relation_bank_t proof;bool complete;
    reset(1);peer.firmware_version=59;
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_NOT_SUPPORTED&&exchanges==1&&zero(rows,sizeof(rows)));
    reset(1);peer.capabilities=0;
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_INVALID_RESPONSE&&exchanges==1);
    reset(1);fail_mutex=true;
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_TIMEOUT&&exchanges==0);
    reset(1);delay_mutex=true;
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_TIMEOUT&&exchanges==0&&takes==gives);
    reset(1);link_mutex=NULL;
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_INVALID_STATE&&exchanges==0);
    reset(1);now_us=-1;
    CHECK(c5_spi_link_wifi_relation_begin(&reader,255,5000,rows,32)==ESP_OK);
    CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&complete,NULL,NULL)==ESP_ERR_INVALID_STATE&&exchanges==0);
    reset(1);CHECK(c5_spi_link_wifi_relation_begin(&reader,255,1000,rows,32)==ESP_OK);
    CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&complete,NULL,NULL)==ESP_ERR_TIMEOUT&&exchanges==0);
    reset(1);status.state=CERBERUS_WIFI_RELATION_RUNNING;
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_ERR_NOT_FINISHED&&exchanges==2);
    reset(1);now_us=(int64_t)(UINT64_C(4294967296)*1000U-2000U);
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_OK);
    CHECK(proof.request_started_us==UINT64_C(4294967296)*1000U-2000U);
    reset(1);source_now=0;remote_rows[0].to_ap_callback_us=remote_rows[0].from_ap_callback_us=0;now_us=0;
    CHECK(read_bank(&reader,rows,32,&proof)==ESP_OK&&proof.clock_valid&&!proof.request_started_us&&!proof.source_now_us);
    reset(2);CHECK(c5_spi_link_copy_wifi_relations(255,6000,&proof,rows,32,proceed,(void *)7)==ESP_OK);
    CHECK(exchanges==16&&proof.status.count==2);
    cerberus_c5_wifi_relation_bank_t frozen=proof;
    CHECK(c5_spi_link_check_wifi_relation_bank(&frozen,6000)==ESP_OK&&exchanges==22);
    peer.firmware_version=59;
    CHECK(c5_spi_link_check_wifi_relation_bank(&frozen,6000)==ESP_ERR_INVALID_RESPONSE);
    reset(2);CHECK(c5_spi_link_wifi_relation_begin(&reader,255,6000,rows,32)==ESP_OK);
    for(unsigned i=0;i<8;++i)CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&complete,NULL,NULL)==ESP_OK&&!complete);
    CHECK(!zero(rows,sizeof(rows)));unsigned before=exchanges;
    c5_spi_link_wifi_relation_cancel(&reader);CHECK(exchanges==before&&zero(rows,sizeof(rows)));
    CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&complete,NULL,NULL)==ESP_ERR_NOT_FINISHED&&!complete&&exchanges==before);
}

static void exact_ap_capture_binding(void)
{
    cerberus_c5_wifi_relation_reader_t reader={0};cerberus_wifi_relation_row_t rows[32];
    cerberus_c5_wifi_relation_bank_t captured,proof;bool complete=false;
    reset(2);
    CHECK(c5_spi_link_get_wifi_relation_bank(255,6000,&captured)==ESP_OK);
    cerberus_c5_wifi_relation_bank_t original=captured;
    now_us+=3000000;source_now+=3000000;
    CHECK(c5_spi_link_wifi_relation_begin_expected(&reader,&captured,10000,rows,32)==ESP_OK);
    CHECK(!memcmp(&reader.expected,&original,sizeof(original)));
    CHECK(drive(&reader,&proof)==ESP_OK);
    CHECK(proof.request_started_us==original.request_started_us&&proof.source_now_us==original.source_now_us);
    CHECK(!memcmp(&captured,&original,sizeof(original)));
    CHECK(!memcmp(rows,remote_rows,2*sizeof(rows[0])));
    for(unsigned change=0;change<9;++change){
        reset(2);CHECK(c5_spi_link_get_wifi_relation_bank(255,6000,&captured)==ESP_OK);
        if(change==0)status.epoch+=256; /* Same public generation/count, different scan. */
        else if(change==1)peer.boot_count++;
        else if(change==2)peer.firmware_version=59;
        else if(change==3)peer.reset_reason++;
        else if(change==4)peer.capabilities^=CERBERUS_C5_CAP_WIFI_5_GHZ;
        else if(change==5)status.state=CERBERUS_WIFI_RELATION_RUNNING;
        else if(change==6)source_now--;
        else if(change==7)remote_rows[1].from_ap_callback_us=source_now+1;
        else {source_now+=1000;mutate_at=exchanges+4;mutation=17;}
        CHECK(c5_spi_link_wifi_relation_begin_expected(&reader,&captured,6000,rows,32)==ESP_OK);
        CHECK(drive(&reader,&proof)==ESP_ERR_INVALID_RESPONSE);
        CHECK(zero(rows,sizeof(rows))&&zero(&proof,sizeof(proof)));
    }
    reset(2);CHECK(c5_spi_link_get_wifi_relation_bank(255,6000,&captured)==ESP_OK);
    CHECK(c5_spi_link_wifi_relation_begin_expected(&reader,&captured,6000,rows,32)==ESP_OK);
    CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&complete,NULL,NULL)==ESP_OK&&!complete);
    unsigned before=exchanges;now_us-=2000;
    CHECK(c5_spi_link_wifi_relation_step(&reader,&proof,&complete,NULL,NULL)==ESP_ERR_INVALID_STATE);
    CHECK(exchanges==before&&zero(rows,sizeof(rows)));
    captured.status.epoch=0;
    CHECK(c5_spi_link_wifi_relation_begin_expected(&reader,&captured,6000,rows,32)==ESP_ERR_INVALID_ARG);
    CHECK(exchanges==before&&zero(rows,sizeof(rows)));
}

static void console_metadata_only(void)
{
    reset(0);scan_state=CERBERUS_WIFI_SCAN_IDLE;status.epoch=0;
    status.flags=0;status.state=CERBERUS_WIFI_RELATION_IDLE;
    print_wifi_relations();
    CHECK(strstr(console_output,"processor=C5_WIFI_RELATION\r\n")!=NULL);
    CHECK(strstr(console_output,"relation_evidence=not_complete\r\n")!=NULL);
    CHECK(strstr(console_output,"stored=")==NULL&&exchanges==2&&chunk_requests==0);
    reset(0);peer.firmware_version=59;print_wifi_relations();
    CHECK(strstr(console_output,"relation_evidence=unsupported\r\n")!=NULL);
    CHECK(exchanges==1&&chunk_requests==0);
    reset(0);status.flags=0;status.epoch=0;print_wifi_relations();
    CHECK(strstr(console_output,"relation_evidence=unavailable\r\n")!=NULL);
    CHECK(strstr(console_output,"stored=")==NULL&&exchanges==4&&chunk_requests==0);
    reset(0);print_wifi_relations();
    CHECK(strstr(console_output,"relation_evidence=completed_bank\r\n")!=NULL);
    CHECK(strstr(console_output,"stored=0\r\ncapacity=32\r\ncallbacks=0\r\n")!=NULL);
    CHECK(strstr(console_output,"relation_scope=DS_header_pairs_per_channel_not_association\r\n")!=NULL);
    CHECK(exchanges==6&&chunk_requests==0&&scan_requests==1);
    reset(32);status.flags|=CERBERUS_WIFI_RELATION_OVERFLOW;
    status.matched_frames=status.dropped_frames=UINT32_MAX;
    print_wifi_relations();
    CHECK(strstr(console_output,"stored=32\r\ncapacity=32\r\ncallbacks=4294967295\r\ncapacity_drops=4294967295\r\noverflow=1\r\n")!=NULL);
    CHECK(strstr(console_output,"02:02:")==NULL&&strstr(console_output,"epoch=")==NULL);
    CHECK(exchanges==6&&chunk_requests==0);
    for(unsigned failed=1;failed<=6;++failed){
        reset(1);fail_at=failed;print_wifi_relations();
        CHECK(strstr(console_output,"relation_evidence=read_error\r\n")!=NULL);
        CHECK(strstr(console_output,"completed_bank")==NULL&&strstr(console_output,"callbacks=")==NULL);
        CHECK(exchanges==failed&&chunk_requests==0&&!held);
    }
    reset(1);scan_error=ESP_ERR_TIMEOUT;print_wifi_relations();
    CHECK(strstr(console_output,"relation_evidence=read_error\r\n")!=NULL&&exchanges==0);
    reset(1);scan_malformed=true;print_wifi_relations();
    CHECK(strstr(console_output,"relation_evidence=read_error\r\n")!=NULL&&exchanges==0);
    reset(1);now_us=-1;print_wifi_relations();
    CHECK(strstr(console_output,"relation_evidence=read_error\r\n")!=NULL&&scan_requests==0&&exchanges==0);
    reset(1);scan_delayed=true;print_wifi_relations();
    CHECK(strstr(console_output,"transport=ESP_ERR_TIMEOUT\r\n")!=NULL&&scan_requests==1&&exchanges==0);
    reset(0);
    cerberus_wardrive_status_t summary={.c5_relation_state=CERBERUS_WARDRIVE_RELATION_AVAILABLE,
        .c5_relation_observations=UINT32_MAX,.c5_relation_logged=UINT32_MAX-1,
        .c5_relation_frames=UINT32_MAX-2,.c5_relation_dropped=UINT32_MAX-3,
        .c5_relation_unavailable_sweeps=UINT32_MAX-4};
    print_wardrive_relations(&summary);
    CHECK(strstr(console_output,"relation_state=1\r\nrelation_observations=4294967295\r\nrelation_logged=4294967294\r\n")!=NULL);
    CHECK(strstr(console_output,"relation_frame_scope=matched_callbacks_in_fully_drained_sweeps\r\n")!=NULL);
    CHECK(strstr(console_output,"relation_frames=4294967293\r\nrelation_dropped=4294967292\r\nrelation_unavailable_sweeps=4294967291\r\n")!=NULL);
    CHECK(scan_requests==0&&exchanges==0&&chunk_requests==0);
    reset(0);memset(&summary,0,sizeof(summary));print_wardrive_relations(&summary);
    CHECK(strstr(console_output,"relation_state=0\r\nrelation_observations=0\r\nrelation_logged=0\r\n")!=NULL);
    CHECK(strstr(console_output,"relation_frames=0\r\nrelation_dropped=0\r\nrelation_unavailable_sweeps=0\r\n")!=NULL);
}

int main(void)
{
    happy_and_bounds();every_boundary_failure();malformed_identity_and_fields();lifecycle_and_wrappers();exact_ap_capture_binding();
    console_metadata_only();
    printf("Incremental relationship transport: %u checks; reader=%zu proof=%zu rows=%zu bytes; one physical exchange per step\n",
        checks,sizeof(cerberus_c5_wifi_relation_reader_t),sizeof(cerberus_c5_wifi_relation_bank_t),sizeof(remote_rows));
    return 0;
}
