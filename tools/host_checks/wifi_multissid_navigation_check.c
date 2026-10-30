/* Reuse the production MultiSSID UI fixture and service boundary fakes. Its
 * separate component test still runs unchanged before this navigation test. */
#define MULTISSID_NAVIGATION_CHECK
#define main multissid_component_check_main
#include "wifi_multissid_ui_check.c"
#undef main
#include "sentinel_scan_view.h"
#include "sentinel_scan_session.h"
#include "field_menu.h"

esp_err_t cerberus_display_draw_mono_bitmap(uint16_t x,uint16_t y,uint16_t width,
    uint16_t height,const uint8_t *bitmap,uint16_t color,uint16_t background)
{
    bounds(x,y,width,height);
    size_t stride=(width+7U)/8U;
    for(unsigned row=0;row<height;++row)for(unsigned col=0;col<width;++col)
        screen[(y+row)*320U+x+col]=(bitmap[row*stride+col/8U]&(0x80U>>(col%8U)))?color:background;
    return ESP_OK;
}

static bool ui_capability_blocked,wifi_attack_selecting,wifi_selection_valid;
static const char *ui_capability_blocked_reason;
static unsigned wifi_selected_index,wifi_target_count,wifi_analysis_generation;
static esp_err_t wifi_analysis_error,wifi_trust_error,wifi_connection_error;
static cerberus_c5_wifi_result_t wifi_selected_result;
static cerberus_wifi_connection_status_payload_t wifi_connection_status;
static cerberus_field_menu_t field_menu;
static char wifi_target_message[64],wifi_export_message[64];
static struct {bool checked;} wifi_baseline;
static bool packet_monitor_from_target,packet_monitor_attempted;
static unsigned packet_monitor_channel,packet_monitor_last_poll;

/* Unrelated page state referenced by the complete production launcher. */
static unsigned dashboard_mode,ble_mode,ieee802154_mode,script_mode,log_mode;
static unsigned settings_mode,qr_mode,usb_mode,self_test_mode;
static bool ble_attack_start_pending,ble_attack_return_pending,wardrive_gps_enabled;
static bool wardrive_start_pending,wardrive_return_pending,file_listing_checked;
static bool script_listing_checked,script_running,script_complete,log_checked;
static bool settings_reset_confirmation,settings_restore_confirmation;
static bool settings_backup_checked,settings_restore_checked;
static unsigned ble_attack_job_id,ble_attack_last_draw_ms,ble_sound_last_tick_ms,ble_sound_last_draw_ms;
static unsigned wardrive_last_draw_ms,script_page_offset;
static esp_err_t ble_attack_start_error,wardrive_start_error,settings_backup_error,settings_restore_error;
static esp_err_t settings_reset_error,usb_recovery_error;
static size_t settings_restore_length;
static char dashboard_crash_action[64],wardrive_blocked_reason[64],log_action_message[64];
static char settings_backup_file_name[32],settings_restore_file_name[32],settings_restore_info[32],settings_backup_buffer[32];
static void refresh_capability_map(void){}
static const char *page_capability_issue(ui_page_t page){(void)page;return NULL;}
static void clear_qr_wifi_credentials(void){}
static void reset_qr_save_status(void){}

static sentinel_scan_view_t live_view;
static sentinel_scan_session_t nav_scan_session;
static unsigned live_page[SENTINEL_SCAN_TOOL_COUNT],live_last_poll,live_frame;
static struct {uint16_t count;uint8_t saved_generation;} live_records[SENTINEL_SCAN_TOOL_COUNT];
static struct {
    cerberus_c5_wifi_result_t wifi[4];
    struct {uint8_t address_type,address[6];} ble[4];
} nav_store,*live_store=&nav_store;
static char live_hint[64];
/* BLE branches in the unmodified live handler are compiled but not exercised. */
typedef unsigned cerberus_s3_ble_filter_t;
typedef unsigned cerberus_s3_ble_sort_t;
enum {CERBERUS_S3_BLE_FILTER_COUNT=6,CERBERUS_S3_BLE_FILTER_FIND_MY=1,CERBERUS_S3_BLE_SORT_COUNT=3};
static unsigned ble_filter,live_ble_sort;
static bool ble_attack_selecting;
static size_t live_ble_filtered_count;
static uint8_t live_ble_indexes[4];
static struct {uint8_t generation;struct {uint8_t address_type,address[6];} peer;} *live_ble_evidence;
static sentinel_scan_tool_t live_page_tool(void){return SENTINEL_SCAN_WIFI;}
static sentinel_scan_session_t *live_session_for(sentinel_scan_tool_t tool)
{assert(tool==SENTINEL_SCAN_WIFI);return &nav_scan_session;}
static void actual_wifi_controls(void);
static void live_make_view(void){actual_wifi_controls();}
static void wifi_targets_clear(void){wifi_target_count=0;}
static unsigned row_toggles,attack_routes,target_routes;
static bool wifi_target_toggle_selection(const cerberus_c5_wifi_result_t *ap,uint8_t index)
{assert(ap==&nav_store.wifi[index]);++row_toggles;return true;}
static void wifi_target_sync_menu(void){++attack_routes;}
static bool ble_connection_is_active(void){assert(false);return false;}
static void ble_findmy_monitor_open(void){assert(false);}
static void ble_select_frozen_live(const void *p){(void)p;assert(false);}
esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *out)
{memset(out,0,sizeof(*out));return ESP_OK;}

/* Any accidental dispatch into an unrelated page fails this test. */
static void wifi_target_handle_tap(uint16_t x,uint16_t y){(void)x;(void)y;++target_routes;}
#define UNRELATED_TAP(name) static void name(uint16_t x,uint16_t y){(void)x;(void)y;assert(false);}
UNRELATED_TAP(net_tap)
UNRELATED_TAP(station_tap)
UNRELATED_TAP(ap_tap)
UNRELATED_TAP(portal_tap)
UNRELATED_TAP(capture_tap)
UNRELATED_TAP(pinescan_tap)
UNRELATED_TAP(field_survey_view_tap)
static void pinescan_enter(void){assert(false);}
static void open_learning_lesson(unsigned n){(void)n;assert(false);}
static esp_err_t refresh_wifi_scan_status(void){return ESP_OK;}
static esp_err_t refresh_wifi_defense_survey(void){assert(false);return ESP_OK;}
static void packet_monitor_start_ui(void){assert(false);}
#include "wifi_navigation_routes.h"

static void paint_live_controls(void)
{
    live_view=(sentinel_scan_view_t){.kind=SENTINEL_VIEW_WIFI,.state=SENTINEL_VIEW_COMPLETE,
        .page_count=1,.row_count=1,.total_count=1};
    actual_wifi_controls();
    live_records[SENTINEL_SCAN_WIFI].count=1;
    /* Use the actual renderer and hit-test contract for the footer. */
    drawn[0]='\0';assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);
}
static void navigate(bool selected)
{
    memset(&remote,0,sizeof(remote));multissid_ui_open=false;
    mounted=false;c5_online=true;status_error=page_error=start_error=ESP_OK;
    wifi_selection_valid=selected;wifi_target_count=selected?1:0;wifi_selected_index=3;
    wifi_selected_result=(cerberus_c5_wifi_result_t){.channel=36,.rssi=-47};
    memcpy(wifi_selected_result.bssid,"\x02\x14\x26\x38\x4a\x5c",6);
    memcpy(wifi_selected_result.ssid,"retained AP",12);
    cerberus_c5_wifi_result_t retained=wifi_selected_result;
    unsigned old_starts=starts;
    open_tool_page(UI_PAGE_WIFI); /* Actual launch defaults; never seed MENU. */
    assert(current_page==UI_PAGE_WIFI&&wifi_mode==UI_WIFI_MODE_LIST&&!wifi_attack_selecting);
    assert(wifi_selection_valid==selected&&wifi_selected_index==3&&!memcmp(&retained,&wifi_selected_result,sizeof(retained)));
    paint_live_controls();assert(strstr(drawn,"TOOLS")&&!strstr(drawn,"TARGET"));
    assert(sentinel_scan_view_hit_test(&live_view,270,380)==SENTINEL_VIEW_TOOLS);
    handle_wifi_tap(270,380); /* Production dispatch includes live_is_view guard. */
    assert(wifi_mode==UI_WIFI_MODE_MENU&&target_routes==0);
    assert(wifi_selection_valid==selected&&wifi_selected_index==3&&!memcmp(&retained,&wifi_selected_result,sizeof(retained)));
    assert(sentinel_hub_draw(SENTINEL_HUB_WIFI,&theme)==ESP_OK);
    handle_wifi_tap(196,380);assert(wifi_mode==UI_WIFI_MODE_MULTISSID&&multissid_ui_open);
    handle_wifi_tap(100,380);assert(starts==old_starts+1&&remote.capture.busy);
    assert(wifi_selection_valid==selected&&!memcmp(&retained,&wifi_selected_result,sizeof(retained)));
    remote.capture.busy=false;assert(multissid_finish_exit());
}
static void row_and_explicit_selection(void)
{
    open_tool_page(UI_PAGE_WIFI);paint_live_controls();
    nav_store.wifi[0]=(cerberus_c5_wifi_result_t){.channel=6,.rssi=-55};
    memcpy(nav_store.wifi[0].bssid,"\x02\x01\x02\x03\x04\x05",6);
    handle_wifi_tap(40,150);
    assert(wifi_mode==UI_WIFI_MODE_TARGET&&wifi_selection_valid&&wifi_selected_index==0);
    assert(!memcmp(&wifi_selected_result,&nav_store.wifi[0],sizeof(wifi_selected_result)));
    cerberus_c5_wifi_result_t retained=wifi_selected_result;
    open_tool_page(UI_PAGE_WIFI);wifi_attack_selecting=true;wifi_target_count=0;paint_live_controls();
    assert(!strcmp(live_view.tools_label,"ATTACKS")&&live_view.tools_disabled);
    assert(sentinel_scan_view_hit_test(&live_view,270,380)==SENTINEL_VIEW_NONE);
    handle_wifi_tap(270,380);assert(wifi_mode==UI_WIFI_MODE_LIST&&attack_routes==0);
    wifi_target_count=1;paint_live_controls();
    assert(!live_view.tools_disabled);
    handle_wifi_tap(270,380);
    assert(current_page==UI_PAGE_FIELD_MENU&&field_menu.attack&&attack_routes==1);
    assert(!memcmp(&retained,&wifi_selected_result,sizeof(retained)));
    open_tool_page(UI_PAGE_WIFI);wifi_attack_selecting=true;wifi_target_count=1;paint_live_controls();
    handle_wifi_tap(40,150);assert(row_toggles==1&&wifi_mode==UI_WIFI_MODE_LIST);
    assert(!memcmp(&retained,&wifi_selected_result,sizeof(retained)));
}
int main(void)
{
    navigate(false);navigate(true);row_and_explicit_selection();
    puts("Production Wi-Fi navigation: launcher -> live TOOLS -> hub -> MULTISSID -> START; retained AP, row target and explicit-selection routes passed.");
    return 0;
}
