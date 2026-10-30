/* Reuse the checked display rasterizer; this TU adds the full production
 * adapter plus extracted Scan routing, linked to actual pure state machines. */
#define main remote_id_renderer_suite_main
#include "remote_id_ui_check.c"
#undef main
#include "c5_spi_link.h"
#include "s3_ble_scanner.h"
#include "s3_ble_host.h"
#include "s3_wifi_owner.h"
#include "wardrive_service.h"
#include "field_menu.h"
#include "privacy.h"
#include "remote_id_diagnostics.h"
#include "esp_check.h"
#include "remote_id_route_types.h"

#define MALLOC_CAP_SPIRAM 4U
#define MALLOC_CAP_8BIT 2U
#define UI_BACKGROUND RID_UI_BG
#define UI_BAD UINT16_C(0xF440)
#define TAG "host"
#define ESP_ERROR_CHECK(call) CHECK((call)==ESP_OK)

static ui_page_t current_page=UI_PAGE_FIELD_MENU,tool_parent_page;
static bool learning_tool_active,ui_capability_blocked,wardrive_start_pending,wardrive_return_pending;
static const char *ui_capability_blocked_reason;
static cerberus_field_menu_t field_menu;
static uint32_t live_capture_pending;
static uint64_t clock_ms;
static bool allocation_failure,takeover_ready,privacy_masked;
static unsigned allocations,frees,wifi_starts,wifi_stops,ble_starts,ble_stops,active_starts;
static unsigned status_reads,watch_reads,watch_rows;
static unsigned diagnostics_publishes;
static cerberus_remote_id_diagnostics_t diagnostics;
static bool unchanged_wifi_start,hold_ble_release,stop_noop;
static esp_err_t wifi_poll_error,ble_poll_error,wifi_start_error,ble_start_error,stop_error,watch_error;
static cerberus_c5_status_t peer;
static cerberus_wifi_scan_status_payload_t wifi;
static cerberus_ble_scan_status_payload_t ble;
static cerberus_wifi_channel_survey_status_payload_t survey;
static cerberus_wifi_connection_status_payload_t connection;
static cerberus_wardrive_status_t wardrive;
static cerberus_s3_ble_owner_t ble_owner;
static cerberus_s3_wifi_owner_t wifi_owner;
static unsigned ble_epoch;
static cerberus_watch_scan_status_payload_t watch_status[2];
static cerberus_watch_observation_t watch_data[2][12];
static cerberus_watch_receive_timing_t watch_timing[2][12];
static unsigned observed_rows[2];
static unsigned detailed_rows[2],legacy_observed_calls,legacy_row_calls;
static sentinel_watch_motion_t watch_motion[2][12];
static cerberus_watch_receive_timing_t watch_motion_timing[2][12];
static esp_err_t motion_error[2];
static unsigned motion_failure_index;
static esp_err_t timing_error;
static cerberus_remote_id_status_payload_t priority_status;
static cerberus_remote_id_start_request_t priority_request;
static unsigned priority_starts,priority_stops,priority_renews,priority_reads;
static uint64_t priority_stop_id,priority_renew_id,priority_query_id;
static esp_err_t priority_start_error,priority_poll_error,priority_stop_error,priority_renew_error;
static bool priority_delivered,priority_rejected,priority_hold_release,priority_drop_data_after_row;
static bool priority_drop_data_after_status,priority_generation_override;
static uint8_t priority_next_generation;
static unsigned watch_status_reads[2];
static unsigned priority_peer_change_before_control,priority_preflight_rejections;
static bool priority_complete_on_renew;
static const cerberus_ui_widget_theme_t theme={RID_UI_BG,RID_UI_PANEL,RID_UI_CYAN,RID_UI_TEXT,RID_UI_MUTED};
void cerberus_remote_id_diagnostics_publish(const cerberus_remote_id_diagnostics_t *snapshot)
{++diagnostics_publishes;if(snapshot)diagnostics=*snapshot;else memset(&diagnostics,0,sizeof(diagnostics));}

static void *heap_caps_calloc(size_t count,size_t size,unsigned capabilities)
{
    CHECK(capabilities==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(allocation_failure)return NULL;
    void *memory=calloc(count,size);CHECK(memory!=NULL);++allocations;return memory;
}
static void heap_caps_free(void *memory);
static bool recon_sessions_idle_for_takeover(void){return takeover_ready;}
void cerberus_wardrive_get_status(cerberus_wardrive_status_t *out){*out=wardrive;}
int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000LL;}
static bool privacy_addresses_masked(void){return privacy_masked;}
cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void){return ble_owner;}
unsigned cerberus_s3_ble_host_epoch(void){return ble_epoch;}
cerberus_s3_wifi_owner_t cerberus_s3_wifi_owner(void){return wifi_owner;}
static const char *esp_err_to_name(esp_err_t error)
{
    switch(error){case ESP_OK:return "ESP_OK";case ESP_ERR_NO_MEM:return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_STATE:return "ESP_ERR_INVALID_STATE";case ESP_ERR_TIMEOUT:return "ESP_ERR_TIMEOUT";
    default:return "ESP_FAIL";}
}
esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *out)
{++status_reads;*out=peer;return wifi_poll_error;}
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *out)
{++status_reads;*out=wifi;return wifi_poll_error;}
esp_err_t c5_spi_link_wifi_channel_survey_status(cerberus_wifi_channel_survey_status_payload_t *out)
{*out=survey;return ESP_OK;}
esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *out)
{*out=connection;return ESP_OK;}
esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *out)
{++status_reads;*out=ble;return ble_poll_error;}
esp_err_t c5_spi_link_start_wifi_scan(cerberus_wifi_scan_status_payload_t *out)
{
    ++wifi_starts;
    if(!wifi_start_error&&!unchanged_wifi_start){++wifi.generation;wifi.state=CERBERUS_WIFI_SCAN_RUNNING;
        peer.radio_state|=CERBERUS_C5_RADIO_WIFI_SCAN;}
    *out=wifi;return wifi_start_error;
}
esp_err_t cerberus_s3_ble_scanner_start(cerberus_ble_scan_status_payload_t *out)
{
    ++ble_starts;
    if(!ble_start_error){++ble.generation;ble.state=CERBERUS_BLE_SCAN_RUNNING;ble_owner=CERBERUS_S3_BLE_OWNER_SCANNER;}
    *out=ble;return ble_start_error;
}
esp_err_t cerberus_s3_ble_scanner_start_active(cerberus_ble_scan_status_payload_t *out)
{(void)out;++active_starts;return ESP_FAIL;}
esp_err_t c5_spi_link_stop_wifi_scan(cerberus_wifi_scan_status_payload_t *out)
{
    ++wifi_stops;
    if(!stop_error&&!stop_noop){wifi.state=CERBERUS_WIFI_SCAN_COMPLETE;peer.radio_state&=(uint8_t)~CERBERUS_C5_RADIO_WIFI_SCAN;}
    *out=wifi;return stop_error;
}
esp_err_t c5_spi_link_remote_id_start(const cerberus_remote_id_start_request_t *request,
    cerberus_remote_id_status_payload_t *out)
{
    ++priority_starts;priority_request=*request;
    CHECK(request->job_id!=0&&request->duration_ms==8000&&request->lease_ms==3000&&request->dwell_ms==250);
    if(!request->flags){
        for(unsigned i=0;i<sizeof(request->reserved);++i)CHECK(request->reserved[i]==0);
    }else{
        CHECK(peer.firmware_version>=CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
        CHECK(request->flags==CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON||request->flags==CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN);
        uint8_t any=0;for(unsigned i=0;i<6;++i)any|=request->transmitter[i];
        CHECK(any&&!(request->transmitter[0]&1U));
    }
    if(priority_rejected){
        *out=(cerberus_remote_id_status_payload_t){.job_id=request->job_id,
            .state=CERBERUS_REMOTE_ID_STATE_REJECTED,.error_code=ESP_ERR_INVALID_STATE,
            .generation=priority_status.generation};
        return ESP_OK;
    }
    if(priority_delivered){
        CHECK(request->job_id>priority_status.job_id);
        priority_status=(cerberus_remote_id_status_payload_t){.job_id=request->job_id,
            .generation=priority_generation_override?priority_next_generation:(uint8_t)(priority_status.generation+1U),
            .state=CERBERUS_REMOTE_ID_STATE_RUNNING,
            .flags=CERBERUS_REMOTE_ID_FLAG_OWNED|CERBERUS_REMOTE_ID_FLAG_RECEIVING,
            .lease_remaining_ms=3000,.channel=6};
        if(request->flags){
            priority_status.flags|=CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED;
            if(request->follow_channel)priority_status.flags|=CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
        }
        peer.radio_state|=CERBERUS_C5_RADIO_WIFI_SCAN;
    }
    *out=priority_status;return priority_start_error;
}
esp_err_t c5_spi_link_remote_id_status(uint64_t job_id,cerberus_remote_id_status_payload_t *out)
{
    ++priority_reads;priority_query_id=job_id;
    *out=priority_status;
    if(priority_poll_error)return priority_poll_error;
    if(job_id&&job_id!=priority_status.job_id){
        *out=(cerberus_remote_id_status_payload_t){.job_id=job_id,.generation=priority_status.generation,
            .state=CERBERUS_REMOTE_ID_STATE_REJECTED,.error_code=ESP_ERR_INVALID_STATE};
    }
    return ESP_OK;
}
esp_err_t c5_spi_link_remote_id_stop(uint64_t job_id,cerberus_remote_id_status_payload_t *out)
{
    ++priority_stops;priority_stop_id=job_id;CHECK(job_id==priority_status.job_id&&job_id!=0);
    if(!priority_stop_error){
        priority_status.state=priority_hold_release?CERBERUS_REMOTE_ID_STATE_STOPPING:CERBERUS_REMOTE_ID_STATE_STOPPED;
        priority_status.flags=(priority_status.flags&(CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED|CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND))|
            (priority_hold_release?CERBERUS_REMOTE_ID_FLAG_OWNED:0);
        if(!priority_hold_release)peer.radio_state&=(uint8_t)~CERBERUS_C5_RADIO_WIFI_SCAN;
    }
    *out=priority_status;return priority_stop_error;
}
esp_err_t c5_spi_link_remote_id_renew(uint64_t job_id,cerberus_remote_id_status_payload_t *out)
{
    ++priority_renews;priority_renew_id=job_id;CHECK(job_id==priority_status.job_id&&job_id!=0);
    if(!priority_renew_error){
        if(priority_complete_on_renew){
            priority_status.state=CERBERUS_REMOTE_ID_STATE_COMPLETE;
            priority_status.flags=(priority_status.flags&(CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED|CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND))|
                CERBERUS_REMOTE_ID_FLAG_DATA_READY;
            priority_status.lease_remaining_ms=0;priority_status.channel=0;
            peer.radio_state&=(uint8_t)~CERBERUS_C5_RADIO_WIFI_SCAN;
        }else priority_status.lease_remaining_ms=3000;
    }
    *out=priority_status;return priority_renew_error;
}
static esp_err_t priority_bound_preflight(unsigned operation,const cerberus_c5_status_t *expected,
    cerberus_remote_id_status_payload_t *out)
{
    CHECK(expected!=NULL);
    if(priority_peer_change_before_control==operation){++peer.boot_count;priority_peer_change_before_control=0;}
    if(expected->firmware_version!=peer.firmware_version||expected->capabilities!=peer.capabilities||
        expected->protocol_version!=peer.protocol_version||expected->reset_reason!=peer.reset_reason||
        expected->boot_count!=peer.boot_count||expected->uptime_ms>peer.uptime_ms){
        ++priority_preflight_rejections;memset(out,0,sizeof(*out));
        return CERBERUS_REMOTE_ID_ERR_PEER_CHANGED;
    }
    return ESP_OK;
}
esp_err_t c5_spi_link_remote_id_start_for_peer(const cerberus_remote_id_start_request_t *request,
    const cerberus_c5_status_t *expected,cerberus_remote_id_status_payload_t *out)
{esp_err_t e=priority_bound_preflight(1,expected,out);return e?e:c5_spi_link_remote_id_start(request,out);}
esp_err_t c5_spi_link_remote_id_status_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected,cerberus_remote_id_status_payload_t *out)
{esp_err_t e=priority_bound_preflight(4,expected,out);return e?e:c5_spi_link_remote_id_status(job_id,out);}
esp_err_t c5_spi_link_remote_id_stop_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected,cerberus_remote_id_status_payload_t *out)
{esp_err_t e=priority_bound_preflight(2,expected,out);return e?e:c5_spi_link_remote_id_stop(job_id,out);}
esp_err_t c5_spi_link_remote_id_renew_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected,cerberus_remote_id_status_payload_t *out)
{esp_err_t e=priority_bound_preflight(3,expected,out);return e?e:c5_spi_link_remote_id_renew(job_id,out);}
esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *out)
{
    ++ble_stops;
    if(!stop_error&&!stop_noop){ble.state=CERBERUS_BLE_SCAN_COMPLETE;
        if(!hold_ble_release)ble_owner=CERBERUS_S3_BLE_OWNER_NONE;}
    *out=ble;return stop_error;
}
static esp_err_t get_watch(unsigned slot,uint8_t generation,cerberus_watch_scan_status_payload_t *out)
{
    ++watch_reads;++watch_status_reads[slot];*out=watch_status[slot];
    if(!slot&&priority_drop_data_after_status&&watch_status_reads[slot]==2)
        priority_status.flags&=(uint8_t)~CERBERUS_REMOTE_ID_FLAG_DATA_READY;
    return watch_error?watch_error:generation==out->generation?ESP_OK:ESP_ERR_INVALID_STATE;
}
static esp_err_t get_watch_row(unsigned slot,uint8_t generation,uint8_t index,cerberus_watch_observation_t *out)
{
    ++watch_rows;CHECK(index<12);*out=watch_data[slot][index];
    return watch_error?watch_error:generation==watch_status[slot].generation?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t c5_spi_link_get_watch_status(uint8_t radio,uint8_t generation,cerberus_watch_scan_status_payload_t *out)
{CHECK(radio==CERBERUS_WATCH_RADIO_WIFI);return get_watch(0,generation,out);}
esp_err_t c5_spi_link_get_watch_result(uint8_t radio,uint8_t generation,uint8_t index,cerberus_watch_observation_t *out)
{++legacy_row_calls;CHECK(radio==CERBERUS_WATCH_RADIO_WIFI);return get_watch_row(0,generation,index,out);}
esp_err_t cerberus_s3_ble_scanner_get_watch_status(uint8_t generation,cerberus_watch_scan_status_payload_t *out)
{return get_watch(1,generation,out);}
esp_err_t cerberus_s3_ble_scanner_get_watch_result(uint8_t generation,uint8_t index,cerberus_watch_observation_t *out)
{++legacy_row_calls;return get_watch_row(1,generation,index,out);}
static esp_err_t get_watch_observed(unsigned slot,uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing)
{
    ++observed_rows[slot];memset(timing,0,sizeof(*timing));
    esp_err_t error=get_watch_row(slot,generation,index,out);
    if(error)return error;
    if(timing_error)return timing_error;
    *timing=watch_timing[slot][index];
    if(!slot&&priority_drop_data_after_row)priority_status.flags&=(uint8_t)~CERBERUS_REMOTE_ID_FLAG_DATA_READY;
    return ESP_OK;
}
esp_err_t c5_spi_link_get_watch_observed_result(uint8_t radio,uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing)
{++legacy_observed_calls;CHECK(radio==CERBERUS_WATCH_RADIO_WIFI);return get_watch_observed(0,generation,index,out,timing);}
esp_err_t cerberus_s3_ble_scanner_get_watch_observed_result(uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing)
{++legacy_observed_calls;return get_watch_observed(1,generation,index,out,timing);}
static esp_err_t get_watch_detailed(unsigned slot,uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion,cerberus_watch_receive_timing_t *motion_timing)
{
    CHECK(out&&timing&&motion&&motion_timing);++detailed_rows[slot];
    memset(motion,0,sizeof(*motion));memset(motion_timing,0,sizeof(*motion_timing));
    esp_err_t error=get_watch_observed(slot,generation,index,out,timing);
    if(!error&&motion_error[slot]&&index==motion_failure_index)error=motion_error[slot];
    if(error){memset(out,0,sizeof(*out));memset(timing,0,sizeof(*timing));return error;}
    if(slot||peer.firmware_version>=CERBERUS_WATCH_MOTION_MIN_FIRMWARE){
        *motion=watch_motion[slot][index];*motion_timing=watch_motion_timing[slot][index];
    }
    return ESP_OK;
}
esp_err_t c5_spi_link_get_watch_detailed_result(uint8_t radio,uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion,cerberus_watch_receive_timing_t *motion_timing)
{CHECK(radio==CERBERUS_WATCH_RADIO_WIFI);return get_watch_detailed(0,generation,index,out,timing,motion,motion_timing);}
esp_err_t cerberus_s3_ble_scanner_get_watch_detailed_result(uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion,cerberus_watch_receive_timing_t *motion_timing)
{return get_watch_detailed(1,generation,index,out,timing,motion,motion_timing);}

static esp_err_t draw_header(const char *text)
{return cerberus_display_draw_text(16,16,text,RID_UI_TEXT,RID_UI_BG,2);}
static esp_err_t draw_centered(uint16_t y,const char *text,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320U-strlen(text)*6U*scale)/2U),y,text,color,RID_UI_BG,scale);}
static esp_err_t draw_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *text)
{return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){x,y,w,h},text,1,true,&theme);}
static esp_err_t draw_current_page(void);
#include "remote_id_live_ui.h"
static esp_err_t draw_current_page(void)
{
    if(current_page==UI_PAGE_FIELD_MENU)return cerberus_field_menu_draw(&field_menu,&theme);
    if(current_page==UI_PAGE_REMOTE_ID)return remote_id_draw(true);
    return ESP_OK;
}
static void heap_caps_free(void *memory)
{
    if(!memory)return;
    CHECK(memory==remote_id_live);
    CHECK(cerberus_remote_id_session_released(&remote_id_live->session));
    ++frees;free(memory);
}
static void open_main_menu(void){current_page=UI_PAGE_MAIN;}
static void watch_open(uint32_t now){(void)now;CHECK(false);}
#include "remote_id_routes.h"

static void live_reset(void)
{
    CHECK(remote_id_live==NULL);reset();
    clock_ms=1000;allocations=frees=wifi_starts=wifi_stops=ble_starts=ble_stops=active_starts=0;
    status_reads=watch_reads=watch_rows=0;live_capture_pending=0;
    diagnostics_publishes=0;memset(&diagnostics,0,sizeof(diagnostics));
    current_page=UI_PAGE_FIELD_MENU;tool_parent_page=UI_PAGE_MAIN;
    memset(&field_menu,0,sizeof(field_menu));memset(&wardrive,0,sizeof(wardrive));
    memset(&wifi,0,sizeof(wifi));memset(&ble,0,sizeof(ble));
    memset(&survey,0,sizeof(survey));memset(&connection,0,sizeof(connection));
    memset(watch_status,0,sizeof(watch_status));memset(watch_data,0,sizeof(watch_data));
    memset(watch_timing,0,sizeof(watch_timing));memset(observed_rows,0,sizeof(observed_rows));timing_error=0;
    memset(watch_motion,0,sizeof(watch_motion));memset(watch_motion_timing,0,sizeof(watch_motion_timing));
    memset(detailed_rows,0,sizeof(detailed_rows));memset(motion_error,0,sizeof(motion_error));
    legacy_observed_calls=legacy_row_calls=motion_failure_index=0;
    memset(&priority_status,0,sizeof(priority_status));memset(&priority_request,0,sizeof(priority_request));
    priority_starts=priority_stops=priority_renews=priority_reads=0;
    priority_stop_id=priority_renew_id=priority_query_id=0;
    priority_start_error=priority_poll_error=priority_stop_error=priority_renew_error=0;
    priority_delivered=true;priority_rejected=priority_hold_release=priority_drop_data_after_row=false;
    priority_drop_data_after_status=priority_generation_override=false;priority_next_generation=0;
    memset(watch_status_reads,0,sizeof(watch_status_reads));
    priority_peer_change_before_control=priority_preflight_rejections=0;
    priority_complete_on_renew=false;
    remote_id_last_job_id=0;
    peer=(cerberus_c5_status_t){.firmware_version=51,.boot_count=2,.uptime_ms=100,
        .capabilities=CERBERUS_C5_CAP_WATCH_OBSERVATIONS|CERBERUS_C5_CAP_WIFI_2_4_GHZ|CERBERUS_C5_CAP_WIFI_STATION};
    takeover_ready=privacy_masked=true;allocation_failure=wardrive_start_pending=wardrive_return_pending=false;
    unchanged_wifi_start=hold_ble_release=stop_noop=false;
    wifi_poll_error=ble_poll_error=wifi_start_error=ble_start_error=stop_error=watch_error=0;
    ble_owner=CERBERUS_S3_BLE_OWNER_NONE;wifi_owner=CERBERUS_S3_WIFI_NONE;ble_epoch=1;
}
static void tap(unsigned x,unsigned y)
{drawn[0]=0;actual_remote_id_route((uint16_t)x,(uint16_t)y);}
static void tick(void)
{clock_ms+=500;drawn[0]=0;remote_id_tick((uint32_t)clock_ms,false);}
static void enter(void)
{
    CHECK(cerberus_field_menu_tap(&field_menu,20,343)==CERBERUS_FIELD_REMOTE_ID);
    tap(20,343);CHECK(current_page==UI_PAGE_REMOTE_ID);
}
static void start_scan(void)
{tap(185,440);tick();CHECK(wifi_starts+ble_starts>0);}
static void stop_and_exit(void)
{
    stop_error=0;stop_noop=false;hold_ble_release=false;
    if(ble.state!=CERBERUS_BLE_SCAN_RUNNING)ble_owner=CERBERUS_S3_BLE_OWNER_NONE;
    if(remote_id_live&&remote_id_live->detail&&remote_id_live->motion_detail)tap(20,440);
    if(remote_id_live&&remote_id_live->detail)tap(20,440);
    tap(20,440);
    for(unsigned i=0;i<15&&remote_id_live;++i)tick();
    CHECK(!remote_id_live&&current_page==UI_PAGE_FIELD_MENU);
}
static void complete(unsigned slot,unsigned count)
{
    uint8_t radio=slot?CERBERUS_WATCH_RADIO_BLE:CERBERUS_WATCH_RADIO_WIFI;
    if(slot){ble.state=CERBERUS_BLE_SCAN_COMPLETE;if(!hold_ble_release)ble_owner=CERBERUS_S3_BLE_OWNER_NONE;}
    else{wifi.state=CERBERUS_WIFI_SCAN_COMPLETE;peer.radio_state&=(uint8_t)~CERBERUS_C5_RADIO_WIFI_SCAN;}
    watch_status[slot]=(cerberus_watch_scan_status_payload_t){.radio=radio,
        .generation=slot?ble.generation:wifi.generation,.state=CERBERUS_WATCH_COMPLETE,
        .stored_count=(uint8_t)count,.reports=count};
    for(unsigned i=0;i<count;++i)watch_data[slot][i]=row(radio,i);
}

static void menu_allocation_and_no_automatic_rf(void)
{
    live_reset();drawn[0]=0;CHECK(draw_current_page()==ESP_OK);
    CHECK(strstr(drawn,"REMOTE ID")&&strstr(drawn,"AIRCRAFT BROADCASTS"));
    CHECK(!strstr(drawn,"FIELD GUIDE"));
    tap(320,375);CHECK(!remote_id_live);
    allocation_failure=true;enter();CHECK(!remote_id_live&&remote_id_open_error==ESP_ERR_NO_MEM);
    CHECK(diagnostics_publishes==1&&!diagnostics.open);
    CHECK(!wifi_starts&&!ble_starts);tap(64,420);CHECK(current_page==UI_PAGE_FIELD_MENU);
    allocation_failure=false;enter();CHECK(remote_id_live&&allocations==1);
    CHECK(diagnostics.open&&!diagnostics.running&&!diagnostics.selected);
    CHECK(!wifi_starts&&!ble_starts&&!status_reads);CHECK(!remote_id_keeps_awake());
    for(unsigned i=0;i<4;++i)tick();CHECK(!wifi_starts&&!ble_starts&&!watch_reads);
    tap(20,440);CHECK(!remote_id_live&&frees==1&&!diagnostics.open);
}

static void acknowledged_start_stop_and_host_drain(void)
{
    live_reset();enter();start_scan();
    CHECK(wifi_starts==1&&ble_starts==1&&active_starts==0);
    CHECK(wifi.generation==1&&ble.generation==1);
    CHECK(live_capture_pending==((1U<<SENTINEL_SCAN_WIFI)|(1U<<SENTINEL_SCAN_BLE)));
    CHECK(remote_id_keeps_awake());
    CHECK(diagnostics.running&&diagnostics.radios[0].owned&&diagnostics.radios[1].owned);
    hold_ble_release=true;tap(185,440);tick();
    CHECK(wifi_stops==1&&ble_stops==1&&remote_id_live);
    tick();CHECK(!cerberus_remote_id_session_released(&remote_id_live->session));
    CHECK(diagnostics.stopping&&!diagnostics.running);
    CHECK(!remote_id_live->view.start_available&&remote_id_live->view.stop_available);
    tap(185,440);CHECK(wifi_starts==1&&ble_starts==1); /* STOP, never a hidden new START. */
    tap(20,440);CHECK(remote_id_live&&frees==0&&remote_id_live->exit_pending);
    tick();CHECK(remote_id_live&&frees==0);
    hold_ble_release=false;ble_owner=CERBERUS_S3_BLE_OWNER_NONE;tick();
    CHECK(!remote_id_live&&frees==1&&current_page==UI_PAGE_FIELD_MENU);
    CHECK(!diagnostics.open&&!diagnostics.radios[0].owned&&!diagnostics.radios[1].owned);
    live_reset();enter();start_scan();tap(185,440);tick();tick();
    CHECK(cerberus_remote_id_session_released(&remote_id_live->session));
    CHECK(remote_id_live->view.start_available);tap(185,440);tick();
    CHECK(wifi_starts==2&&ble_starts==2);stop_and_exit();
}

static void rejected_legacy_and_foreign_work(void)
{
    live_reset();wifi.generation=ble.generation=255;enter();start_scan();
    CHECK(wifi.generation==0&&ble.generation==0);
    CHECK(remote_id_live->session.radios[0].owned&&remote_id_live->session.radios[1].owned);
    stop_and_exit();
    live_reset();enter();unchanged_wifi_start=true;start_scan();
    CHECK(wifi_starts==1&&!remote_id_live->session.radios[0].owned);
    CHECK(remote_id_live->session.radios[0].error==ESP_ERR_INVALID_STATE);
    CHECK(!(live_capture_pending&(1U<<SENTINEL_SCAN_WIFI)));
    stop_and_exit();CHECK(wifi_stops==0);
    live_reset();enter();ble_start_error=ESP_ERR_NO_MEM;start_scan();
    CHECK(ble_starts==1&&!remote_id_live->session.radios[1].owned);
    CHECK(!(live_capture_pending&(1U<<SENTINEL_SCAN_BLE)));
    stop_and_exit();CHECK(ble_stops==0);
    live_reset();peer.capabilities=0;ble_poll_error=ESP_ERR_NOT_SUPPORTED;enter();tap(185,440);tick();
    CHECK(!wifi_starts&&!ble_starts&&!wifi_stops&&!ble_stops);
    CHECK(strstr(remote_id_live->view.wifi_status,"UNAVAILABLE")&&strstr(remote_id_live->view.ble_status,"UNAVAILABLE"));
    stop_and_exit();
    for(unsigned scenario=0;scenario<9;++scenario){
        live_reset();enter();
        switch(scenario){
        case 0:takeover_ready=false;break;
        case 1:wardrive.state=CERBERUS_WARDRIVE_RUNNING;break;
        case 2:wardrive_start_pending=true;break;
        case 3:wardrive_return_pending=true;break;
        case 4:survey.state=CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING;break;
        case 5:connection.state=CERBERUS_WIFI_CONNECTION_CONNECTING;break;
        case 6:peer.radio_state=CERBERUS_C5_RADIO_TEST;break;
        case 7:ble_owner=CERBERUS_S3_BLE_OWNER_CENTRAL;break;
        default:wifi_owner=CERBERUS_S3_WIFI_RECEIVER;break;
        }
        tap(185,440);tick();
        CHECK(scenario<7?wifi_starts==0:wifi_starts==1);
        CHECK(scenario<4||scenario>=7?ble_starts==0:ble_starts==1);
        CHECK(!active_starts);
        stop_and_exit();
        CHECK(scenario<7?wifi_stops==0:wifi_stops==1);
        CHECK(scenario<4||scenario>=7?ble_stops==0:ble_stops==1);
    }
}

static void stop_rechecks_exact_identity(void)
{
    live_reset();enter();cerberus_remote_id_radio_observation_t observed;
    wifi.state=CERBERUS_WIFI_SCAN_RUNNING;wifi.generation=9;peer.radio_state=CERBERUS_C5_RADIO_WIFI_SCAN;
    CHECK(remote_id_poll(remote_id_live,CERBERUS_WATCH_RADIO_WIFI,true,&observed)==0);
    ++wifi.generation;
    cerberus_remote_id_command_result_t result=remote_id_command(remote_id_live,CERBERUS_WATCH_RADIO_WIFI,false,&observed);
    CHECK(result.error==CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED&&wifi_stops==0);
    CHECK(remote_id_poll(remote_id_live,CERBERUS_WATCH_RADIO_WIFI,true,&observed)==0);
    ++peer.boot_count;
    result=remote_id_command(remote_id_live,CERBERUS_WATCH_RADIO_WIFI,false,&observed);
    CHECK(result.error==CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED&&wifi_stops==0);
    ble.state=CERBERUS_BLE_SCAN_RUNNING;ble.generation=7;ble_owner=CERBERUS_S3_BLE_OWNER_SCANNER;
    CHECK(remote_id_poll(remote_id_live,CERBERUS_WATCH_RADIO_BLE,true,&observed)==0);
    ++ble_epoch;
    result=remote_id_command(remote_id_live,CERBERUS_WATCH_RADIO_BLE,false,&observed);
    CHECK(result.error==CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED&&ble_stops==0);
    CHECK(remote_id_poll(remote_id_live,CERBERUS_WATCH_RADIO_BLE,true,&observed)==0);
    result=remote_id_command(remote_id_live,CERBERUS_WATCH_RADIO_BLE,false,&observed);
    CHECK(result.error==0&&ble_stops==1&&wifi_stops==0);
    tap(20,440);CHECK(!remote_id_live&&wifi.state==CERBERUS_WIFI_SCAN_RUNNING);
}

static void publication_frozen_tap_and_snapshot_failure(void)
{
    live_reset();enter();start_scan();complete(0,6);complete(1,1);
    tick();tick();CHECK(remote_id_live->inventory.banks[0].count==6);
    CHECK(remote_id_live->inventory.banks[1].count==1);
    CHECK(remote_id_live->view.page.total==7&&watch_rows==7);
    CHECK(diagnostics.radios[0].completed_sweeps==1&&diagnostics.radios[0].retained_sources==6);
    CHECK(diagnostics.radios[1].completed_sweeps==1&&diagnostics.radios[1].retained_sources==1);
    tap(270,440);CHECK(remote_id_live->offset==4&&remote_id_live->cache.row_count==3);
    CHECK(strstr(remote_id_live->cache.painted.rows[2][1],"BLE-")&&
        strstr(remote_id_live->cache.painted.rows[2][1]," / T3"));
    CHECK(!strstr(remote_id_live->cache.painted.rows[2][1],"00:89:67:45:23:02"));
    privacy_masked=false;drawn[0]=0;CHECK(remote_id_draw(true)==ESP_OK);
    CHECK(strstr(remote_id_live->cache.painted.rows[2][1],"00:89:67:45:23:02 / T3"));
    tap(20,250);CHECK(remote_id_live->detail&&remote_id_live->inventory.selected.frozen.radio==CERBERUS_WATCH_RADIO_BLE);
    CHECK(remote_id_live->inventory.selected.frozen.observation.address_type==3);
    CHECK(strstr(remote_id_live->cache.painted.detail[0],"00:89:67:45:23:02 / T3"));
    tap(20,440);privacy_masked=true;
    tap(100,440);CHECK(remote_id_live->offset==0);
    cerberus_remote_id_record_t frozen=remote_id_live->cache.rows[0];
    /* Advance the actual backend before dispatching a tap from the old frame. */
    cerberus_watch_observation_t replacement=row(CERBERUS_WATCH_RADIO_WIFI,11);
    cerberus_watch_scan_status_payload_t status={.radio=CERBERUS_WATCH_RADIO_WIFI,
        .generation=5,.state=CERBERUS_WATCH_COMPLETE,.stored_count=1,.reports=1};
    CHECK(cerberus_remote_id_inventory_publish(&remote_id_live->inventory,2,&status,&status,&replacement,1,clock_ms)==CERBERUS_REMOTE_ID_PUBLISHED);
    tap(20,150);CHECK(remote_id_live->detail&&remote_id_live->inventory.selected.valid);
    CHECK(diagnostics.selected&&!diagnostics.present);
    CHECK(!memcmp(&remote_id_live->inventory.selected.frozen,&frozen,sizeof(frozen)));
    CHECK(!remote_id_live->inventory.selected.present);
    CHECK(!strstr(drawn,"02:23:45:67:89:00"));
    tap(20,440);CHECK(!remote_id_live->detail&&!remote_id_live->inventory.selected.valid);
    CHECK(!diagnostics.selected);
    /* A failed read on the next owned sweep cannot publish partial data. */
    tick();CHECK(wifi_starts==2&&ble_starts==2);complete(0,2);complete(1,0);
    tick();watch_error=ESP_ERR_TIMEOUT;
    cerberus_remote_id_bank_t retained=remote_id_live->inventory.banks[0];
    tick();CHECK(!memcmp(&retained,&remote_id_live->inventory.banks[0],sizeof(retained)));
    watch_error=0;stop_and_exit();
}

static void offscreen_and_faulted_cleanup(void)
{
    live_reset();enter();start_scan();stop_error=ESP_ERR_TIMEOUT;tap(185,440);
    for(unsigned i=0;i<30;++i)tick();
    CHECK(remote_id_live&&remote_id_live->session.radios[0].scan.faulted&&remote_id_live->session.radios[1].scan.faulted);
    CHECK(remote_id_live->view.stop_available&&!remote_id_live->view.start_available&&frees==0);
    stop_error=0;tap(185,440); /* The production STOP control re-arms failed cleanup only. */
    for(unsigned i=0;i<8;++i)tick();
    CHECK(cerberus_remote_id_session_released(&remote_id_live->session));
    CHECK(wifi_starts==1&&ble_starts==1);tap(20,440);CHECK(!remote_id_live&&frees==1);
    live_reset();enter();start_scan();stop_error=ESP_ERR_TIMEOUT;
    current_page=UI_PAGE_MAIN;tick();CHECK(remote_id_live&&remote_id_live->exit_pending&&frees==0);
    for(unsigned i=0;i<20;++i)tick();CHECK(remote_id_live&&frees==0);
    CHECK(wifi_starts==1&&ble_starts==1);
    stop_error=0;cerberus_remote_id_session_stop(&remote_id_live->session);
    for(unsigned i=0;i<20&&remote_id_live;++i)tick();
    CHECK(!remote_id_live&&frees==1&&current_page==UI_PAGE_MAIN);
}

static void observed_timing_routing_and_retry(void)
{
    live_reset();peer.firmware_version=CERBERUS_WATCH_TIMING_MIN_FIRMWARE;
    enter();start_scan();complete(0,1);complete(1,1);
    watch_timing[0][0]=(cerberus_watch_receive_timing_t){clock_ms*1000U,701,true};
    watch_timing[1][0]=(cerberus_watch_receive_timing_t){clock_ms*1000U,1199,true};
    tick();tick();
    CHECK(observed_rows[0]==1&&observed_rows[1]==1);
    CHECK(remote_id_live->view.page.copied==2);
    CHECK(cerberus_watch_receive_timing_equal(&remote_id_live->view.page.rows[0].timing,&watch_timing[0][0]));
    CHECK(cerberus_watch_receive_timing_equal(&remote_id_live->view.page.rows[1].timing,&watch_timing[1][0]));
    tap(20,150);CHECK(remote_id_live->detail&&remote_id_live->inventory.selected.valid);
    cerberus_remote_id_record_t frozen=remote_id_live->inventory.selected.frozen;
    CHECK(frozen.timing.valid&&frozen.timing.source_age_ms==701);
    tick();CHECK(wifi_starts==2&&ble_starts==2);complete(0,1);complete(1,1);
    memset(&watch_timing[0][0],0,sizeof(watch_timing[0][0]));
    watch_timing[1][0]=(cerberus_watch_receive_timing_t){clock_ms*1000U,9,true};
    tick();
    cerberus_remote_id_bank_t retained=remote_id_live->inventory.banks[0];
    timing_error=ESP_ERR_TIMEOUT;tick();
    CHECK(!memcmp(&retained,&remote_id_live->inventory.banks[0],sizeof(retained)));
    CHECK(!memcmp(&frozen,&remote_id_live->inventory.selected.frozen,sizeof(frozen)));
    CHECK(remote_id_live->session.radios[0].error==ESP_ERR_TIMEOUT);
    timing_error=0;tick();
    CHECK(remote_id_live->session.radios[0].published_sweeps==2);
    CHECK(remote_id_live->session.radios[1].published_sweeps==2);
    CHECK(remote_id_live->inventory.selected.present&&!remote_id_live->inventory.selected.latest.timing.valid);
    CHECK(!memcmp(&frozen,&remote_id_live->inventory.selected.frozen,sizeof(frozen)));
    stop_and_exit();
}

static void priority_reset(void)
{live_reset();peer.firmware_version=CERBERUS_REMOTE_ID_MIN_FIRMWARE;}
static void priority_complete(unsigned count)
{
    complete(0,count);watch_status[0].generation=priority_status.generation;
    priority_status.state=CERBERUS_REMOTE_ID_STATE_COMPLETE;
    priority_status.flags=(priority_status.flags&(CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED|CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND))|
        CERBERUS_REMOTE_ID_FLAG_DATA_READY;
    priority_status.channel=0;priority_status.lease_remaining_ms=0;
}

static void priority_start_windows_and_identity(void)
{
    priority_reset();priority_status.job_id=UINT64_C(0x10000000073);
    priority_status.generation=255;priority_status.state=CERBERUS_REMOTE_ID_STATE_STOPPED;
    enter();for(unsigned i=0;i<4;++i)tick();
    CHECK(!priority_reads&&!priority_starts&&!priority_renews&&!wifi_starts&&!ble_starts);
    start_scan();CHECK(priority_starts==1&&!wifi_starts&&ble_starts==1);
    CHECK(priority_request.job_id==UINT64_C(0x10000000074)&&priority_status.generation==0);
    CHECK(remote_id_live->c5_job_id==priority_request.job_id);
    CHECK(live_capture_pending==(1U<<SENTINEL_SCAN_BLE));
    tick();CHECK(strstr(remote_id_live->view.wifi_status,"PRIORITY CH6"));
    for(unsigned window=0;window<2;++window){
        priority_complete(1);complete(1,1);tick();tick();
        CHECK(remote_id_live->session.radios[0].published_sweeps==window+1U);
        CHECK(remote_id_live->session.radios[1].published_sweeps==window+1U);
        CHECK(remote_id_live->inventory.banks[0].count==1);
        CHECK(!(live_capture_pending&(1U<<SENTINEL_SCAN_WIFI)));
        if(!window){tick();CHECK(priority_starts==2&&ble_starts==2&&!wifi_starts);}
    }
    uint64_t completed_id=priority_status.job_id;
    stop_and_exit();CHECK(!wifi_stops);
    enter();start_scan();CHECK(priority_request.job_id==completed_id+1U);
    stop_and_exit();completed_id=priority_status.job_id;
    /* Simulate only the S3 counter restarting. C5 retains its full64 high-water. */
    remote_id_last_job_id=0;enter();start_scan();CHECK(priority_request.job_id==completed_id+1U);
    stop_and_exit();
    priority_reset();priority_status.job_id=UINT64_MAX;
    priority_status.state=CERBERUS_REMOTE_ID_STATE_STOPPED;
    enter();tap(185,440);tick();CHECK(!priority_starts&&!wifi_starts&&ble_starts==1);
    CHECK(remote_id_live->session.radios[0].error==CERBERUS_REMOTE_ID_ERROR_TOKEN_EXHAUSTED);
    stop_and_exit();
}

static void priority_start_uncertainty(void)
{
    priority_reset();priority_start_error=ESP_ERR_TIMEOUT;
    enter();start_scan();uint64_t attempt=priority_request.job_id;
    CHECK(remote_id_live->session.radios[0].owned&&priority_status.job_id==attempt);
    tick();CHECK(priority_query_id==attempt&&remote_id_live->session.radios[0].owned);
    CHECK(priority_starts==1&&!wifi_starts);
    priority_start_error=0;stop_and_exit();CHECK(priority_stops==1&&priority_stop_id==attempt);
    priority_reset();priority_start_error=ESP_ERR_TIMEOUT;priority_delivered=false;
    enter();start_scan();attempt=priority_request.job_id;
    CHECK(remote_id_live->session.radios[0].owned);
    tick();CHECK(!remote_id_live->session.radios[0].owned&&!priority_stops&&!wifi_starts);
    priority_start_error=0;priority_delivered=true;
    for(unsigned i=0;i<4&&priority_starts<2;++i)tick();
    CHECK(priority_starts==2&&priority_request.job_id==attempt+1U);
    stop_and_exit();
    priority_reset();priority_rejected=true;enter();start_scan();
    CHECK(priority_starts==1&&!remote_id_live->session.radios[0].owned&&!wifi_starts);
    CHECK(remote_id_live->session.radios[0].error==ESP_ERR_INVALID_STATE);
    complete(1,1);tick();tick();CHECK(remote_id_live->session.radios[1].published_sweeps==1);
    stop_and_exit();CHECK(!priority_stops&&!wifi_stops);
    priority_reset();priority_poll_error=ESP_ERR_TIMEOUT;enter();start_scan();
    CHECK(!priority_starts&&!wifi_starts&&ble_starts==1);
    complete(1,1);tick();tick();CHECK(remote_id_live->session.radios[1].published_sweeps==1);
    priority_poll_error=0;stop_and_exit();CHECK(!priority_stops);
    /* An intervening ordinary Wi-Fi scan advanced the shared Watch byte.
     * Lost ACK must reconcile via exact64jobID instead of guessing old+1. */
    priority_reset();priority_status.generation=7;
    priority_generation_override=true;priority_next_generation=23;priority_start_error=ESP_ERR_TIMEOUT;
    enter();start_scan();attempt=priority_request.job_id;
    CHECK(priority_status.generation==23&&remote_id_live->session.radios[0].owned);
    priority_status.lease_remaining_ms=1;tick();
    CHECK(remote_id_live->session.radios[0].owned&&!remote_id_live->session.radios[0].faulted);
    CHECK(remote_id_live->session.radios[0].expected_generation==23&&!priority_renews);
    tick();CHECK(priority_renews==1&&priority_renew_id==attempt);
    priority_start_error=0;stop_and_exit();CHECK(priority_stops==1&&priority_stop_id==attempt);
}

static void priority_lease_cleanup_and_foreign_identity(void)
{
    priority_reset();enter();start_scan();uint64_t id=priority_status.job_id;
    priority_status.lease_remaining_ms=1500;tick();CHECK(!priority_renews);
    priority_status.lease_remaining_ms=1499;tick();CHECK(priority_renews==1&&priority_renew_id==id);
    priority_status.lease_remaining_ms=1;tap(185,440);tick();
    CHECK(priority_renews==1&&priority_stops==1&&priority_stop_id==id);
    tick();stop_and_exit();
    for(unsigned state=0;state<2;++state){
        priority_reset();enter();start_scan();priority_hold_release=true;
        tap(185,440);tick();CHECK(priority_stops==1);
        if(state){priority_status.state=CERBERUS_REMOTE_ID_STATE_ERROR;priority_status.error_code=ESP_FAIL;}
        priority_status.lease_remaining_ms=1;tap(20,440);
        for(unsigned i=0;i<3;++i)tick();
        CHECK(remote_id_live&&frees==0&&!priority_renews);
        CHECK(!remote_id_live->view.start_available&&remote_id_live->view.stop_available);
        priority_hold_release=false;priority_status.state=CERBERUS_REMOTE_ID_STATE_STOPPED;
        priority_status.flags=0;peer.radio_state=0;
        for(unsigned i=0;i<4&&remote_id_live;++i)tick();
        CHECK(!remote_id_live&&frees==1);
    }
    for(unsigned changed=0;changed<5;++changed){
        priority_reset();enter();start_scan();
        switch(changed){
        case 0:++peer.boot_count;break;
        case 1:++priority_status.job_id;break;
        case 2:peer.capabilities^=UINT32_C(0x40000000);break;
        case 3:++peer.protocol_version;break;
        default:++peer.reset_reason;break;
        }
        priority_status.lease_remaining_ms=1;
        tick();CHECK(!priority_stops&&!priority_renews);
        CHECK(!remote_id_live->session.radios[0].owned);
        stop_and_exit();CHECK(!priority_stops&&!wifi_stops);
    }
}

static void priority_atomic_publication_guards(void)
{
    for(unsigned failure=0;failure<4;++failure){
        priority_reset();enter();start_scan();priority_complete(1);complete(1,1);
        tick();CHECK(remote_id_live->session.radios[0].capture_pending);
        if(failure==0)priority_status.flags=0;
        else if(failure==1)priority_drop_data_after_row=true;
        else if(failure==2)++watch_status[0].generation; /* Foreign ordinary Watch overwrite. */
        else priority_drop_data_after_status=true;
        cerberus_remote_id_bank_t retained=remote_id_live->inventory.banks[0];
        tick();CHECK(!memcmp(&retained,&remote_id_live->inventory.banks[0],sizeof(retained)));
        CHECK(remote_id_live->session.radios[0].published_sweeps==0);
        CHECK(remote_id_live->session.radios[1].published_sweeps==1);
        CHECK(failure==1||failure==3?observed_rows[0]==1:observed_rows[0]==0);
        CHECK(!wifi_starts&&!wifi_stops);
        priority_drop_data_after_row=false;stop_and_exit();
    }
    /* Final complete status without DATA_READY must not even stage a capture. */
    priority_reset();enter();start_scan();priority_complete(1);priority_status.flags=0;
    tick();tick();CHECK(remote_id_live->session.radios[0].published_sweeps==0);
    CHECK(!observed_rows[0]);stop_and_exit();
}

static void priority_peer_change_between_poll_and_control(void)
{
    priority_reset();priority_peer_change_before_control=1;
    enter();start_scan();CHECK(priority_preflight_rejections==1);
    CHECK(!priority_starts&&!wifi_starts&&!remote_id_live->session.radios[0].owned);
    CHECK(remote_id_live->session.radios[0].error==CERBERUS_REMOTE_ID_ERR_PEER_CHANGED);
    stop_and_exit();CHECK(!priority_stops&&!priority_renews);
    for(unsigned operation=2;operation<=4;++operation){
        priority_reset();enter();start_scan();
        priority_peer_change_before_control=operation;
        priority_status.lease_remaining_ms=1;
        if(operation==2)tap(185,440);
        tick();CHECK(priority_preflight_rejections==1);
        CHECK(!priority_stops&&!priority_renews);
        tick();CHECK(!remote_id_live->session.radios[0].owned);
        stop_and_exit();CHECK(!priority_stops&&!priority_renews&&!wifi_stops);
    }
}

static void priority_completion_between_status_and_renew(void)
{
    priority_reset();enter();start_scan();uint64_t id=priority_status.job_id;
    watch_status[0]=(cerberus_watch_scan_status_payload_t){.radio=CERBERUS_WATCH_RADIO_WIFI,
        .generation=priority_status.generation,.state=CERBERUS_WATCH_COMPLETE,.stored_count=1,.reports=1};
    watch_data[0][0]=row(CERBERUS_WATCH_RADIO_WIFI,3);
    watch_timing[0][0]=(cerberus_watch_receive_timing_t){clock_ms*1000U,27,true};
    priority_status.lease_remaining_ms=1;priority_complete_on_renew=true;
    tick();CHECK(priority_renews==1&&priority_renew_id==id);
    CHECK(!remote_id_live->session.radios[0].owned&&remote_id_live->session.radios[0].capture_pending);
    CHECK(!remote_id_live->session.radios[0].faulted&&!priority_stops);
    tick();CHECK(remote_id_live->session.radios[0].published_sweeps==1);
    CHECK(remote_id_live->inventory.banks[0].count==1);
    CHECK(!memcmp(&remote_id_live->inventory.banks[0].rows[0],&watch_data[0][0],sizeof(watch_data[0][0])));
    CHECK(priority_starts==1&&!wifi_starts&&!wifi_stops);
    CHECK(!(live_capture_pending&(1U<<SENTINEL_SCAN_WIFI)));
    stop_and_exit();CHECK(!priority_stops);
}

static void priority_lost_ack_terminal_first_poll(void)
{
    for(unsigned failed=0;failed<2;++failed){
        priority_reset();priority_generation_override=true;priority_next_generation=43;
        priority_start_error=ESP_ERR_TIMEOUT;
        enter();start_scan();CHECK(remote_id_live->session.radios[0].identity_pending);
        uint64_t id=priority_request.job_id;
        /* No RUNNING follow-up was observed, and actual generation differs
         * from the pre-START profile. Exact64ID plus unchanged peer resolves it. */
        priority_complete(1);
        if(failed){priority_status.state=CERBERUS_REMOTE_ID_STATE_ERROR;
            priority_status.flags=0;priority_status.error_code=ESP_FAIL;}
        tick();CHECK(priority_query_id==id&&!remote_id_live->session.radios[0].owned);
        CHECK(!remote_id_live->session.radios[0].identity_pending);
        CHECK(remote_id_live->session.radios[0].expected_generation==43);
        CHECK(!priority_renews&&!priority_stops&&!wifi_starts);
        if(failed){
            CHECK(!remote_id_live->session.radios[0].capture_pending);
            CHECK(remote_id_live->session.radios[0].error==ESP_FAIL);
            CHECK(!remote_id_live->session.radios[0].published_sweeps&&!observed_rows[0]);
        }else{
            CHECK(remote_id_live->session.radios[0].capture_pending);
            tick();CHECK(remote_id_live->session.radios[0].published_sweeps==1);
            CHECK(remote_id_live->inventory.banks[0].count==1&&observed_rows[0]==1);
            CHECK(!memcmp(&remote_id_live->inventory.banks[0].rows[0],&watch_data[0][0],sizeof(watch_data[0][0])));
        }
        priority_start_error=0;stop_and_exit();CHECK(!priority_stops&&!priority_renews);
    }
}

static void priority_diagnostics_publication(void)
{
    priority_reset();enter();CHECK(!diagnostics.wifi_priority&&!diagnostics.wifi_profile_valid&&!diagnostics.wifi_job_id);
    start_scan();CHECK(diagnostics.wifi_priority&&diagnostics.wifi_profile_valid);
    CHECK(diagnostics.wifi_job_id==priority_status.job_id&&diagnostics.radios[0].owned);
    priority_status.elapsed_ms=4500;priority_status.lease_remaining_ms=2000;priority_status.channel=149;
    tick();CHECK(diagnostics.wifi_profile_valid&&diagnostics.wifi_job_state==CERBERUS_REMOTE_ID_STATE_RUNNING);
    CHECK(diagnostics.wifi_channel==149&&diagnostics.wifi_window_elapsed_ms==4500&&diagnostics.wifi_lease_remaining_ms==2000);
    unsigned reads=priority_reads,starts=priority_starts,renews=priority_renews,stops=priority_stops;
    remote_id_publish_diagnostics();
    CHECK(reads==priority_reads&&starts==priority_starts&&renews==priority_renews&&stops==priority_stops);
    priority_poll_error=ESP_ERR_TIMEOUT;tick();
    CHECK(diagnostics.wifi_priority&&!diagnostics.wifi_profile_valid);
    CHECK(!diagnostics.wifi_job_id&&!diagnostics.wifi_job_state&&!diagnostics.wifi_channel);
    CHECK(!diagnostics.wifi_window_elapsed_ms&&!diagnostics.wifi_lease_remaining_ms);
    CHECK(diagnostics.radios[0].error==ESP_ERR_TIMEOUT);
    priority_poll_error=0;tick();CHECK(diagnostics.wifi_profile_valid&&diagnostics.wifi_job_id==priority_status.job_id);
    priority_complete(1);tick();tick();
    CHECK(diagnostics.wifi_profile_valid&&diagnostics.wifi_job_state==CERBERUS_REMOTE_ID_STATE_COMPLETE);
    CHECK(!diagnostics.radios[0].owned&&diagnostics.radios[0].completed_sweeps==1);
    CHECK(!diagnostics.wifi_channel&&!diagnostics.wifi_lease_remaining_ms);
    stop_and_exit();CHECK(!diagnostics.open&&!diagnostics.wifi_priority&&!diagnostics.wifi_profile_valid&&!diagnostics.wifi_job_id);
}

static void follow_idle_sources(uint32_t firmware)
{
    priority_reset();peer.firmware_version=firmware;peer.capabilities|=CERBERUS_C5_CAP_WIFI_5_GHZ;
    enter();start_scan();priority_complete(2);complete(1,1);
    watch_data[0][0].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;watch_data[0][0].channel=6;
    watch_data[0][1].channel=149;
    for(unsigned i=0;i<2;++i)watch_timing[0][i]=(cerberus_watch_receive_timing_t){clock_ms*1000U,0,true};
    tick();tick();CHECK(remote_id_live->inventory.banks[0].count==2&&remote_id_live->inventory.banks[1].count==1);
    tap(185,440);tick();CHECK(!remote_id_live->session.running&&cerberus_remote_id_session_released(&remote_id_live->session));
    unsigned starts=priority_starts,ble_count=ble_starts;
    tap(20,150);CHECK(remote_id_live->detail&&remote_id_live->inventory.selected.valid);
    CHECK(!remote_id_live->follow_enabled&&priority_starts==starts&&ble_starts==ble_count);
}

static void follow_explicit_start_and_next_target(void)
{
    follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
    CHECK(remote_id_live->cache.painted.follow_available);
    cerberus_remote_id_record_t frozen=remote_id_live->inventory.selected.frozen;
    tap(100,440);CHECK(remote_id_live->follow_enabled&&remote_id_live->session.running&&priority_starts==1);
    tick();CHECK(priority_starts==2&&ble_starts==2&&!wifi_starts);
    CHECK(priority_request.flags==CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN&&priority_request.follow_channel==6);
    CHECK(!memcmp(priority_request.transmitter,frozen.observation.address,6));
    CHECK(!memcmp(&remote_id_live->inventory.selected.frozen,&frozen,sizeof(frozen)));
    CHECK(strstr(remote_id_live->cache.painted.footer[1],"FOLLOWING WITH BACKGROUND"));
    CHECK(diagnostics.wifi_follow_flags==(CERBERUS_REMOTE_ID_DIAG_FOLLOW_ENABLED|
        CERBERUS_REMOTE_ID_DIAG_FOLLOW_REQUESTED|CERBERUS_REMOTE_ID_DIAG_FOLLOW_BOUND));
    uint64_t first_job=priority_request.job_id;
    cerberus_remote_id_follow_hint_t attempt=remote_id_live->c5_follow_hint;
    tap(20,440);CHECK(!remote_id_live->detail&&!remote_id_live->follow_enabled);
    tap(20,208);CHECK(remote_id_live->inventory.selected.frozen.observation.address[5]==1);
    tap(100,440);tick();CHECK(priority_starts==2&&remote_id_live->follow_enabled);
    CHECK(!memcmp(&remote_id_live->c5_follow_hint,&attempt,sizeof(attempt)));
    CHECK(strstr(remote_id_live->cache.painted.footer[1],"NEXT WI-FI WINDOW"));
    CHECK(!strstr(remote_id_live->cache.painted.footer[1],"FOLLOWING WITH"));
    priority_complete(2);complete(1,1);
    watch_data[0][0].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    watch_data[0][1].channel=36;
    for(unsigned i=0;i<2;++i)watch_timing[0][i]=(cerberus_watch_receive_timing_t){clock_ms*1000U,0,true};
    tick();tick();CHECK(remote_id_live->session.radios[0].published_sweeps==2);
    tick();CHECK(priority_starts==3&&priority_request.job_id>first_job);
    CHECK(priority_request.flags==CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON&&priority_request.follow_channel==36);
    CHECK(priority_request.transmitter[5]==1);
    CHECK(strstr(remote_id_live->cache.painted.footer[1],"FOLLOWING WITH BACKGROUND"));
    stop_and_exit();CHECK(!diagnostics.wifi_follow_flags);
}

static void follow_search_and_fresh_channel(void)
{
    for(unsigned missing=0;missing<6;++missing){
        follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
        cerberus_remote_id_selection_t *selected=&remote_id_live->inventory.selected;
        cerberus_remote_id_record_t frozen=selected->frozen;
        if(missing==0)memset(&selected->latest.timing,0,sizeof(selected->latest.timing));
        else if(missing==1)clock_ms+=30001U;
        else if(missing==2)selected->present=false;
        else if(missing==3)selected->radio_unavailable=true;
        else if(missing==4)selected->latest.observation.channel=35;
        else{selected->latest.observation.channel=149;peer.capabilities&=~CERBERUS_C5_CAP_WIFI_5_GHZ;}
        tap(100,440);tick();CHECK(priority_request.flags==CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN);
        CHECK(priority_request.follow_channel==0&&!memcmp(priority_request.transmitter,frozen.observation.address,6));
        CHECK(strstr(remote_id_live->cache.painted.footer[1],"SEARCHING FOR SELECTED"));
        CHECK(diagnostics.wifi_follow_flags==(CERBERUS_REMOTE_ID_DIAG_FOLLOW_ENABLED|CERBERUS_REMOTE_ID_DIAG_FOLLOW_REQUESTED));
        CHECK(!memcmp(&selected->frozen,&frozen,sizeof(frozen)));stop_and_exit();
    }
    follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
    cerberus_remote_id_record_t frozen=remote_id_live->inventory.selected.frozen;
    tap(100,440);tick();uint64_t first_job=priority_request.job_id;
    priority_complete(2);complete(1,1);
    watch_data[0][0].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;watch_data[0][0].channel=177;
    watch_timing[0][0]=(cerberus_watch_receive_timing_t){clock_ms*1000U,1,true};
    tick();tick();CHECK(remote_id_live->inventory.selected.latest.observation.channel==177);
    CHECK(!memcmp(&remote_id_live->inventory.selected.frozen,&frozen,sizeof(frozen)));
    tick();CHECK(priority_request.job_id>first_job&&priority_request.follow_channel==177);
    CHECK(!memcmp(priority_request.transmitter,frozen.observation.address,6));stop_and_exit();
}

static void follow_compatibility_foreign_and_uncertain(void)
{
    follow_idle_sources(CERBERUS_REMOTE_ID_MIN_FIRMWARE);
    CHECK(!remote_id_live->cache.painted.follow_available);
    tap(100,440);tick();CHECK(!remote_id_live->follow_enabled&&priority_starts==1);
    tap(185,440);tick();CHECK(priority_starts==2&&priority_request.flags==0);stop_and_exit();
    follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
    tap(100,440);tick();CHECK(remote_id_live->follow_enabled);
    tap(185,440);tick();tick();CHECK(cerberus_remote_id_session_released(&remote_id_live->session));
    unsigned starts=priority_starts;peer.firmware_version=CERBERUS_REMOTE_ID_MIN_FIRMWARE;
    tap(185,440);tick();CHECK(priority_starts==starts&&remote_id_live->follow_enabled);
    CHECK(remote_id_live->session.radios[0].error==ESP_ERR_NOT_SUPPORTED);
    CHECK(remote_id_live->cache.painted.follow_available&&remote_id_live->cache.painted.follow_enabled);
    tap(100,440);CHECK(!remote_id_live->follow_enabled);
    for(unsigned i=0;i<5&&priority_starts==starts;++i)tick();
    CHECK(priority_starts==starts+1U&&priority_request.flags==0);stop_and_exit();
    follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
    priority_status.job_id+=100;priority_status.state=CERBERUS_REMOTE_ID_STATE_RUNNING;
    priority_status.flags=CERBERUS_REMOTE_ID_FLAG_OWNED|CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED|CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
    peer.radio_state=CERBERUS_C5_RADIO_WIFI_SCAN;
    tap(100,440);tick();CHECK(priority_query_id==0&&priority_starts==1);
    CHECK(!remote_id_live->session.radios[0].owned&&strstr(remote_id_live->cache.painted.footer[1],"NEXT WI-FI WINDOW"));
    CHECK(!strstr(remote_id_live->cache.painted.footer[1],"FOLLOWING WITH"));
    stop_and_exit();CHECK(!priority_stops&&!priority_renews);
    follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
    priority_start_error=ESP_ERR_TIMEOUT;tap(100,440);tick();
    CHECK(remote_id_live->session.radios[0].identity_pending);
    cerberus_remote_id_follow_hint_t attempted=remote_id_live->c5_follow_hint;
    uint64_t attempted_job=remote_id_live->c5_job_id;
    tap(20,440);tap(20,208);tap(100,440);tick();
    CHECK(remote_id_live->c5_job_id==attempted_job&&!memcmp(&remote_id_live->c5_follow_hint,&attempted,sizeof(attempted)));
    CHECK(strstr(remote_id_live->cache.painted.footer[1],"NEXT WI-FI WINDOW"));
    priority_start_error=0;stop_and_exit();
}

static void follow_painted_action_and_ble_selection(void)
{
    follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
    cerberus_remote_id_record_t old_painted=remote_id_live->cache.rows[0];
    cerberus_remote_id_page_t records;CHECK(cerberus_remote_id_inventory_page(&remote_id_live->inventory,0,&records));
    CHECK(cerberus_remote_id_inventory_select(&remote_id_live->inventory,&records.rows[1]));
    tap(100,440);CHECK(!remote_id_live->follow_enabled&&priority_starts==1);
    CHECK(!memcmp(&remote_id_live->cache.rows[0],&old_painted,sizeof(old_painted)));
    CHECK(remote_id_draw(true)==ESP_OK);
    tap(100,440);tick();CHECK(priority_request.transmitter[5]==1&&priority_request.flags==CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON);
    uint64_t job=priority_request.job_id;cerberus_remote_id_follow_hint_t attempted=remote_id_live->c5_follow_hint;
    tap(20,440);tap(20,266);CHECK(remote_id_live->inventory.selected.frozen.radio==CERBERUS_WATCH_RADIO_BLE);
    CHECK(!remote_id_live->cache.painted.follow_visible);tap(100,440);tick();
    CHECK(!remote_id_live->follow_enabled&&priority_request.job_id==job);
    CHECK(!memcmp(&remote_id_live->c5_follow_hint,&attempted,sizeof(attempted)));
    priority_complete(2);complete(1,1);tick();tick();tick();
    CHECK(priority_request.job_id>job&&priority_request.flags==0);stop_and_exit();
}

static void follow_owned_cleanup_never_reports_ready(void)
{
    for(unsigned failed=0;failed<2;++failed){
        follow_idle_sources(CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE);
        tap(100,440);tick();priority_hold_release=true;
        priority_status.state=failed?CERBERUS_REMOTE_ID_STATE_ERROR:CERBERUS_REMOTE_ID_STATE_STOPPING;
        priority_status.error_code=failed?ESP_FAIL:0;
        priority_status.flags=CERBERUS_REMOTE_ID_FLAG_OWNED|CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED|
            CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
        cerberus_remote_id_radio_observation_t observation={0};
        CHECK(remote_id_poll(remote_id_live,CERBERUS_WATCH_RADIO_WIFI,true,&observation)==ESP_OK);
        CHECK(remote_id_draw(false)==ESP_OK);
        CHECK(strstr(remote_id_live->cache.painted.footer[1],"FOLLOW CLEANUP"));
        CHECK(!strstr(remote_id_live->cache.painted.footer[1],"FOLLOWING"));
        CHECK(!strstr(remote_id_live->cache.painted.footer[1],"FOLLOW READY"));
        unsigned starts=priority_starts,renews=priority_renews;
        tap(185,440);tick();
        CHECK(remote_id_live->session.radios[0].owned&&!remote_id_live->session.running&&frees==0);
        CHECK(strstr(remote_id_live->cache.painted.footer[1],"STOPPING FOLLOW"));
        CHECK(!strstr(remote_id_live->cache.painted.footer[1],"FOLLOWING"));
        CHECK(!strstr(remote_id_live->cache.painted.footer[1],"FOLLOW READY"));
        CHECK(remote_id_live->cache.painted.follow_available&&remote_id_live->cache.painted.follow_enabled);
        tap(100,440);CHECK(!remote_id_live->follow_enabled&&!remote_id_live->session.running);
        CHECK(priority_starts==starts&&priority_renews==renews);
        CHECK(!(diagnostics.wifi_follow_flags&CERBERUS_REMOTE_ID_DIAG_FOLLOW_ENABLED));
        priority_hold_release=false;priority_status.state=CERBERUS_REMOTE_ID_STATE_STOPPED;
        priority_status.flags&=(uint8_t)~CERBERUS_REMOTE_ID_FLAG_OWNED;peer.radio_state=0;
        stop_and_exit();
    }
}

static void set_motion(unsigned slot,unsigned index,uint16_t speed,uint64_t age)
{
    watch_timing[slot][index]=(cerberus_watch_receive_timing_t){clock_ms*1000U,1,true};
    watch_motion[slot][index]=(sentinel_watch_motion_t){speed,360,1,31,0};
    watch_motion_timing[slot][index]=(cerberus_watch_receive_timing_t){clock_ms*1000U,age,true};
}
static unsigned all_radio_calls(void)
{
    return wifi_starts+wifi_stops+ble_starts+ble_stops+active_starts+priority_starts+
        priority_stops+priority_renews+priority_reads+status_reads+watch_reads+watch_rows;
}
static void detailed_adapter_atomic_retry(void)
{
    priority_reset();peer.firmware_version=CERBERUS_WATCH_MOTION_MIN_FIRMWARE;
    enter();start_scan();priority_complete(2);complete(1,1);
    set_motion(0,0,1017,60000);set_motion(0,1,258,100);set_motion(1,0,0,200);
    tick();tick();
    CHECK(detailed_rows[0]==2&&detailed_rows[1]==1&&!legacy_observed_calls&&!legacy_row_calls);
    CHECK(remote_id_live->inventory.banks[0].count==2&&remote_id_live->inventory.banks[1].count==1);
    cerberus_remote_id_page_t records;CHECK(cerberus_remote_id_inventory_page(&remote_id_live->inventory,0,&records));
    CHECK(records.rows[0].motion.speed_qms==1017&&records.rows[1].motion.speed_qms==258&&records.rows[2].motion.speed_qms==0);
    CHECK(records.rows[0].motion_timing.source_age_ms==60000&&records.rows[2].motion_timing.source_age_ms==200);
    tap(20,150);tap(270,440);CHECK(remote_id_live->detail&&remote_id_live->motion_detail);
    cerberus_remote_id_record_t frozen=remote_id_live->inventory.selected.frozen;
    CHECK(!strcmp(remote_id_live->cache.painted.detail[9],"LOCATION CALLBACK AGE <=62s"));
    CHECK(!strcmp(remote_id_live->cache.painted.detail[10],"SOURCE CALLBACK AGE <=2s"));
    tick();CHECK(priority_starts==2&&ble_starts==2);
    priority_complete(2);complete(1,1);
    set_motion(0,0,0,0);set_motion(0,1,255,0);set_motion(1,0,40,0);
    watch_motion[0][0]=(sentinel_watch_motion_t){.flags=1};
    motion_error[0]=ESP_ERR_TIMEOUT;motion_failure_index=1;
    cerberus_remote_id_bank_t retained=remote_id_live->inventory.banks[0];
    cerberus_remote_id_record_t latest=remote_id_live->inventory.selected.latest;
    tick();tick();
    CHECK(!memcmp(&remote_id_live->inventory.banks[0],&retained,sizeof(retained)));
    CHECK(!memcmp(&remote_id_live->inventory.selected.latest,&latest,sizeof(latest)));
    CHECK(!memcmp(&remote_id_live->inventory.selected.frozen,&frozen,sizeof(frozen)));
    CHECK(remote_id_live->session.radios[0].error==ESP_ERR_TIMEOUT&&remote_id_live->session.radios[0].published_sweeps==1);
    CHECK(remote_id_live->session.radios[1].published_sweeps==2&&remote_id_live->inventory.banks[1].motions[0].speed_qms==40);
    CHECK(!legacy_observed_calls&&!legacy_row_calls);
    motion_error[0]=0;tick();
    CHECK(remote_id_live->session.radios[0].published_sweeps==2);
    CHECK(remote_id_live->inventory.selected.latest.motion.flags==1);
    CHECK(!strcmp(remote_id_live->cache.painted.detail[6],"UNAVAILABLE"));
    CHECK(!strcmp(remote_id_live->cache.painted.detail[7],"DIRECTION UNAVAILABLE"));
    CHECK(!strcmp(remote_id_live->cache.painted.detail[8],"HEIGHT UNAVAILABLE"));
    CHECK(!memcmp(&remote_id_live->inventory.selected.frozen,&frozen,sizeof(frozen)));
    CHECK(!legacy_observed_calls&&!legacy_row_calls);stop_and_exit();
    /* Explicit older-peer path: only C5 extras unavailable, local BLE stays rich. */
    priority_reset();peer.firmware_version=CERBERUS_WATCH_MOTION_MIN_FIRMWARE-1;
    enter();start_scan();priority_complete(1);complete(1,1);set_motion(0,0,1017,100);set_motion(1,0,255,300);
    tick();tick();
    CHECK(!remote_id_live->inventory.banks[0].motions[0].flags&&!remote_id_live->inventory.banks[0].motion_timings[0].valid);
    CHECK(remote_id_live->inventory.banks[1].motions[0].speed_qms==255&&remote_id_live->inventory.banks[1].motion_timings[0].valid);
    tap(20,150);tap(270,440);CHECK(strstr(remote_id_live->cache.painted.detail[11],"NO LOCATION GROUP"));
    CHECK(!legacy_observed_calls&&!legacy_row_calls);stop_and_exit();
}

static void motion_navigation_is_radio_free(void)
{
    priority_reset();peer.firmware_version=CERBERUS_WATCH_MOTION_MIN_FIRMWARE;
    enter();start_scan();priority_complete(2);complete(1,1);
    set_motion(0,0,1017,9000);set_motion(0,1,258,8000);set_motion(1,0,255,7000);
    tick();tick();tap(185,440);tick();CHECK(cerberus_remote_id_session_released(&remote_id_live->session));
    unsigned quiet=all_radio_calls();tap(20,150);
    CHECK(remote_id_live->detail&&!remote_id_live->motion_detail&&all_radio_calls()==quiet);
    cerberus_remote_id_record_t frozen=remote_id_live->inventory.selected.frozen;
    tap(270,440);CHECK(remote_id_live->motion_detail&&all_radio_calls()==quiet);
    tap(270,440);CHECK(!remote_id_live->motion_detail&&remote_id_live->detail&&all_radio_calls()==quiet);
    tap(270,440);tap(20,440);CHECK(!remote_id_live->motion_detail&&remote_id_live->detail&&all_radio_calls()==quiet);
    CHECK(!memcmp(&remote_id_live->inventory.selected.frozen,&frozen,sizeof(frozen)));
    /* An old painted action cannot toggle a newly selected foreign source. */
    cerberus_remote_id_page_t records;CHECK(cerberus_remote_id_inventory_page(&remote_id_live->inventory,0,&records));
    CHECK(cerberus_remote_id_inventory_select(&remote_id_live->inventory,&records.rows[1]));
    tap(270,440);CHECK(!remote_id_live->motion_detail&&all_radio_calls()==quiet);
    CHECK(remote_id_draw(true)==ESP_OK);tap(270,440);CHECK(remote_id_live->motion_detail&&all_radio_calls()==quiet);
    tap(100,440);CHECK(remote_id_live->follow_enabled&&remote_id_live->session.running);
    CHECK(all_radio_calls()==quiet); /* Follow only queues the explicit start. */
    tick();CHECK(priority_starts==2&&ble_starts==2&&priority_request.transmitter[5]==1);
    CHECK(remote_id_live->motion_detail&&remote_id_live->cache.painted.follow_enabled);
    quiet=all_radio_calls();tap(20,440);
    CHECK(remote_id_live->detail&&!remote_id_live->motion_detail&&remote_id_live->follow_enabled&&all_radio_calls()==quiet);
    tap(270,440);tap(270,440);CHECK(remote_id_live->follow_enabled&&all_radio_calls()==quiet);
    tap(20,440);CHECK(!remote_id_live->detail&&!remote_id_live->follow_enabled&&!remote_id_live->inventory.selected.valid&&all_radio_calls()==quiet);
    tap(20,150);tap(270,440);CHECK(remote_id_live->motion_detail&&all_radio_calls()==quiet);
    tap(185,440);tick();CHECK(!remote_id_live->session.running&&priority_stops==1&&ble_stops==1);
    CHECK(remote_id_live->detail&&remote_id_live->motion_detail);stop_and_exit();
}

int main(void)
{
    menu_allocation_and_no_automatic_rf();acknowledged_start_stop_and_host_drain();
    rejected_legacy_and_foreign_work();stop_rechecks_exact_identity();
    publication_frozen_tap_and_snapshot_failure();offscreen_and_faulted_cleanup();
    observed_timing_routing_and_retry();
    priority_start_windows_and_identity();priority_start_uncertainty();
    priority_lease_cleanup_and_foreign_identity();priority_atomic_publication_guards();
    priority_peer_change_between_poll_and_control();
    priority_completion_between_status_and_renew();
    priority_lost_ack_terminal_first_poll();priority_diagnostics_publication();
    follow_explicit_start_and_next_target();follow_search_and_fresh_channel();
    follow_compatibility_foreign_and_uncertain();follow_painted_action_and_ble_selection();
    follow_owned_cleanup_never_reports_ready();
    detailed_adapter_atomic_retry();motion_navigation_is_radio_free();
    printf("Remote ID production live adapter/routes: %u checks; store=%zu bytes\n",checks,sizeof(remote_id_live_store_t));
    return 0;
}
