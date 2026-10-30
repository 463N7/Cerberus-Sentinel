#ifdef NDEBUG
#undef NDEBUG
#endif
#include "fake_rx_platform.h"
#include "ble_test.h"
#include "s3_ble_host.h"
#include "s3_ble_scanner.h"
#include "wifi_sae_commit.h"
#include "sae_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned tick,stop_at,free_bytes=128*1024,step,fail_step,netif_calls,event_calls,deinit_calls;
static bool init_ok,started_ok,renew=true,task_fail,stop_early,fail_cleanup;
static unsigned ble_state,expected_filter=1,setup_delay;
static cerberus_s3_ble_owner_t ble_owner;
static uint8_t scanner_state;
static unsigned scanner_stop_calls,scanner_release_at;
static bool scanner_stuck;
static rx_callback callback;
static void(*queued)(void*);
static esp_err_t advance(void){return ++step==fail_step?ESP_FAIL:ESP_OK;}
void enter(int *p){assert(!*p);*p=1;}
void leave(int *p){assert(*p);*p=0;}
esp_err_t esp_netif_init(void){++netif_calls;return ESP_OK;}
esp_err_t esp_event_loop_create_default(void){++event_calls;return ESP_OK;}
esp_err_t esp_wifi_init(wifi_init_config_t *c)
{assert(c->static_rx_buf_num==4&&c->dynamic_rx_buf_num==8&&!c->ampdu_rx_enable);tick+=setup_delay;esp_err_t e=advance();init_ok=e==ESP_OK;return e;}
esp_err_t esp_wifi_set_storage(int mode){assert(init_ok&&mode==WIFI_STORAGE_RAM);return advance();}
esp_err_t esp_wifi_set_mode(int mode){assert(init_ok&&mode==WIFI_MODE_NULL);return advance();}
esp_err_t esp_wifi_start(void){assert(init_ok);esp_err_t e=advance();started_ok=e==ESP_OK;return e;}
esp_err_t esp_wifi_set_channel(uint8_t ch,int second){assert(started_ok&&ch==6&&!second);return advance();}
esp_err_t esp_wifi_set_promiscuous_filter(wifi_promiscuous_filter_t *f){assert(f->filter_mask==expected_filter);return advance();}
esp_err_t esp_wifi_set_promiscuous_rx_cb(rx_callback cb){if(cb){esp_err_t e=advance();if(!e)callback=cb;return e;}callback=NULL;return ESP_OK;}
esp_err_t esp_wifi_set_promiscuous(bool enabled){if(enabled){assert(callback&&started_ok);return advance();}return fail_cleanup?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_stop(void){started_ok=false;return ESP_OK;}
esp_err_t esp_wifi_deinit(void){++deinit_calls;init_ok=false;return ESP_OK;}
int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void *a,unsigned p,void *out)
{(void)name;(void)a;(void)p;(void)out;assert(stack==4096);if(task_fail)return 0;assert(!queued);queued=fn;return pdPASS;}
void vTaskDelete(void *p){assert(!p);}
int64_t esp_timer_get_time(void){return (int64_t)tick*1000;}
size_t heap_caps_get_free_size(unsigned flags){(void)flags;return free_bytes;}
void cerberus_ble_test_status(uint16_t id,cerberus_radio_test_status_t *s){assert(!id);*s=(cerberus_radio_test_status_t){.state=ble_state};}
cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void){return ble_owner;}
esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *s)
{*s=(cerberus_ble_scan_status_payload_t){.state=scanner_state};return ESP_OK;}
esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *s)
{assert(scanner_state==CERBERUS_BLE_SCAN_RUNNING);++scanner_stop_calls;*s=(cerberus_ble_scan_status_payload_t){.state=scanner_state};
 if(!scanner_stuck)scanner_release_at=tick+20;return ESP_OK;}
#include "s3_wifi_owner.c"
#include "s3_wifi_receiver.c"
void vTaskDelay(unsigned ms)
{
    tick+=ms;
    if(ble_owner==CERBERUS_S3_BLE_OWNER_SCANNER){
        if(scanner_release_at&&tick>=scanner_release_at){
            ble_owner=CERBERUS_S3_BLE_OWNER_NONE;scanner_state=CERBERUS_BLE_SCAN_IDLE;scanner_release_at=0;
        }
        return;
    }
    if(stop_at&&tick>=stop_at){stop_at=0;cerberus_s3_rx_stop(request.job_id);}
    assert(!lock&&callback);
    cerberus_s3_rx_status_t s;cerberus_s3_rx_status(renew?request.job_id:0,&s);
    if(request.mode==CERBERUS_AP_SAE_COMMIT){
        uint8_t source[]={2,9,8,7,6,5},scalar[32]={0};scalar[31]=2;
        wifi_promiscuous_pkt_t packet={.rx_ctrl={.channel=6,.rssi=-42}};
        size_t n=cerberus_wifi_sae_commit_frame(&request,source,scalar,cerberus_test_sae_2g,
            NULL,0,packet.payload,sizeof(packet.payload));
        assert(n==128);packet.rx_ctrl.sig_len=n+4;
        unsigned matches=status.matching_frames;
        callback(&packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+1);
        packet.payload[30]=20;callback(&packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+1);packet.payload[30]=19;
        packet.payload[4]^=2;callback(&packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+1);
        if(stop_early)cerberus_s3_rx_stop(request.job_id);
        return;
    }
    if(request.mode==CERBERUS_AP_CLONE_SPAM){
        const uint8_t caps[2]={0x31,4};
        wifi_promiscuous_pkt_t clone_packet={.rx_ctrl={.channel=6,.rssi=-42}};
        unsigned pad=clone_ssid_length+(tick/25U)%3U;
        size_t n=cerberus_wifi_ap_clone_beacon(&request,request.station,clone_ssid,
            clone_ssid_length,caps,pad,0,clone_packet.payload,sizeof(clone_packet.payload));
        assert(n);clone_packet.rx_ctrl.sig_len=n+4;
        bool ready=clone_capabilities_ready;unsigned matches=status.matching_frames;
        callback(&clone_packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+(ready?1U:0U));
        wifi_promiscuous_pkt_t source={.rx_ctrl={.channel=6,.rssi=-51}};
        uint8_t *p=source.payload;unsigned source_len=41U+clone_ssid_length;
        p[0]=0x80;memset(p+4,255,6);memcpy(p+10,request.bssid,6);
        memcpy(p+16,request.bssid,6);p[34]=caps[0];p[35]=caps[1];
        p[37]=clone_ssid_length;memcpy(p+38,clone_ssid,clone_ssid_length);
        p[38+clone_ssid_length]=3;p[39+clone_ssid_length]=1;
        p[40+clone_ssid_length]=request.channel;source.rx_ctrl.sig_len=source_len+4;
        if(!ready){
            p[40+clone_ssid_length]=11;callback(&source,WIFI_PKT_MGMT);
            assert(!clone_capabilities_ready&&!status.target_beacons);
            p[40+clone_ssid_length]=request.channel;
            p[10]^=2;callback(&source,WIFI_PKT_MGMT);
            assert(!clone_capabilities_ready&&!status.target_beacons);p[10]^=2;
            if(clone_ssid_length){p[38]^=1;callback(&source,WIFI_PKT_MGMT);
                assert(!clone_capabilities_ready&&!status.target_beacons);p[38]^=1;}
        }
        unsigned beacons=status.target_beacons;callback(&source,WIFI_PKT_MGMT);
        assert(clone_capabilities_ready&&status.target_beacons==beacons+1&&
            status.matching_frames==matches+(ready?1U:0U));
        unsigned next_pad=clone_ssid_length+(tick/25U+1U)%3U;
        n=cerberus_wifi_ap_clone_beacon(&request,request.station,clone_ssid,
            clone_ssid_length,caps,next_pad,0,clone_packet.payload,sizeof(clone_packet.payload));
        assert(n);clone_packet.rx_ctrl.sig_len=n+4;
        callback(&clone_packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+(ready?2U:1U));
        clone_packet.payload[50+next_pad]=11;callback(&clone_packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+(ready?2U:1U));
        clone_packet.payload[50+next_pad]=request.channel;
        clone_packet.payload[34]^=1;callback(&clone_packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+(ready?2U:1U));
        clone_packet.payload[34]^=1;
        clone_packet.payload[10]^=2;callback(&clone_packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+(ready?2U:1U));
        clone_packet.payload[10]^=2;
        clone_packet.rx_ctrl.channel=11;callback(&clone_packet,WIFI_PKT_MGMT);
        assert(status.matching_frames==matches+(ready?2U:1U));
        if(stop_early)cerberus_s3_rx_stop(request.job_id);
        return;
    }
    wifi_promiscuous_pkt_t packet={.rx_ctrl={.channel=6,.sig_len=30,.rssi=-42},.payload={0xc0}};
    memset(packet.payload+4,255,6);memcpy(packet.payload+10,request.bssid,6);memcpy(packet.payload+16,request.bssid,6);packet.payload[24]=7;
    callback(&packet,WIFI_PKT_MGMT);
    if(request.scope==CERBERUS_AP_STATION_LIST){
        cerberus_wifi_ap_job_t one=request;one.scope=CERBERUS_AP_ONE_STATION;
        memcpy(one.station,station_set.mac[(tick/25U)%station_set.count],6);
        packet.payload[0]=0x80;packet.rx_ctrl.sig_len=40;callback(&packet,WIFI_PKT_MGMT);
        size_t n=0;
        if(request.mode==CERBERUS_AP_DEAUTH)n=cerberus_wifi_ap_frame(&one,false,packet.payload,sizeof(packet.payload));
        else if(request.mode==CERBERUS_AP_BAD_MESSAGE){uint8_t nonce[32]={1};n=cerberus_wifi_ap_bad_message(&one,false,1,nonce,packet.payload,sizeof(packet.payload));}
        else n=cerberus_wifi_ap_association(&one,(const uint8_t*)"Test",4,packet.payload,sizeof(packet.payload));
        assert(n);packet.rx_ctrl.sig_len=n+4;
        wifi_promiscuous_pkt_type_t type=request.mode==CERBERUS_AP_BAD_MESSAGE?WIFI_PKT_DATA:WIFI_PKT_MGMT;
        callback(&packet,type);
        one.station[5]=99;assert(cerberus_wifi_ap_job_valid(&one));
        if(request.mode==CERBERUS_AP_DEAUTH)n=cerberus_wifi_ap_frame(&one,false,packet.payload,sizeof(packet.payload));
        else if(request.mode==CERBERUS_AP_BAD_MESSAGE){uint8_t nonce[32]={1};n=cerberus_wifi_ap_bad_message(&one,false,1,nonce,packet.payload,sizeof(packet.payload));}
        else n=cerberus_wifi_ap_association(&one,(const uint8_t*)"Test",4,packet.payload,sizeof(packet.payload));
        assert(n);packet.rx_ctrl.sig_len=n+4;callback(&packet,type);
        return;
    }
    if(request.mode>=CERBERUS_AP_BAD_MESSAGE){
        size_t n=0;uint8_t nonce[32]={1},cap[2]={0x31,4};
        if(request.mode==CERBERUS_AP_BAD_MESSAGE)n=cerberus_wifi_ap_bad_message(&request,false,1,nonce,packet.payload,sizeof(packet.payload));
        else if(request.mode==CERBERUS_AP_ASSOC_SLEEP)n=cerberus_wifi_ap_association(&request,(const uint8_t*)"Test",4,packet.payload,sizeof(packet.payload));
        else n=cerberus_wifi_ap_announcement(&request,(const uint8_t*)"Test",4,cap,11,0,packet.payload,sizeof(packet.payload));
        assert(n);packet.rx_ctrl.sig_len=n+4;
        callback(&packet,request.mode==CERBERUS_AP_BAD_MESSAGE?WIFI_PKT_DATA:WIFI_PKT_MGMT);
        return;
    }
    packet.payload[0]=0x80;packet.rx_ctrl.sig_len=40;callback(&packet,WIFI_PKT_MGMT);
    packet.rx_ctrl.channel=11;callback(&packet,WIFI_PKT_MGMT);
    packet.rx_ctrl.channel=6;packet.rx_ctrl.rx_state=1;callback(&packet,WIFI_PKT_MGMT);
    packet.rx_ctrl.rx_state=0;callback(&packet,WIFI_PKT_DATA);
    if(stop_early)cerberus_s3_rx_stop(request.job_id);
}
static void run(void){void(*fn)(void*)=queued;queued=NULL;assert(fn);fn(NULL);assert(!lock&&!receiving&&!callback&&!init_ok&&!started_ok);}
int main(void)
{
    cerberus_wifi_ap_job_t r={.job_id=1,.mode=CERBERUS_AP_DEAUTH,.seconds=1,.channel=6,.bssid={2,1,2,3,4,5}};
    uint8_t frame[80]={0xc0};memset(frame+4,255,6);memcpy(frame+10,r.bssid,6);memcpy(frame+16,r.bssid,6);frame[24]=7;
    assert(cerberus_wifi_ap_rx_classify(&r,frame,26)==CERBERUS_AP_RX_MATCH);
    for(unsigned n=0;n<26;++n)assert(!cerberus_wifi_ap_rx_classify(&r,frame,n));
    frame[1]=0x40;assert(!cerberus_wifi_ap_rx_classify(&r,frame,26));frame[1]=4;assert(!cerberus_wifi_ap_rx_classify(&r,frame,26));frame[1]=0;
    frame[22]=1;assert(!cerberus_wifi_ap_rx_classify(&r,frame,26));frame[22]=0;
    frame[24]=8;assert(!cerberus_wifi_ap_rx_classify(&r,frame,26));frame[24]=7;
    r.scope=1;memcpy(r.station,(uint8_t[]){2,9,8,7,6,5},6);
    assert(!cerberus_wifi_ap_rx_classify(&r,frame,26));memcpy(frame+4,r.station,6);assert(cerberus_wifi_ap_rx_classify(&r,frame,26)==2);
    memcpy(frame+4,r.bssid,6);memcpy(frame+10,r.station,6);assert(cerberus_wifi_ap_rx_classify(&r,frame,26)==2);
    frame[16]^=2;assert(!cerberus_wifi_ap_rx_classify(&r,frame,26));r.scope=0;memset(r.station,0,6);
    r.mode=CERBERUS_AP_PROBE_REQUEST;uint8_t source[]={2,9,9,9,9,9};
    size_t n=cerberus_wifi_ap_probe(&r,source,(const uint8_t*)"Test AP",7,frame,sizeof(frame));assert(n==43);
    assert(cerberus_wifi_ap_rx_classify(&r,frame,n)==CERBERUS_AP_RX_MATCH);
    frame[25]=33;assert(!cerberus_wifi_ap_rx_classify(&r,frame,n));frame[25]=7;
    frame[33]=2;assert(!cerberus_wifi_ap_rx_classify(&r,frame,n));frame[33]=1;
    frame[10]=1;assert(!cerberus_wifi_ap_rx_classify(&r,frame,n));frame[10]=2;
    r.mode=CERBERUS_AP_DEAUTH;assert(!cerberus_wifi_ap_rx_classify(&r,frame,n));
    assert(cerberus_s3_wifi_prepare(CERBERUS_S3_WIFI_WEB)==ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_wifi_claim(CERBERUS_S3_WIFI_WEB)==ESP_OK);
    assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_STATE&&!queued);
    assert(cerberus_s3_wifi_release(CERBERUS_S3_WIFI_RECEIVER)==ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_wifi_release(CERBERUS_S3_WIFI_WEB)==ESP_OK);
    ble_state=CERBERUS_TEST_RUNNING;assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_STATE);ble_state=0;
    ble_owner=CERBERUS_S3_BLE_OWNER_SCANNER;scanner_state=CERBERUS_BLE_SCAN_RUNNING;scanner_stuck=true;
    assert(cerberus_s3_rx_start(&r)==ESP_ERR_TIMEOUT&&!queued&&scanner_stop_calls==1);
    ble_owner=CERBERUS_S3_BLE_OWNER_NONE;scanner_state=CERBERUS_BLE_SCAN_IDLE;scanner_stuck=false;
    r.channel=36;assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_ARG);r.channel=6;
    free_bytes=65000;assert(cerberus_s3_rx_start(&r)==ESP_ERR_NO_MEM);free_bytes=128*1024;
    assert(cerberus_s3_rx_start(&r)==ESP_OK);assert(cerberus_s3_rx_start(&r)==ESP_OK);
    r.seconds=2;assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_ARG);r.seconds=1;
    ++r.job_id;assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_STATE);--r.job_id;
    assert(cerberus_s3_wifi_claim(CERBERUS_S3_WIFI_WEB)==ESP_ERR_INVALID_STATE);
    run();assert(status.state==CERBERUS_TEST_COMPLETE&&!status.reserved&&status.target_beacons==160&&status.matching_frames==160&&status.observed_frames==320&&status.last_match_rssi==-42);
    assert(cerberus_s3_wifi_owner()==CERBERUS_S3_WIFI_NONE&&netif_calls==1&&event_calls==1);
    ++r.job_id;ble_owner=CERBERUS_S3_BLE_OWNER_SCANNER;scanner_state=CERBERUS_BLE_SCAN_RUNNING;
    assert(cerberus_s3_rx_start(&r)==ESP_OK&&scanner_stop_calls==2&&ble_owner==CERBERUS_S3_BLE_OWNER_NONE);
    run();assert(status.state==CERBERUS_TEST_COMPLETE&&!status.reserved);
    ++r.job_id;setup_delay=1200;assert(cerberus_s3_rx_start(&r)==ESP_OK);run();
    assert(status.elapsed_ms==5200&&status.matching_frames==160);setup_delay=0;
    ++r.job_id;r.seconds=20;renew=false;unsigned before=tick;
    assert(cerberus_s3_rx_start(&r)==ESP_OK);run();assert(status.state==CERBERUS_TEST_CANCELLED&&tick-before<=5050);
    renew=true;stop_early=true;++r.job_id;assert(cerberus_s3_rx_start(&r)==ESP_OK);run();assert(status.state==CERBERUS_TEST_CANCELLED);stop_early=false;
    ++r.job_id;assert(cerberus_s3_rx_start(&r)==ESP_OK);before=deinit_calls;cerberus_s3_rx_stop(r.job_id);run();assert(status.state==CERBERUS_TEST_CANCELLED&&deinit_calls==before);
    for(unsigned i=1;i<=8;++i){++r.job_id;step=0;fail_step=i;assert(cerberus_s3_rx_start(&r)==ESP_OK);run();assert(status.state==CERBERUS_TEST_ERROR&&!status.reserved&&cerberus_s3_wifi_owner()==CERBERUS_S3_WIFI_NONE);}
    fail_step=0;task_fail=true;++r.job_id;assert(cerberus_s3_rx_start(&r)==ESP_ERR_NO_MEM&&!queued&&cerberus_s3_wifi_owner()==CERBERUS_S3_WIFI_NONE);task_fail=false;
    for(unsigned mode=3;mode<=6;++mode){
        ++r.job_id;r.mode=mode;r.seconds=1;r.scope=cerberus_wifi_ap_needs_station(mode)?1:0;
        memset(r.station,0,6);if(r.scope)memcpy(r.station,(uint8_t[]){2,9,8,7,6,5},6);
        expected_filter=mode==CERBERUS_AP_BAD_MESSAGE?3:1;
        assert(cerberus_s3_rx_start(&r)==ESP_OK);run();assert(status.matching_frames==160&&!status.reserved);
    }
    ++r.job_id;r.mode=CERBERUS_AP_SAE_COMMIT;r.scope=CERBERUS_AP_BROADCAST;
    r.channel=36;assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_ARG);r.channel=6;
    expected_filter=1;assert(cerberus_s3_rx_start(&r)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_COMPLETE&&!status.reserved&&
        status.matching_frames==160&&status.observed_frames==480&&status.target_beacons==0);
    ++r.job_id;r.seconds=0;stop_at=tick+10000U;
    assert(cerberus_s3_rx_start(&r)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_CANCELLED&&!status.reserved&&
        status.elapsed_ms>=10000U&&status.matching_frames>=400U);
    ++r.job_id;renew=false;unsigned manual_started=tick;
    assert(cerberus_s3_rx_start(&r)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_CANCELLED&&!status.reserved&&
        tick-manual_started>=5000U&&tick-manual_started<=5050U);
    renew=true;r.seconds=1;
    cerberus_wifi_ap_station_set_t selected={.count=2,.mac={{2,9,8,7,6,5},{2,9,8,7,6,6}}};
    assert(cerberus_s3_rx_start_stations(&r,&selected)==ESP_ERR_INVALID_ARG);
    r.mode=CERBERUS_AP_DEAUTH;r.scope=CERBERUS_AP_STATION_LIST;memset(r.station,0,6);
    expected_filter=1;
    for(unsigned mode=CERBERUS_AP_DEAUTH;mode<=CERBERUS_AP_ASSOC_SLEEP;mode+=mode==CERBERUS_AP_DEAUTH?2:1){
        ++r.job_id;r.mode=mode;
        assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_ARG);
        assert(cerberus_s3_rx_start_stations(&r,NULL)==ESP_ERR_INVALID_ARG);
        cerberus_wifi_ap_station_set_t invalid=selected;invalid.count=1;
        assert(cerberus_s3_rx_start_stations(&r,&invalid)==ESP_ERR_INVALID_ARG);
        invalid=selected;memcpy(invalid.mac[1],invalid.mac[0],6);
        assert(cerberus_s3_rx_start_stations(&r,&invalid)==ESP_ERR_INVALID_ARG);
        expected_filter=mode==CERBERUS_AP_BAD_MESSAGE?3:1;
        assert(cerberus_s3_rx_start_stations(&r,&selected)==ESP_OK);
        invalid=selected;memset(invalid.mac[31],0xaa,6); /* Unused slots do not define job identity. */
        assert(cerberus_s3_rx_start_stations(&r,&invalid)==ESP_OK);
        invalid=selected;invalid.mac[1][5]=7;
        assert(cerberus_s3_rx_start_stations(&r,&invalid)==ESP_ERR_INVALID_ARG);
        assert(cerberus_s3_wifi_claim(CERBERUS_S3_WIFI_WEB)==ESP_ERR_INVALID_STATE);
        run();
        assert(status.state==CERBERUS_TEST_COMPLETE&&!status.reserved);
        /* Broadcast deauth, AP beacon, selected client, and other client are
         * all observed; only the beacon and selected client are attributed. */
        assert(status.target_beacons==160&&status.matching_frames==160&&status.observed_frames==640);
    }
    /* A clone only counts after receiving the selected AP's real capability
     * bytes. The source AP and randomized space-padding variants stay distinct. */
    ++r.job_id;r.mode=CERBERUS_AP_CLONE_SPAM;r.scope=CERBERUS_AP_BROADCAST;
    memcpy(r.station,(uint8_t[]){2,0xaa,0xbb,0xcc,0xdd,0xee},6);
    assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_ARG);
    assert(cerberus_s3_rx_start_clone(&r,NULL,1)==ESP_ERR_INVALID_ARG);
    assert(cerberus_s3_rx_start_clone(&r,(const uint8_t*)"Test",33)==ESP_ERR_INVALID_ARG);
    assert(cerberus_s3_rx_start_clone(&r,(const uint8_t*)"Test",4)==ESP_OK);
    assert(cerberus_s3_rx_start_clone(&r,(const uint8_t*)"Test",4)==ESP_OK);
    assert(cerberus_s3_rx_start_clone(&r,(const uint8_t*)"Tess",4)==ESP_ERR_INVALID_ARG);
    expected_filter=1;run();
    assert(status.state==CERBERUS_TEST_COMPLETE&&!status.reserved&&
        status.target_beacons==160&&status.matching_frames==319);
    ++r.job_id;const uint8_t hidden=0;
    assert(cerberus_s3_rx_start_clone(&r,&hidden,0)==ESP_OK);
    assert(cerberus_s3_rx_start_clone(&r,NULL,0)==ESP_OK);run();
    assert(status.target_beacons==160&&status.matching_frames==319);
    r.channel=36;++r.job_id;
    assert(cerberus_s3_rx_start_clone(&r,(const uint8_t*)"Test",4)==ESP_ERR_INVALID_ARG);
    r.channel=6;r.mode=CERBERUS_AP_DEAUTH;r.scope=0;memset(r.station,0,6);expected_filter=1;
    ++r.job_id;fail_cleanup=true;stop_early=true;assert(cerberus_s3_rx_start(&r)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_ERROR&&status.reserved&&cerberus_s3_wifi_owner()==CERBERUS_S3_WIFI_RECEIVER);
    assert(cerberus_s3_wifi_claim(CERBERUS_S3_WIFI_WEB)==ESP_ERR_INVALID_STATE);
    ++r.job_id;assert(cerberus_s3_rx_start(&r)==ESP_ERR_INVALID_STATE);assert(netif_calls==1&&event_calls==1);
    puts("S3 RX passed: multi-client and AP-clone attribution, 2.4 GHz SAE, indefinite Stop and lease expiry, exclusive Wi-Fi/BLE ownership and cleanup reservation.");
}
