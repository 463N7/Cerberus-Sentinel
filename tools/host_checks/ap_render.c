#include "ap_framebuffer.h"
#include "c5_spi_link.h"
#include "wifi_targets.h"
#include "ui_widgets.h"
#include "s3_wifi_receiver.h"
#include "storage_report_limits.h"
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_NOT_FOUND 0x105
#define UI_BACKGROUND 0x0042
#define UI_PANEL 0x08A4
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define TAG "ap-test"
static const cerberus_ui_widget_theme_t theme={UI_BACKGROUND,UI_PANEL,UI_MUTED,UI_TEXT,UI_MUTED};
#define UI_WIDGET_THEME (&theme)
enum {UI_WIFI_MODE_STATIONS=1};
static int wifi_mode;
static cerberus_c5_status_t c5_status={.firmware_version=42,.capabilities=CERBERUS_C5_CAPABILITIES};
static cerberus_c5_wifi_result_t wifi_selected_result={.ssid="Cerberus Test AP",.bssid={2,0,0,0,0,1},.channel=36};
static bool wifi_selection_valid=true,c5_online=true,station_selected_valid,masked,missing_ap,lost_ack,start_rejected,dropped_start;
static uint8_t wifi_selected_index;
static bool renamed_ap;
static cerberus_wifi_station_result_t station_selected={.mac={2,3,4,5,6,7},.channel=36};
static uint8_t station_selected_ap[6]={2,0,0,0,0,1};
static uint8_t connection_state=CERBERUS_WIFI_CONNECTION_IDLE;
static unsigned starts,stops,leases,saves,station_entries,record_reads;
static esp_err_t link_error,save_error;
static cerberus_radio_test_status_t remote;
static char csv_saved[65536];
static cerberus_s3_rx_status_t local_rx;
static unsigned rx_starts,rx_stops,rx_leases,rx_list_starts,stages;
static esp_err_t rx_start_error;
static esp_err_t stage_error;
static cerberus_wifi_ap_station_set_t staged_sets[32];
static uint8_t staged_bssid[32];
static bool use_inventory;
static unsigned inventory_count=3,validation_calls,validation_reject=99,start_log_count;
static unsigned change_after_validation;
static bool change_generation,delay_rx_cleanup,duplicate_random;
static uint8_t start_log[128];
static cerberus_c5_wifi_result_t inventory[32];
static unsigned random_calls,clone_rx_starts,clone_validation_count;
static uint8_t clone_rx_macs[32][6],clone_validation_macs[32][6];
static void esp_fill_random(void *buffer,size_t length)
{
    ++random_calls;uint8_t *bytes=buffer;
    for(size_t i=0;i<length;++i)
        bytes[i]=(uint8_t)(0x20U+(duplicate_random?0U:random_calls)+i);
}
esp_err_t cerberus_s3_rx_start(const cerberus_wifi_ap_job_t *r)
{++rx_starts;if(rx_start_error)return rx_start_error;local_rx=(cerberus_s3_rx_status_t){.job_id=r->job_id,.channel=r->channel,.state=CERBERUS_TEST_STAGED,.reserved=true};return ESP_OK;}
esp_err_t cerberus_s3_rx_start_stations(const cerberus_wifi_ap_job_t *r,
    const cerberus_wifi_ap_station_set_t *set)
{assert(cerberus_wifi_ap_station_set_valid(r,set));++rx_list_starts;return cerberus_s3_rx_start(r);}
esp_err_t cerberus_s3_rx_start_clone(const cerberus_wifi_ap_job_t *r,
    const uint8_t *ssid,size_t ssid_length)
{
    assert(r->mode==CERBERUS_AP_CLONE_SPAM&&cerberus_wifi_ap_job_valid(r));
    assert(ssid&&ssid_length<=32&&clone_rx_starts<32);
    memcpy(clone_rx_macs[clone_rx_starts++],r->station,6);
    return cerberus_s3_rx_start(r);
}
void cerberus_s3_rx_stop(uint16_t id)
{if(id==local_rx.job_id&&local_rx.reserved){++rx_stops;local_rx.state=delay_rx_cleanup?CERBERUS_TEST_STOPPING:CERBERUS_TEST_CANCELLED;local_rx.reserved=delay_rx_cleanup;}}
void cerberus_s3_rx_status(uint16_t id,cerberus_s3_rx_status_t *out)
{if(id&&id==local_rx.job_id)++rx_leases;*out=local_rx;}
static const char *esp_err_to_name(int error){return error?"ESP_ERR_INVALID_STATE":"ESP_OK";}
static esp_err_t draw_centered(uint16_t y,const char *s,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320-strlen(s)*6*scale)/2),y,s,color,UI_BACKGROUND,scale);}
static esp_err_t draw_small_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *s)
{return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){x,y,w,h},s,1,true,&theme);}
esp_err_t cerberus_display_draw_mono_bitmap(uint16_t x,uint16_t y,uint16_t width,uint16_t height,
    const uint8_t *bitmap,uint16_t color,uint16_t background)
{
    if(!bitmap||!width||!height||x+width>320||y+height>480)return ESP_ERR_INVALID_ARG;
    size_t stride=(width+7U)/8U;
    for(unsigned row=0;row<height;++row)for(unsigned col=0;col<width;++col)
        screen[(y+row)*320U+x+col]=bitmap[row*stride+col/8U]&(0x80U>>(col%8U))?
            color:background;
    return ESP_OK;
}
static bool privacy_addresses_masked(void){return masked;}
static void cerberus_privacy_format_wifi_identifier(const uint8_t *mac,bool hide,char *out,size_t size)
{if(hide)snprintf(out,size,"XX:XX:XX:XX:%02X:%02X",mac[4],mac[5]);else snprintf(out,size,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);}
static void station_enter(void){++station_entries;}
esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *s)
{*s=(cerberus_wifi_connection_status_payload_t){.state=connection_state};return link_error;}
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *s)
{*s=(cerberus_wifi_scan_status_payload_t){.state=CERBERUS_WIFI_SCAN_COMPLETE,.generation=change_generation?5:4,.stored_count=use_inventory?inventory_count:2};return link_error;}
esp_err_t c5_spi_link_get_wifi_scan_result(uint8_t generation,uint8_t index,cerberus_c5_wifi_result_t *r)
{++record_reads;assert(generation==4&&index<(use_inventory?inventory_count:2));*r=use_inventory?inventory[index]:wifi_selected_result;
 if((!use_inventory&&index==0)||missing_ap)r->bssid[5]=9;if(renamed_ap)snprintf(r->ssid,sizeof(r->ssid),"Renamed AP");return link_error;}
esp_err_t c5_spi_link_test_status(uint16_t id,cerberus_radio_test_status_t *s)
{if(id)++leases;*s=remote;return link_error;}
esp_err_t c5_spi_link_ap_validate(const cerberus_wifi_ap_job_t *r)
{
    assert(cerberus_wifi_ap_job_valid(r));++validation_calls;
    if(r->mode==CERBERUS_AP_CLONE_SPAM){
        assert(clone_validation_count<32);
        memcpy(clone_validation_macs[clone_validation_count++],r->station,6);
    }
    if(validation_calls==change_after_validation)change_generation=true;
    return r->index==validation_reject?ESP_ERR_INVALID_STATE:link_error;
}
esp_err_t c5_spi_link_ap_stage(const cerberus_wifi_ap_job_t *r,
    const cerberus_wifi_ap_station_set_t *set)
{
    assert(cerberus_wifi_ap_station_set_valid(r,set));
    assert(stages<32);staged_sets[stages]=*set;staged_bssid[stages]=r->bssid[5];++stages;
    return stage_error?stage_error:link_error;
}
esp_err_t c5_spi_link_ap_start(const cerberus_wifi_ap_job_t *r,cerberus_radio_test_status_t *s)
{
    ++starts;assert(cerberus_wifi_ap_job_valid(r)&&r->generation==4&&!memcmp(r->bssid,wifi_selected_result.bssid,6));
    if(use_inventory){assert(r->index<inventory_count&&!memcmp(r->bssid,inventory[r->index].bssid,6));start_log[start_log_count++]=r->bssid[5];}
    else assert(r->index==1);
    if(dropped_start)return ESP_FAIL;
    if(start_rejected){*s=(cerberus_radio_test_status_t){.job_id=r->job_id,.state=CERBERUS_TEST_ERROR,.mode=CERBERUS_TEST_WIFI_AP,.error_code=ESP_ERR_INVALID_STATE};return ESP_ERR_INVALID_STATE;}
    remote=(cerberus_radio_test_status_t){.job_id=r->job_id,.state=CERBERUS_TEST_RUNNING,.mode=CERBERUS_TEST_WIFI_AP,.channel=r->channel,.seconds=r->seconds,.counts_valid=1};
    if(lost_ack)return ESP_FAIL;*s=remote;return ESP_OK;
}
esp_err_t c5_spi_link_test_stop(uint16_t id,cerberus_radio_test_status_t *s)
{++stops;if(link_error)return link_error;if(id==remote.job_id)remote.state=CERBERUS_TEST_CANCELLED;*s=remote;return ESP_OK;}
static esp_err_t cerberus_storage_write_wifi_report(const char *data,size_t n,char *name,size_t cap)
{assert(n<sizeof(csv_saved)&&n<=CERBERUS_STORAGE_WIFI_REPORT_MAX_BYTES);if(save_error)return save_error;memcpy(csv_saved,data,n);csv_saved[n]=0;snprintf(name,cap,"WIFI0003.CSV");++saves;return ESP_OK;}
#include "wifi_ap_ui.h"
static void finish_preflight(unsigned *tick)
{
    for(unsigned i=0;i<2048&&ap_batch_preflighting;++i)ap_poll((*tick)+=250,true);
    assert(!ap_batch_preflighting);
}
static void save_image(const char *prefix,const char *suffix)
{
    assert(cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND)==ESP_OK);assert(ap_draw()==ESP_OK);
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,suffix);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv)
{
    assert(argc==2);ap_enter();assert(!starts&&!ap_job.job_id);
    save_image(argv[1],"broadcast");
    assert(screen[258U*320U+94U]==UI_TEXT); /* Drawn infinity glyph. */
    ap_tap(20,170);assert(ap_scope==0);ap_tap(20,150);assert(ap_scope==WIFI_TARGET_ALL_STATIONS);
    ap_tap(20,150);assert(ap_scope==WIFI_TARGET_PICK_STATIONS);
    ap_start();assert(!starts&&strstr(ap_message,"STATION"));
    ap_tap(20,210);assert(ap_station_selecting&&wifi_mode==UI_WIFI_MODE_STATIONS&&station_entries==1);
    station_selected=(cerberus_wifi_station_result_t){.mac={2,3,4,5,6,7},.channel=36};
    station_selected_valid=true;ap_enter();assert(wifi_targets[0].station_valid&&ap_scope==1);save_image(argv[1],"station");
    connection_state=CERBERUS_WIFI_CONNECTION_CONNECTED;ap_start();assert(!starts&&strstr(ap_message,"DISCONNECT"));connection_state=CERBERUS_WIFI_CONNECTION_IDLE;
    missing_ap=true;ap_start();assert(!starts&&strstr(ap_message,"TARGET CHANGED"));missing_ap=false;
    c5_status.capabilities=0;ap_start();assert(!starts);c5_status.capabilities=CERBERUS_C5_CAPABILITIES;
    station_selected_ap[5]=8;ap_enter();assert(wifi_targets[0].station_valid);station_selected_ap[5]=1;
    unsigned before_reads=record_reads;
    ap_tap(160,380);assert(!starts);ap_tap(20,380);assert(starts==1&&ap_busy()&&ap_job.scope==CERBERUS_AP_ONE_STATION);
    assert(record_reads-before_reads==2&&wifi_selected_index==1);
    ap_tap(20,150);ap_tap(20,260);ap_tap(20,210);assert(ap_scope==1&&ap_job.seconds==0&&station_entries==1);
    ap_save();assert(!saves);ap_poll(250,true);save_image(argv[1],"running");
    remote.state=CERBERUS_TEST_COMPLETE;remote.elapsed_ms=10000;remote.accepted_frames=200;
    ap_poll(500,true);ap_save();assert(saves==1&&strstr(csv_saved,"driver_accepted_frames")&&strstr(csv_saved,"one_station"));save_image(argv[1],"complete");
    save_error=ESP_FAIL;ap_save();assert(ap_save_error==ESP_FAIL);save_error=0;ap_save();assert(ap_save_error==ESP_OK);
    uint16_t retained_job=ap_job.job_id;
    ap_tap(20,260);assert(ap_job.job_id==retained_job&&ap_job.seconds==0);
    before_reads=record_reads;ap_start();assert(starts==2&&record_reads-before_reads==1);
    unsigned old_leases=leases;link_error=ESP_FAIL;ap_poll(750,false);assert(stops==1&&leases==old_leases&&ap_busy());link_error=0;ap_poll(1000,false);assert(!ap_busy());
    lost_ack=true;ap_start();assert(ap_pending);lost_ack=false;ap_poll(1250,true);assert(!ap_pending&&ap_busy());ap_stop();assert(!ap_busy());
    start_rejected=true;ap_start();assert(!ap_pending&&ap_status.state==CERBERUS_TEST_ERROR);start_rejected=false;
    ap_start();++remote.job_id;ap_poll(1500,true);assert(!ap_busy()&&ap_error==ESP_ERR_INVALID_STATE);
    unsigned old_starts=starts;ap_start();assert(starts==old_starts&&strstr(ap_message,"BUSY"));remote.state=CERBERUS_TEST_COMPLETE;
    ap_start();assert(ap_busy());wifi_selected_result.bssid[5]=8;ap_enter();assert(ap_bound_bssid[5]==1); /* Running context cannot follow a changed selection. */
    ap_stop();ap_enter();assert(ap_bound_bssid[5]==8&&!ap_job.job_id&&ap_scope==CERBERUS_AP_BROADCAST);
    wifi_selected_result.channel=52;ap_enter();ap_start();assert(strstr(ap_message,"CHANNEL"));
    wifi_selected_result.channel=6;station_selected_valid=false;ap_enter();
    old_starts=starts;ap_start();assert(ap_wait_rx&&rx_starts==1&&starts==old_starts);
    ap_poll(1750,true);assert(ap_wait_rx&&starts==old_starts);save_image(argv[1],"rx-starting");
    unsigned old_rx_leases=rx_leases;ap_poll(2000,false);assert(!ap_busy()&&rx_stops==1&&starts==old_starts&&old_rx_leases==rx_leases);
    ap_start();local_rx.state=CERBERUS_TEST_RUNNING;ap_poll(2250,true);assert(starts==old_starts+1&&!ap_wait_rx&&ap_busy());
    local_rx.matching_frames=12;local_rx.target_beacons=49;local_rx.last_match_rssi=-42;
    ap_poll(2500,true);save_image(argv[1],"dual-radio");
    remote.state=CERBERUS_TEST_COMPLETE;remote.accepted_frames=200;ap_poll(2750,true);ap_poll(3000,true);
    assert(!ap_busy());ap_save();assert(strstr(csv_saved,"s3_matching_frames")&&strstr(csv_saved,",49,12,-42,"));
    ap_start();local_rx.state=CERBERUS_TEST_ERROR;local_rx.error_code=ESP_FAIL;local_rx.reserved=false;
    ap_poll(3250,true);assert(!ap_busy()&&starts==old_starts+1&&ap_error==ESP_FAIL);
    ap_start();local_rx.state=CERBERUS_TEST_RUNNING;ap_poll(3500,true);assert(ap_c5_busy());
    local_rx.state=CERBERUS_TEST_ERROR;local_rx.reserved=false;ap_poll(3750,true);assert(!ap_busy()&&remote.state==CERBERUS_TEST_CANCELLED);
    rx_start_error=ESP_ERR_INVALID_STATE;old_starts=starts;ap_start();assert(!ap_busy()&&starts==old_starts&&ap_status.state==CERBERUS_TEST_ERROR);
    wifi_selected_result.channel=36;ap_enter();dropped_start=true;ap_start();assert(ap_pending);ap_poll(4000,false);
    assert(!ap_busy()&&!ap_pending&&ap_status.state==CERBERUS_TEST_ERROR);dropped_start=false;
    ap_start();++remote.job_id;ap_poll(4250,false);assert(!ap_busy()&&remote.state==CERBERUS_TEST_RUNNING);
    remote.state=CERBERUS_TEST_COMPLETE;ap_choose(CERBERUS_AP_PROBE_REQUEST);ap_enter();
    unsigned entries=station_entries;ap_tap(20,150);ap_tap(20,210);assert(ap_scope==0&&station_entries==entries);
    c5_status.capabilities&=~CERBERUS_C5_CAP_AP_PROBE;old_starts=starts;ap_start();assert(starts==old_starts);
    c5_status.capabilities|=CERBERUS_C5_CAP_AP_PROBE;
    renamed_ap=true;ap_start();assert(starts==old_starts&&strstr(ap_message,"SSID CHANGED"));renamed_ap=false;
    ap_start();assert(ap_job.mode==CERBERUS_AP_PROBE_REQUEST&&ap_busy());save_image(argv[1],"probe");
    ap_choose(CERBERUS_AP_DEAUTH);assert(ap_mode==CERBERUS_AP_PROBE_REQUEST);ap_stop();ap_save();assert(strstr(csv_saved,"directed_probe")&&strstr(csv_saved,"ap_directed"));
    ap_choose(CERBERUS_AP_DEAUTH);assert(ap_mode==CERBERUS_AP_DEAUTH&&!ap_job.job_id);
    for(unsigned mode=3;mode<=6;++mode){
        ap_choose(mode);ap_enter();unsigned old=starts;
        assert(ap_scope==(cerberus_wifi_ap_needs_station(mode)?1:0));
        c5_status.capabilities&=~ap_capability(mode);ap_start();assert(starts==old);
        c5_status.capabilities|=ap_capability(mode);
        if(cerberus_wifi_ap_needs_station(mode)){
            station_selected_valid=false;ap_start();assert(starts==old&&strstr(ap_message,"STATION"));
            ap_tap(20,210);assert(ap_station_selecting);station_selected_valid=true;
            memcpy(station_selected_ap,wifi_selected_result.bssid,6);station_selected.channel=wifi_selected_result.channel;
        }
        renamed_ap=true;ap_start();assert(starts==old&&strstr(ap_message,"SSID CHANGED"));renamed_ap=false;
        ap_start();assert(starts==old+1&&ap_job.mode==mode&&ap_busy());save_image(argv[1],ap_report_mode(mode));
        ap_stop();ap_save();assert(strstr(csv_saved,ap_report_mode(mode))&&!ap_busy());
    }
    ap_choose(CERBERUS_AP_SAE_COMMIT);ap_enter();old_starts=starts;
    c5_status.capabilities&=~CERBERUS_C5_CAP_AP_SAE_COMMIT;
    ap_start();assert(starts==old_starts);
    c5_status.capabilities|=CERBERUS_C5_CAP_AP_SAE_COMMIT;
    ap_start();assert(starts==old_starts&&strstr(ap_message,"REVISION 44"));
    c5_status.firmware_version=44;
    ap_start();assert(starts==old_starts+1&&ap_job.mode==CERBERUS_AP_SAE_COMMIT&&
        ap_job.scope==CERBERUS_AP_BROADCAST&&ap_busy());
    save_image(argv[1],"sae-commit");ap_stop();ap_save();
    assert(strstr(csv_saved,"sae_commit")&&strstr(csv_saved,"ap_auth_commit"));
    ap_choose(CERBERUS_AP_DEAUTH);c5_status.firmware_version=42;
    masked=true;save_image(argv[1],"masked");masked=false;
    use_inventory=true;rx_start_error=0;ap_choose(CERBERUS_AP_PROBE_REQUEST);ap_clear_result();wifi_targets_clear();
    for(unsigned i=0;i<3;++i){inventory[i]=(cerberus_c5_wifi_result_t){.ssid="Same SSID / distinct AP",.bssid={2,0,0,0,0,i+1},.channel=i==1?36:6};assert(wifi_targets_add(&inventory[i],i));}
    ap_focus_target(&wifi_targets[0]);ap_enter();save_image(argv[1],"batch-setup");
    unsigned batch_tick=5000,batch_starts=starts,reads=record_reads;
    ap_start();assert(ap_batch_active&&ap_batch_preflighting&&starts==batch_starts);
    ap_poll(batch_tick+=250,true);assert(ap_batch_preflighting&&validation_calls==0&&starts==batch_starts);
    finish_preflight(&batch_tick);
    assert(ap_batch_active&&ap_wait_rx&&starts==batch_starts&&validation_calls==3&&record_reads==reads+4);
    wifi_targets[1].ap.bssid[5]=99; /* Frozen queue cannot be redirected by later selection changes. */
    for(unsigned i=0;i<24&&ap_batch_completed<5;++i){
        if(ap_wait_rx)local_rx.state=CERBERUS_TEST_RUNNING;
        if(ap_c5_busy()){remote.state=CERBERUS_TEST_COMPLETE;remote.accepted_frames=200;}
        ap_poll(batch_tick+=250,true);
    }
    assert(ap_batch_active&&ap_batch_completed==5&&starts==batch_starts+5);
    assert(start_log_count==5&&!memcmp(start_log,(uint8_t[]){1,2,3,1,2},5)&&
        ap_batch[0].visits==2&&ap_batch[1].visits==2&&ap_batch[2].visits==1&&
        ap_batch[0].total_driver_frames==400&&ap_batch[1].total_driver_frames==400&&
        ap_batch[2].total_driver_frames==200&&ap_batch[0].job.seconds==AP_BATCH_DWELL_SECONDS&&
        record_reads==reads+9);
    ap_poll(batch_tick+=250,false); /* Leaving the AP page stops an endless round. */
    assert(!ap_busy()&&ap_batch_state==CERBERUS_TEST_CANCELLED);
    unsigned stopped_starts=starts;ap_poll(batch_tick+=250,true);
    assert(starts==stopped_starts&&!ap_batch_active);
    wifi_targets[1].ap.bssid[5]=2;ap_save();
    assert(strstr(csv_saved,"targets=3; completed=5")&&strstr(csv_saved,"02:00:00:00:00:03")&&
        strstr(csv_saved,"total_c5_frames"));save_image(argv[1],"batch-complete");
    /* Every target is preflighted before either processor starts RF. */
    batch_starts=starts;unsigned receivers=rx_starts;inventory[1].ssid[0]='X';ap_start();finish_preflight(&batch_tick);
    assert(!ap_busy()&&ap_batch_state==CERBERUS_TEST_ERROR&&starts==batch_starts&&rx_starts==receivers&&strstr(ap_message,"NETWORK 2"));inventory[1].ssid[0]='S';
    validation_reject=1;ap_start();finish_preflight(&batch_tick);assert(!ap_busy()&&starts==batch_starts);validation_reject=99;
    change_after_validation=validation_calls+3;ap_start();finish_preflight(&batch_tick);
    assert(!ap_busy()&&ap_error==ESP_ERR_INVALID_STATE&&starts==batch_starts&&rx_starts==receivers);
    change_after_validation=0;change_generation=false;
    c5_status.firmware_version=40;ap_start();assert(!ap_busy()&&ap_error==ESP_ERR_NOT_SUPPORTED&&starts==batch_starts);c5_status.firmware_version=42;
    ap_start();assert(ap_batch_preflighting&&ap_batch_active);ap_poll(batch_tick+=250,false);
    assert(!ap_busy()&&ap_batch_state==CERBERUS_TEST_CANCELLED&&starts==batch_starts);ap_save();assert(strstr(csv_saved,"completed=0"));
    /* Cancellation after TX cannot advance to the second network. */
    ap_start();finish_preflight(&batch_tick);local_rx.state=CERBERUS_TEST_RUNNING;ap_poll(batch_tick+=250,true);assert(starts==batch_starts+1);
    ap_stop();ap_poll(batch_tick+=250,true);assert(!ap_busy()&&ap_batch_completed==0&&ap_batch_state==CERBERUS_TEST_CANCELLED);
    assert(!ap_batch[1].attempted&&!ap_batch[2].attempted);
    /* Station scope needs a separately captured choice for every AP. */
    ap_choose(CERBERUS_AP_BAD_MESSAGE);ap_enter();batch_starts=starts;ap_start();finish_preflight(&batch_tick);assert(!ap_busy()&&starts==batch_starts);
    for(unsigned i=0;i<3;++i){
        cerberus_wifi_station_result_t client={.channel=inventory[i].channel,.mac={2,9,8,7,6,i+1}};
        assert(wifi_targets_station(inventory[i].bssid,&client,true));
    }
    ap_enter();ap_start();finish_preflight(&batch_tick);assert(ap_wait_rx&&ap_batch_active);
    for(unsigned i=0;i<20&&ap_batch_completed<3;++i){
        if(ap_wait_rx)local_rx.state=CERBERUS_TEST_RUNNING;
        if(ap_c5_busy())remote.state=CERBERUS_TEST_COMPLETE;
        ap_poll(batch_tick+=250,true);
    }
    assert(ap_batch_active&&ap_batch_completed==3);ap_stop();ap_poll(batch_tick+=250,true);
    assert(!ap_busy()&&ap_batch_state==CERBERUS_TEST_CANCELLED);
    for(unsigned i=0;i<3;++i)assert(ap_batch[i].job.scope==1&&ap_batch[i].job.station[5]==i+1);
    ap_save();assert(strstr(csv_saved,"bad_message")&&strstr(csv_saved,"one_station"));
    /* Reordered inventories must follow identity, never the stale row index. */
    cerberus_c5_wifi_result_t swap=inventory[0];inventory[0]=inventory[2];inventory[2]=swap;
    ap_start();finish_preflight(&batch_tick);assert(ap_batch_active&&ap_batch[0].target.index==2&&ap_batch[2].target.index==0);
    batch_starts=starts;delay_rx_cleanup=true;local_rx.state=CERBERUS_TEST_RUNNING;
    ap_poll(batch_tick+=250,true);assert(starts==batch_starts+1);
    remote.state=CERBERUS_TEST_COMPLETE;ap_poll(batch_tick+=250,true);
    ap_poll(batch_tick+=250,true);assert(ap_batch_active&&ap_batch_index==0&&starts==batch_starts+1);
    delay_rx_cleanup=false;local_rx.reserved=false;local_rx.state=CERBERUS_TEST_CANCELLED;
    ap_poll(batch_tick+=250,true);assert(ap_batch_index==1&&starts==batch_starts+2);
    remote.state=CERBERUS_TEST_ERROR;remote.error_code=ESP_FAIL;ap_poll(batch_tick+=250,true);
    assert(!ap_busy()&&ap_batch_state==CERBERUS_TEST_ERROR&&ap_batch_completed==1&&!ap_batch[2].attempted);
    /* Maximum-size results include large counters and fit the storage bound. */
    ap_batch_release();wifi_targets_clear();inventory_count=32;remote=(cerberus_radio_test_status_t){0};
    for(unsigned i=0;i<inventory_count;++i){
        inventory[i]=(cerberus_c5_wifi_result_t){.ssid="Maximum batch",.bssid={2,0,0,0,0,i+1},.channel=6};
        assert(wifi_targets_add(&inventory[i],31-i));
        cerberus_wifi_station_result_t client={.channel=6,.mac={2,9,8,7,6,i+1}};
        assert(wifi_targets_station(inventory[i].bssid,&client,true));
    }
    ap_focus_target(&wifi_targets[0]);ap_enter();ap_start();finish_preflight(&batch_tick);
    for(unsigned i=0;i<128&&ap_batch_completed<32;++i){
        if(ap_wait_rx)local_rx.state=CERBERUS_TEST_RUNNING;
        if(ap_c5_busy()){
            remote.state=CERBERUS_TEST_COMPLETE;remote.elapsed_ms=UINT32_MAX;remote.accepted_frames=UINT32_MAX;
            local_rx.target_beacons=local_rx.matching_frames=UINT32_MAX;
        }
        ap_poll(batch_tick+=250,true);
    }
    assert(ap_batch_active&&ap_batch_completed==32);ap_stop();ap_poll(batch_tick+=250,true);
    assert(!ap_busy()&&ap_batch_state==CERBERUS_TEST_CANCELLED);ap_save();
    assert(strlen(csv_saved)>CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES&&strstr(csv_saved,"targets=32; completed=32"));
    /* The batch freezes a distinct scope and client set for every AP. */
    ap_batch_release();wifi_targets_clear();inventory_count=3;
    ap_choose(CERBERUS_AP_DEAUTH);ap_clear_result();
    for(unsigned i=0;i<3;++i){
        inventory[i]=(cerberus_c5_wifi_result_t){.ssid="Mixed profiles",.bssid={2,0,0,0,0,i+1},.channel=i==1?36:6};
        assert(wifi_targets_add(&inventory[i],i));
    }
    cerberus_wifi_station_result_t client_a[2]={
        {.mac={2,9,8,7,6,11},.channel=6},{.mac={2,9,8,7,6,12},.channel=6}};
    cerberus_wifi_station_result_t client_b={.mac={2,9,8,7,6,21},.channel=36};
    assert(wifi_targets_inventory(inventory[0].bssid,6,client_a,2));
    assert(wifi_targets_station(inventory[1].bssid,&client_b,true));
    wifi_targets_set_scope(&wifi_targets[0],CERBERUS_AP_DEAUTH,WIFI_TARGET_ALL_STATIONS);
    wifi_targets_set_scope(&wifi_targets[1],CERBERUS_AP_DEAUTH,WIFI_TARGET_PICK_STATIONS);
    ap_focus_target(&wifi_targets[0]);ap_enter();
    unsigned validated=validation_calls,old_stages=stages,old_list_rx=rx_list_starts;
    batch_starts=starts;ap_start();assert(ap_batch_preflighting&&starts==batch_starts);
    wifi_targets[0].observed[0][5]=99; /* Live edits cannot change the frozen job. */
    finish_preflight(&batch_tick);
    assert(ap_batch_active&&ap_wait_rx&&starts==batch_starts&&stages==old_stages&&
        validation_calls==validated+4&&rx_list_starts==old_list_rx+1);
    for(unsigned i=0;i<24&&ap_batch_completed<3;++i){
        if(ap_wait_rx)local_rx.state=CERBERUS_TEST_RUNNING;
        if(ap_c5_busy())remote.state=CERBERUS_TEST_COMPLETE;
        ap_poll(batch_tick+=250,true);
    }
    assert(ap_batch_active&&ap_batch_completed==3&&starts==batch_starts+3&&stages==old_stages+1);
    ap_stop();ap_poll(batch_tick+=250,true);assert(!ap_busy());
    assert(ap_batch[0].job.scope==CERBERUS_AP_STATION_LIST&&
        ap_batch[1].job.scope==CERBERUS_AP_ONE_STATION&&ap_batch[2].job.scope==CERBERUS_AP_BROADCAST);
    assert(staged_bssid[old_stages]==1&&staged_sets[old_stages].count==2&&
        staged_sets[old_stages].mac[0][5]==11&&staged_sets[old_stages].mac[1][5]==12);
    ap_save();assert(strstr(csv_saved,"station_list,all_observed,2")&&
        strstr(csv_saved,"one_station,picked,1")&&strstr(csv_saved,"broadcast,broadcast,0")&&
        strstr(csv_saved,"1,1,02:09:08:07:06:0B")&&strstr(csv_saved,"1,2,02:09:08:07:06:0C"));
    /* Stop after the final read-only check but before first RF. */
    batch_starts=starts;ap_start();
    for(unsigned i=0;i<200&&!ap_preflight_ready;++i)ap_poll(batch_tick+=250,true);
    assert(ap_preflight_ready&&starts==batch_starts);ap_stop();ap_poll(batch_tick+=250,true);
    assert(!ap_busy()&&ap_batch_state==CERBERUS_TEST_CANCELLED&&starts==batch_starts);
    /* A station-list stage failure stops the S3 receiver before C5 starts. */
    ap_batch_release();wifi_targets_clear();assert(wifi_targets_add(&inventory[0],0));
    assert(wifi_targets_inventory(inventory[0].bssid,6,client_a,2));
    wifi_targets_set_scope(&wifi_targets[0],CERBERUS_AP_BAD_MESSAGE,WIFI_TARGET_ALL_STATIONS);
    ap_focus_target(&wifi_targets[0]);ap_choose(CERBERUS_AP_BAD_MESSAGE);ap_enter();
    stage_error=ESP_FAIL;batch_starts=starts;ap_start();assert(ap_wait_rx);
    local_rx.state=CERBERUS_TEST_RUNNING;ap_poll(batch_tick+=250,true);
    assert(starts==batch_starts&&ap_status.state==CERBERUS_TEST_ERROR&&!local_rx.reserved);
    stage_error=ESP_OK;ap_poll(batch_tick+=250,true);ap_start();assert(ap_wait_rx);local_rx.state=CERBERUS_TEST_RUNNING;
    ap_poll(batch_tick+=250,true);assert(starts==batch_starts+1&&ap_job.scope==CERBERUS_AP_STATION_LIST);
    ap_stop();ap_choose(CERBERUS_AP_ASSOC_SLEEP);
    wifi_targets_set_scope(&wifi_targets[0],CERBERUS_AP_ASSOC_SLEEP,WIFI_TARGET_ALL_STATIONS);
    ap_enter();ap_start();assert(ap_wait_rx);local_rx.state=CERBERUS_TEST_RUNNING;
    ap_poll(batch_tick+=250,true);assert(ap_job.scope==CERBERUS_AP_STATION_LIST&&starts==batch_starts+2);
    ap_stop();
    ap_batch_release();wifi_targets_clear();
    /* Clone batches freeze a unique local BSSID for each target before RF. */
    ap_choose(CERBERUS_AP_CLONE_SPAM);c5_status.firmware_version=43;
    for(unsigned i=0;i<3;++i){
        inventory[i]=(cerberus_c5_wifi_result_t){.ssid="Clone batch AP",.bssid={2,0,0,0,0,i+1},.channel=i==1?36:6};
        assert(wifi_targets_add(&inventory[i],i));
    }
    ap_focus_target(&wifi_targets[0]);ap_enter();
    duplicate_random=true;batch_starts=starts;ap_start();
    assert(!ap_busy()&&ap_error==ESP_ERR_INVALID_STATE&&starts==batch_starts&&
        strstr(ap_message,"UNIQUE CLONE IDENTITY"));
    duplicate_random=false;
    unsigned old_random=random_calls,old_validations=validation_calls,old_clone_rx=clone_rx_starts;
    unsigned old_clone_validations=clone_validation_count;
    ap_start();assert(ap_batch_active&&ap_batch_preflighting&&random_calls==old_random+3);
    uint8_t frozen_clones[3][6];
    for(unsigned i=0;i<3;++i){
        memcpy(frozen_clones[i],ap_batch[i].target.clone_mac,6);
        assert(ap_clone_identity_valid(frozen_clones[i],ap_batch[i].target.ap.bssid));
        for(unsigned j=0;j<3;++j)assert(memcmp(frozen_clones[i],inventory[j].bssid,6));
        for(unsigned j=0;j<i;++j)assert(memcmp(frozen_clones[i],frozen_clones[j],6));
    }
    memcpy(wifi_targets[0].clone_mac,(uint8_t[]){2,3,4,5,6,7},6);
    finish_preflight(&batch_tick);
    assert(validation_calls==old_validations+3&&clone_validation_count==old_clone_validations+3&&
        ap_batch_active&&ap_wait_rx);
    for(unsigned i=0;i<3;++i)
        assert(!memcmp(clone_validation_macs[old_clone_validations+i],frozen_clones[i],6));
    for(unsigned i=0;i<20&&ap_batch_completed<3;++i){
        if(ap_wait_rx)local_rx.state=CERBERUS_TEST_RUNNING;
        if(ap_c5_busy())remote.state=CERBERUS_TEST_COMPLETE;
        ap_poll(batch_tick+=250,true);
    }
    assert(ap_batch_active&&ap_batch_completed==3&&starts==batch_starts+3&&
        random_calls==old_random+3&&clone_rx_starts==old_clone_rx+3);
    ap_stop();ap_poll(batch_tick+=250,true);assert(!ap_busy());
    for(unsigned i=0;i<3;++i)assert(ap_batch[i].job.mode==CERBERUS_AP_CLONE_SPAM&&
        !memcmp(ap_batch[i].job.station,frozen_clones[i],6));
    assert(!memcmp(clone_rx_macs[old_clone_rx],frozen_clones[0],6)&&
        !memcmp(clone_rx_macs[old_clone_rx+1],frozen_clones[2],6));
    ap_save();assert(strstr(csv_saved,"station_or_clone_bssid")&&strstr(csv_saved,"ssid_clone"));
    for(unsigned i=0;i<3;++i){
        char mac[18];const uint8_t *p=frozen_clones[i];
        snprintf(mac,sizeof(mac),"%02X:%02X:%02X:%02X:%02X:%02X",p[0],p[1],p[2],p[3],p[4],p[5]);
        assert(strstr(csv_saved,mac));
    }
    ap_batch_release();wifi_targets_clear();
    puts("AP UI passed: drawn infinity/no duration control, endless 32-AP round robin with bounded dwell and totals, frozen scopes, preflight, Stop/leave, reports and S3 readiness.");
}
