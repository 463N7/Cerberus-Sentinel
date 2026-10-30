#ifdef NDEBUG
#undef NDEBUG
#endif
#include "target_framebuffer.h"
#include "wifi_target_view.h"
#include "c5_spi_link.h"
#include "wifi_packet_monitor_core.h"
#include "csi_session.h"
#include <stdlib.h>
static bool fail_allocation;
static void *target_allocate(size_t size){return fail_allocation?NULL:calloc(1,size);}
#define CERBERUS_UI_JOB_ALLOC(n) target_allocate(n)
#include "wifi_targets.h"
#include "field_menu.h"
#include "keyboard.h"
static const cerberus_ui_widget_theme_t theme={0x0042,0x08A4,0x3F1B,0xE77F,0x7CB5};
#define UI_WIDGET_THEME (&theme)
enum {UI_WIFI_MODE_LIST,UI_WIFI_MODE_TARGET,UI_WIFI_MODE_CONNECTION,UI_WIFI_MODE_PASSWORD,
    UI_WIFI_MODE_NETWORK_SCAN,UI_WIFI_MODE_STATIONS,UI_WIFI_MODE_DETAIL,UI_WIFI_MODE_CAPTURE,
    UI_WIFI_MODE_PACKET_MONITOR,UI_WIFI_MODE_CSI,UI_WIFI_MODE_OBSERVED_CLIENTS,UI_PAGE_FIELD_MENU};
static uint8_t wifi_selected_index;
static unsigned wifi_mode=UI_WIFI_MODE_TARGET,current_page;
static cerberus_c5_wifi_result_t wifi_selected_result,wifi_connection_target;
static bool wifi_selection_valid,wifi_connection_target_valid,c5_online=true,ready=true;
static cerberus_c5_status_t c5_status={.capabilities=CERBERUS_C5_CAP_NETWORK_SCAN};
static cerberus_wifi_scan_status_payload_t wifi_scan_status={.state=CERBERUS_WIFI_SCAN_COMPLETE};
static cerberus_wifi_connection_status_payload_t wifi_connection_status,remote;
static esp_err_t wifi_connection_error,link_error;
static cerberus_field_menu_t field_menu;
static cerberus_keyboard_t wifi_keyboard;
static char wifi_connection_password[64];
static unsigned connects,passwords,net_entries,station_entries,capture_entries;
static uint8_t packet_monitor_channel;
static bool packet_monitor_from_target,packet_monitor_attempted;
static uint32_t packet_monitor_last_poll;
static unsigned monitor_entries;
static unsigned csi_entries;
static unsigned relation_entries;
static esp_err_t csi_open_error;
static const char *esp_err_to_name(esp_err_t error){return error==ESP_ERR_NO_MEM?"ESP_ERR_NO_MEM":"ERROR";}
static esp_err_t csi_open_selected(void)
{++csi_entries;if(csi_open_error)return csi_open_error;wifi_mode=UI_WIFI_MODE_CSI;return ESP_OK;}
static void packet_monitor_start_ui(void){++monitor_entries;}
static void cerberus_privacy_format_wifi_identifier(const uint8_t *mac,bool masked,char *out,size_t cap)
{(void)masked;snprintf(out,cap,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);}
static bool privacy_addresses_masked(void){return false;}
static const char *wifi_security_text(uint8_t security){return security?"WPA2":"OPEN";}
esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *out)
{if(link_error)return link_error;*out=remote;return ESP_OK;}
static bool live_connection_ready(void){return ready;}
static esp_err_t live_connect_selected_wifi(const char *password)
{assert(!*password);++connects;wifi_connection_target=wifi_selected_result;wifi_connection_target_valid=true;return ESP_OK;}
esp_err_t cerberus_keyboard_begin(cerberus_keyboard_t *k,char *b,size_t cap,const char *initial)
{(void)k;(void)b;(void)cap;assert(!*initial);++passwords;return ESP_OK;}
void cerberus_keyboard_set_masked(cerberus_keyboard_t *k,bool masked){(void)k;assert(masked);}
static void net_enter(void){++net_entries;}
static void station_enter(void){++station_entries;}
static void capture_enter(void){++capture_entries;}
#include "relation_target_support.h"
/* The actual relation page/reader has its own integration suite. This adapter
 * verifies target dispatch cannot enter the selected-AP station service. */
static void wifi_relation_open(void){++relation_entries;wifi_mode=UI_WIFI_MODE_OBSERVED_CLIENTS;}
#include "wifi_target_ui.h"
static void save(const char *prefix,const char *suffix)
{
    assert(cerberus_display_fill_rect(0,0,320,480,theme.background)==ESP_OK);
    assert(draw_wifi_target()==ESP_OK);
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,suffix);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv)
{
    assert(argc==2);char id[24],metadata[64];
    save(argv[1],"empty");wifi_target_handle_tap(30,145);assert(current_page!=UI_PAGE_FIELD_MENU);
    wifi_selected_result=(cerberus_c5_wifi_result_t){.ssid="Cerberus Test AP",.bssid={2,0,0,0,0,1},.channel=36,.rssi=-57,.security=3};
    wifi_selection_valid=true;save(argv[1],"selected");
    wifi_target_handle_tap(30,145);assert(field_menu.attack&&!field_menu.bluetooth&&current_page==UI_PAGE_FIELD_MENU);
    assert(field_menu.wifi_target_valid&&!strcmp(field_menu.wifi_target,"Cerberus Test AP"));
    assert(strstr(field_menu.wifi_target_detail,"02:00:00:00:00:01 / CH 36 / 5 GHZ"));
    current_page=0;wifi_attack_selecting=true;field_menu.page=2;
    wifi_selected_result.bssid[5]=2;wifi_target_finish_selection();
    assert(current_page==0&&wifi_attack_selecting&&wifi_mode==UI_WIFI_MODE_LIST); /* Toggle flow stays in picker. */
    wifi_target_open_attacks();assert(current_page==UI_PAGE_FIELD_MENU&&!wifi_attack_selecting&&field_menu.page==0);
    current_page=0;wifi_selected_result.bssid[5]=1;wifi_target_finish_selection();
    assert(current_page==0&&wifi_mode==UI_WIFI_MODE_TARGET); /* Ordinary scan keeps the tools card. */
    assert(!connects&&!passwords&&!net_entries); /* Selecting and opening options never transmits. */
    wifi_target_handle_tap(30,178);assert(!capture_entries); /* Old C5 cannot capture. */
    c5_status.firmware_version=CERBERUS_WIFI_CAPTURE_MIN_FIRMWARE;
    wifi_target_handle_tap(30,178);assert(capture_entries==1&&wifi_mode==UI_WIFI_MODE_CAPTURE);
    wifi_mode=UI_WIFI_MODE_TARGET;
    wifi_target_handle_tap(30,214);assert(passwords==1&&wifi_mode==UI_WIFI_MODE_PASSWORD);
    ready=false;wifi_target_connect();assert(passwords==1&&strstr(wifi_target_message,"WAIT"));ready=true;
    wifi_selected_result.security=0;wifi_target_connect();assert(connects==1);
    remote.state=CERBERUS_WIFI_CONNECTION_CONNECTED;remote.gateway[0]=192;remote.gateway[1]=168;remote.gateway[2]=20;remote.gateway[3]=1;
    wifi_connection_status=remote;wifi_target_message[0]=0;
    cerberus_wifi_target_view_t v=wifi_target_view(id,metadata);assert(v.can_scan_ports);
    save(argv[1],"connected");wifi_target_handle_tap(30,258);assert(!net_entries); /* Gap. */
    wifi_target_handle_tap(30,250);assert(net_entries==1&&wifi_mode==UI_WIFI_MODE_NETWORK_SCAN);
    wifi_selected_result.bssid[5]=2; /* Same SSID, different AP must not inherit the connection. */
    v=wifi_target_view(id,metadata);assert(!v.can_scan_ports&&!v.connection_matches);
    wifi_target_handle_tap(30,250);assert(net_entries==1);save(argv[1],"other-network");
    wifi_selected_result.bssid[5]=1;remote.state=CERBERUS_WIFI_CONNECTION_IDLE;
    wifi_target_handle_tap(30,250);assert(net_entries==1&&strstr(wifi_target_message,"CHANGED"));
    remote.state=CERBERUS_WIFI_CONNECTION_CONNECTED;wifi_connection_status=remote;link_error=ESP_FAIL;
    wifi_target_handle_tap(30,250);assert(net_entries==1);link_error=0;wifi_connection_error=0;
    wifi_target_handle_tap(30,290);assert(!station_entries); /* Old C5 does not expose stations. */
    c5_status.capabilities|=CERBERUS_C5_CAP_WIFI_STATIONS;
    wifi_target_handle_tap(30,290);assert(station_entries==1&&wifi_mode==UI_WIFI_MODE_STATIONS);
    c5_status.firmware_version=CERBERUS_WIFI_RELATION_MIN_FIRMWARE;
    c5_status.capabilities&=~CERBERUS_C5_CAP_WIFI_STATIONS;
    v=wifi_target_view(id,metadata);
    assert(v.can_scan_stations&&!strcmp(v.station_label,"OBSERVED CLIENTS"));
    wifi_target_handle_tap(30,290);
    assert(relation_entries==1&&station_entries==1&&wifi_mode==UI_WIFI_MODE_OBSERVED_CLIENTS);
    /* Retained passive evidence is inspectable offline without a radio call. */
    c5_online=false;wifi_mode=UI_WIFI_MODE_TARGET;
    wifi_target_handle_tap(30,290);
    assert(relation_entries==2&&station_entries==1&&wifi_mode==UI_WIFI_MODE_OBSERVED_CLIENTS);
    c5_online=true;wifi_mode=UI_WIFI_MODE_TARGET;
    save(argv[1],"observed-clients");
    wifi_target_handle_tap(30,320);assert(wifi_mode==UI_WIFI_MODE_DETAIL);
    c5_status.firmware_version=CERBERUS_WIFI_PACKET_MONITOR_MIN_FIRMWARE;
    wifi_target_handle_tap(30,345);
    assert(wifi_mode==UI_WIFI_MODE_PACKET_MONITOR&&monitor_entries==1&&
        packet_monitor_channel==36U&&packet_monitor_from_target);
    wifi_mode=UI_WIFI_MODE_TARGET;
    wifi_target_handle_tap(200,345);assert(!csi_entries); /* Older peer: no CSI entry. */
    c5_status.firmware_version=CERBERUS_CSI_MIN_FIRMWARE;
    wifi_target_handle_tap(200,345);assert(csi_entries==1&&wifi_mode==UI_WIFI_MODE_CSI);
    wifi_mode=UI_WIFI_MODE_TARGET;csi_open_error=ESP_ERR_NO_MEM;
    wifi_target_handle_tap(200,345);assert(csi_entries==2&&wifi_mode==UI_WIFI_MODE_TARGET&&strstr(wifi_target_message,"CSI:"));
    csi_open_error=0;wifi_target_message[0]=0;
    for(unsigned channel=169;channel<=177;channel+=4){
        wifi_selected_result.channel=(uint8_t)channel;v=wifi_target_view(id,metadata);
        assert(v.can_csi&&!v.can_packet_monitor);
    }
    wifi_selected_result.channel=36;
    save(argv[1],"csi-ready");
    wifi_target_handle_tap(30,365);assert(wifi_mode==UI_WIFI_MODE_LIST);
    strcpy(wifi_selected_result.ssid,"01234567890123456789012345678901");save(argv[1],"long-name");
    wifi_connection_status.state=CERBERUS_WIFI_CONNECTION_IDLE;
    wifi_selected_result.ssid[0]=0;v=wifi_target_view(id,metadata);assert(!v.can_connect);save(argv[1],"hidden");
    wifi_selection_valid=false;v=wifi_target_view(id,metadata);
    for(unsigned y=132;y<330;++y)assert(cerberus_wifi_target_tap(&v,30,y)==CERBERUS_WIFI_TARGET_NONE);
    wifi_targets_clear();wifi_selection_valid=false;
    cerberus_c5_wifi_result_t a={.ssid="Same SSID",.bssid={2,0,0,0,0,1},.channel=6},b=a;
    b.bssid[5]=2;
    fail_allocation=true;assert(!wifi_target_toggle_selection(&a,7)&&!wifi_selection_valid&&!wifi_target_count);
    fail_allocation=false;assert(wifi_target_toggle_selection(&a,7)&&wifi_target_count==1&&wifi_selected_index==7);
    assert(wifi_target_toggle_selection(&b,2)&&wifi_target_count==2&&wifi_selected_result.bssid[5]==2);
    cerberus_wifi_station_result_t client={.mac={2,9,8,7,6,5},.channel=6};
    wifi_targets_station(a.bssid,&client,true);assert(wifi_targets[0].station_valid&&!wifi_targets[1].station_valid);
    wifi_target_sync_menu();assert(!strcmp(field_menu.wifi_target,"2 NETWORKS SELECTED"));
    assert(wifi_target_toggle_selection(&b,19)&&wifi_target_count==1&&wifi_selected_result.bssid[5]==1&&wifi_selected_index==7);
    assert(wifi_targets[0].station_valid); /* Removing a different same-SSID AP preserves its client. */
    assert(wifi_target_toggle_selection(&a,29)&&!wifi_target_count&&!wifi_selection_valid&&!wifi_targets);
    wifi_target_sync_menu();assert(!field_menu.wifi_target_valid); /* Empty selection cannot silently reseed. */
    for(unsigned i=0;i<CERBERUS_WIFI_SCAN_MAX_RESULTS;++i){b.bssid[5]=i+1;assert(wifi_target_toggle_selection(&b,i));}
    b.bssid[5]=99;assert(!wifi_target_toggle_selection(&b,0)&&wifi_target_count==32&&wifi_selected_result.bssid[5]==32);
    b.bssid[5]=7;wifi_targets_station(b.bssid,&client,true);
    assert(wifi_targets_single(&b,0)&&wifi_target_count==1&&wifi_targets[0].station_valid);
    b.bssid[5]=99;fail_allocation=true;
    assert(wifi_targets_single(&b,9)&&wifi_target_count==1&&!wifi_targets[0].station_valid&&wifi_targets[0].index==9);
    wifi_targets_clear();
    puts("Native target checks passed: bounds, connection identity, toggle/focus, same-SSID selections, per-AP clients, full/empty collection, allocation failure and no automatic action.");
}
