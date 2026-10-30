#ifdef NDEBUG
#undef NDEBUG
#endif
#include "fake_radio_platform.h"
#include "wifi_ap_job.h"
#include "radio_test_service.h"
#include "sae_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned tick,tx_count,created,stop_at,fail_tx,free_bytes=128*1024;
static unsigned ap_tx_delay_ms,pressure_left,pressure_code,last_keepalive;
static uint16_t active_job;
static bool sample_live;
static unsigned last_stack,sae_count,material_count,zeroized_count;
static bool fail_material;
static void (*queued)(void*);
static int held,band=3;static uint8_t channel=6;
static unsigned raw_count,vendor_count,fail_restore,rate_calls,create_fail;
static bool target_valid=true;
static uint8_t invalid_station_last;
static unsigned target_checks;
static unsigned probe_count,random_calls,assoc_count,bad_count,announcement_count,clone_count;
static cerberus_wifi_ap_job_t expected_clone_job;
static bool capture_clients;
static unsigned captured_count;
static uint8_t captured_clients[128][6],captured_from_station[128];
static bool observe_beacon=true,promiscuous_enabled,short_eapol;
static bool keepalive;
static unsigned promiscuous_failure;
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *f){assert(f->filter_mask==1);return promiscuous_failure==1?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_set_promiscuous(bool on){if(promiscuous_failure==(on?2U:3U))return ESP_FAIL;promiscuous_enabled=on;return ESP_OK;}
void esp_fill_random(void *p,size_t n){assert(n==6||n==32||n==1);memset(p,++random_calls,n);}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned timeout){(void)m;(void)timeout;assert(!held);held=1;return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){(void)m;assert(held);held=0;return 1;}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned p,void*out){(void)n;(void)a;(void)p;(void)out;assert(stack==4096||stack==8192);last_stack=stack;if(create_fail)return 0;queued=fn;++created;return 1;}
void vTaskDelete(void*p){(void)p;}
int64_t esp_timer_get_time(void){return (int64_t)tick*1000;}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return free_bytes;}
esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t*p){*p=band;return 0;}
esp_err_t esp_wifi_get_channel(uint8_t*p,wifi_second_chan_t*s){*p=channel;*s=0;return 0;}
esp_err_t esp_wifi_get_mac(int i,uint8_t*m){(void)i;const uint8_t mac[]={2,4,6,8,10,12};memcpy(m,mac,6);return 0;}
esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t b){band=b;return fail_restore&&b==3?ESP_FAIL:ESP_OK;}
esp_err_t esp_wifi_set_channel(uint8_t c,wifi_second_chan_t s){(void)s;channel=c;return 0;}
esp_err_t esp_wifi_config_80211_tx_rate(int i,int rate){(void)i;(void)rate;++rate_calls;return ESP_FAIL;}
void mbedtls_platform_zeroize(void *data,size_t size){volatile uint8_t *p=data;while(size--)*p++=0;++zeroized_count;}
esp_err_t cerberus_c5_sae_material_generate(uint8_t scalar[32],uint8_t element[64])
{
    ++material_count;
    if(fail_material)return ESP_ERR_NO_MEM;
    memset(scalar,0,32);scalar[31]=2;
    memcpy(element,cerberus_test_sae_2g,64);
    return ESP_OK;
}
#include "raw_wifi_tx.c"
int cerberus_vendor_raw_frame_sanity_check(int i,const void*p,int n,bool seq)
{(void)i;(void)seq;++vendor_count;return p&&n>=36&&(((const uint8_t*)p)[0]==0x40||((const uint8_t*)p)[0]==8||((const uint8_t*)p)[0]==0x80)?ESP_OK:ESP_FAIL;}
esp_err_t esp_wifi_80211_tx(int i,const void*frame,int n,bool sequence){
    const uint8_t*p=frame;assert(sequence);
    if(capture_clients){
        assert(captured_count<128);
        const uint8_t *client=NULL;
        if(p[0]==0xc0){
            bool from_station=!memcmp(p+4,p+16,6);
            client=from_station?p+10:p+4;
            captured_from_station[captured_count]=from_station;
        }else if(p[0]==8){client=p+4;}
        else if(p[0]==0){client=p+10;}
        else assert(0);
        memcpy(captured_clients[captured_count++],client,6);
    }
    if(p[0]==0xc0){assert(n==26&&ieee80211_raw_frame_sanity_check(i,frame,n,sequence)==ESP_OK);++raw_count;}
    else if(p[0]==0x40){assert(!approved_frame&&n==43&&(p[10]&3)==2&&p[25]==7&&!memcmp(p+26,"Test AP",7));assert(!memcmp(p+4,p+16,6));assert(ieee80211_raw_frame_sanity_check(i,frame,n,sequence)==ESP_OK);++probe_count;}
    else if(p[0]==0){assert(n==107&&p[1]==0x10&&p[29]==7&&!memcmp(p+30,"Test AP",7));assert(approved_frame==frame&&ieee80211_raw_frame_sanity_check(i,frame,n,sequence)==ESP_OK);++assoc_count;}
    else if(p[0]==8){assert(n==(short_eapol?131:153)&&p[1]==2&&p[48]>0&&p[49]);assert(!approved_frame&&ieee80211_raw_frame_sanity_check(i,frame,n,sequence)==ESP_OK);++bad_count;}
    else if(p[0]==0xb0){
        assert(n==128&&p[1]==0&&p[24]==3&&p[25]==0&&p[26]==1&&p[27]==0&&p[30]==19&&p[31]==0);
        assert(!memcmp(p+4,p+16,6)&&!(p[4]&1));
        assert((p[10]&3)==2&&!memcmp(p+64,cerberus_test_sae_2g,64));
        for(unsigned j=32;j<63;++j)assert(p[j]==0);
        assert(p[63]==2&&approved_frame==frame&&
            ieee80211_raw_frame_sanity_check(i,frame,n,sequence)==ESP_OK);
        ++sae_count;
    }
    else {
        assert(n>=51&&n<=121&&p[0]==0x80&&p[4]==255&&p[10]==2);
        if(expected_clone_job.mode==CERBERUS_AP_CLONE_SPAM){
            assert(n>=77&&n<=109&&!promiscuous_enabled);
            assert(!memcmp(p+10,expected_clone_job.station,6)&&!memcmp(p+16,expected_clone_job.station,6));
            assert(p[37]>=7&&p[37]<=32&&!memcmp(p+38,"Test AP",7));
            for(unsigned j=7;j<p[37];++j)assert(p[38+j]==' ');
            assert(p[34]==0x31&&p[35]==4&&p[50+p[37]]==expected_clone_job.channel);
            ++clone_count;
        }else if(n==63||n==89){assert(p[34]==0x31&&p[35]==4&&!promiscuous_enabled);++announcement_count;}
    }
    ++tx_count;
    if(sample_live&&tx_count==10){
        cerberus_radio_test_status_t live;
        cerberus_c5_test_status(active_job,&live);
        assert(live.state==CERBERUS_TEST_RUNNING&&live.accepted_frames>0&&live.accepted_frames<10);
    }
    tick+=ap_tx_delay_ms;
    if(keepalive&&(unsigned)(tick-last_keepalive)>=250U){
        last_keepalive=tick;
        cerberus_radio_test_status_t lease;cerberus_c5_test_status(active_job,&lease);
    }
    if(stop_at&&tick>=stop_at){stop_at=0;assert(cerberus_c5_test_stop(active_job)==ESP_OK);}
    if(pressure_left){--pressure_left;((uint8_t*)frame)[22]=0x10;return pressure_code;}
    return fail_tx&&tx_count==fail_tx?ESP_FAIL:ESP_OK;
}
#include "radio_test_service.c"
esp_err_t cerberus_wifi_ap_target_valid(const cerberus_wifi_ap_job_t *r)
{assert(cerberus_wifi_ap_job_valid(r));++target_checks;return target_valid&&
    !(r->scope==CERBERUS_AP_ONE_STATION&&invalid_station_last&&r->station[5]==invalid_station_last)?
    ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t cerberus_wifi_ap_target_ssid(const cerberus_wifi_ap_job_t *r,uint8_t name[32],uint8_t *length)
{assert(cerberus_wifi_ap_job_valid(r));*length=7;memcpy(name,"Test AP",7);return target_valid?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t cerberus_wifi_ap_target_metadata(const cerberus_wifi_ap_job_t *r,cerberus_wifi_ap_metadata_t *m)
{m->short_eapol=short_eapol;return cerberus_wifi_ap_target_ssid(r,m->ssid,&m->ssid_length);}
void vTaskDelay(unsigned delay){tick+=delay;
    if(keepalive){cerberus_radio_test_status_t lease;cerberus_c5_test_status(status.job_id,&lease);}
    if(promiscuous_enabled&&observe_beacon){
        uint8_t frame[48]={0x80};memset(frame+4,255,6);memcpy(frame+10,ap_job.bssid,6);memcpy(frame+16,ap_job.bssid,6);
        frame[34]=0x31;frame[35]=4;frame[37]=7;memcpy(frame+38,"Test AP",7);frame[45]=3;frame[46]=1;frame[47]=ap_job.channel;
        cerberus_c5_test_receive(frame,sizeof(frame),ap_job.channel);
    }if(stop_at&&tick>=stop_at){stop_at=0;assert(cerberus_c5_test_stop(status.job_id)==ESP_OK);}}
static void stage(uint16_t id,unsigned seconds)
{
    cerberus_radio_test_begin_t begin={.job_id=id,.mode=CERBERUS_TEST_WIFI_BEACON,.channel=36,.seconds=seconds,.name_length=32};
    memset(begin.name,'A',16);assert(cerberus_c5_test_begin(&begin)==ESP_OK);
    assert(cerberus_c5_test_start(id,false)==ESP_ERR_INVALID_STATE);
    cerberus_radio_test_chunk_t chunk={.job_id=id,.offset=16,.length=16};memset(chunk.data,'B',16);
    assert(cerberus_c5_test_chunk(&chunk)==ESP_OK);
}
static void stage_selected(const cerberus_wifi_ap_job_t *ap,const uint8_t mac[][6],unsigned count)
{
    cerberus_wifi_ap_station_begin_t begin={.job=*ap,.count=count};
    assert(cerberus_c5_ap_stage_begin(&begin,false)==ESP_OK);
    for(unsigned offset=0;offset<count;offset+=3){
        cerberus_wifi_ap_station_chunk_t chunk={.job_id=ap->job_id,.offset=offset,
            .count=count-offset<3?count-offset:3};
        memcpy(chunk.mac,mac[offset],chunk.count*6);
        assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_OK);
    }
}
static void run(void){void(*fn)(void*)=queued;queued=NULL;assert(fn);
    cerberus_radio_test_status_t current;cerberus_c5_test_status(0,&current);
    active_job=current.job_id;last_keepalive=tick;fn(NULL);active_job=0;
    assert(!held&&!cerberus_c5_test_running());}
int main(void)
{
    assert(cerberus_c5_test_init()==ESP_OK);stage(1,1);
    assert(cerberus_c5_test_start(1,true)==ESP_ERR_INVALID_STATE);assert(!queued);
    assert(cerberus_c5_test_start(1,false)==ESP_OK);assert(cerberus_c5_test_start(1,false)==ESP_OK&&created==1);
    run();assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==10&&band==3&&channel==6);
    assert(cerberus_c5_test_start(1,false)==ESP_OK&&!queued);
    stage(2,10);assert(cerberus_c5_test_start(2,false)==ESP_OK);unsigned before=tick;run();
    assert(status.state==CERBERUS_TEST_CANCELLED&&tick-before<=3020&&status.accepted_frames<=31);
    stage(3,10);assert(cerberus_c5_test_start(3,false)==ESP_OK);stop_at=tick+250;before=tick;run();assert(status.state==CERBERUS_TEST_CANCELLED&&tick-before<=300);
    stage(4,1);tx_count=0;fail_tx=3;assert(cerberus_c5_test_start(4,false)==ESP_OK);run();assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_FAIL&&band==3&&channel==6);
    stage(5,1);free_bytes=16*1024;assert(cerberus_c5_test_start(5,false)==ESP_ERR_NO_MEM&&!queued);
    cerberus_radio_test_status_t s;before=contact_ms;tick+=100;cerberus_c5_test_status(0,&s);assert(contact_ms==before);cerberus_c5_test_status(5,&s);assert(contact_ms==tick);
    assert(cerberus_c5_test_stop(55)==ESP_ERR_NOT_FOUND);
    assert(cerberus_c5_test_stop(5)==ESP_OK&&status.state==CERBERUS_TEST_CANCELLED);
    cerberus_radio_test_config_t c={.job_id=1,.mode=1,.channel=6,.seconds=30};uint8_t out[98],mac[]={2,4,6,8,10,12};memset(c.name,'X',32);
    for(unsigned n=1;n<=32;++n){c.name_length=n;memset(out,0xa5,sizeof(out));size_t length=cerberus_radio_test_beacon(&c,mac,0x1122334455667788ULL,out+1,96);assert(length==51+n&&out[0]==0xa5&&out[length+1]==0xa5&&out[25]==0x88&&out[38]==n);assert(!cerberus_radio_test_beacon(&c,mac,0,out,length-1));}
    c.mode=2;c.channel=0;for(unsigned n=1;n<=26;++n){c.name_length=n;size_t length=cerberus_radio_test_ble(&c,out,31);assert(length==5+n&&out[0]==2&&out[4]==9);}
    c.name_length=27;assert(!cerberus_radio_test_ble(&c,out,31));c.name_length=3;c.name[1]='\n';assert(!cerberus_radio_test_config_valid(&c));c.name[1]='X';c.seconds=31;assert(!cerberus_radio_test_config_valid(&c));
    free_bytes=128*1024;fail_tx=0;tx_count=0;ap_tx_delay_ms=50;
    cerberus_wifi_ap_job_t ap={.job_id=6,.generation=4,.index=1,.bssid={2,0,0,0,0,1},.channel=36,.seconds=1,.mode=CERBERUS_AP_DEAUTH};
    target_valid=false;assert(cerberus_c5_ap_start(&ap,false)==ESP_ERR_INVALID_STATE&&!queued);target_valid=true;
    assert(cerberus_c5_ap_start(&ap,true)==ESP_ERR_INVALID_STATE&&!queued);
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);
    unsigned jobs=created;assert(cerberus_c5_ap_start(&ap,false)==ESP_OK&&created==jobs);
    ap.channel=40;assert(cerberus_c5_ap_start(&ap,false)==ESP_ERR_INVALID_ARG);ap.channel=36;
    run();assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==20&&raw_count==20);
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK&&!queued); /* Finished retry never restarts. */
    ap.job_id=7;ap.scope=CERBERUS_AP_ONE_STATION;memcpy(ap.station,(uint8_t[]){2,2,3,4,5,6},6);
    uint8_t frame[28];memset(frame,0xa5,sizeof(frame));
    assert(cerberus_wifi_ap_frame(&ap,false,frame+1,26)==26&&frame[0]==0xa5&&frame[27]==0xa5);
    assert(!memcmp(frame+5,ap.station,6)&&!memcmp(frame+11,ap.bssid,6)&&!memcmp(frame+17,ap.bssid,6));
    assert(cerberus_wifi_ap_frame(&ap,true,frame,26)==26&&!memcmp(frame+4,ap.bssid,6)&&!memcmp(frame+10,ap.station,6)&&!memcmp(frame+16,ap.bssid,6));
    assert(!cerberus_wifi_ap_frame(&ap,false,frame,25));
    assert(ieee80211_raw_frame_sanity_check(0,frame,26,true)==ESP_FAIL&&vendor_count==1); /* Unapproved pointer uses vendor check. */
    frame[1]=0x40;assert(cerberus_c5_raw_deauth(frame,26)==ESP_ERR_INVALID_ARG);frame[1]=0;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();assert(status.accepted_frames==20);
    ap.job_id=8;ap.seconds=10;assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();assert(status.state==CERBERUS_TEST_CANCELLED&&status.elapsed_ms<=3100);
    ap.job_id=9;assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);stop_at=tick+10;run();assert(status.state==CERBERUS_TEST_CANCELLED&&status.accepted_frames==1);
    ap.job_id=10;create_fail=1;assert(cerberus_c5_ap_start(&ap,false)==ESP_ERR_NO_MEM&&!cerberus_c5_test_running());create_fail=0;
    ap.job_id=11;ap.seconds=1;ap.mode=CERBERUS_AP_PROBE_REQUEST;
    assert(!cerberus_wifi_ap_job_valid(&ap));ap.scope=0;memset(ap.station,0,6);
    unsigned old_raw=raw_count,old_vendor=vendor_count;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(probe_count==20&&random_calls==20&&raw_count==old_raw&&vendor_count==old_vendor+20&&status.accepted_frames==20);
    uint8_t name[32];memset(name,'Z',32);uint8_t probe[70];
    for(unsigned n=0;n<=32;++n){memset(probe,0xa5,sizeof(probe));size_t bytes=cerberus_wifi_ap_probe(&ap,mac,name,n,probe+1,68);
        assert(bytes==36+n&&probe[0]==0xa5&&probe[bytes+1]==0xa5&&probe[26]==n&&probe[27+n]==1&&probe[28+n]==8);
        assert(!cerberus_wifi_ap_probe(&ap,mac,name,n,probe,bytes-1));}
    assert(!cerberus_wifi_ap_probe(&ap,ap.bssid,name,32,probe,70));
    mac[0]=1;assert(!cerberus_wifi_ap_probe(&ap,mac,name,32,probe,70));mac[0]=2;
    for(unsigned mode=CERBERUS_AP_BAD_MESSAGE;mode<=CERBERUS_AP_QUIET;++mode){
        ap.mode=mode;ap.job_id=20+mode;ap.seconds=1;ap.scope=cerberus_wifi_ap_needs_station(mode)?1:0;
        memset(ap.station,0,6);if(ap.scope)memcpy(ap.station,(uint8_t[]){2,2,3,4,5,6},6);
        assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==20&&!approved_frame&&!promiscuous_enabled);
    }
    assert(assoc_count==20&&bad_count==20&&announcement_count==40);
    ap.mode=CERBERUS_AP_BAD_MESSAGE;ap.job_id=30;ap.scope=1;memcpy(ap.station,(uint8_t[]){2,2,3,4,5,6},6);short_eapol=true;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();assert(bad_count==40);short_eapol=false;
    /* One AP job stages a complete selected-client set before any radio work. */
    const uint8_t selected[3][6]={{2,2,3,4,5,1},{2,2,3,4,5,2},{2,2,3,4,5,3}};
    cerberus_wifi_ap_job_t list={.job_id=50,.generation=4,.index=1,
        .bssid={2,0,0,0,0,1},.channel=36,.seconds=1,
        .scope=CERBERUS_AP_STATION_LIST,.mode=CERBERUS_AP_DEAUTH};
    cerberus_wifi_ap_station_begin_t begin={.job=list,.count=3};
    assert(cerberus_wifi_ap_job_valid(&list));
    assert(cerberus_c5_ap_stage_begin(NULL,false)==ESP_ERR_INVALID_ARG);
    assert(cerberus_c5_ap_stage_chunk(NULL)==ESP_ERR_INVALID_ARG);
    assert(cerberus_c5_ap_start(&list,false)==ESP_ERR_INVALID_STATE&&!queued);
    begin.count=1;assert(cerberus_c5_ap_stage_begin(&begin,false)==ESP_ERR_INVALID_ARG);
    begin.count=33;assert(cerberus_c5_ap_stage_begin(&begin,false)==ESP_ERR_INVALID_ARG);
    begin.count=3;begin.job.mode=CERBERUS_AP_PROBE_REQUEST;
    assert(cerberus_c5_ap_stage_begin(&begin,false)==ESP_ERR_INVALID_ARG);
    begin.job.mode=CERBERUS_AP_DEAUTH;
    assert(cerberus_c5_ap_stage_begin(&begin,true)==ESP_ERR_INVALID_STATE);
    assert(cerberus_c5_ap_stage_begin(&begin,false)==ESP_OK);
    assert(cerberus_c5_ap_stage_begin(&begin,true)==ESP_OK); /* Lost ACK retry keeps coverage. */
    cerberus_wifi_ap_station_begin_t conflicting=begin;conflicting.count=2;
    assert(cerberus_c5_ap_stage_begin(&conflicting,false)==ESP_ERR_INVALID_ARG);
    cerberus_wifi_ap_station_chunk_t chunk={.job_id=50,.offset=2,.count=1};
    memcpy(chunk.mac[0],selected[2],6);
    chunk.count=4;assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    chunk.count=1;chunk.offset=32;
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    chunk.offset=2;chunk.mac[0][0]=1;
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    memcpy(chunk.mac[0],selected[2],6);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_OK);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_OK);
    assert(cerberus_c5_ap_start(&list,false)==ESP_ERR_INVALID_STATE&&!queued);
    chunk.job_id=49;assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_STATE);
    chunk.job_id=50;chunk.offset=3;assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_STATE);
    chunk.offset=2;memcpy(chunk.mac[0],selected[1],6);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    chunk.offset=0;chunk.count=2;memcpy(chunk.mac[0],selected[0],6);
    memcpy(chunk.mac[1],selected[0],6);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    assert(cerberus_c5_ap_start(&list,false)==ESP_ERR_INVALID_STATE&&!queued);
    memcpy(chunk.mac[1],list.bssid,6);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    memcpy(chunk.mac[1],selected[1],6);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_OK);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_OK);
    chunk.offset=1;chunk.count=1;memcpy(chunk.mac[0],selected[0],6);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    chunk.offset=0;chunk.count=2;memcpy(chunk.mac[0],selected[0],6);
    memcpy(chunk.mac[1],selected[1],6);
    invalid_station_last=3;unsigned checked=target_checks,jobs_before=created;
    assert(cerberus_c5_ap_start(&list,false)==ESP_ERR_INVALID_STATE&&created==jobs_before&&!queued);
    assert(target_checks-checked==3);invalid_station_last=0;
    assert(cerberus_c5_ap_start(&list,true)==ESP_ERR_INVALID_STATE);
    free_bytes=16*1024;
    assert(cerberus_c5_ap_start(&list,false)==ESP_ERR_NO_MEM&&!queued);
    free_bytes=128*1024;
    capture_clients=true;captured_count=0;
    assert(cerberus_c5_ap_start(&list,false)==ESP_OK);
    jobs_before=created;
    assert(cerberus_c5_ap_start(&list,false)==ESP_OK&&created==jobs_before);
    assert(cerberus_c5_ap_stage_begin(&begin,false)==ESP_OK);
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_OK);
    chunk.mac[0][5]=9;
    assert(cerberus_c5_ap_stage_chunk(&chunk)==ESP_ERR_INVALID_ARG);
    chunk.mac[0][5]=1;
    conflicting.job.job_id=51;
    assert(cerberus_c5_ap_stage_begin(&conflicting,false)==ESP_ERR_INVALID_STATE);
    run();capture_clients=false;
    assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==20&&captured_count==20);
    for(unsigned i=0;i<captured_count;++i){
        assert(!memcmp(captured_clients[i],selected[(i/2)%3],6));
        assert(captured_from_station[i]==(i&1));
    }
    assert(cerberus_c5_ap_start(&list,false)==ESP_OK&&!queued); /* Finished ACK is inert. */
    for(unsigned mode=CERBERUS_AP_BAD_MESSAGE;mode<=CERBERUS_AP_ASSOC_SLEEP;++mode){
        list.job_id=48+mode;list.mode=mode;
        stage_selected(&list,selected,3);
        capture_clients=true;captured_count=0;
        assert(cerberus_c5_ap_start(&list,false)==ESP_OK);run();capture_clients=false;
        assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==20&&captured_count==20);
        for(unsigned i=0;i<captured_count;++i)
            assert(!memcmp(captured_clients[i],selected[i%3],6));
    }
    uint8_t many[32][6];
    for(unsigned i=0;i<32;++i){memcpy(many[i],(uint8_t[]){2,2,3,4,6,0},6);many[i][5]=i+1;}
    list.job_id=53;list.mode=CERBERUS_AP_DEAUTH;list.seconds=4;
    stage_selected(&list,(const uint8_t (*)[6])many,32);
    capture_clients=keepalive=true;captured_count=0;checked=target_checks;
    assert(cerberus_c5_ap_start(&list,false)==ESP_OK);
    assert(target_checks-checked==32);
    run();capture_clients=keepalive=false;
    assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==80&&captured_count==80);
    for(unsigned i=0;i<captured_count;++i){
        assert(!memcmp(captured_clients[i],many[(i/2)%32],6));
        assert(captured_from_station[i]==(i&1));
    }
    list.job_id=54;list.seconds=10;
    stage_selected(&list,selected,3);
    assert(cerberus_c5_ap_start(&list,false)==ESP_OK);stop_at=tick+250;
    run();assert(status.state==CERBERUS_TEST_CANCELLED&&status.accepted_frames<=6);
    ap.mode=CERBERUS_AP_SAE_COMMIT;ap.scope=0;memset(ap.station,0,6);ap.job_id=56;ap.seconds=1;
    assert(cerberus_wifi_ap_job_valid(&ap));
    free_bytes=32*1024-1;assert(cerberus_c5_ap_start(&ap,false)==ESP_ERR_NO_MEM&&!queued);
    free_bytes=128*1024;
    unsigned materials=material_count,sae_before=sae_count,zeroized=zeroized_count;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK&&last_stack==8192);run();
    assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==20&&
        material_count==materials+20&&sae_count==sae_before+20&&zeroized_count>=zeroized+80);
    uint8_t auth[128],source[]={2,0x12,0x34,0x56,0x78,0x9a},scalar[32]={0};scalar[31]=2;
    assert(cerberus_wifi_sae_commit_frame(&ap,source,scalar,cerberus_test_sae_2g,NULL,0,auth,sizeof(auth))==128);
    unsigned tx_before=tx_count,old_vendor_sae=vendor_count;
    assert(ieee80211_raw_frame_sanity_check(WIFI_IF_STA,auth,128,true)==ESP_FAIL&&vendor_count==old_vendor_sae+1);
    auth[24]=2;assert(cerberus_c5_raw_sae_commit(&ap,auth,sizeof(auth))==ESP_ERR_INVALID_ARG&&tx_count==tx_before);
    auth[24]=3;auth[4]^=2;assert(cerberus_c5_raw_sae_commit(&ap,auth,sizeof(auth))==ESP_ERR_INVALID_ARG&&tx_count==tx_before);auth[4]^=2;
    uint8_t x_backup[32];memcpy(x_backup,auth+64,32);memset(auth+64,0xff,32);
    assert(cerberus_c5_raw_sae_commit(&ap,auth,sizeof(auth))==ESP_ERR_INVALID_ARG&&tx_count==tx_before);
    memcpy(auth+64,x_backup,32);
    assert(cerberus_c5_raw_sae_commit(&ap,auth,sizeof(auth)-1)==ESP_ERR_INVALID_ARG&&tx_count==tx_before);
    cerberus_wifi_ap_job_t foreign=ap;foreign.bssid[5]^=2;
    assert(cerberus_c5_raw_sae_commit(&foreign,auth,sizeof(auth))==ESP_ERR_INVALID_ARG&&tx_count==tx_before);
    ap.job_id=59;tx_count=0;fail_tx=1;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_FAIL&&
        status.name_length==CERBERUS_AP_STAGE_TRANSMIT&&status.accepted_frames==0&&
        !approved_frame&&band==3&&channel==6);
    fail_tx=0;fail_material=true;ap.job_id=60;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_ERR_NO_MEM&&
        status.name_length==CERBERUS_AP_STAGE_SAE_MATERIAL&&!status.accepted_frames&&
        !approved_frame&&band==3&&channel==6);
    fail_material=false;
    /* Zero duration has no TX deadline: live counts publish while the lease is
     * renewed, then only explicit Stop ends a ten-second simulated run. */
    ap.mode=CERBERUS_AP_DEAUTH;ap.job_id=61;ap.seconds=0;
    assert(cerberus_wifi_ap_job_valid(&ap));
    keepalive=sample_live=true;tx_count=0;unsigned manual_started=tick;
    stop_at=tick+10000U;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_CANCELLED&&status.seconds==0&&
        status.elapsed_ms>=10000U&&status.elapsed_ms<=10050U&&
        status.accepted_frames>=195U&&status.accepted_frames==tx_count);
    keepalive=sample_live=false;
    /* Lease expiry remains a fail-safe for a lost S3 controller. */
    ap.job_id=62;manual_started=tick;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_CANCELLED&&
        tick-manual_started>=3000U&&tick-manual_started<=3100U);
    /* A faster mock driver exceeds the former 20-fps cap without spinning
     * forever; Stop is injected from the TX callback after 100 ms. */
    ap.job_id=63;ap_tx_delay_ms=1;tx_count=0;keepalive=true;stop_at=tick+100U;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_CANCELLED&&status.accepted_frames>20U&&
        status.accepted_frames==tx_count&&status.elapsed_ms<=110U);
    ap_tx_delay_ms=50;keepalive=false;
    /* Queue pressure retries the same frame; persistent pressure fails with
     * an exact TX stage rather than silently burning the session. */
    ap.mode=CERBERUS_AP_SAE_COMMIT;ap.seconds=1;ap.job_id=64;
    tx_count=0;pressure_left=2;pressure_code=ESP_ERR_WIFI_WOULD_BLOCK;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames>0&&
        tx_count==status.accepted_frames+2U&&!pressure_left);
    ap.job_id=65;tx_count=0;pressure_left=5;pressure_code=ESP_ERR_NO_MEM;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_ERR_NO_MEM&&
        status.name_length==CERBERUS_AP_STAGE_TRANSMIT&&!status.accepted_frames&&
        tx_count==5&&!pressure_left&&!approved_frame);
    ap.mode=CERBERUS_AP_CLONE_SPAM;ap.job_id=57;
    memcpy(ap.station,(uint8_t[]){2,9,8,7,6,5},6);
    expected_clone_job=ap;
    assert(cerberus_wifi_ap_job_valid(&ap));
    unsigned old_clones=clone_count,old_random=random_calls;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==20&&
        clone_count==old_clones+20&&random_calls>=old_random+20);
    ap.job_id=58;observe_beacon=false;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_ERR_TIMEOUT&&
        status.name_length==CERBERUS_AP_STAGE_BEACON_WAIT&&
        !status.accepted_frames&&!promiscuous_enabled&&clone_count==old_clones+20);
    observe_beacon=true;
    ap.mode=CERBERUS_AP_CHANNEL_SWITCH;ap.scope=0;memset(ap.station,0,6);ap.job_id=31;observe_beacon=false;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_ERR_TIMEOUT&&!status.accepted_frames&&!promiscuous_enabled);
    observe_beacon=true;ap.job_id=32;assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);stop_at=tick+10;run();assert(status.state==CERBERUS_TEST_CANCELLED&&!status.accepted_frames);
    ap.mode=CERBERUS_AP_PROBE_REQUEST;ap.job_id=37;
    assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_COMPLETE&&status.accepted_frames==20&&rate_calls==0);
    ap.mode=CERBERUS_AP_CHANNEL_SWITCH;
    for(unsigned failure=1;failure<=2;++failure){ap.job_id=32+failure;promiscuous_failure=failure;assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);run();assert(status.state==CERBERUS_TEST_ERROR&&!status.accepted_frames&&!promiscuous_enabled);}promiscuous_failure=0;
    ap.job_id=39;promiscuous_failure=3;assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);queued(NULL);queued=NULL;
    assert(status.state==CERBERUS_TEST_ERROR&&!status.accepted_frames&&restore_fault&&cerberus_c5_test_running());
    ++ap.job_id;assert(cerberus_c5_ap_start(&ap,false)==ESP_ERR_INVALID_STATE);
    /* Simulate recovery by reset before checking the independent band restore fault. */
    promiscuous_failure=0;promiscuous_enabled=false;restore_fault=false;
    ap.mode=CERBERUS_AP_PROBE_REQUEST;ap.job_id=41;ap.seconds=1;assert(cerberus_c5_ap_start(&ap,false)==ESP_OK);fail_restore=1;queued(NULL);queued=NULL;
    assert(status.state==CERBERUS_TEST_ERROR&&cerberus_c5_test_running()&&restore_fault); /* Reserve radio until reboot. */
    ++ap.job_id;assert(cerberus_c5_ap_start(&ap,false)==ESP_ERR_INVALID_STATE&&!queued);
    ap.channel=52;assert(!cerberus_wifi_ap_job_valid(&ap));ap.channel=36;ap.station[0]=1;assert(!cerberus_wifi_ap_job_valid(&ap));
    puts("Radio service passed: AP manual Stop beyond 10 seconds, lease expiry, live/final counts, faster-than-20-fps mock, bounded queue retries, station rotation and cleanup ownership.");
}
