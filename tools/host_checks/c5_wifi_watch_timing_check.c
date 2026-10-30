#undef NDEBUG
#include "cerberus_watch_capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
enum { ESP_OK,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE };
typedef int esp_err_t;
typedef unsigned wifi_promiscuous_pkt_type_t;
typedef struct { unsigned rx_state,sig_len,channel; int rssi,noise_floor; } wifi_pkt_rx_ctrl_t;
typedef struct { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[2400]; } wifi_promiscuous_pkt_t;
static atomic_uint mac_admission_token;
static void mac_receive(const wifi_promiscuous_pkt_t *p,wifi_promiscuous_pkt_type_t type,int64_t entered,uint32_t token)
{ (void)p;(void)type;(void)entered;(void)token; }
enum { WIFI_PKT_MGMT,WIFI_PKT_CTRL,WIFI_PKT_DATA,WIFI_PKT_MISC };
static int defense_capture_mux;
static bool locked,watch_capture_active,defense_capture_active;
static uint64_t watch_capture_armed_us,watch_capture_deadline_us;
static uint32_t watch_callback_inflight;
static uint8_t watch_capture_channel;
static unsigned enters,handler_turnover;
static unsigned defense_management_frames,defense_control_frames,defense_data_frames;
static unsigned defense_beacon_frames,defense_deauth_frames,defense_disassoc_frames,defense_noise_samples;
static int64_t defense_noise_sum,clock_us,handler_delay;
static int8_t defense_strongest_rssi;
static cerberus_watch_capture_t watch_capture;
static cerberus_wifi_probe_table_t probe_capture;
static cerberus_wifi_relation_table_t relation_capture;
static atomic_bool relation_receiving;
static wifi_promiscuous_pkt_t packet;
static void enter(int *m){CHECK(m==&defense_capture_mux&&!locked);locked=true;++enters;}
static void leave(int *m){CHECK(m==&defense_capture_mux&&locked);locked=false;}
#define portENTER_CRITICAL(m) enter(m)
#define portEXIT_CRITICAL(m) leave(m)
static int64_t esp_timer_get_time(void){return clock_us;}
static void scan_observation_receive(const wifi_promiscuous_pkt_t *p,unsigned t)
{
    (void)p;(void)t;clock_us+=handler_delay;
    if(handler_turnover){
        unsigned action=handler_turnover;handler_turnover=0;
        if(action==2)watch_capture_active=false;
        else {
            uint8_t generation=watch_capture.status.generation;
            uint64_t epoch=watch_capture.capture_epoch+1;
            cerberus_watch_capture_begin_with_epoch(&watch_capture,CERBERUS_WATCH_RADIO_WIFI,generation,0,epoch);
            watch_capture_active=true;
        }
    }
}
static void channel_survey_receive(const wifi_promiscuous_pkt_t *p,unsigned t){(void)p;(void)t;}
static void packet_monitor_receive(const wifi_promiscuous_pkt_t *p,unsigned t){(void)p;(void)t;}
static bool cerberus_wifi_capture_service_active(void){return false;}
static void cerberus_wifi_capture_service_receive(const uint8_t *p,unsigned n,unsigned c,int r,uint64_t t)
{(void)p;(void)n;(void)c;(void)r;(void)t;CHECK(false);}
static void station_receive(const wifi_promiscuous_pkt_t *p){(void)p;}
static void cerberus_c5_test_receive(const uint8_t *p,unsigned n,unsigned c){(void)p;(void)n;(void)c;}
#include "c5_wifi_watch_timing_impl.h"
static void begin(uint64_t epoch)
{
    cerberus_watch_capture_begin_with_epoch(&watch_capture,CERBERUS_WATCH_RADIO_WIFI,255,0,epoch);
    watch_capture_active=true;defense_capture_active=false;handler_turnover=0;handler_delay=0;clock_us=0;
    memset(&packet,0,sizeof(packet));
    packet.rx_ctrl=(wifi_pkt_rx_ctrl_t){.sig_len=77,.channel=149,.rssi=-40,.noise_floor=-96};
    uint8_t *p=packet.payload;p[0]=0x80;memset(p+4,255,6);
    const uint8_t addr[]={2,4,6,8,10,12};memcpy(p+10,addr,6);memcpy(p+16,addr,6);
    p[32]=100;p[34]=1; /* fixed beacon fields, then empty SSID IE at36 */
    const uint8_t vendor[]={221,33,0xfa,0x0b,0xbc,0x0d,7,0xf2,25,1,0x02,0x10,'T','E','S','T','-','A'};
    memcpy(p+38,vendor,sizeof(vendor));memset(p+73,0xb7,4); /* FCS not decoder bytes */
}
static void receive(void){wifi_promiscuous_callback(&packet,WIFI_PKT_MGMT);CHECK(!locked);}
static cerberus_watch_scan_timing_payload_t query(void)
{
    watch_capture_active=false;cerberus_watch_capture_finish(&watch_capture,CERBERUS_WATCH_COMPLETE,1000);
    cerberus_watch_scan_timing_request_t q={CERBERUS_WATCH_RADIO_WIFI,255,0};cerberus_watch_scan_timing_payload_t out;
    CHECK(cerberus_wifi_watch_get_timing(&q,&out)==ESP_OK);return out;
}
static void entry_time_and_turnover(void)
{
    begin(1);handler_delay=5000;receive();CHECK(watch_capture.status.stored_count==1);
    CHECK(clock_us==5000&&watch_capture.qualifying_valid==1&&watch_capture.qualifying_us[0]==0);
    handler_delay=0;clock_us=9000;packet.rx_ctrl.rssi=-90;receive();
    CHECK(watch_capture.qualifying_us[0]==9000&&watch_capture.records[0].rssi==-90);
    clock_us=10000;packet.payload[73]=99;++packet.rx_ctrl.sig_len;receive();
    CHECK(watch_capture.qualifying_us[0]==9000&&watch_capture.status.stored_count==1);
    --packet.rx_ctrl.sig_len;clock_us=10001;cerberus_watch_scan_timing_payload_t out=query();CHECK(out.flags==1&&out.source_age_ms==2);
    clock_us=-1;out=query();CHECK(!out.flags&&!out.source_age_ms&&out.capture_epoch==1);
    for(unsigned action=1;action<=3;++action){
        begin(20);if(action==3)watch_capture_active=false;handler_turnover=action;handler_delay=5000;receive();
        CHECK(!watch_capture.status.reports&&!watch_capture.status.stored_count&&!watch_capture.qualifying_valid);
        CHECK(watch_capture.capture_epoch==(action==2?20:21)&&watch_capture.status.generation==255);
    }
    begin(30);clock_us=-1;receive();CHECK(watch_capture.status.stored_count==1&&!watch_capture.qualifying_valid);
    clock_us=0;out=query();CHECK(!out.flags&&out.capture_epoch==30);
}
static void admission_and_query(void)
{
    for(unsigned reason=0;reason<7;++reason){
        begin(40);unsigned type=WIFI_PKT_MGMT;
        if(reason==0)watch_capture_active=false;
        if(reason==1)packet.rx_ctrl.rx_state=1;
        if(reason==2)packet.rx_ctrl.sig_len=27;
        if(reason==3)type=WIFI_PKT_MISC;
        if(reason==4)type=WIFI_PKT_CTRL;
        if(reason==5)type=WIFI_PKT_DATA;
        unsigned start=enters;wifi_promiscuous_callback(reason==6?NULL:&packet,type);
        CHECK(!watch_capture.status.reports&&!watch_capture.status.stored_count&&!locked);
        if(reason==4||reason==5)CHECK(enters-start==1); /* no new Watch lock for data/control */
    }
    begin(50);receive();clock_us=INT64_MAX;cerberus_watch_scan_timing_payload_t out=query();
    CHECK(out.flags==1&&out.source_age_ms==UINT64_C(9223372036854776));
    for(unsigned defect=0;defect<4;++defect){
        cerberus_watch_scan_timing_request_t q={CERBERUS_WATCH_RADIO_WIFI,255,0};
        if(defect==0)q.radio=CERBERUS_WATCH_RADIO_BLE;if(defect==1)q.generation=0;if(defect==2)q.index=1;
        memset(&out,0xa5,sizeof(out));CHECK(cerberus_wifi_watch_get_timing(defect==3?NULL:&q,&out)!=ESP_OK);
        for(size_t i=0;i<sizeof(out);++i)CHECK(!((uint8_t *)&out)[i]);
    }
}
/* Independent ODID wire vector: version2 Location, high speed range, second
 * direction half, AGL. Advertised maxima are 254.25m/s, 360deg and 31767.5m. */
static const uint8_t motion_location[25]={0x12,7,180,254,0,0,0,0,0,0,0,0,0,0,0,0,0,255,255};
static void motion_receive(const uint8_t message[25],int64_t at)
{memcpy(packet.payload+48,message,25);clock_us=at;receive();}
static void motion_finish(void)
{watch_capture_active=false;cerberus_watch_capture_finish(&watch_capture,CERBERUS_WATCH_COMPLETE,1000);}
static void motion_wire(void)
{
    begin(900);uint8_t basic[25];memcpy(basic,packet.payload+48,25);receive();
    handler_delay=9000;motion_receive(motion_location,1000);handler_delay=0;
    CHECK(clock_us==10000&&watch_capture.motion_us[0]==1000);
    packet.rx_ctrl.rssi=-90;motion_receive(basic,20000); /* newer source, older Location */
    uint8_t bad[25];memcpy(bad,motion_location,25);bad[24]=1;motion_receive(bad,30000);
    CHECK(watch_capture.qualifying_us[0]==20000&&watch_capture.motion_us[0]==1000);
    motion_finish();clock_us=30001;
    cerberus_watch_scan_data_request_t q={CERBERUS_WATCH_RADIO_WIFI,255,0,0};
    cerberus_watch_scan_motion_payload_t data;cerberus_watch_scan_timing_payload_t age;
    CHECK(cerberus_wifi_watch_get_motion(&q,&data)==ESP_OK);
    CHECK(data.radio==q.radio&&data.generation==255&&!data.index&&data.capture_epoch==900);
    CHECK(!data.reserved0&&!data.reserved[0]&&!data.reserved[1]);
    CHECK(data.motion.flags==31&&data.motion.speed_qms==1017&&data.motion.direction_deg==360&&
          data.motion.height_encoded==65535&&!data.motion.reserved);
    CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&age.flags==1&&age.source_age_ms==30);
    CHECK(age.capture_epoch==data.capture_epoch&&age.radio==q.radio&&age.generation==255&&!age.index);
    CHECK(query().source_age_ms==11); /* no source-age substitution */
    sentinel_watch_motion_t retained=data.motion;
    clock_us=999;CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&!age.flags&&!age.source_age_ms);
    clock_us=-1;CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&!age.flags&&!age.source_age_ms&&age.capture_epoch==900);
    CHECK(cerberus_wifi_watch_get_motion(&q,&data)==ESP_OK&&!memcmp(&data.motion,&retained,sizeof(retained)));
    for(unsigned defect=0;defect<8;++defect){
        cerberus_watch_scan_data_request_t wrong=q;
        if(defect==0)wrong.radio=CERBERUS_WATCH_RADIO_BLE;
        if(defect==1)wrong.generation=0;
        if(defect==2)wrong.index=1;
        if(defect==3)wrong.chunk=1;
        if(defect==5)watch_capture.status.state=CERBERUS_WATCH_RUNNING;
        if(defect==6)watch_capture.capture_epoch=0;
        if(defect==7)watch_capture.motion[0].reserved=1;
        memset(&data,0xa5,sizeof(data));memset(&age,0xa5,sizeof(age));
        CHECK(cerberus_wifi_watch_get_motion(defect==4?NULL:&wrong,&data)!=ESP_OK);
        CHECK(cerberus_wifi_watch_get_motion_timing(defect==4?NULL:&wrong,&age)!=ESP_OK);
        for(size_t i=0;i<sizeof(data);++i)CHECK(!((uint8_t*)&data)[i]);
        for(size_t i=0;i<sizeof(age);++i)CHECK(!((uint8_t*)&age)[i]);
        watch_capture.status.state=CERBERUS_WATCH_COMPLETE;watch_capture.capture_epoch=900;watch_capture.motion[0]=retained;
    }
    CHECK(cerberus_wifi_watch_get_motion(&q,NULL)==ESP_ERR_INVALID_ARG);
    CHECK(cerberus_wifi_watch_get_motion_timing(&q,NULL)==ESP_ERR_INVALID_ARG);
    /* A real zero callback time is valid, including a full-width query age. */
    begin(901);motion_receive(motion_location,0);motion_finish();clock_us=INT64_MAX;
    CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&age.flags==1&&age.source_age_ms==UINT64_C(9223372036854776));
    /* All advertised components unavailable is still a timed Location group. */
    begin(902);uint8_t unavailable[25]={0x12,3,181,255};motion_receive(motion_location,1000);motion_receive(unavailable,2000);motion_finish();clock_us=2001;
    CHECK(cerberus_wifi_watch_get_motion(&q,&data)==ESP_OK&&data.motion.flags==1&&!data.motion.speed_qms&&!data.motion.direction_deg&&!data.motion.height_encoded);
    CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&age.flags==1&&age.source_age_ms==1);
    /* Untimed qualifying Location clears only timing; a changed Basic ID clears
     * old Location evidence. A new Basic-only epoch is canonically absent. */
    begin(903);motion_receive(motion_location,-1);motion_finish();clock_us=1000;
    CHECK(cerberus_wifi_watch_get_motion(&q,&data)==ESP_OK&&data.motion.flags==31);
    CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&!age.flags&&!age.source_age_ms);
    begin(904);motion_receive(basic,0);motion_receive(motion_location,1000);basic[7]='B';motion_receive(basic,2000);motion_finish();clock_us=3000;
    CHECK(cerberus_wifi_watch_get_motion(&q,&data)==ESP_OK&&!data.motion.flags&&data.capture_epoch==904);
    CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&!age.flags&&!age.source_age_ms);
    begin(905);receive();motion_finish();clock_us=4000;
    CHECK(cerberus_wifi_watch_get_motion(&q,&data)==ESP_OK&&data.capture_epoch==905);
    for(size_t i=0;i<sizeof(data.motion);++i)CHECK(!((uint8_t*)&data.motion)[i]);
    CHECK(cerberus_wifi_watch_get_motion_timing(&q,&age)==ESP_OK&&age.capture_epoch==905&&!age.flags&&!age.source_age_ms);
}
int main(void)
{
    entry_time_and_turnover();admission_and_query();motion_wire();
    printf("C5 Wi-Fi timing: %u checks; actual callback/getters, source vs Location ages, atomic motion, replacement, epoch/admission turnover, malformed exclusion, negative/full-width ages and unchanged non-management lock cost passed.\n",checks);
    return 0;
}
