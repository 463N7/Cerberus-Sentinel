#include "station_framebuffer.h"
#include "c5_spi_link.h"
#include "wifi_targets.h"
#include "ui_widgets.h"
#define UI_BACKGROUND 0x0042
#define UI_PANEL 0x08A4
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define UI_GOOD 0x471B
#define TAG "station-test"
static const cerberus_ui_widget_theme_t theme={UI_BACKGROUND,UI_PANEL,UI_MUTED,UI_TEXT,UI_MUTED};
#define UI_WIDGET_THEME (&theme)
static cerberus_c5_status_t c5_status={.capabilities=CERBERUS_C5_CAPABILITIES};
static cerberus_c5_wifi_result_t wifi_selected_result={.ssid="Cerberus Test AP",.bssid={2,0,0,0,0,1},.channel=36};
static uint8_t wifi_selected_index;
static bool wifi_selection_valid=true,masked,missing_ap,lost_ack,start_rejected;
static uint8_t connection_state=CERBERUS_WIFI_CONNECTION_IDLE;
static unsigned starts,stops,polls,leases,saves,result_reads;
static esp_err_t link_error,save_error;
static cerberus_wifi_station_status_t remote;
static char csv_saved[4096];
static const char *esp_err_to_name(int error){return error?"ESP_ERR_INVALID_STATE":"ESP_OK";}
static esp_err_t draw_centered(uint16_t y,const char *s,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320-strlen(s)*6*scale)/2),y,s,color,UI_BACKGROUND,scale);}
static esp_err_t draw_small_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *s)
{return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){x,y,w,h},s,1,true,&theme);}
static bool privacy_addresses_masked(void){return masked;}
static void cerberus_privacy_format_wifi_identifier(const uint8_t *mac,bool hide,char *out,size_t size)
{if(hide)snprintf(out,size,"XX:XX:XX:XX:%02X:%02X",mac[4],mac[5]);else snprintf(out,size,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);}
esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *s)
{*s=(cerberus_wifi_connection_status_payload_t){.state=connection_state};return link_error;}
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *s)
{*s=(cerberus_wifi_scan_status_payload_t){.state=CERBERUS_WIFI_SCAN_COMPLETE,.generation=4,.stored_count=2};return link_error;}
esp_err_t c5_spi_link_get_wifi_scan_result(uint8_t generation,uint8_t index,cerberus_c5_wifi_result_t *r)
{assert(generation==4&&index<2);*r=wifi_selected_result;if(index==0||missing_ap)r->bssid[5]=9;return link_error;}
esp_err_t c5_spi_link_station_status(uint16_t id,cerberus_wifi_station_status_t *s)
{++polls;if(id)++leases;*s=remote;return link_error;}
esp_err_t c5_spi_link_station_start(const cerberus_wifi_station_request_t *r,cerberus_wifi_station_status_t *s)
{
    ++starts;assert(r->generation==4&&r->index==1&&!memcmp(r->bssid,wifi_selected_result.bssid,6));
    if(start_rejected){*s=(cerberus_wifi_station_status_t){.job_id=r->job_id,.state=CERBERUS_STATION_ERROR,.error_code=ESP_ERR_INVALID_STATE};return ESP_ERR_INVALID_STATE;}
    remote=(cerberus_wifi_station_status_t){.job_id=r->job_id,.state=CERBERUS_STATION_RUNNING};
    if(lost_ack)return ESP_FAIL;*s=remote;return ESP_OK;
}
esp_err_t c5_spi_link_station_stop(uint16_t id,cerberus_wifi_station_status_t *s)
{++stops;assert(id==remote.job_id);if(link_error)return link_error;remote.state=CERBERUS_STATION_CANCELLED;*s=remote;return ESP_OK;}
esp_err_t c5_spi_link_station_result(uint16_t id,uint8_t index,cerberus_wifi_station_result_t *r)
{
    assert(id==remote.job_id&&index<remote.count);++result_reads;
    *r=(cerberus_wifi_station_result_t){.job_id=id,.index=index,.mac={2,0x12,0x34,0xab,0xcd,index},.channel=36,.rssi=-61,
        .flags=CERBERUS_STATION_DATA|(index?CERBERUS_STATION_TX:CERBERUS_STATION_AP_TX),.frames=123,.last_seen_ms=14990,
        .from_station=index?123:0,.to_station=index?0:123};
    assert(cerberus_wifi_station_result_valid(r,id,index));return link_error;
}
static esp_err_t cerberus_storage_write_wifi_report(const char *data,size_t n,char *name,size_t cap)
{assert(n<sizeof(csv_saved));if(save_error)return save_error;memcpy(csv_saved,data,n);csv_saved[n]=0;snprintf(name,cap,"WIFI0002.CSV");++saves;return ESP_OK;}
#include "wifi_station_ui.h"
static void save_image(const char *prefix,const char *suffix)
{
    assert(cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND)==ESP_OK);assert(station_draw()==ESP_OK);
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,suffix);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv)
{
    assert(argc==2);station_enter();assert(!starts&&!station_job.job_id);save_image(argv[1],"ready");
    connection_state=CERBERUS_WIFI_CONNECTION_CONNECTED;station_start();assert(!starts&&strstr(station_message,"DISCONNECT"));connection_state=CERBERUS_WIFI_CONNECTION_IDLE;
    missing_ap=true;station_start();assert(!starts&&strstr(station_message,"TARGET CHANGED"));missing_ap=false;
    c5_status.capabilities=0;station_start();assert(!starts);c5_status.capabilities=CERBERUS_C5_CAPABILITIES;
    station_tap(88,390);assert(!starts); /* Gap. */
    station_tap(20,390);assert(starts==1&&station_busy());
    station_tap(110,390);assert(station_seconds==15); /* Duration locked while capturing. */
    station_poll(500,true);assert(!result_reads);station_save();assert(!saves);save_image(argv[1],"listening");
    remote.state=CERBERUS_STATION_COMPLETE;remote.elapsed_ms=15000;remote.count=5;remote.observed_frames=900;remote.matched_frames=615;
    station_poll(1000,true);assert(station_loaded==4&&!station_busy());station_save();assert(!saves);
    station_poll(1500,true);assert(station_loaded==5&&wifi_target_count==1&&wifi_targets[0].observed_count==5);
    station_save();assert(saves==1&&strstr(csv_saved,"signal_source")&&strstr(csv_saved,"-61,ap_only,123,0,123"));
    station_tap(20,185);assert(!station_selected_valid); /* Row gap. */
    station_tap(20,160);assert(station_selected_valid&&!station_selected.from_station);save_image(argv[1],"ap-only");
    station_tap(20,160);assert(!station_selected_valid);station_tap(20,202);
    assert(station_selected_valid&&station_selected.from_station&&wifi_targets_selected_count(&wifi_targets[0])==1);
    save_image(argv[1],"selected");
    station_tap(190,390);assert(station_page==1);station_tap(20,160);
    assert(station_selected.index==4&&wifi_targets_selected_count(&wifi_targets[0])==2);
    station_tap(190,390);assert(!station_page);
    station_tap(20,335);assert(wifi_targets_selected_count(&wifi_targets[0])==5);
    station_tap(200,335);assert(!station_selected_valid&&!wifi_targets_selected_count(&wifi_targets[0]));
    station_tap(190,390);station_tap(20,160);assert(station_selected_valid&&station_selected.index==4);
    station_tap(190,390);
    save_error=ESP_FAIL;station_save();assert(station_save_error==ESP_FAIL);save_error=0;station_save();assert(station_save_error==ESP_OK);
    masked=true;save_image(argv[1],"masked");masked=false;
    station_tap(110,390);assert(station_seconds==30);station_start();assert(starts==2);
    unsigned before_polls=polls,before_leases=leases;link_error=ESP_FAIL;station_poll(2000,false);
    assert(stops==1&&polls==before_polls&&leases==before_leases&&station_busy());link_error=0;station_poll(2500,false);assert(!station_busy());
    lost_ack=true;station_start();assert(starts==3&&station_pending);lost_ack=false;station_poll(3000,true);assert(!station_pending&&station_busy());station_cancel();assert(!station_busy());
    start_rejected=true;station_start();assert(starts==4&&!station_pending&&station_status.state==CERBERUS_STATION_ERROR);start_rejected=false;
    station_start();remote.state=CERBERUS_STATION_COMPLETE;remote.count=32;remote.matched_frames=remote.observed_frames=UINT32_MAX;
    for(unsigned i=0;i<8;++i)station_poll(4000+i*500,true);
    assert(station_loaded==32&&wifi_targets[0].observed_count==32&&
        wifi_targets_selected_count(&wifi_targets[0])==1&&station_selected_valid);
    station_save();assert(station_save_error==ESP_OK);
    station_tap(20,160);assert(station_selected_valid&&wifi_targets_selected_count(&wifi_targets[0])==2);
    wifi_selected_result.bssid[5]=8;station_enter();assert(!station_selected_valid&&!station_loaded&&!station_job.job_id&&
        wifi_target_count==2&&wifi_targets_selected_count(&wifi_targets[0])==2);
    station_start();++remote.job_id;station_poll(9000,true);assert(station_error==ESP_ERR_INVALID_STATE&&!station_busy()&&!station_loaded);
    puts("Station UI passed: 32-client inventory, multi-pick, Pick All, Clear, per-AP retention, recapture, CSV, stale jobs and hidden-screen cancellation.");
}
