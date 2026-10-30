/* Reuse the normal scan platform fixture. All receive/worker/start/stop/getter
 * bodies below are extracted production code; only SDK/task/lock stubs remain. */
#define main observation_fixture_main
#include "wifi_scan_observation_lifecycle_check.c"
#undef main

static unsigned hook_kind, response_count;
static uint8_t reply_bytes[22],reply_length,reply_sequence,reply_command;
static void prepare_frame_response(cerberus_spi_command_t command,uint8_t sequence,const void *p,size_t n)
{
    CHECK(n<=22);memset(reply_bytes,0,sizeof(reply_bytes));memcpy(reply_bytes,p,n);
    reply_length=(uint8_t)n;reply_sequence=sequence;reply_command=(uint8_t)command;++response_count;
}
static void command(const cerberus_spi_frame_t *request)
{
    switch((cerberus_spi_command_t)request->command){
#include "wifi_probe_dispatch.h"
    default: CHECK(false);break;
    }
}
static void probe_packet(void)
{
    memset(&received,0,sizeof(received));received.rx_ctrl.channel=149;received.rx_ctrl.rssi=-88;
    received.payload[0]=0x40;memset(received.payload+4,255,6);memset(received.payload+16,255,6);
    const uint8_t mac[6]={2,1,2,3,4,5};memcpy(received.payload+10,mac,6);
    const char name[]="my-FLOCK-probe";received.payload[24]=0;received.payload[25]=sizeof(name)-1;
    memcpy(received.payload+26,name,sizeof(name)-1);received.rx_ctrl.sig_len=26+sizeof(name)-1+4;
    memset(received.payload+received.rx_ctrl.sig_len-4,0xa5,4); /* FCS is never parsed as IEs. */
}
static cerberus_wifi_probe_status_t status(void)
{
    cerberus_wifi_probe_status_t s;memset(&s,0xa5,sizeof(s));
    CHECK(cerberus_wifi_probe_get_status(scan_status.generation,&s)==ESP_OK);
    CHECK(cerberus_wifi_probe_status_valid(&s));return s;
}
static cerberus_wifi_probe_row_t row(cerberus_wifi_probe_status_t s)
{
    cerberus_wifi_probe_row_t r={0};
    for(unsigned i=0;i<6;++i){cerberus_wifi_probe_query_t q={s.epoch,s.generation,0,(uint8_t)i};
        cerberus_wifi_probe_chunk_t c;
        CHECK(cerberus_wifi_probe_get_result_chunk(&q,&c)==ESP_OK);
        CHECK(c.epoch==s.epoch&&c.generation==s.generation&&c.index==0&&c.chunk==i&&c.length==(i==5?5:10));
        memcpy((uint8_t *)&r+i*10,c.data,c.length);for(unsigned j=c.length;j<10;++j)CHECK(c.data[j]==0);}
    CHECK(cerberus_wifi_probe_row_valid(&r));return r;
}
static void delay_consumer(void)
{
    probe_test_consumer_hook=NULL;now_us+=50000;
    if(hook_kind){CHECK(cerberus_wifi_scanner_stop()==ESP_OK);finish_scan(ESP_OK);
        if(hook_kind==2){CHECK(cerberus_wifi_scanner_start()==ESP_OK);cerberus_wifi_probe_arm(&probe_capture,true,0);}
        if(hook_kind==3){for(unsigned i=0;i<256;++i){CHECK(cerberus_wifi_scanner_start()==ESP_OK);
            if(i<255)finish_scan(ESP_OK);}cerberus_wifi_probe_arm(&probe_capture,true,0);}}
}
static void delay_before_admission(void)
{
    /* The callback already sampled its timestamp but has not read any epoch. */
    now_us+=50000;CHECK(cerberus_wifi_scanner_stop()==ESP_OK);finish_scan(ESP_OK);
    CHECK(cerberus_wifi_scanner_start()==ESP_OK);
    cerberus_wifi_probe_arm(&probe_capture,true,now_us);
}
static void lifecycle(void)
{
    reset();probe_packet();wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
    CHECK(!probe_capture.status.count);CHECK(cerberus_wifi_scanner_start()==ESP_OK);
    CHECK(status().state==1&&status().epoch==1&&!status().flags);
    wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);CHECK(!status().count); /* queued, not armed */
    CHECK(cerberus_wifi_scanner_start()==ESP_ERR_INVALID_STATE);CHECK(status().epoch==1);
    dispatch();cerberus_wifi_probe_status_t s=status();cerberus_wifi_probe_row_t r=row(s);
    CHECK(s.state==2&&s.flags==1&&s.count==1&&s.matched_frames==1&&!s.dropped_frames);
    CHECK(r.callback_ms==1001&&r.rssi==-88&&r.channel==149&&r.name_bits==1&&!r.oui_hint&&r.frames==1);
    CHECK(scan_calls==1&&enable_calls==1&&disable_calls==1&&scan_status.stored_count==1);
    uint64_t frozen=r.callback_ms;received.rx_ctrl.rssi=-1;now_us+=1000000;
    wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);r=row(s);CHECK(r.callback_ms==frozen&&r.rssi==-88&&r.frames==1);
    /* Enable/filter failure is explicit unavailable, while AP inventory can complete. */
    for(unsigned fault=0;fault<6;++fault){reset();probe_packet();filter_failure=fault==0;enable_failure=fault==1;
        scan_failure=fault==2;disable_failure=fault==3;records_failure=fault==4;num_failure=fault==5;
        CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();s=status();
        CHECK(s.state==(fault<2?2:3)&&!s.flags&&!s.count&&!s.matched_frames&&!s.dropped_frames);
        cerberus_wifi_probe_query_t q={s.epoch,s.generation,0,0};cerberus_wifi_probe_chunk_t c;
        CHECK(cerberus_wifi_probe_get_result_chunk(&q,&c)==ESP_ERR_INVALID_STATE);}
    for(unsigned when=0;when<3;++when){reset();probe_packet();CHECK(cerberus_wifi_scanner_start()==ESP_OK);
        if(!when)CHECK(cerberus_wifi_scanner_stop()==ESP_OK);stop_during_enable=when==1;stop_during_scan=when==2;
        dispatch();s=status();CHECK(s.state==0&&!s.flags&&!s.count&&!probe_capture.accepting);}
    /* A delayed earlier consumer cannot transplant a callback across Stop or
     * even 256 accepted starts with the same eight-bit wire generation. */
    for(hook_kind=0;hook_kind<4;++hook_kind){reset();probe_packet();CHECK(cerberus_wifi_scanner_start()==ESP_OK);
        cerberus_wifi_probe_arm(&probe_capture,true,0);now_us=0;probe_test_consumer_hook=delay_consumer;
        wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);s=status();
        if(!hook_kind){CHECK(s.count==1&&probe_capture.rows[0].callback_ms==0&&probe_capture.callback_us[0]==0);}
        else CHECK(!s.count&&!s.matched_frames);
        if(hook_kind==3)CHECK(s.generation==1&&s.epoch==257&&probe_capture.accepting);
    }
    reset();probe_packet();CHECK(cerberus_wifi_scanner_start()==ESP_OK);
    cerberus_wifi_probe_arm(&probe_capture,true,0);now_us=0;
    probe_test_admission_hook=delay_before_admission;
    wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
    CHECK(probe_capture.status.epoch==2&&probe_capture.armed_us==50000&&!status().count);
    wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
    CHECK(status().count==1&&probe_capture.rows[0].callback_ms==50); /* equality is valid */
    reset();probe_packet();now_us=(uint64_t)INT64_C(-200000);
    CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();s=status();
    CHECK(s.state==2&&!s.flags&&!s.count); /* negative arm clock is unavailable */
    for(unsigned why=0;why<8;++why){reset();probe_packet();CHECK(cerberus_wifi_scanner_start()==ESP_OK);
        cerberus_wifi_probe_arm(&probe_capture,true,0);
        if(why==0)received.rx_ctrl.rx_state=1;if(why==1)received.rx_ctrl.sig_len=27;
        if(why==2)received.payload[25]++;if(why==3)received.rx_ctrl.channel=0;
        if(why==4)received.rx_ctrl.rssi=127;if(why==5)now_us=UINT64_MAX;
        wifi_promiscuous_callback(why==6?NULL:&received,why==7?WIFI_PKT_DATA:WIFI_PKT_MGMT);
        CHECK(!status().count);}
    reset();probe_packet();CHECK(cerberus_wifi_scanner_start()==ESP_OK);cerberus_wifi_probe_arm(&probe_capture,true,0);
    now_us=2001;wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
    now_us=2999;received.rx_ctrl.rssi=-120;wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
    now_us=2998;received.rx_ctrl.rssi=-1;wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
    CHECK(probe_capture.rows[0].frames==2&&probe_capture.rows[0].rssi==-120&&probe_capture.callback_us[0]==2999);
    now_us=100000;received.payload[25]++;wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
    CHECK(probe_capture.rows[0].frames==2&&probe_capture.rows[0].rssi==-120&&probe_capture.callback_us[0]==2999);
    received.payload[25]--;
    finish_scan(ESP_OK);s=status();r=row(s);CHECK(r.callback_ms==2&&r.rssi==-120&&r.frames==2);
    cerberus_wifi_probe_query_t old={s.epoch,s.generation,0,0};
    for(unsigned i=0;i<256;++i){CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();}
    CHECK(scan_status.generation==old.generation&&status().epoch==old.epoch+256);
    cerberus_wifi_probe_chunk_t c;memset(&c,0xa5,sizeof(c));
    CHECK(cerberus_wifi_probe_get_result_chunk(&old,&c)==ESP_ERR_INVALID_STATE);
    for(size_t i=0;i<sizeof(c);++i)CHECK(((uint8_t *)&c)[i]==0);
    reset();probe_packet();probe_capture.epoch_counter=UINT64_MAX;
    CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();s=status();
    CHECK(s.state==2&&!s.epoch&&!s.flags&&!s.count&&scan_status.stored_count==1);
}
static void wire(void)
{
    reset();probe_packet();CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();
    cerberus_wifi_probe_status_t s=status();cerberus_spi_frame_t req={0};req.sequence=219;
    req.command=CERBERUS_SPI_COMMAND_WIFI_PROBE_STATUS;req.payload[0]=s.generation;
    for(unsigned n=0;n<=22;++n){response_count=0;req.payload_length=(uint8_t)n;command(&req);
        CHECK(response_count==(n==1));if(n==1){CHECK(reply_command==96&&reply_sequence==219&&reply_length==22);CHECK(!memcmp(reply_bytes,&s,22));}}
    req.payload_length=1;req.payload[0]++;response_count=0;command(&req);CHECK(!response_count);
    req.command=CERBERUS_SPI_COMMAND_WIFI_PROBE_CHUNK;
    for(unsigned chunk=0;chunk<6;++chunk){cerberus_wifi_probe_query_t q={s.epoch,s.generation,0,(uint8_t)chunk};
        memcpy(req.payload,&q,sizeof(q));for(unsigned n=0;n<=22;++n){response_count=0;req.payload_length=(uint8_t)n;command(&req);
            CHECK(response_count==(n==11));if(n==11){cerberus_wifi_probe_chunk_t c;memcpy(&c,reply_bytes,22);
                CHECK(reply_command==97&&reply_sequence==219&&reply_length==22&&c.chunk==chunk&&c.length==(chunk==5?5:10));}}}
    cerberus_wifi_probe_query_t q={s.epoch+1,s.generation,0,0};memcpy(req.payload,&q,sizeof(q));req.payload_length=11;
    response_count=0;command(&req);CHECK(!response_count);
    CHECK(cerberus_wifi_probe_get_status(s.generation,NULL)==ESP_ERR_INVALID_ARG);
    cerberus_wifi_probe_chunk_t c;CHECK(cerberus_wifi_probe_get_result_chunk(NULL,&c)==ESP_ERR_INVALID_ARG);
    CHECK(cerberus_wifi_probe_get_result_chunk(&q,NULL)==ESP_ERR_INVALID_ARG);
    scanner_mutex=NULL;CHECK(cerberus_wifi_probe_get_status(s.generation,&s)==ESP_ERR_INVALID_STATE);
    CHECK(cerberus_wifi_probe_get_result_chunk(&q,&c)==ESP_ERR_INVALID_STATE);scanner_mutex=&mutex_token;
}
int main(void)
{lifecycle();wire();printf("C5 actual probe callback/worker/getters/dispatch: %u checks.\n",checks);return 0;}
