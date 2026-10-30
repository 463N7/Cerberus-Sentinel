/* Actual C5 callback/worker/Start/Stop/getters + SPI cases, platform fakes only. */
#define main observation_fixture_main
#include "wifi_scan_observation_lifecycle_check.c"
#undef main

static unsigned replies;
static uint8_t response[22],response_command,response_sequence;
static void prepare_frame_response(cerberus_spi_command_t command,uint8_t sequence,const void *p,size_t n)
{CHECK(n==22);memcpy(response,p,n);response_command=(uint8_t)command;response_sequence=sequence;++replies;}
static void command(const cerberus_spi_frame_t *r)
{
    const cerberus_spi_frame_t *request=r;
    switch((cerberus_spi_command_t)r->command){
#include "wifi_relation_dispatch.h"
    default:CHECK(false);break;
    }
}
static void frame(unsigned ds,unsigned station,uint8_t channel)
{
    memset(&received,0,sizeof(received));received_type=WIFI_PKT_DATA;
    received.rx_ctrl.channel=channel;received.rx_ctrl.rssi=-77;received.rx_ctrl.sig_len=30;
    received.payload[0]=0x88;received.payload[1]=(uint8_t)(0x40|ds); /* protected QoS */
    uint8_t station_mac[6]={4,7,8,9,10,(uint8_t)station};
    memcpy(received.payload+(ds==1?4:10),sdk_results[0].bssid,6);
    memcpy(received.payload+(ds==1?10:4),station_mac,6);
    memset(received.payload+16,0xff,6);memset(received.payload+26,0xa5,4); /* FCS */
}
static void arm(void)
{cerberus_wifi_relation_arm(&relation_capture,true,now_us);atomic_store(&relation_receiving,relation_capture.accepting);}
static cerberus_wifi_relation_status_t status(void)
{
    cerberus_wifi_relation_status_t s;memset(&s,0xa5,sizeof(s));
    CHECK(cerberus_wifi_relation_get_status(scan_status.generation,&s)==ESP_OK);
    CHECK(cerberus_wifi_relation_status_valid(&s));return s;
}
static cerberus_wifi_relation_row_t row(const cerberus_wifi_relation_status_t *s,uint8_t index)
{
    cerberus_wifi_relation_row_t r={0};
    for(unsigned i=0;i<5;++i){cerberus_wifi_relation_query_t q={s->epoch,s->generation,index,(uint8_t)i};
        cerberus_wifi_relation_chunk_t c;memset(&c,0xa5,sizeof(c));
        CHECK(cerberus_wifi_relation_get_result_chunk(&q,&c)==ESP_OK);
        CHECK(c.epoch==s->epoch&&c.generation==s->generation&&c.index==index&&c.chunk==i&&c.length==(i==4?4:10));
        memcpy((uint8_t *)&r+i*10,c.data,c.length);for(unsigned k=c.length;k<10;++k)CHECK(!c.data[k]);}
    CHECK(cerberus_wifi_relation_row_valid(&r));return r;
}
static void turnover(void)
{
    probe_test_consumer_hook=NULL;CHECK(cerberus_wifi_scanner_stop()==ESP_OK);finish_scan(ESP_OK);
    for(unsigned i=0;i<256;++i){CHECK(cerberus_wifi_scanner_start()==ESP_OK);if(i<255)finish_scan(ESP_OK);}
    arm();
}
static void before_admission(void)
{
    now_us+=50000;CHECK(cerberus_wifi_scanner_stop()==ESP_OK);finish_scan(ESP_OK);
    CHECK(cerberus_wifi_scanner_start()==ESP_OK);arm();
}
static void lifecycle(void)
{
    reset();frame(1,9,6);wifi_promiscuous_callback(&received,WIFI_PKT_DATA);CHECK(!relation_capture.status.count);
    CHECK(cerberus_wifi_scanner_start()==ESP_OK);CHECK(status().epoch==1&&!status().flags);
    wifi_promiscuous_callback(&received,WIFI_PKT_DATA);CHECK(!status().count);
    CHECK(cerberus_wifi_scanner_start()==ESP_ERR_INVALID_STATE);dispatch();
    cerberus_wifi_relation_status_t s=status();cerberus_wifi_relation_row_t r=row(&s,0);
    CHECK(s.state==2&&s.count==1&&s.matched_frames==1&&s.flags==1&&!s.dropped_frames);
    CHECK(r.to_ap_callback_us==1001234&&r.to_ap_rssi==-77&&r.from_ap_rssi==127&&r.latest_direction==1);
    CHECK(scan_calls==1&&enable_calls==1&&disable_calls==1&&scan_status.stored_count==1);
    CHECK(!atomic_load(&relation_receiving)&&!relation_capture.accepting);
    cerberus_wifi_relation_query_t old={s.epoch,s.generation,0,0};
    frame(2,9,6);now_us+=1000000;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);
    CHECK(!memcmp(&r,&relation_capture.rows[0],44));
    cerberus_wifi_relation_clock_query_t cq={s.epoch,s.generation};cerberus_wifi_relation_clock_t c;
    CHECK(cerberus_wifi_relation_get_source_clock(&cq,&c)==ESP_OK);
    CHECK(c.epoch==s.epoch&&c.generation==s.generation&&c.count==1&&c.source_now_us==now_us);
    now_us=r.to_ap_callback_us-1;CHECK(cerberus_wifi_relation_get_source_clock(&cq,&c)==ESP_ERR_INVALID_STATE);
    for(unsigned i=0;i<22;++i)CHECK(!((uint8_t *)&c)[i]);
    now_us=UINT64_MAX;CHECK(cerberus_wifi_relation_get_source_clock(&cq,&c)==ESP_ERR_INVALID_STATE);
    for(unsigned i=0;i<22;++i)CHECK(!((uint8_t *)&c)[i]);
    for(unsigned fault=0;fault<6;++fault){reset();frame(1,9,6);filter_failure=fault==0;enable_failure=fault==1;
        scan_failure=fault==2;disable_failure=fault==3;records_failure=fault==4;num_failure=fault==5;
        CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();s=status();
        CHECK(s.state==(fault<2?2:3)&&!s.flags&&!s.count&&!s.matched_frames&&!s.dropped_frames);
        CHECK(scan_status.stored_count==(fault<2?1:0));}
    for(unsigned when=0;when<3;++when){reset();frame(1,9,6);CHECK(cerberus_wifi_scanner_start()==ESP_OK);
        if(!when)CHECK(cerberus_wifi_scanner_stop()==ESP_OK);
        stop_during_enable=when==1;stop_during_scan=when==2;dispatch();s=status();
        CHECK(!s.state&&!s.flags&&!s.count&&!atomic_load(&relation_receiving));}
    reset();now_us=0;frame(1,9,6);CHECK(cerberus_wifi_scanner_start()==ESP_OK);arm();
    wifi_promiscuous_callback(&received,WIFI_PKT_DATA);CHECK(status().count==1&&relation_capture.rows[0].to_ap_callback_us==0);
    frame(2,9,6);received.rx_ctrl.rssi=-100;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);
    CHECK(relation_capture.rows[0].directions==3&&relation_capture.rows[0].latest_direction==2&&relation_capture.rows[0].from_ap_callback_us==0);
    frame(1,9,6);received.rx_ctrl.rssi=127;now_us=100;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);
    CHECK(relation_capture.rows[0].rssi_valid==2&&relation_capture.rows[0].to_ap_rssi==127&&relation_capture.rows[0].to_ap_frames==2);
    received.rx_ctrl.rssi=-1;now_us=99;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);
    CHECK(relation_capture.rows[0].to_ap_rssi==127&&relation_capture.rows[0].to_ap_frames==2);
    now_us=101;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);CHECK(relation_capture.rows[0].to_ap_rssi==-1&&relation_capture.rows[0].to_ap_frames==3);
    /* Full bank accepts updates, drops new keys, and preserves direction provenance. */
    for(unsigned i=0;i<32;++i){frame(1,30+i,149);++now_us;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);}
    CHECK(status().count==32&&status().dropped_frames==1&&status().flags==3);
    frame(2,9,6);++now_us;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);CHECK(relation_capture.rows[0].from_ap_frames==2);
    finish_scan(ESP_OK);s=status();uint32_t sum=s.dropped_frames;
    for(unsigned i=0;i<s.count;++i){r=row(&s,(uint8_t)i);sum+=r.to_ap_frames+r.from_ap_frames;}CHECK(sum==s.matched_frames);
    reset();frame(1,9,6);CHECK(cerberus_wifi_scanner_start()==ESP_OK);arm();probe_test_consumer_hook=turnover;
    wifi_promiscuous_callback(&received,WIFI_PKT_DATA);s=status();CHECK(s.generation==1&&s.epoch==257&&!s.count);
    cerberus_wifi_relation_chunk_t chunk;CHECK(cerberus_wifi_relation_get_result_chunk(&old,&chunk)==ESP_ERR_INVALID_STATE);
    for(unsigned i=0;i<22;++i)CHECK(!((uint8_t *)&chunk)[i]);
    reset();frame(1,9,6);CHECK(cerberus_wifi_scanner_start()==ESP_OK);arm();
    probe_test_admission_hook=before_admission;wifi_promiscuous_callback(&received,WIFI_PKT_DATA);
    CHECK(status().epoch==2&&!status().count&&relation_capture.armed_us==1050000);
    wifi_promiscuous_callback(&received,WIFI_PKT_DATA);CHECK(status().count==1); /* exact arm floor */
    for(unsigned why=0;why<8;++why){reset();frame(1,9,6);CHECK(cerberus_wifi_scanner_start()==ESP_OK);arm();
        if(why==0)received.rx_ctrl.rx_state=1;if(why==1)received.rx_ctrl.sig_len=29;
        if(why==2)received.payload[1]&=(uint8_t)~3U;if(why==3)received.rx_ctrl.channel=0;
        if(why==4)received.payload[22]=1;if(why==5)now_us=UINT64_MAX;
        wifi_promiscuous_callback(why==6?NULL:&received,why==7?WIFI_PKT_MGMT:WIFI_PKT_DATA);
        CHECK(!status().count&&!status().matched_frames);}
    reset();frame(1,9,6);now_us=UINT64_MAX-50000;CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();CHECK(status().state==2&&!status().flags);
    reset();frame(1,9,6);relation_capture.epoch_counter=UINT64_MAX;
    CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();CHECK(status().state==2&&!status().epoch&&!status().flags&&scan_status.stored_count==1);
}
static void wire(void)
{
    reset();frame(2,7,177);CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();cerberus_wifi_relation_status_t s=status();
    cerberus_spi_frame_t q={0};q.sequence=219;
    for(unsigned which=0;which<3;++which){q.command=(uint8_t)(112+which);unsigned length=which==0?1:which==1?11:9;
        if(!which)q.payload[0]=s.generation;
        else if(which==1){cerberus_wifi_relation_query_t v={s.epoch,s.generation,0,4};memcpy(q.payload,&v,11);}
        else {cerberus_wifi_relation_clock_query_t v={s.epoch,s.generation};memcpy(q.payload,&v,9);}
        for(unsigned n=0;n<=22;++n){q.payload_length=(uint8_t)n;replies=0;command(&q);CHECK(replies==(n==length));
            if(replies)CHECK(response_command==q.command&&response_sequence==219);}
        q.payload_length=(uint8_t)length;q.payload[which?8:0]++;replies=0;command(&q);CHECK(!replies);
    }
    CHECK(cerberus_wifi_relation_get_status(s.generation,NULL)==ESP_ERR_INVALID_ARG);
    cerberus_wifi_relation_query_t rq={s.epoch,s.generation,0,0};cerberus_wifi_relation_chunk_t chunk;
    cerberus_wifi_relation_clock_query_t cq={s.epoch,s.generation};cerberus_wifi_relation_clock_t clock;
    CHECK(cerberus_wifi_relation_get_result_chunk(NULL,&chunk)==ESP_ERR_INVALID_ARG);
    CHECK(cerberus_wifi_relation_get_result_chunk(&rq,NULL)==ESP_ERR_INVALID_ARG);
    CHECK(cerberus_wifi_relation_get_source_clock(NULL,&clock)==ESP_ERR_INVALID_ARG);
    CHECK(cerberus_wifi_relation_get_source_clock(&cq,NULL)==ESP_ERR_INVALID_ARG);
    scanner_mutex=NULL;CHECK(cerberus_wifi_relation_get_status(s.generation,&s)==ESP_ERR_INVALID_STATE);
    CHECK(cerberus_wifi_relation_get_result_chunk(&rq,&chunk)==ESP_ERR_INVALID_STATE);
    CHECK(cerberus_wifi_relation_get_source_clock(&cq,&clock)==ESP_ERR_INVALID_STATE);scanner_mutex=&mutex_token;
}
int main(void)
{lifecycle();wire();printf("C5 actual relationship scan/callback/transport: %u checks.\n",checks);return 0;}
