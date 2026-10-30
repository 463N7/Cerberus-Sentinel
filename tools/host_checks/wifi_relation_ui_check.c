#include <stdint.h>
static void relation_text(uint16_t x,uint16_t y,const char *text,uint8_t scale);
static void relation_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h);
#include "relation_framebuffer.h"
esp_err_t cerberus_display_draw_mono_bitmap(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *data,uint16_t fg,uint16_t bg)
{
    bounds(x,y,w,h);
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)
        screen[(y+row)*320+x+col]=(data[row*((w+7U)/8U)+col/8U]&(0x80U>>(col%8U)))?fg:bg;
    return ESP_OK;
}
#include "c5_spi_link.h"
#include "ui_widgets.h"
#include "wifi_target_view.h"
#include "sentinel_scan_session.h"
#include "sentinel_scan_view.h"
#include "relation_route_types.h"
#include "recon_db.h"
#include <limits.h>
#define UI_BACKGROUND 0x0042
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define UI_PANEL 0x08A4
#define UI_BAD 0xFFFF
#define UI_GOOD 0x3F1B
#define UI_HEADER_HEIGHT 48
#define UI_BACK_Y 420
#define UI_BACK_HEIGHT 44
#define TAG "host"
#define MALLOC_CAP_SPIRAM 4U
#define MALLOC_CAP_8BIT 2U
#define ESP_ERROR_CHECK(call) assert((call)==ESP_OK)
static const cerberus_ui_widget_theme_t theme={0x0042,0x08A4,0x3F1B,0xE77F,0x7CB5};
#define UI_WIDGET_THEME (&theme)
static ui_wifi_mode_t wifi_mode;
static ui_page_t current_page=UI_PAGE_WIFI,tool_parent_page=UI_PAGE_MAIN;
static unsigned allocations, frees, starts, steps, cancels, station_entries, writes;
static unsigned launches, scan_starts, body_back_labels;
static char footer_text[32];
static uint8_t footer_scale;
static uint16_t footer_x,footer_width;
static void relation_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{if(y==UI_BACK_Y&&h==UI_BACK_HEIGHT){footer_x=x;footer_width=w;}}
static void relation_text(uint16_t x,uint16_t y,const char *text,uint8_t scale)
{
    if(y>=UI_BACK_Y&&y<UI_BACK_Y+UI_BACK_HEIGHT){snprintf(footer_text,sizeof(footer_text),"%s",text);footer_scale=scale;assert(x>=8U&&x+strlen(text)*6U*scale<=312U);}
    else if(!strcmp(text,"BACK TO CLIENTS"))++body_back_labels;
}
static bool ui_capability_blocked,learning_tool_active,wifi_attack_selecting,wifi_selection_valid=true;
static const char *ui_capability_blocked_reason;
static unsigned ble_mode,ieee802154_mode;
static unsigned dashboard_mode,file_mode,qr_mode,usb_mode,log_mode,settings_mode,self_test_mode,script_mode;
static unsigned touch_calibration_step;
static bool settings_reset_confirmation,settings_restore_confirmation,ap_station_selecting,ble_attack_selecting;
static bool packet_monitor_from_target;
static sentinel_scan_session_t live_session;
static struct {bool captured,valid;uint8_t saved_generation;int error;} live_records[SENTINEL_SCAN_TOOL_COUNT];
static cerberus_wifi_scan_status_payload_t wifi_scan_status={.state=CERBERUS_WIFI_SCAN_COMPLETE};
static uint32_t live_last_poll;
static bool allocation_fail, masked, c5_online=true;
static uint64_t clock_us=UINT64_C(5000000);
static cerberus_c5_status_t c5_status={.firmware_version=CERBERUS_WIFI_RELATION_MIN_FIRMWARE,
    .capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ|CERBERUS_C5_CAP_WIFI_5_GHZ|CERBERUS_C5_CAP_WIFI_STATIONS};
static cerberus_c5_wifi_result_t wifi_selected_result={.ssid="Cerberus AP",.bssid={2,0,0,0,0,1},.channel=6};
static struct { recon_db_result_t result; char name[64]; } vendor_cache;
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps)
{ assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)); ++allocations; return allocation_fail?NULL:calloc(n,size); }
static void heap_caps_free(void *p){if(p)++frees;free(p);}
int64_t esp_timer_get_time(void){return (int64_t)clock_us;}
static bool privacy_addresses_masked(void){return masked;}
static void cerberus_privacy_format_wifi_identifier(const uint8_t *m,bool hide,char *out,size_t cap)
{if(hide)snprintf(out,cap,"PRIVATE ADDRESS");else snprintf(out,cap,"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]);}
static void refresh_selected_vendor(bool ble,const uint8_t *mac,uint8_t type,int64_t now)
{(void)mac;(void)type;(void)now;assert(!ble);vendor_cache.result=RECON_DB_PRIVATE_ADDRESS;}
static const char *esp_err_to_name(esp_err_t e){return e==ESP_ERR_NO_MEM?"ESP_ERR_NO_MEM":"ESP_ERR_INVALID_STATE";}
static esp_err_t draw_centered(uint16_t y,const char *text,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320-strlen(text)*6U*scale)/2U),y,text,color,UI_BACKGROUND,scale);}
static esp_err_t draw_small_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *label)
{return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){x,y,w,h},label,1,true,UI_WIDGET_THEME);}
static esp_err_t draw_detail_page(void);
static esp_err_t draw_current_page(void){return draw_detail_page();}
static void return_to_launcher(void){++launches;current_page=tool_parent_page;}
static int unexpected_call(void){assert(!"Unrelated route called");return 0;}
static bool live_supported(sentinel_scan_tool_t tool){return tool==SENTINEL_SCAN_WIFI;}
static sentinel_scan_tool_t watch_requested_tool(void){assert(false);return SENTINEL_SCAN_NONE;}
static bool ble_discovery_page(void){return false;}
static void ble_discovery_toggle(void){assert(false);}
static sentinel_scan_session_t *live_session_for(sentinel_scan_tool_t tool){assert(tool==SENTINEL_SCAN_WIFI);return &live_session;}
static bool live_scan_paused(void){return live_session.paused||live_session.faulted;}
static const char *live_scan_action_label(void){return live_session.completed?"SCAN AGAIN":"SCAN";}
static const char *tools_group_title(void){return "TOOLS";}
typedef struct {const char *label;} ui_menu_item_t;
static const ui_menu_item_t *find_item(ui_page_t page){static const ui_menu_item_t item={"WI-FI"};assert(page==UI_PAGE_WIFI);return &item;}
#include "relation_live_helpers.h"
static sentinel_scan_view_t live_view;
static esp_err_t draw_live_scan(void)
{live_view=(sentinel_scan_view_t){.kind=SENTINEL_VIEW_WIFI,.state=live_session.completed?SENTINEL_VIEW_COMPLETE:SENTINEL_VIEW_LIVE,
    .subtitle="PASSIVE SCAN",.page_count=1};return sentinel_scan_view_draw(&live_view,0);}
#include "relation_scan_controls.h"
static esp_err_t draw_wifi_target(void)
{cerberus_wifi_target_view_t v={.selected=true,.ssid=wifi_selected_result.ssid,.identifier="02:00:00:00:00:01",
    .metadata="CH 6",.station_label="OBSERVED CLIENTS",.can_scan_stations=true};return cerberus_wifi_target_draw(&v,UI_WIDGET_THEME);}
static void wifi_target_handle_tap(uint16_t x,uint16_t y);
static void station_enter(void){++station_entries;}
static char written[8192];
esp_err_t cerberus_storage_write_wifi_report(const char *csv,size_t length,char *name,size_t cap)
{assert(length<sizeof(written));memcpy(written,csv,length);written[length]=0;snprintf(name,cap,"test.csv");++writes;return ESP_OK;}

static cerberus_c5_wifi_relation_bank_t response;
static cerberus_wifi_relation_row_t rows[32];
static esp_err_t step_error;
static unsigned remaining_steps=2;
esp_err_t c5_spi_link_wifi_relation_begin_expected(cerberus_c5_wifi_relation_reader_t *r,const cerberus_c5_wifi_relation_bank_t *expected,
    uint32_t deadline,cerberus_wifi_relation_row_t *out,size_t capacity)
{assert(capacity==32&&deadline>(uint32_t)(clock_us/1000));assert(expected->status.epoch==response.status.epoch);
    memset(r,0,sizeof(*r));r->rows=out;r->generation=expected->status.generation;++starts;return ESP_OK;}
esp_err_t c5_spi_link_wifi_relation_step(cerberus_c5_wifi_relation_reader_t *r,
    cerberus_c5_wifi_relation_bank_t *proof,bool *complete,cerberus_c5_relation_continue_fn fn,void *ctx)
{assert(!fn&&!ctx);++steps;*complete=false;if(step_error)return step_error;if(--remaining_steps)return ESP_OK;
    *proof=response;memcpy(r->rows,rows,sizeof(rows));*complete=true;return ESP_OK;}
void c5_spi_link_wifi_relation_cancel(cerberus_c5_wifi_relation_reader_t *r)
{++cancels;memset(r,0,sizeof(*r));}
esp_err_t c5_spi_link_get_wifi_relation_bank(uint8_t gen,uint32_t deadline,cerberus_c5_wifi_relation_bank_t *proof)
{assert(gen==response.status.generation&&deadline==(uint32_t)(clock_us/1000)+100U);*proof=response;return ESP_OK;}
esp_err_t c5_spi_link_check_wifi_relation_bank(const cerberus_c5_wifi_relation_bank_t *proof,uint32_t deadline)
{assert(proof->status.epoch==response.status.epoch&&deadline==(uint32_t)(clock_us/1000)+100U);return ESP_OK;}
#include "wifi_relation_ui.h"
static void wifi_target_handle_tap(uint16_t x,uint16_t y)
{cerberus_wifi_target_view_t v={.selected=true,.can_scan_stations=true};
    cerberus_wifi_target_action_t action=cerberus_wifi_target_tap(&v,x,y);
    if(action==CERBERUS_WIFI_TARGET_STATIONS)wifi_relation_open();
    else if(action==CERBERUS_WIFI_TARGET_CHANGE)wifi_mode=UI_WIFI_MODE_LIST;
    else assert(action==CERBERUS_WIFI_TARGET_NONE);}
#include "relation_unused_routes.h"
#include "relation_parent_impl.h"
static void publish(uint8_t gen)
{
    cerberus_c5_wifi_relation_bank_t proof;
    assert(wifi_relation_capture_before(gen,&proof)==ESP_OK);
    assert(wifi_relation_capture_after(&proof)==ESP_OK);
    wifi_relation_publish(gen,&proof,ESP_OK);
}

static void snapshot(const char *prefix,const char *name)
{
    memset(screen,0,sizeof(screen));footer_text[0]=0;assert(draw_detail_page()==ESP_OK);
    assert(!strcmp(footer_text,wifi_mode==UI_WIFI_MODE_TARGET?"BACK TO SCAN":
        wifi_relation_ui&&wifi_relation_ui->detail?"BACK TO CLIENTS":"BACK TO AP"));
    assert(footer_scale==2U&&!body_back_labels);
    assert(footer_x==(wifi_mode==UI_WIFI_MODE_TARGET?64U:8U));
    assert(footer_width==(wifi_mode==UI_WIFI_MODE_TARGET?192U:304U));
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320U*480U;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}
    assert(!fclose(f));
}
static void fixture(unsigned count,uint8_t gen)
{
    memset(rows,0,sizeof(rows));response=(cerberus_c5_wifi_relation_bank_t){
        .status={.epoch=UINT64_C(0x100000007),.generation=gen,.count=(uint8_t)count,
            .state=CERBERUS_WIFI_RELATION_COMPLETE,.flags=CERBERUS_WIFI_RELATION_AVAILABLE,.matched_frames=count*3U},
        .request_started_us=clock_us-100U,.source_now_us=1000000U,
        .firmware_version=60,.capabilities=c5_status.capabilities,.protocol_version=CERBERUS_PROTOCOL_VERSION,.clock_valid=true};
    for(unsigned i=0;i<count;++i){rows[i]=(cerberus_wifi_relation_row_t){.station={2,0,0,0,1,(uint8_t)(i+1U)},
        .channel=6,.directions=3,.latest_direction=2,.rssi_valid=3,.to_ap_frames=1,.from_ap_frames=2,
        .to_ap_callback_us=900000,.from_ap_callback_us=950000,.to_ap_rssi=-80,.from_ap_rssi=-30};
        memcpy(rows[i].bssid,wifi_selected_result.bssid,6);}
    remaining_steps=2;step_error=ESP_OK;
}
static void finish(void)
{
    unsigned old=steps;wifi_relation_tick((uint32_t)(clock_us/1000),true,true);assert(steps==old+1U&&wifi_relation_ui->reading);
    wifi_relation_tick((uint32_t)(clock_us/1000),true,true);assert(steps==old+2U&&!wifi_relation_ui->reading);
}
static void check_composite_navigation(const char *prefix)
{
    const char *names[]={"pending-footer","loading-footer","ready-footer","detail-footer","empty-footer","error-footer","oom-footer"};
    for(unsigned state=0;state<7U;++state){
        current_page=UI_PAGE_WIFI;allocation_fail=false;
        fixture(state==4U?0U:1U,(uint8_t)(20U+state));publish((uint8_t)(20U+state));
        if(state==6U){free(wifi_relation_ui);wifi_relation_ui=NULL;allocation_fail=true;}
        wifi_relation_bind_selection(true);wifi_relation_open();
        if(state==1U)wifi_relation_tick((uint32_t)(clock_us/1000),true,true);
        if(state>=2U&&state<=4U)finish();
        if(state==3U){handle_tap(160,160);assert(wifi_relation_ui->detail);}
        if(state==5U){step_error=ESP_ERR_TIMEOUT;wifi_relation_tick((uint32_t)(clock_us/1000),true,true);}
        snapshot(prefix,names[state]);
        unsigned before_steps=steps,before_starts=starts,before_launches=launches;
        handle_tap(320,442);handle_tap(160,480);handle_tap(160,419);
        assert(wifi_mode==UI_WIFI_MODE_OBSERVED_CLIENTS&&steps==before_steps&&starts==before_starts);
        if(state==3U){
            handle_tap(160,390);assert(wifi_relation_ui->detail); /* Blank body has no hidden Back. */
            handle_tap(8,420);assert(!wifi_relation_ui->detail&&wifi_mode==UI_WIFI_MODE_OBSERVED_CLIENTS);
            snapshot(prefix,"detail-return-clients");
        }
        handle_tap(311,463);assert(wifi_mode==UI_WIFI_MODE_TARGET&&!wifi_relation_queued);
        assert(!wifi_relation_ui||!wifi_relation_ui->reading);
        assert(steps==before_steps&&starts==before_starts&&launches==before_launches);
        snapshot(prefix,"target-footer");
        /* Actual cached-entry policy + actual parent Back must not arm RF. */
        sentinel_scan_session_init(&live_session);
        live_records[SENTINEL_SCAN_WIFI].captured=live_records[SENTINEL_SCAN_WIFI].valid=true;
        live_records[SENTINEL_SCAN_WIFI].saved_generation=wifi_scan_status.generation;
        actual_scan_page_selection();assert(live_session.desired==SENTINEL_SCAN_NONE);
        handle_tap(160,442);assert(wifi_mode==UI_WIFI_MODE_LIST);
        actual_scan_page_selection();assert(live_session.completed&&!live_session.paused);
        sentinel_scan_command_t command=sentinel_scan_session_step(&live_session,1000U,
            (sentinel_scan_observation_t){.valid=true,.tool=SENTINEL_SCAN_NONE,.state=SENTINEL_SCAN_COMPLETE});
        assert(command.kind==SENTINEL_SCAN_COMMAND_NONE);
        assert(draw_detail_page()==ESP_OK);
        /* Only a real Scan-button hit arms one bounded normal scan. */
        handle_tap(118,385);assert(!live_session.completed&&!live_session.paused);
        command=sentinel_scan_session_step(&live_session,1500U,
            (sentinel_scan_observation_t){.valid=true,.tool=SENTINEL_SCAN_NONE,.state=SENTINEL_SCAN_COMPLETE});
        assert(command.kind==SENTINEL_SCAN_COMMAND_START&&command.tool==SENTINEL_SCAN_WIFI);++scan_starts;
        sentinel_scan_session_ack(&live_session,command,SENTINEL_SCAN_ACCEPTED,1500U);
        command=sentinel_scan_session_step(&live_session,2000U,
            (sentinel_scan_observation_t){.valid=true,.tool=SENTINEL_SCAN_WIFI,.state=SENTINEL_SCAN_RUNNING});
        assert(command.kind==SENTINEL_SCAN_COMMAND_NONE);
        command=sentinel_scan_session_step(&live_session,2500U,
            (sentinel_scan_observation_t){.valid=true,.tool=SENTINEL_SCAN_WIFI,.state=SENTINEL_SCAN_COMPLETE});
        assert(command.kind==SENTINEL_SCAN_COMMAND_NONE&&live_session.completed);
        assert(steps==before_steps&&starts==before_starts); /* No old relation-reader work. */
        allocation_fail=false;
    }
    assert(scan_starts==7U);
    /* A fresh AP publication after the explicit scan opens fresh client rows. */
    fixture(1,40);rows[0].station[5]=99;publish(40);wifi_relation_bind_selection(true);
    wifi_mode=UI_WIFI_MODE_TARGET;handle_tap(160,277);assert(wifi_mode==UI_WIFI_MODE_OBSERVED_CLIENTS);
    finish();assert(wifi_relation_ui->selected.count==1&&wifi_relation_ui->selected.bank.rows[0].station[5]==99);
    snapshot(prefix,"rescan-return-clients");
}
int main(int argc,char **argv)
{
    assert(argc==2);assert(wifi_relation_supported());fixture(32,255);
    publish(255);assert(wifi_relation_publication==1&&allocations==0);
    wifi_relation_bind_selection(true);wifi_relation_open();assert(allocations==1&&wifi_relation_ui);
    snapshot(argv[1],"loading");finish();
    assert(wifi_relation_ui->selected.count==32&&wifi_relation_ui->selected.bank.outcome==CERBERUS_S3_WIFI_RELATION_READY);
    assert(!station_entries&&!writes);snapshot(argv[1],"list");
    wifi_selected_result.bssid[5]=9;wifi_relation_open();assert(!wifi_relation_ui->selected.valid);
    wifi_selected_result.bssid[5]=1;wifi_selected_result.channel=11;wifi_relation_open();assert(!wifi_relation_ui->selected.valid);
    wifi_selected_result.channel=6;wifi_relation_open();assert(wifi_relation_ui->selected.count==32);
    handle_tap(20,151);assert(wifi_relation_ui->detail&&wifi_relation_ui->detail_index==0);
    snapshot(argv[1],"detail");masked=true;snapshot(argv[1],"private");masked=false;
    assert(rows[0].to_ap_rssi==-80&&rows[0].from_ap_rssi==-30);
    handle_tap(160,442);assert(!wifi_relation_ui->detail&&wifi_mode==UI_WIFI_MODE_OBSERVED_CLIENTS);
    wifi_relation_tap(100,390);assert(wifi_relation_ui->page==1);wifi_relation_tap(20,151);assert(wifi_relation_ui->detail_index==4);
    handle_tap(10,380);assert(wifi_relation_ui->detail); /* Removed duplicate body action. */
    handle_tap(160,442);assert(!wifi_relation_ui->detail);
    wifi_relation_tap(170,390);assert(writes==1&&strstr(written,"station_rssi,ap_rssi")&&strstr(written,",-80,-30,900000,950000,2\n"));
    assert(allocations==2&&frees==1); /* Persistent PSRAM store, one temporary report. */
    char buffer[8192];assert(!wifi_relation_csv(buffer,50,&wifi_relation_ui->selected));
    for(unsigned i=0;i<32;++i){cerberus_wifi_relation_row_t *r=&wifi_relation_ui->selected.bank.rows[i];
        r->to_ap_frames=r->from_ap_frames=UINT32_MAX;r->to_ap_callback_us=r->from_ap_callback_us=INT64_MAX;
        r->to_ap_rssi=r->from_ap_rssi=-127;r->channel=177;}
    size_t max_length=wifi_relation_csv(buffer,sizeof(buffer),&wifi_relation_ui->selected);
    assert(max_length>3000U&&max_length<sizeof(buffer));
    /* A new publication cannot change the displayed frozen rows or their age anchor. */
    cerberus_s3_wifi_relation_selection_t frozen=wifi_relation_ui->selected;
    fixture(0,0);publish(0);assert(wifi_relation_publication==2);
    finish();assert(!memcmp(&frozen,&wifi_relation_ui->selected,sizeof(frozen)));
    wifi_relation_bind_selection(true);wifi_relation_open();assert(!wifi_relation_ui->selected.count&&
        wifi_relation_ui->selected.bank.outcome==CERBERUS_S3_WIFI_RELATION_READY);snapshot(argv[1],"empty");
    wifi_relation_bind_selection(false);wifi_relation_open();assert(!wifi_relation_ui->selected.valid);
    snapshot(argv[1],"unbound");unsigned old_starts=starts;wifi_relation_retry();assert(starts==old_starts&&!wifi_relation_queued);
    /* Same eight-bit generation after wrap is a new publication, never old rows. */
    fixture(1,0);publish(0);wifi_relation_bind_selection(true);wifi_relation_open();
    wifi_relation_tick((uint32_t)(clock_us/1000),true,true);assert(wifi_relation_ui->reading);
    unsigned old_steps=steps;wifi_relation_tick((uint32_t)(clock_us/1000),false,false);
    assert(!wifi_relation_ui->reading&&steps==old_steps&&wifi_relation_ui->selected.bank.outcome==CERBERUS_S3_WIFI_RELATION_CANCELLED);
    for(unsigned i=0;i<5;++i)wifi_relation_tick((uint32_t)(clock_us/1000),true,true);
    assert(steps==old_steps); /* Visibility return never silently retries. */
    wifi_relation_retry();remaining_steps=2;step_error=ESP_ERR_TIMEOUT;
    wifi_relation_tick((uint32_t)(clock_us/1000),true,true);assert(!wifi_relation_ui->reading&&
        wifi_relation_ui->selected.bank.outcome==CERBERUS_S3_WIFI_RELATION_FAILED);snapshot(argv[1],"error");
    old_steps=steps;wifi_relation_tick((uint32_t)(clock_us/1000),true,true);assert(steps==old_steps);
    wifi_relation_retry();fixture(1,0);finish();assert(wifi_relation_ui->selected.count==1);
    wifi_relation_tap(20,330);assert(station_entries==1&&wifi_mode==UI_WIFI_MODE_STATIONS);
    wifi_relation_open();handle_tap(160,442);assert(wifi_mode==UI_WIFI_MODE_TARGET);
    free(wifi_relation_ui);wifi_relation_ui=NULL;allocation_fail=true;
    fixture(1,1);publish(1);wifi_relation_bind_selection(true);wifi_relation_open();
    unsigned old_allocations=allocations;wifi_relation_tick((uint32_t)(clock_us/1000),true,true);
    wifi_relation_tick((uint32_t)(clock_us/1000),true,true);assert(allocations==old_allocations);
    snapshot(argv[1],"no-memory");allocation_fail=false;wifi_relation_retry();fixture(1,1);finish();
    assert(wifi_relation_ui->selected.valid&&wifi_relation_ui->selected.count==1);
    wifi_relation_publish(2,NULL,ESP_ERR_TIMEOUT);wifi_relation_bind_selection(true);wifi_relation_open();
    assert(wifi_relation_ui->selected.bank.outcome==CERBERUS_S3_WIFI_RELATION_FAILED&&!wifi_relation_queued);
    old_starts=starts;wifi_relation_retry();wifi_relation_tick((uint32_t)(clock_us/1000),true,true);
    assert(starts==old_starts&&!wifi_relation_queued); /* Failed capture proof forbids generation-only retry. */
    check_composite_navigation(argv[1]);
    wifi_relation_publication=UINT64_MAX;wifi_relation_publish(2,NULL,ESP_ERR_TIMEOUT);assert(wifi_relation_exhausted&&!wifi_relation_publication&&!wifi_relation_queued);
    wifi_relation_publish(3,NULL,ESP_ERR_TIMEOUT);assert(!wifi_relation_publication);
    free(wifi_relation_ui);puts("Actual passive relationship UI: composite parent header/body/footer and dispatch; seven pending/loading/ready/detail/empty/error/OOM Back paths, 2x full-width labels, body Back removed, cached list return and seven explicit-only scan starts; one-step reads, immutable AP selection, generation wrap, cancellation, allocation recovery, per-direction signal, privacy and bounded CSV passed.");
}
