/* Reuse the scanner/host fakes and actual frozen-selection lifecycle. */
#pragma push_macro("main")
#undef main
#define main existing_monitor_suite
#include "s3_findmy_monitor_ui_check.c"
#undef main
#pragma pop_macro("main")
#include "scan_display_store.h"
#include "s3_ble_list.h"
#include "s3_ble_recent.h"
#include "sentinel_scan_session.h"
#include "sentinel_scan_view.h"
#include "ble_analyzer.h"
#include "ble_flock_oui.h"
#include "discovery_live_types.h"
#include "s3_ble_discovery_session.h"
#include "s3_ble_discovery_inventory.h"

enum { UI_WIFI_MODE_LIST, UI_WIFI_MODE_MENU, UI_WIFI_MODE_BASELINE, UI_WIFI_MODE_STATS,
    UI_WIFI_MODE_DETAIL, UI_WIFI_MODE_TARGET, UI_WIFI_MODE_PASSWORD, UI_WIFI_MODE_CONNECTION,
    UI_WIFI_MODE_STATIONS, UI_WIFI_MODE_OBSERVED_CLIENTS, UI_WIFI_MODE_AP_JOB };
enum { UI_IEEE802154_MODE_ENERGY, UI_IEEE802154_MODE_BEACONS, UI_IEEE802154_MODE_ACTIVITY,
    UI_IEEE802154_MODE_MENU, UI_IEEE802154_MODE_ASSESSMENT };
#define UI_PAGE_FIELD_MENU 90
static struct { bool attack; } field_menu;
static unsigned wifi_mode,ieee802154_mode,wifi_target_count,wifi_selected_index,wifi_analysis_generation;
static bool wifi_attack_selecting,wifi_selection_valid,c5_online=true,live_radio_busy;
static esp_err_t wifi_analysis_error,wifi_trust_error,wifi_connection_error;
static cerberus_c5_wifi_result_t wifi_selected_result;
static unsigned relation_selection_calls;
static bool relation_selection_ordinary;
static cerberus_c5_wifi_result_t relation_selected_ap;
static void wifi_relation_bind_selection(bool ordinary)
{
    assert(current_page==UI_PAGE_WIFI);
    ++relation_selection_calls;relation_selection_ordinary=ordinary;
    relation_selected_ap=wifi_selected_result;
}
static cerberus_scan_display_store_t store,*live_store=&store;
static cerberus_findmy_monitor_record_t observations[32];
static live_scan_record_t live_records[SENTINEL_SCAN_TOOL_COUNT];
static uint32_t live_capture_pending;
static uint8_t live_ble_focus_bits[32],live_ble_indexes[32];
static size_t live_ble_filtered_count;
static bool live_wifi_recent_only;
static uint8_t live_wifi_indexes[32];
static size_t live_wifi_filtered_count;
static cerberus_c5_status_t c5_status;
static uint16_t live_page[SENTINEL_SCAN_TOOL_COUNT];
static bool live_ble_order_dirty;
static bool live_ble_order_discovery;
static uint16_t live_ble_order_count;
static uint64_t live_ble_order_epoch,live_ble_order_revision;
static bool live_ble_recent_only;
static uint32_t live_ble_visible_mask=UINT32_MAX;
static int64_t live_ble_published_ms;
static cerberus_s3_ble_filter_t live_ble_order_filter;
static cerberus_s3_ble_sort_t live_ble_sort,live_ble_order_sort;
static char live_names[4][40],live_details[4][52],live_ble_filter_label[19],live_subtitle[52],live_hint[52];
static uint32_t live_frame,live_last_poll;
static int8_t live_energy[16];
static uint8_t live_activity[16][CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
static sentinel_scan_view_t live_view;
static sentinel_scan_session_t session;
#define live_ble_session session
static sentinel_scan_session_t live_session;
static bool live_ble_session_initialized,ui_capability_blocked,live_scan_observation_valid,live_ble_observation_valid;
static cerberus_wifi_scan_status_payload_t wifi_scan_status;
static struct { uint8_t state; } wifi_defense_status;
static unsigned live_commands,live_observations;
static esp_err_t live_command_error;
static bool watch_ble_requested(void){return false;}
static bool live_observe(uint32_t now){(void)now;++live_observations;return false;}
static esp_err_t live_execute(sentinel_scan_command_t command){(void)command;++live_commands;return live_command_error;}
static esp_err_t refresh_wifi_defense_survey(void){return ESP_OK;}
static sentinel_scan_tool_t live_page_tool(void){return current_page==UI_PAGE_BLE&&ble_mode==UI_BLE_MODE_LIST?SENTINEL_SCAN_BLE:
    current_page==UI_PAGE_WIFI&&wifi_mode==UI_WIFI_MODE_LIST?SENTINEL_SCAN_WIFI:SENTINEL_SCAN_NONE;}
static sentinel_scan_session_t *live_session_for(sentinel_scan_tool_t tool){return tool==SENTINEL_SCAN_BLE?&session:&live_session;}
typedef enum {CERBERUS_WIFI_TRUST_UNKNOWN,CERBERUS_WIFI_TRUST_EXACT,CERBERUS_WIFI_TRUST_MISMATCH} cerberus_wifi_trust_state_t;
static cerberus_wifi_trust_state_t cerberus_settings_wifi_trust_state(const char *s,const uint8_t *b,uint8_t security)
{(void)s;(void)b;(void)security;return CERBERUS_WIFI_TRUST_UNKNOWN;}
static const char *wifi_security_text(uint8_t security){(void)security;return "OPEN";}
static bool cerberus_settings_ble_is_favorite(const uint8_t *address,uint8_t type){(void)address;(void)type;return false;}
#define wifi_targets_find(...) (-1)
#define wifi_targets_clear() ((void)0)
#define wifi_target_open_attacks() ((void)0)
#define wifi_target_toggle_selection(...) true
#define wifi_target_finish_selection() ((void)(wifi_mode=UI_WIFI_MODE_DETAIL))
#define c5_spi_link_get_wifi_connection_status(...) ESP_OK
static bool central_active;
static bool ble_connection_is_active(void){return central_active;}
static esp_err_t draw_current_page(void){return ESP_OK;}
static void return_to_launcher(void){current_page=UI_PAGE_OTHER;}
static bool ble_analyzer_exit_pending,ble_analyzer_retry_requested,ble_analyzer_start_pending;
static bool ble_analyzer_started;
static esp_err_t ble_analyzer_error;
static uint32_t ble_analyzer_start_requested_ms,ble_analyzer_exit_started_ms,ble_analyzer_last_tick_ms;
static bool ble_fox_hunt_exit_pending,ble_fox_hunt_retry_requested;
static esp_err_t ble_fox_hunt_error;
static uint32_t ble_fox_hunt_exit_started_ms,ble_fox_hunt_last_tick_ms,ble_fox_hunt_last_draw_ms;
static ui_page_t ble_session_exit_page,tool_parent_page;
static bool learning_tool_active;
#define UI_CAP_BLE_GATT 1U
static unsigned ui_capability_map=UI_CAP_BLE_GATT;
static void ble_sound_open(bool all){(void)all;assert(!"Unexpected Sound action in observational test");}
static unsigned track_begins,track_ends,passive_starts;
static uint8_t track_address[6],track_type;
static esp_err_t track_error;
static cerberus_ble_analyzer_view_t analyzer_view;
static esp_err_t cerberus_s3_ble_scanner_analyzer_status(cerberus_ble_analyzer_view_t *out)
{*out=analyzer_view;return ESP_OK;}
static esp_err_t draw_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *s)
{return draw_small_button(x,y,w,h,s);}
static esp_err_t cerberus_s3_ble_scanner_track_begin(const uint8_t *address,uint8_t type)
{++track_begins;if(track_error)return track_error;memcpy(track_address,address,6);track_type=type;return ESP_OK;}
static void cerberus_s3_ble_scanner_track_end(void){++track_ends;}
static esp_err_t cerberus_s3_ble_scanner_start(cerberus_ble_scan_status_payload_t *status)
{++passive_starts;assert(owner==CERBERUS_S3_BLE_OWNER_NONE);owner=CERBERUS_S3_BLE_OWNER_SCANNER;scan.state=CERBERUS_BLE_SCAN_RUNNING;++scan.generation;*status=scan;return ESP_OK;}
esp_err_t cerberus_display_draw_mono_bitmap(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *data,uint16_t fg,uint16_t bg)
{
    bounds(x,y,w,h);
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)
        screen[(y+row)*320+x+col]=data[row*((w+7)/8)+col/8]&(0x80>>(col%8))?fg:bg;
    return ESP_OK;
}
#ifdef CERBERUS_DISCOVERY_NATIVE_TEST
#include "s3_ble_discovery_native_fakes.h"
static unsigned discovery_ui_allocations;
static void *discovery_ui_calloc(size_t n,size_t size,unsigned capabilities)
{++discovery_ui_allocations;return heap_caps_calloc(n,size,capabilities);}
#define heap_caps_calloc discovery_ui_calloc
#include "s3_ble_discovery_live_ui.h"
#undef heap_caps_calloc
#else
/* This suite intentionally retains the legacy/frozen-bank presentation path
 * used by selection/Watch consumers. The separate native live suite includes
 * the actual new ordinary-list header and tests its scheduler and ownership. */
#include "discovery_native_types.h"
static ble_discovery_live_store_t *ble_discovery_live;
static bool ble_discovery_page(void){return false;}
static bool ble_discovery_presenting(void){return false;}
static bool ble_discovery_acquire(void){return false;}
static void ble_discovery_leave(void){assert(!"native leave in legacy fixture");}
static void ble_discovery_tick(uint64_t now,bool asleep){(void)now;(void)asleep;}
static void ble_discovery_select_painted(unsigned row){(void)row;assert(!"native selection in legacy fixture");}
static void ble_discovery_toggle(void){assert(!"native toggle in legacy fixture");}
static uint32_t ble_discovery_visible_mask(uint64_t now){(void)now;return UINT32_MAX;}
#endif
#include "discovery_routes_impl.h"

static void seed_list(void)
{
    (void)draw_live_scan; /* Also compiled for the separate ordinary-live entry fixture. */
    reset();memset(observations,0,sizeof(observations));memset(&store,0,sizeof(store));
    memset(live_records,0,sizeof(live_records));memset(live_ble_focus_bits,0,sizeof(live_ble_focus_bits));
    memset(live_page,0,sizeof(live_page));sentinel_scan_session_init(&session);
    session.completed=true;session.desired=SENTINEL_SCAN_BLE;ble_mode=UI_BLE_MODE_LIST;
    sentinel_scan_session_init(&live_session);live_session.completed=true;live_ble_session_initialized=true;
    live_ble_recent_only=false;live_ble_visible_mask=UINT32_MAX;live_ble_published_ms=1200;
    live_commands=live_observations=0;live_last_poll=0;
    live_capture_pending=0;live_command_error=ESP_OK;
    relation_selection_calls=0;relation_selection_ordinary=false;
    live_scan_observation_valid=live_ble_observation_valid=true;
    live_records[SENTINEL_SCAN_BLE]=(live_scan_record_t){.valid=true,.captured=true,.count=4,.saved_generation=7,.state=SENTINEL_SCAN_COMPLETE,.captured_ms=1200};
    const char *names[]={"Zeta","Ordinary","alpha","Beta"};
    const int8_t signal[]={-30,-10,-60,-80};
    for(unsigned i=0;i<4;++i){
        cerberus_findmy_monitor_record_t *r=&observations[i];
        r->peer.address[0]=(uint8_t)(0x30+i);r->peer.address[5]=0x77;r->peer.address_type=(uint8_t)(i%2);
        strcpy(r->peer.name,names[i]);r->peer.rssi=signal[i];r->peer.flags=CERBERUS_BLE_RESULT_CONNECTABLE;
        r->generation=7;r->index=(uint8_t)i;r->observed_ms=100;
        memcpy(r->advertisement.address,r->peer.address,6);r->advertisement.address_type=r->peer.address_type;
        r->advertisement.length=4;r->advertisement.data[0]=3;r->advertisement.data[1]=3;r->advertisement.data[2]=0x82;r->advertisement.data[3]=0x30;
        cerberus_s3_ble_discovery_evidence_t *e=&r->focus.discovery;
        e->generation=7;memcpy(e->address,r->peer.address,6);e->address_type=r->peer.address_type;
        e->advertisement_sequence=1;e->advertisement_observed_ms=100;e->scan_response_sequence=2;e->scan_response_observed_ms=1100;
        e->flags=CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE|CERBERUS_S3_BLE_EVIDENCE_ADV_VALID|CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE|CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
        e->flipper_advertisement=(cerberus_ble_flipper_hints_t){CERBERUS_BLE_FLIPPER_HINT_WHITE,CERBERUS_BLE_FLIPPER_UUID16};
        e->flipper_scan_response=(cerberus_ble_flipper_hints_t){CERBERUS_BLE_FLIPPER_HINT_TRANSPARENT,CERBERUS_BLE_FLIPPER_UUID16|CERBERUS_BLE_FLIPPER_NAME};
        if(i!=1){r->focus.category_bits=CERBERUS_S3_BLE_FOCUS_FLIPPER;live_ble_focus_bits[i]=r->focus.category_bits;}
        store.ble[i]=r->peer;
    }
    observations[3].peer.name[0]='\0';store.ble[3]=observations[3].peer;
    live_ble_evidence=observations;live_ble_order_dirty=true;live_ble_sort=CERBERUS_S3_BLE_SORT_SIGNAL_DESC;
    ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;clock_ms=8100;masked=true;live_make_view();
}

static void check_meta_ui(const char *prefix)
{
    seed_list();track_begins=track_ends=passive_starts=0;
    for(unsigned i=0;i<4;++i){
        cerberus_s3_ble_focus_t *f=&observations[i].focus;
        f->category_bits=0;live_ble_focus_bits[i]=0;
        memset(&f->discovery.flipper_advertisement,0,sizeof(f->discovery.flipper_advertisement));
        memset(&f->discovery.flipper_scan_response,0,sizeof(f->discovery.flipper_scan_response));
    }
    cerberus_s3_ble_focus_t *f=&observations[0].focus;
    assert(cerberus_ble_meta_evidence_add(&f->discovery.meta_advertisement,0xFD5F,CERBERUS_BLE_META_MFG16));
    assert(cerberus_ble_meta_evidence_add(&f->discovery.meta_advertisement,0xFEB7,CERBERUS_BLE_META_LIST128));
    f->category_bits=CERBERUS_S3_BLE_FOCUS_META_HINT;live_ble_focus_bits[0]=f->category_bits;
    f=&observations[1].focus;f->meta_suppressed=true;
    assert(cerberus_ble_meta_evidence_add(&f->discovery.meta_advertisement,0xFD5F,CERBERUS_BLE_META_MFG16));
    /* Earlier blocked evidence suppresses this row even though its current
     * source has an allowed identifier: UI must trust published categories. */
    f=&observations[2].focus;
    assert(cerberus_ble_meta_evidence_add(&f->discovery.meta_scan_response,0xFD5F,CERBERUS_BLE_META_LIST128));
    assert(cerberus_ble_meta_evidence_add(&f->discovery.meta_scan_response,0xFEB7,CERBERUS_BLE_META_DATA16));
    f->category_bits=CERBERUS_S3_BLE_FOCUS_META_HINT;live_ble_focus_bits[2]=f->category_bits;
    observations[2].advertisement.length=3;observations[2].advertisement.data[0]=2;
    observations[2].advertisement.data[1]=1;observations[2].advertisement.data[2]=6;
    ble_filter=CERBERUS_S3_BLE_FILTER_FLOCK;live_ble_order_dirty=true;live_make_view();
    handle_ble_tap(100,65);assert(ble_filter==CERBERUS_S3_BLE_FILTER_META_HINT);
    assert(live_ble_filtered_count==2&&live_ble_indexes[0]==0&&live_ble_indexes[1]==2);
    assert(!strcmp(live_badges[0],"ID PAIRS / ADV 2 / RSP 0"));
    handle_ble_tap(250,65);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_NAME_ASC&&live_ble_indexes[0]==2);
    drawn[0]='\0';assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"meta-list");
    cerberus_findmy_monitor_record_t expected=observations[2];
    handle_ble_tap(20,135);tick(8200);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&ble_selected_scan_index==2&&ble_selected_sound_profile==CERBERUS_FINDMY_PROFILE_NONE);
    assert(!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    memset(observations,0,sizeof(observations));memset(&store,0,sizeof(store));++scan.generation;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV NO META IDENTIFIER PAIRS"));
    assert(strstr(drawn,"RSP FD5F LIST128, FEB7 DATA16 / 7s"));
    assert(!strstr(drawn,"FEB7 LIST128")&&!strstr(drawn,"FD5F DATA16"));save(prefix,"meta-frozen-detail");
    handle_ble_tap(30,300);assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&track_begins==1);
    assert(!memcmp(track_address,expected.peer.address,6)&&track_type==expected.peer.address_type);
    ble_fox_hunt_tick(8500,false);clock_ms=8600;actual_back(440);hold_stop=true;
    ble_fox_hunt_tick(8800,false);assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&ble_fox_hunt_exit_pending);
    owner=CERBERUS_S3_BLE_OWNER_NONE;ble_fox_hunt_tick(9100,false);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&track_ends==1&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    actual_back(440);assert(ble_mode==UI_BLE_MODE_LIST&&ble_filter==CERBERUS_S3_BLE_FILTER_META_HINT);

    /* Preserve all 77 exact pairs while the small display names two and
     * reports +75 MORE. No context is associated with a different ID. */
    cerberus_ble_meta_evidence_t all={0};
    for(size_t id=0;id<CERBERUS_BLE_META_ID_COUNT;++id)
        for(unsigned context=0;context<CERBERUS_BLE_META_CONTEXT_COUNT;++context)
            assert(cerberus_ble_meta_evidence_add(&all,cerberus_ble_meta_id_value(id),(cerberus_ble_meta_context_t)context));
    assert(cerberus_ble_meta_evidence_count(&all)==77);
    ble_selected_focus=(cerberus_s3_ble_focus_t){0};ble_selected_focus.meta_suppressed=true;
    ble_selected_focus.discovery.meta_advertisement=all;ble_selected_focus.discovery.meta_scan_response=all;
    clock_ms=UINT32_MAX;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"META FILTER SUPPRESSED FOR THIS SWEEP"));
    assert(strstr(drawn,"ADV FD5F MFG16, FD5F LIST16 +75 MORE / 4294967s"));save(prefix,"meta-77-pairs");
    assert(cerberus_ble_meta_evidence_count(&ble_selected_focus.discovery.meta_scan_response)==77);
    memset(&ble_selected_focus.discovery,0,sizeof(ble_selected_focus.discovery));
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"SUPPRESSED FOR THIS SWEEP")&&strstr(drawn,"NO META IDENTIFIER PAIRS"));

    /* Actual Analyzer renderer retains stronger Watch text and separately
     * presents the exact latest event source/matrix/time without clipping. */
    analyzer_view=(cerberus_ble_analyzer_view_t){.active=true,.has_last=true,.last_report_valid=true,.last_scan_response=true,
        .last_meta=all,.last_seen_age_ms=UINT32_MAX,.last_rssi_dbm=-127,.last_name_length=32,
        .total_events=UINT32_MAX,.current_events=UINT16_MAX,.malformed_reports=UINT32_MAX,.unclassified_reports=UINT32_MAX,
        .evidence_kind=CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH,.evidence_confidence=SENTINEL_WATCH_CONFIDENCE_HIGH,.evidence_label="FIND MY OFFLINE FINDING"};
    memset(analyzer_view.last_name,'W',sizeof(analyzer_view.last_name));ble_analyzer_started=true;ble_analyzer_start_pending=false;ble_analyzer_error=ESP_OK;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"HINT: FIND MY OFFLINE FINDING"));
    assert(strstr(drawn,"RSP FD5F MFG16, FD5F LIST16 +75 MORE / 4294967s"));
    assert(strstr(drawn,"META FILTER BLOCKED IN THIS REPORT"));save(prefix,"meta-analyzer-max");
    analyzer_view.last_meta=expected.focus.discovery.meta_scan_response;analyzer_view.last_scan_response=false;
    analyzer_view.evidence_kind=CERBERUS_BLE_ANALYZER_EVIDENCE_META_ID_HINT;analyzer_view.evidence_label="META IDENTIFIER";
    analyzer_view.evidence_confidence=SENTINEL_WATCH_CONFIDENCE_LOW;
    analyzer_view.last_seen_age_ms=2100;analyzer_view.last_name_length=0;analyzer_view.total_events=12;
    analyzer_view.malformed_reports=analyzer_view.unclassified_reports=0;analyzer_view.current_events=2;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"ADV FD5F LIST128, FEB7 DATA16 / 2s"));assert(!strstr(drawn,"BLOCKED IN THIS REPORT"));save(prefix,"meta-analyzer");
    analyzer_view.last_report_valid=false;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(!strstr(drawn,"FD5F LIST128"));
}

static void check_ibeacon_ui(const char *prefix)
{
    seed_list();track_begins=track_ends=passive_starts=0;
    for(unsigned i=0;i<4;++i){
        cerberus_s3_ble_focus_t *f=&observations[i].focus;
        f->category_bits=0;live_ble_focus_bits[i]=0;
        memset(&f->discovery.flipper_advertisement,0,sizeof(f->discovery.flipper_advertisement));
        memset(&f->discovery.flipper_scan_response,0,sizeof(f->discovery.flipper_scan_response));
        if(i==1)continue;
        f->ibeacon_valid=true;f->ibeacon_sequence=i==2?2:1;
        f->ibeacon_scan_response=i==2;f->ibeacon_observed_ms=i==2?1100:i==3?50:100;
        for(unsigned b=0;b<16;++b)f->ibeacon.uuid[b]=(uint8_t)((i==2?0xF0:0)+b);
        f->ibeacon.major=65535;f->ibeacon.minor=i==2?65535:0;
        f->ibeacon.measured_power_dbm=i==2?127:-128;
        f->category_bits=CERBERUS_S3_BLE_FOCUS_IBEACON;live_ble_focus_bits[i]=f->category_bits;
    }
    observations[3].focus.discovery.advertisement_sequence=3;
    observations[3].focus.discovery.scan_response_sequence=0;
    observations[3].focus.discovery.scan_response_observed_ms=0;
    observations[3].focus.discovery.flags&=(uint8_t)~CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    observations[2].advertisement.length=3;observations[2].advertisement.data[0]=2;
    observations[2].advertisement.data[1]=1;observations[2].advertisement.data[2]=6;
    cerberus_s3_ble_focus_t earlier=observations[3].focus;
    assert(cerberus_s3_ble_ibeacon_is_current(&observations[0].focus));
    assert(cerberus_s3_ble_ibeacon_is_current(&observations[2].focus));
    assert(!cerberus_s3_ble_ibeacon_is_current(&earlier));
    ble_filter=CERBERUS_S3_BLE_FILTER_ALL;live_ble_order_dirty=true;live_make_view();
    handle_ble_tap(100,65);assert(ble_filter==CERBERUS_S3_BLE_FILTER_IBEACON);
    assert(live_ble_filtered_count==3&&live_ble_indexes[0]==0&&live_ble_indexes[1]==2);
    assert(!strcmp(live_badges[0],"IBEACON ADV CURRENT / 8s"));
    assert(!strcmp(live_badges[1],"IBEACON RSP CURRENT / 7s"));
    assert(!strcmp(live_badges[2],"IBEACON ADV EARLIER / 8s"));
    handle_ble_tap(250,65);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_NAME_ASC);
    assert(live_ble_indexes[0]==3&&live_ble_indexes[1]==2);
    drawn[0]='\0';assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"ibeacon-list");
    cerberus_findmy_monitor_record_t expected=observations[2];
    handle_ble_tap(20,200);tick(8200);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&ble_selected_scan_index==2&&ble_selected_sound_profile==CERBERUS_FINDMY_PROFILE_NONE);
    assert(ble_findmy_selected.observed_ms==100&&ble_selected_focus.ibeacon_observed_ms==1100);
    memset(observations,0,sizeof(observations));memset(&store,0,sizeof(store));++scan.generation;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"IBEACON FORMAT / RSP / CURRENT / 7s"));
    assert(strstr(drawn,"IBEACON MAJOR 65535 MINOR 65535 CAL 127 dBm"));
    assert(strstr(drawn,"F0F1F2F3-F4F5-F6F7-F8F9-FAFBFCFDFEFF"));save(prefix,"ibeacon-frozen-detail");
    handle_ble_tap(30,300);assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&track_begins==1);
    assert(!memcmp(track_address,expected.peer.address,6)&&track_type==expected.peer.address_type);
    ble_fox_hunt_tick(8500,false);clock_ms=8600;actual_back(440);hold_stop=true;
    ble_fox_hunt_tick(8800,false);assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&ble_fox_hunt_exit_pending);
    owner=CERBERUS_S3_BLE_OWNER_NONE;ble_fox_hunt_tick(9100,false);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&track_ends==1&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    clock_ms=12100;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"RSP / CURRENT / 11s"));
    actual_back(440);assert(ble_mode==UI_BLE_MODE_LIST&&ble_filter==CERBERUS_S3_BLE_FILTER_IBEACON);

    /* Historical matching evidence keeps its own payload/time even when the
     * current original advertisement was replaced with unrelated bytes. */
    ble_selected_focus=earlier;clock_ms=8100;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV / EARLIER / 8s"));assert(strstr(drawn,"CAL -128 dBm"));
    assert(strstr(drawn,"00010203-0405-0607-0809-0A0B0C0D0E0F"));save(prefix,"ibeacon-earlier-detail");
    char badge[FINDMY_BADGE_CAPACITY];cerberus_ibeacon_source_line(true,false,UINT32_MAX,true,badge,sizeof(badge));
    assert(!strcmp(badge,"IBEACON RSP EARLIER / 4294967s"));
    clock_ms=UINT32_MAX;ble_selected_focus.ibeacon_observed_ms=0;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"EARLIER / 4294967s"));

    /* Latest-event Analyzer payload uses the same event source/age as line
     * 290, retains stronger Watch text, and takes display priority over the
     * Meta004C field. The complete Meta matrix itself remains unchanged. */
    analyzer_view=(cerberus_ble_analyzer_view_t){.active=true,.has_last=true,.last_report_valid=true,.last_scan_response=true,
        .last_ibeacon=expected.focus.ibeacon,.last_ibeacon_valid=true,.last_seen_age_ms=7100,.last_rssi_dbm=-48,
        .total_events=123,.current_events=4,.evidence_kind=CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH,
        .evidence_label="STRONGER WATCH HINT",.evidence_confidence=SENTINEL_WATCH_CONFIDENCE_HIGH};
    assert(cerberus_ble_meta_evidence_add(&analyzer_view.last_meta,0x004C,CERBERUS_BLE_META_MFG16));
    cerberus_ble_meta_evidence_t matrix=analyzer_view.last_meta;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"RSP -48 dBm / 7.1s"));assert(strstr(drawn,"HINT: STRONGER WATCH HINT / High"));
    assert(strstr(drawn,"IBEACON MAJOR 65535 MINOR 65535 CAL 127 dBm"));
    assert(strstr(drawn,"F0F1F2F3-F4F5-F6F7-F8F9-FAFBFCFDFEFF"));assert(!strstr(drawn,"004C MFG16"));
    assert(!memcmp(&matrix,&analyzer_view.last_meta,sizeof(matrix)));save(prefix,"ibeacon-analyzer");
    analyzer_view.last_ibeacon.measured_power_dbm=-128;analyzer_view.last_seen_age_ms=UINT32_MAX;
    analyzer_view.total_events=analyzer_view.malformed_reports=analyzer_view.unclassified_reports=UINT32_MAX;
    analyzer_view.last_name_length=32;memset(analyzer_view.last_name,'W',32);drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"CAL -128 dBm"));save(prefix,"ibeacon-analyzer-max");
    analyzer_view.last_ibeacon_valid=false;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);
    assert(!strstr(drawn,"F0F1F2F3")&&strstr(drawn,"004C MFG16"));
    analyzer_view.last_ibeacon_valid=true;analyzer_view.last_report_valid=false;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);
    assert(!strstr(drawn,"F0F1F2F3")&&!strstr(drawn,"004C MFG16"));
}

static void check_flock_ui(const char *prefix)
{
    seed_list();track_begins=track_ends=passive_starts=0;
    for(unsigned i=0;i<4;++i){
        cerberus_s3_ble_focus_t *f=&observations[i].focus;f->category_bits=0;live_ble_focus_bits[i]=0;
        memset(&f->discovery.flipper_advertisement,0,sizeof(f->discovery.flipper_advertisement));
        memset(&f->discovery.flipper_scan_response,0,sizeof(f->discovery.flipper_scan_response));
        f->discovery.flock_advertisement.facts=CERBERUS_BLE_FLOCK_NO_LOCAL_NAME;
    }
    cerberus_s3_ble_focus_t *f=&observations[0].focus;
    f->discovery.flock_advertisement.facts=CERBERUS_BLE_FLOCK_FS_BATTERY;
    f->discovery.flock_scan_response.facts=CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD|CERBERUS_BLE_FLOCK_NO_LOCAL_NAME;
    f=&observations[1].focus;observations[1].peer.address_type=0;
    observations[1].peer.address[5]=0x58;observations[1].peer.address[4]=0x8E;observations[1].peer.address[3]=0x81;
    memcpy(observations[1].advertisement.address,observations[1].peer.address,6);observations[1].advertisement.address_type=0;
    memcpy(f->discovery.address,observations[1].peer.address,6);f->discovery.address_type=0;
    f->flock_oui_clue=cerberus_ble_flock_oui_hint(observations[1].peer.address,0);assert(f->flock_oui_clue);
    store.ble[1]=observations[1].peer;
    f=&observations[2].focus;
    f->discovery.flock_scan_response.facts=CERBERUS_BLE_FLOCK_PENGUIN|CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
    observations[2].advertisement.length=3;observations[2].advertisement.data[0]=2;observations[2].advertisement.data[1]=1;observations[2].advertisement.data[2]=6;
    for(unsigned i=0;i<3;++i){observations[i].focus.category_bits=CERBERUS_S3_BLE_FOCUS_FLOCK;live_ble_focus_bits[i]=CERBERUS_S3_BLE_FOCUS_FLOCK;}
    cerberus_s3_ble_focus_t separate=observations[0].focus,oui_only=observations[1].focus;
    uint8_t oui_address[6];memcpy(oui_address,observations[1].peer.address,6);
    ble_filter=CERBERUS_S3_BLE_FILTER_FLIPPER;live_ble_order_dirty=true;live_make_view();
    handle_ble_tap(100,65);assert(ble_filter==CERBERUS_S3_BLE_FILTER_FLOCK);
    assert(live_ble_filtered_count==3&&live_ble_indexes[0]==1&&live_ble_indexes[1]==0&&live_ble_indexes[2]==2);
    assert(!strcmp(live_badges[0],"ADV NONE / RSP NONE / OUI"));
    assert(!strcmp(live_badges[1],"ADV SIG / RSP CLUE"));
    assert(!strcmp(live_badges[2],"ADV NONE / RSP CORROB"));
    handle_ble_tap(250,65);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_NAME_ASC&&live_ble_indexes[0]==2);
    drawn[0]='\0';assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"flock-list");
    cerberus_findmy_monitor_record_t expected=observations[2];
    handle_ble_tap(20,135);tick(8200);assert(ble_mode==UI_BLE_MODE_DETAIL&&ble_selected_scan_index==2);
    assert(ble_selected_sound_profile==CERBERUS_FINDMY_PROFILE_NONE&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    memset(observations,0,sizeof(observations));memset(&store,0,sizeof(store));++scan.generation;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV NO QUALIFYING FLOCK FORMAT"));assert(strstr(drawn,"RSP CORROB PENGUIN+09C8DATA / 7s"));save(prefix,"flock-frozen-detail");
    handle_ble_tap(30,300);assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&track_begins==1);
    assert(!memcmp(track_address,expected.peer.address,6)&&track_type==expected.peer.address_type);
    ble_fox_hunt_tick(8500,false);clock_ms=8600;actual_back(440);hold_stop=true;ble_fox_hunt_tick(8800,false);
    assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&ble_fox_hunt_exit_pending);owner=CERBERUS_S3_BLE_OWNER_NONE;ble_fox_hunt_tick(9100,false);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&track_ends==1&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    actual_back(440);assert(ble_mode==UI_BLE_MODE_LIST&&ble_filter==CERBERUS_S3_BLE_FILTER_FLOCK);

    /* A name in ADV and company payload in RSP never become CORROB. */
    ble_selected_focus=separate;clock_ms=8100;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV SIG FS BATTERY NAME / 8s"));assert(strstr(drawn,"RSP CLUE 09C8+NO NAME +1 FACTS / 7s"));
    assert(!strstr(drawn,"ADV CORROB")&&!strstr(drawn,"RSP CORROB"));save(prefix,"flock-separate-sources");
    ble_selected_focus=oui_only;memcpy(ble_selected_result.address,oui_address,6);masked=true;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADDRESS OUI CLUE / PREFIX MASKED")&&!strstr(drawn,"58:8E:81"));save(prefix,"flock-oui-masked");
    masked=false;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ADDRESS OUI 58:8E:81 / CLUE ONLY"));
    /* General detail gives real payloads priority over an address-only clue. */
    ble_filter=CERBERUS_S3_BLE_FILTER_ALL;ble_selected_focus.discovery.findmy_advertisement=(cerberus_ble_findmy_hints_t){CERBERUS_BLE_FINDMY_HINT_FMNA,CERBERUS_BLE_FINDMY_UUID16};
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ADV HINT FMNA16")&&!strstr(drawn,"ADDRESS OUI"));
    ble_selected_focus=oui_only;ble_selected_focus.discovery.flipper_advertisement=(cerberus_ble_flipper_hints_t){CERBERUS_BLE_FLIPPER_HINT_WHITE,CERBERUS_BLE_FLIPPER_UUID16};
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"3082 WHITE CODE")&&!strstr(drawn,"ADDRESS OUI"));
    ble_selected_focus=oui_only;ble_selected_focus.ibeacon_valid=true;
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"IBEACON MAJOR")&&!strstr(drawn,"ADDRESS OUI"));
    ble_selected_focus=oui_only;assert(cerberus_ble_meta_evidence_add(&ble_selected_focus.discovery.meta_advertisement,0xFD5F,CERBERUS_BLE_META_LIST128));
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"FD5F LIST128")&&!strstr(drawn,"ADDRESS OUI"));
    ble_filter=CERBERUS_S3_BLE_FILTER_FLOCK;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ADDRESS OUI"));

    /* Service-pair contexts stay paired with their own service and use an
     * explicit remainder for extra facts. Bare decimal names stay clues. */
    ble_selected_focus=(cerberus_s3_ble_focus_t){0};
    ble_selected_focus.discovery.flock_advertisement=(cerberus_ble_flock_hints_t){.facts=CERBERUS_BLE_FLOCK_NO_LOCAL_NAME,.raven_3100_contexts=CERBERUS_BLE_FLOCK_LIST128,.raven_3200_contexts=CERBERUS_BLE_FLOCK_DATA128};
    ble_selected_focus.discovery.flock_scan_response.facts=CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_SERIAL10;
    clock_ms=UINT32_MAX;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV SIG 3100 L128+3200 D128 +1 FACTS / 4294967s"));
    assert(strstr(drawn,"RSP CLUE 09C8+10DIGIT NAME / 4294967s"));assert(!strstr(drawn,"SERIAL"));save(prefix,"flock-context-max");
    ble_selected_focus.discovery.flock_advertisement=(cerberus_ble_flock_hints_t){.accessory_contexts=CERBERUS_BLE_FLOCK_LIST128|CERBERUS_BLE_FLOCK_DATA128};
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ACCESSORY UUID L128 +1 FACTS"));
    /* The selected detail signal describes the scan peak and still fits at
     * both metadata widths; it does not borrow a per-signature RSSI. */
    ble_selected_result.rssi=-128;ble_selected_result.address_type=1;ble_selected_result.flags=CERBERUS_BLE_RESULT_CONNECTABLE;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_ble_peak_metadata()==ESP_OK);
    assert(strstr(drawn,"PEAK RSSI UNAVAILABLE / RANDOM / CONNECTABLE"));save(prefix,"ble-peak-metadata");
    ble_selected_result.flags=0;drawn[0]='\0';assert(actual_ble_peak_metadata()==ESP_OK);assert(strstr(drawn,"ADV ONLY"));

    analyzer_view=(cerberus_ble_analyzer_view_t){.active=true,.has_last=true,.last_report_valid=true,.last_scan_response=true,
        .last_flock=expected.focus.discovery.flock_scan_response,.last_seen_age_ms=7100,.last_rssi_dbm=-48,
        .evidence_kind=CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH,.evidence_label="STRONGER WATCH HINT",.evidence_confidence=SENTINEL_WATCH_CONFIDENCE_HIGH};
    assert(cerberus_ble_meta_evidence_add(&analyzer_view.last_meta,0xFD5F,CERBERUS_BLE_META_LIST128));
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"HINT: STRONGER WATCH HINT"));assert(strstr(drawn,"RSP CORROB PENGUIN+09C8DATA / 7s"));
    assert(!strstr(drawn,"FD5F LIST128"));save(prefix,"flock-analyzer");
    analyzer_view.last_ibeacon_valid=true;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"IBEACON MAJOR")&&!strstr(drawn,"RSP CORROB"));
    analyzer_view.last_ibeacon_valid=false;analyzer_view.last_flock=(cerberus_ble_flock_hints_t){.facts=CERBERUS_BLE_FLOCK_NO_LOCAL_NAME};
    analyzer_view.last_flock_oui_clue=true;memcpy(analyzer_view.last_address,oui_address,6);
    drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(strstr(drawn,"FD5F LIST128")&&!strstr(drawn,"ADDRESS OUI"));
    memset(&analyzer_view.last_meta,0,sizeof(analyzer_view.last_meta));masked=true;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"RSP NO QUALIFYING FLOCK FORMAT"));assert(strstr(drawn,"ADDRESS OUI CLUE / PREFIX MASKED"));save(prefix,"flock-analyzer-address-clue");
    analyzer_view.last_report_valid=false;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(!strstr(drawn,"ADDRESS OUI"));
}

static void check_hc_ui(const char *prefix)
{
    seed_list();track_begins=track_ends=passive_starts=0;
    for(unsigned i=0;i<4;++i){
        cerberus_s3_ble_focus_t *f=&observations[i].focus;
        memset(&f->discovery.flipper_advertisement,0,sizeof(f->discovery.flipper_advertisement));
        memset(&f->discovery.flipper_scan_response,0,sizeof(f->discovery.flipper_scan_response));
        f->category_bits=0;live_ble_focus_bits[i]=0;
        observations[i].advertisement.length=3;
        observations[i].advertisement.data[0]=2;observations[i].advertisement.data[1]=1;observations[i].advertisement.data[2]=6;
    }
    observations[0].focus.discovery.hc_advertisement=CERBERUS_BLE_HC_03;
    observations[0].focus.discovery.hc_scan_response=CERBERUS_BLE_HC_05;
    observations[2].focus.discovery.hc_scan_response=CERBERUS_BLE_HC_06;
    /* Same address bytes, different address types must remain distinct. */
    memcpy(observations[2].peer.address,observations[0].peer.address,6);observations[2].peer.address_type=1;
    memcpy(observations[2].advertisement.address,observations[2].peer.address,6);observations[2].advertisement.address_type=1;
    memcpy(observations[2].focus.discovery.address,observations[2].peer.address,6);observations[2].focus.discovery.address_type=1;
    observations[3].focus.discovery.hc_advertisement=CERBERUS_BLE_HC_05;
    observations[3].peer.rssi=CERBERUS_BLE_RSSI_UNAVAILABLE;strcpy(observations[3].peer.name,"Zulu");
    for(unsigned i=0;i<4;++i){
        if(i!=1){observations[i].focus.category_bits=CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT;live_ble_focus_bits[i]=CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT;}
        store.ble[i]=observations[i].peer;
    }
    cerberus_scan_display_store_t unchanged=store;
    cerberus_s3_ble_focus_t separate=observations[0].focus;
    ble_filter=CERBERUS_S3_BLE_FILTER_META_HINT;live_ble_order_dirty=true;live_make_view();
    handle_ble_tap(100,65);assert(ble_filter==CERBERUS_S3_BLE_FILTER_HC_NAME_HINT);
    assert(live_ble_filtered_count==3&&live_ble_indexes[0]==0&&live_ble_indexes[1]==2&&live_ble_indexes[2]==3);
    assert(!strcmp(live_badges[0],"ADV HC-03 / RSP HC-05 / HINTS"));
    assert(!strcmp(live_badges[1],"ADV NONE / RSP HC-06 / HINTS"));
    assert(live_view.rows[0].signal_valid&&live_view.rows[1].signal_valid&&!live_view.rows[2].signal_valid);
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);
    assert(!strstr(drawn,"127 dBm"));save(prefix,"hc-list");
    handle_ble_tap(250,65);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_NAME_ASC&&live_ble_indexes[0]==2);
    assert(!memcmp(&store,&unchanged,sizeof(store)));
    cerberus_findmy_monitor_record_t expected=observations[2];
    handle_ble_tap(20,135);tick(8200);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&ble_selected_scan_index==2&&ble_selected_raw_frozen);
    assert(ble_selected_sound_profile==CERBERUS_FINDMY_PROFILE_NONE&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    memset(observations,0,sizeof(observations));memset(&store,0,sizeof(store));++scan.generation;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV NO EXACT HC NAME")&&strstr(drawn,"RSP NAME HC-06 / 7s"));
    assert(strstr(drawn,"NOT DEVICE IDENTITY")&&!strstr(drawn,"SKIMMER"));save(prefix,"hc-frozen-detail");
    handle_ble_tap(30,300);assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&track_begins==1);
    assert(!memcmp(track_address,expected.peer.address,6)&&track_type==1);
    ble_fox_hunt_tick(8500,false);clock_ms=8600;actual_back(440);hold_stop=true;ble_fox_hunt_tick(8800,false);
    assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&ble_fox_hunt_exit_pending);
    owner=CERBERUS_S3_BLE_OWNER_NONE;ble_fox_hunt_tick(9100,false);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&track_ends==1&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    clock_ms=10100;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"RSP NAME HC-06 / 9s"));
    actual_back(440);assert(ble_mode==UI_BLE_MODE_LIST&&ble_filter==CERBERUS_S3_BLE_FILTER_HC_NAME_HINT);

    ble_selected_focus=separate;clock_ms=8100;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV NAME HC-03 / 8s")&&strstr(drawn,"RSP NAME HC-05 / 7s"));save(prefix,"hc-separate-sources");
    /* A replacement is current evidence only, never the old name at a new age. */
    ble_selected_focus.discovery.hc_advertisement=CERBERUS_BLE_HC_06;
    ble_selected_focus.discovery.advertisement_observed_ms=8000;
    ble_selected_focus.discovery.hc_scan_response=CERBERUS_BLE_HC_NONE;
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV NAME HC-06 / 0s")&&strstr(drawn,"RSP NO EXACT HC NAME"));assert(!strstr(drawn,"HC-03")&&!strstr(drawn,"HC-05"));
    ble_selected_focus.discovery.hc_advertisement=0;ble_selected_focus.category_bits=0;
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(!strstr(drawn,"HC-06"));
    char text[64];cerberus_hc_hint_line(CERBERUS_BLE_HC_03,false,UINT32_MAX,text,sizeof(text));assert(strstr(text,"HC-03 / 4294967s"));
    cerberus_hc_hint_line(255,true,1000,text,sizeof(text));assert(!strcmp(text,"RSP NO EXACT HC NAME"));
    ble_selected_focus=separate;ble_selected_focus.discovery.advertisement_observed_ms=UINT32_MAX-100;
    ble_selected_focus.discovery.scan_response_observed_ms=100;clock_ms=1100;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV NAME HC-03 / 1s")&&strstr(drawn,"RSP NAME HC-05 / 1s"));

    /* Focused HC remains explicit; ALL preserves stronger payload priorities. */
    ble_filter=CERBERUS_S3_BLE_FILTER_ALL;ble_selected_focus=separate;ble_selected_focus.flock_oui_clue=true;
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ADV NAME HC-03")&&!strstr(drawn,"ADDRESS OUI"));
    ble_selected_focus.meta_suppressed=true;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV NAME HC-03")&&!strstr(drawn,"META FILTER SUPPRESSED"));
    assert(cerberus_ble_meta_evidence_add(&ble_selected_focus.discovery.meta_advertisement,0xFD5F,CERBERUS_BLE_META_LIST16));
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"FD5F LIST16")&&!strstr(drawn,"ADV NAME HC-03"));
    ble_filter=CERBERUS_S3_BLE_FILTER_HC_NAME_HINT;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ADV NAME HC-03")&&!strstr(drawn,"FD5F LIST16"));
    ble_filter=CERBERUS_S3_BLE_FILTER_ALL;ble_selected_focus=separate;ble_selected_focus.discovery.flock_advertisement.facts=CERBERUS_BLE_FLOCK_FS_BATTERY;
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"FS BATTERY NAME")&&!strstr(drawn,"ADV NAME HC-03"));
    ble_selected_focus=separate;ble_selected_focus.discovery.findmy_advertisement=(cerberus_ble_findmy_hints_t){CERBERUS_BLE_FINDMY_HINT_FMNA,CERBERUS_BLE_FINDMY_UUID16};
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ADV HINT FMNA16")&&!strstr(drawn,"ADV NAME HC-03"));
    ble_selected_focus=separate;ble_selected_focus.discovery.flipper_advertisement=(cerberus_ble_flipper_hints_t){CERBERUS_BLE_FLIPPER_HINT_WHITE,CERBERUS_BLE_FLIPPER_UUID16};
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"3082 WHITE CODE")&&!strstr(drawn,"ADV NAME HC-03"));
    ble_selected_focus=separate;ble_selected_focus.ibeacon_valid=true;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"IBEACON MAJOR")&&!strstr(drawn,"ADV NAME HC-03"));

    /* Invalid scan peaks have no numeric dBm rendering. Both valid endpoints fit. */
    const int invalid[]={127,-128,21,126};
    ble_selected_result.address_type=1;ble_selected_result.flags=CERBERUS_BLE_RESULT_CONNECTABLE;
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){
        ble_selected_result.rssi=(int8_t)invalid[i];drawn[0]='\0';assert(actual_ble_peak_metadata()==ESP_OK);
        assert(strstr(drawn,"PEAK RSSI UNAVAILABLE / RANDOM / CONNECTABLE")&&!strstr(drawn,"dBm"));
    }
    cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_ble_peak_metadata()==ESP_OK);save(prefix,"hc-unavailable-peak");
    ble_selected_result.rssi=-127;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_ble_peak_metadata()==ESP_OK);
    assert(strstr(drawn,"PEAK RSSI -127 dBm / RANDOM / CONNECTABLE"));save(prefix,"hc-valid-peak");
    ble_selected_result.rssi=20;drawn[0]='\0';assert(actual_ble_peak_metadata()==ESP_OK);assert(strstr(drawn,"PEAK RSSI 20 dBm"));

    analyzer_view=(cerberus_ble_analyzer_view_t){.active=true,.has_last=true,.last_report_valid=true,.last_scan_response=true,
        .last_hc_name=CERBERUS_BLE_HC_06,.last_seen_age_ms=UINT32_MAX,.last_rssi_dbm=127,.last_name_length=32,
        .evidence_kind=CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH,.evidence_label="STRONGER WATCH HINT",.evidence_confidence=SENTINEL_WATCH_CONFIDENCE_HIGH};
    memset(analyzer_view.last_name,'X',32);analyzer_view.last_flock_oui_clue=true;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"HINT: STRONGER WATCH HINT")&&strstr(drawn,"RSP NAME HC-06 / 4294967s"));
    assert(strstr(drawn,"RSP RSSI N/A")&&!strstr(drawn,"127 dBm")&&!strstr(drawn,"ADDRESS OUI"));save(prefix,"hc-analyzer-max");
    analyzer_view.last_ibeacon_valid=true;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(strstr(drawn,"IBEACON MAJOR")&&!strstr(drawn,"RSP NAME HC-06"));
    analyzer_view.last_ibeacon_valid=false;analyzer_view.last_flock.facts=CERBERUS_BLE_FLOCK_FS_BATTERY;
    drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(strstr(drawn,"FS BATTERY NAME")&&!strstr(drawn,"RSP NAME HC-06"));
    analyzer_view.last_flock.facts=0;assert(cerberus_ble_meta_evidence_add(&analyzer_view.last_meta,0xFD5F,CERBERUS_BLE_META_LIST16));
    drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(strstr(drawn,"FD5F LIST16")&&!strstr(drawn,"RSP NAME HC-06"));
    memset(&analyzer_view.last_meta,0,sizeof(analyzer_view.last_meta));analyzer_view.last_scan_response=false;
    analyzer_view.last_hc_name=CERBERUS_BLE_HC_05;analyzer_view.last_seen_age_ms=10;analyzer_view.last_rssi_dbm=20;
    drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(strstr(drawn,"ADV NAME HC-05 / 0s")&&!strstr(drawn,"RSP NAME HC-06"));
    analyzer_view.last_hc_name=0;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(!strstr(drawn,"NAME HC-05"));
    analyzer_view.last_hc_name=CERBERUS_BLE_HC_03;analyzer_view.last_report_valid=false;
    drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);assert(!strstr(drawn,"NAME HC-03"));
}

static void seed_recent_findmy(void)
{
    seed_list();passive_starts=0;live_records[SENTINEL_SCAN_BLE].count=10;clock_ms=40000;live_ble_published_ms=40000;
    const uint32_t times[]={10000,9999,39000,9000,0,90000,35000,10000,38000,37000};
    for(unsigned i=0;i<10;++i){
        cerberus_findmy_monitor_record_t *r=&observations[i];memset(r,0,sizeof(*r));
        r->peer.address[0]=(uint8_t)(40+i);r->peer.address[5]=0x77;r->peer.address_type=(uint8_t)(i%2);
        if(i==0)r->peer.address_type=1;
        if(i==2)r->peer.address[0]=40; /* Equal bytes remain distinct by type. */
        snprintf(r->peer.name,sizeof(r->peer.name),"Node%02u",i);r->peer.rssi=(int8_t)(-40+(int)i);
        r->generation=7;r->index=(uint8_t)i;r->observed_ms=39000;
        memcpy(r->advertisement.address,r->peer.address,6);r->advertisement.address_type=r->peer.address_type;
        r->advertisement.length=3;r->advertisement.data[0]=2;r->advertisement.data[1]=1;r->advertisement.data[2]=6;
        cerberus_s3_ble_discovery_evidence_t *e=&r->focus.discovery;
        memcpy(e->address,r->peer.address,6);e->address_type=r->peer.address_type;e->generation=7;
        e->advertisement_sequence=1;e->advertisement_observed_ms=times[i];
        e->flags=CERBERUS_S3_BLE_EVIDENCE_ADV_VALID|CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE|CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
        e->findmy_advertisement=(cerberus_ble_findmy_hints_t){CERBERUS_BLE_FINDMY_HINT_FMNA,CERBERUS_BLE_FINDMY_UUID16};
        if(i==2||i==7){
            e->findmy_advertisement=(cerberus_ble_findmy_hints_t){0};
            e->flags|=CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;e->scan_response_sequence=2;
            e->scan_response_observed_ms=times[i]+500;e->findmy_scan_response=(cerberus_ble_findmy_hints_t){CERBERUS_BLE_FINDMY_HINT_DULT,0};
        }
        r->focus.category_bits=CERBERUS_S3_BLE_FOCUS_FIND_MY;live_ble_focus_bits[i]=r->focus.category_bits;store.ble[i]=r->peer;
    }
    ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;live_ble_order_dirty=true;live_make_view();
}

static void recent_tick(int64_t now)
{
    clock_ms=now;live_scan_tick((uint32_t)now,false);
}

static void check_recent_ui(const char *prefix)
{
    seed_recent_findmy();assert(!live_ble_recent_only&&live_ble_filtered_count==10);
    assert(!strcmp(live_view.age_label,"ALL AGES >"));
    cerberus_findmy_monitor_record_t before[10];memcpy(before,observations,sizeof(before));
    handle_ble_tap(250,86);assert(live_ble_recent_only&&!strcmp(live_view.age_label,"WITHIN 30S >"));
    assert(live_ble_filtered_count==6&&live_ble_visible_mask==UINT32_C(0x3c5));
    const uint8_t signal_order[]={9,8,7,6,2,0},first_order[]={0,2,6,7,8,9};
    assert(!memcmp(live_ble_indexes,signal_order,sizeof(signal_order)));
    assert(live_commands==0&&starts==0&&passive_starts==0);
    handle_ble_tap(250,60);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_NAME_ASC);
    assert(!memcmp(live_ble_indexes,first_order,sizeof(first_order)));
    handle_ble_tap(250,60);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_DISCOVERY);
    assert(!memcmp(live_ble_indexes,first_order,sizeof(first_order)));
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"recent-findmy");
    handle_ble_tap(190,380);assert(live_page[SENTINEL_SCAN_BLE]==1&&live_view.row_count==2);
    /* The production polling tick repaints a completed list without RF start. */
    recent_tick(40001);assert(live_ble_filtered_count==5&&live_page[SENTINEL_SCAN_BLE]==1);
    assert(live_commands==0&&starts==0&&passive_starts==0&&owner==CERBERUS_S3_BLE_OWNER_NONE);
    recent_tick(70001);assert(live_ble_filtered_count==0&&live_page[SENTINEL_SCAN_BLE]==0&&live_view.row_count==0);
    assert(!strcmp(live_view.message,"NO HINTS WITHIN 30S")&&live_view.state==SENTINEL_VIEW_COMPLETE);
    assert(live_commands==0&&starts==0&&passive_starts==0&&!memcmp(before,observations,sizeof(before)));
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"recent-empty");
    handle_ble_tap(250,86);assert(!live_ble_recent_only&&live_ble_filtered_count==10);
    /* Paused lists also expire by the existing tick, without an implicit rearm. */
    seed_recent_findmy();handle_ble_tap(250,86);session.completed=false;session.paused=true;
    recent_tick(40001);assert(live_ble_filtered_count==5&&live_view.state==SENTINEL_VIEW_PAUSED);
    recent_tick(70001);assert(live_ble_filtered_count==0&&session.paused&&live_commands==0);

    /* Tap the painted boundary row after it expires but before a repaint. The
     * handler must freeze that row, not silently recompute a different target. */
    seed_recent_findmy();handle_ble_tap(250,86);handle_ble_tap(250,60);
    cerberus_findmy_monitor_record_t expected=observations[0];
    clock_ms=40001;handle_ble_tap(20,135);tick(40002);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&ble_selected_scan_index==0&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    assert(ble_selected_sound_profile==CERBERUS_FINDMY_PROFILE_NONE);
    memset(observations,0,sizeof(observations));memset(&store,0,sizeof(store));++scan.generation;
    track_begins=track_ends=passive_starts=0;handle_ble_tap(30,300);
    assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&track_begins==1&&track_type==1&&track_type==expected.peer.address_type&&!memcmp(track_address,expected.peer.address,6));
    ble_fox_hunt_tick(40500,false);clock_ms=40600;actual_back(440);hold_stop=true;ble_fox_hunt_tick(40800,false);
    assert(ble_fox_hunt_exit_pending&&ble_mode==UI_BLE_MODE_FOX_HUNT);
    owner=CERBERUS_S3_BLE_OWNER_NONE;ble_fox_hunt_tick(41100,false);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&track_ends==1&&!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    actual_back(440);assert(ble_mode==UI_BLE_MODE_LIST&&ble_filter==CERBERUS_S3_BLE_FILTER_FIND_MY);

    /* Reset unsupported families; Watch never consumes the normal-list age mask. */
    seed_recent_findmy();handle_ble_tap(250,86);handle_ble_tap(100,65);
    assert(ble_filter==CERBERUS_S3_BLE_FILTER_FLIPPER&&!live_ble_recent_only&&!live_view.age_label);
    ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;live_make_view();assert(!strcmp(live_view.age_label,"ALL AGES >"));
    live_ble_recent_only=true;current_page=UI_PAGE_WATCH;clock_ms=INT64_C(4294967296)+40000;
    assert(live_ble_recent_mask(clock_ms)==UINT32_MAX&&live_ble_recent_only);
    current_page=UI_PAGE_BLE;

    /* A full uint32 clock revolution cannot resurrect a never-before-filtered
     * snapshot. A clock rewind before its publication also excludes it. */
    seed_recent_findmy();clock_ms=INT64_C(4294967296)+40000;handle_ble_tap(250,86);
    assert(live_ble_filtered_count==0&&live_commands==0);
    handle_ble_tap(250,86);assert(live_ble_filtered_count==10);
    clock_ms=39999;handle_ble_tap(250,86);assert(live_ble_filtered_count==0);
    /* A genuinely new publication restores visibility using the new own times. */
    clock_ms=INT64_C(4294967296)+40000;live_ble_published_ms=clock_ms;
    for(unsigned i=0;i<10;++i){observations[i].generation=8;observations[i].focus.discovery.generation=8;}
    live_records[SENTINEL_SCAN_BLE].generation=live_records[SENTINEL_SCAN_BLE].saved_generation=8;
    live_ble_order_dirty=true;live_make_view();assert(live_ble_filtered_count==6);

    seed_recent_findmy();live_records[SENTINEL_SCAN_BLE].count=1;
    observations[0].focus.discovery.advertisement_observed_ms=0;clock_ms=0;live_ble_published_ms=0;
    live_ble_order_dirty=true;live_make_view();handle_ble_tap(250,86);assert(live_ble_filtered_count==1);
    clock_ms=30000;live_make_view();assert(live_ble_filtered_count==1);
    clock_ms=30001;live_make_view();assert(live_ble_filtered_count==0);
    clock_ms=INT64_C(4294967296)+1000;live_ble_published_ms=clock_ms-100;
    observations[0].focus.discovery.advertisement_observed_ms=UINT32_MAX-500;live_ble_order_dirty=true;
    live_make_view();assert(live_ble_filtered_count==1);
    observations[0].focus.discovery.advertisement_observed_ms=1001;live_make_view();assert(live_ble_filtered_count==0);

    /* Retained earlier iBeacon matches use their own timestamp; a recent raw
     * original cannot refresh an expired tuple, nor must it make one CURRENT. */
    seed_list();clock_ms=40000;live_ble_published_ms=40000;
    for(unsigned i=0;i<4;++i){
        cerberus_s3_ble_focus_t *f=&observations[i].focus;memset(&f->discovery.findmy_advertisement,0,sizeof(f->discovery.findmy_advertisement));
        f->ibeacon_valid=true;f->ibeacon_sequence=1;f->ibeacon_observed_ms=i==0?10000:i==1?9999:i==2?39000:90000;
        f->discovery.advertisement_sequence=3;f->discovery.advertisement_observed_ms=39500;
        f->category_bits=CERBERUS_S3_BLE_FOCUS_IBEACON;live_ble_focus_bits[i]=f->category_bits;
    }
    ble_filter=CERBERUS_S3_BLE_FILTER_ALL;live_ble_order_dirty=true;live_make_view();handle_ble_tap(100,65);
    assert(ble_filter==CERBERUS_S3_BLE_FILTER_IBEACON&&!strcmp(live_view.age_label,"ALL AGES >"));
    handle_ble_tap(250,86);assert(live_ble_filtered_count==2&&live_ble_indexes[0]==0&&live_ble_indexes[1]==2);
    assert(!strcmp(live_badges[0],"IBEACON ADV EARLIER / 30s"));
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"recent-ibeacon");
    recent_tick(40001);assert(live_ble_filtered_count==1&&live_ble_indexes[0]==2&&live_commands==0);
}

static void seed_wifi_recent(void);

static void check_start_publication_lifecycle(void)
{
    const esp_err_t replies[]={ESP_OK,ESP_FAIL,ESP_ERR_INVALID_STATE};
    const sentinel_scan_tool_t tools[]={SENTINEL_SCAN_BLE,SENTINEL_SCAN_WIFI};
    for(unsigned t=0;t<2;++t)for(unsigned i=0;i<3;++i){
        sentinel_scan_tool_t tool=tools[t];
        if(tool==SENTINEL_SCAN_BLE){seed_list();session.completed=false;}
        else {seed_wifi_recent();live_session.completed=false;}
        live_command_error=replies[i];
        cerberus_scan_display_store_t frozen=store;int64_t published=live_ble_published_ms;
        recent_tick(41000);assert(live_commands==1);
        assert(!!(live_capture_pending&(UINT32_C(1)<<tool))==(replies[i]==ESP_OK));
        assert(live_records[tool].captured&&live_records[tool].saved_generation==7);
        assert(!memcmp(&frozen,&store,sizeof(store))&&live_ble_published_ms==published);
    }
}

static void seed_wifi_recent(void)
{
    seed_list();current_page=UI_PAGE_WIFI;wifi_mode=UI_WIFI_MODE_LIST;
    c5_status.firmware_version=CERBERUS_WIFI_OBSERVATION_MIN_FIRMWARE;
    c5_online=true;live_radio_busy=false;wifi_attack_selecting=false;
    wifi_selection_valid=false;live_wifi_recent_only=false;
    live_session.desired=SENTINEL_SCAN_WIFI;live_session.completed=true;
    session.desired=SENTINEL_SCAN_NONE;
    clock_ms=40000;live_commands=starts=passive_starts=0;
    live_records[SENTINEL_SCAN_WIFI]=(live_scan_record_t){.valid=true,.captured=true,
        .count=10,.generation=7,.saved_generation=7,.state=SENTINEL_SCAN_COMPLETE,.captured_ms=40000};
    const uint32_t ages[]={30000,30001,500,0,UINT32_MAX,0,25000,500,0,15000};
    for(unsigned i=0;i<10;++i){
        cerberus_c5_wifi_result_t *row=&store.wifi[i];
        snprintf(row->ssid,sizeof(row->ssid),"Network %u",i);
        row->bssid[0]=2;row->bssid[5]=(uint8_t)(i+1);row->channel=i%2?36:6;row->rssi=(int8_t)(-30-i);
        store.wifi_observations[i]=(cerberus_s3_wifi_observation_t){
            .request_started_us=40000000,.source_age_ms=ages[i],.valid=i!=3&&i!=5};
    }
    live_make_view();
}

static void check_wifi_recent_ui(const char *prefix)
{
    seed_wifi_recent();assert(live_view.total_count==10&&!strcmp(live_view.age_label,"ALL AGES >"));
    cerberus_scan_display_store_t frozen=store;
    handle_live_scan_tap(250,86);
    const uint8_t expected[]={0,2,6,7,8,9};
    assert(live_wifi_recent_only&&live_wifi_filtered_count==6&&!memcmp(live_wifi_indexes,expected,sizeof(expected)));
    assert(!strcmp(live_names[1],"Network 2")&&live_view.page_count==2);
    assert(live_commands==0&&starts==0&&passive_starts==0);
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);
    assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"recent-wifi");
    handle_live_scan_tap(190,380);assert(live_page[SENTINEL_SCAN_WIFI]==1&&live_view.row_count==2);
    recent_tick(40001);assert(live_wifi_filtered_count==5&&live_page[SENTINEL_SCAN_WIFI]==1);
    recent_tick(70001);assert(live_wifi_filtered_count==0&&live_page[SENTINEL_SCAN_WIFI]==0);
    assert(live_view.state==SENTINEL_VIEW_COMPLETE&&!strcmp(live_view.message,"NO NETWORKS WITHIN 30S"));
    assert(live_commands==0&&starts==0&&passive_starts==0&&!memcmp(&store,&frozen,sizeof(store)));
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);
    assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(prefix,"recent-wifi-empty");
    handle_live_scan_tap(250,86);assert(live_wifi_filtered_count==10&&!live_wifi_recent_only);

    /* Expiration between paint and tap must never select a different AP. */
    seed_wifi_recent();handle_live_scan_tap(250,86);
    cerberus_c5_wifi_result_t chosen=store.wifi[0];
    clock_ms=40001;handle_live_scan_tap(20,135);
    assert(wifi_selection_valid&&wifi_selected_index==0&&wifi_mode==UI_WIFI_MODE_DETAIL);
    assert(!memcmp(&wifi_selected_result,&chosen,sizeof(chosen)));
    assert(relation_selection_calls==1&&relation_selection_ordinary&&
        !memcmp(&relation_selected_ap,&chosen,sizeof(chosen)));
    memset(store.wifi,0,sizeof(store.wifi));
    assert(!memcmp(&wifi_selected_result,&chosen,sizeof(chosen)));
    /* All ages retains the original driver order, including unproven rows. */
    seed_wifi_recent();handle_live_scan_tap(250,86);handle_live_scan_tap(250,86);
    handle_live_scan_tap(20,200);assert(wifi_selected_index==1&&!strcmp(wifi_selected_result.ssid,"Network 1"));

    seed_wifi_recent();handle_live_scan_tap(250,86);live_session.completed=false;live_session.paused=true;
    recent_tick(70001);assert(live_view.state==SENTINEL_VIEW_PAUSED&&live_view.total_count==0&&live_commands==0);
    seed_wifi_recent();handle_live_scan_tap(250,86);
    c5_status.firmware_version=CERBERUS_WIFI_OBSERVATION_MIN_FIRMWARE-1;
    live_make_view();assert(!live_wifi_recent_only&&!live_view.age_label&&live_view.total_count==10);
    handle_live_scan_tap(250,86);assert(!live_wifi_recent_only&&live_view.total_count==10);
    seed_wifi_recent();c5_online=false;live_make_view();handle_live_scan_tap(250,86);
    assert(live_view.state==SENTINEL_VIEW_OFFLINE&&!live_wifi_recent_only);
    c5_online=true;
}

static void check_analyzer_window_ui(const char *prefix)
{
    cerberus_ble_analyzer_t analyzer;
    const uint8_t ordinary[]={2,1,6};
    cerberus_ble_analyzer_report_t event={.address_type=1,.rssi_dbm=-48,.complete=true,
        .advertisement=ordinary,.advertisement_length=sizeof(ordinary)};
    cerberus_ble_analyzer_begin(&analyzer,0);
    ble_analyzer_started=true;ble_analyzer_start_pending=ble_analyzer_exit_pending=false;ble_analyzer_error=ESP_OK;
    cerberus_ble_analyzer_snapshot(&analyzer,0,&analyzer_view);
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"PEAK -- / MEAN -- / N0"));
    assert(screen[155*320+40]==UI_PANEL);save(prefix,"analyzer-window-empty");
    for(unsigned i=0;i<6;++i)assert(cerberus_ble_analyzer_observe(&analyzer,&event,1));
    for(unsigned i=0;i<3;++i)assert(cerberus_ble_analyzer_observe(&analyzer,&event,201));
    cerberus_ble_analyzer_snapshot(&analyzer,600,&analyzer_view);
    /* A much larger partial bucket is visible as a separate number but must
     * not flatten completed graph bars, change mean, or claim a completed peak. */
    analyzer_view.current_events=UINT16_MAX;
    analyzer_view.total_events=UINT16_MAX+9U;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"PEAK 6 / MEAN 3.0 / N3")&&strstr(drawn,"PARTIAL >=65535"));
    assert(strstr(drawn,"N:200MS COMPLETE"));
    assert(screen[155*320+40]==UI_GOOD&&screen[203*320+40]==UI_MUTED);
    assert(screen[155*320+(32+61*4)]==UI_GOOD); /* actual six-count oldest bar */
    save(prefix,"analyzer-window-partial");
    cerberus_ble_analyzer_begin(&analyzer,0);
    for(unsigned b=0;b<64;++b){
        for(unsigned i=0;i<(b*3U)%13U;++i)assert(cerberus_ble_analyzer_observe(&analyzer,&event,b*200U+1));
        cerberus_ble_analyzer_advance(&analyzer,(b+1U)*200U);
    }
    cerberus_ble_analyzer_snapshot(&analyzer,12800,&analyzer_view);
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"PEAK 12 / MEAN 5.9 / N64"));save(prefix,"analyzer-window-full");
    analyzer_view.history_count=64;for(unsigned i=0;i<64;++i)analyzer_view.history[i]=UINT16_MAX;
    analyzer_view.total_events=UINT32_MAX;analyzer_view.current_events=UINT16_MAX;
    analyzer_view.malformed_reports=analyzer_view.unclassified_reports=UINT32_MAX;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"PEAK >=65535 / MEAN >=65535.0 / N64"));
    assert(strstr(drawn,"BLE CALLBACKS >=4294967295 / PARTIAL >=65535"));save(prefix,"analyzer-window-capped");
    analyzer_view.history_count=255;drawn[0]='\0';assert(draw_ble_analyzer()==ESP_OK);
    assert(strstr(drawn,"PEAK -- / MEAN -- / N0"));
    memset(&analyzer_view,0,sizeof(analyzer_view));
}

int main(int argc,char **argv)
{
    assert(existing_monitor_suite(argc,argv)==0);seed_list();
    /* Actual header hit advances FIND MY to FLIPPER and applies its category. */
    handle_ble_tap(100,65);assert(ble_filter==CERBERUS_S3_BLE_FILTER_FLIPPER);
    assert(live_ble_filtered_count==3&&live_ble_indexes[0]==0&&live_ble_indexes[1]==2&&live_ble_indexes[2]==3);
    assert(!strcmp(live_badges[0],"ADV 3082 / RSP 3083 / HINTS"));
    assert(!strcmp(live_names[2],"Unnamed device"));assert(!strstr(live_details[2],"77:00:00:00:00:33"));
    /* Real sort hit, full view builder and renderer preserve original index. */
    handle_ble_tap(250,65);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_NAME_ASC);
    assert(live_ble_indexes[0]==3&&live_ble_indexes[1]==2&&live_ble_indexes[2]==0);
    drawn[0]='\0';assert(sentinel_scan_view_draw(&live_view,0)==ESP_OK);save(argv[1],"focused-list");
    assert(strstr(drawn,"ADV 3082 / RSP 3083 / HINTS"));
    cerberus_findmy_monitor_record_t expected=observations[2];
    ++observations[2].generation;handle_ble_tap(20,200);assert(ble_mode==UI_BLE_MODE_LIST);observations[2]=expected;
    handle_ble_tap(20,200);assert(ble_mode==UI_BLE_MODE_FINDMY_MONITOR&&ble_findmy_stopping);
    tick(8200);assert(ble_mode==UI_BLE_MODE_DETAIL&&ble_selected_raw_frozen&&ble_selected_scan_index==2);
    assert(!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    assert(ble_selected_sound_profile==CERBERUS_FINDMY_PROFILE_NONE);
    memset(observations,0,sizeof(observations));memset(&store,0,sizeof(store));++scan.generation;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV HINT 3082 WHITE CODE / 8s"));assert(strstr(drawn,"RSP HINT 3083 TRANSPARENT CODE+NAME / 7s"));save(argv[1],"frozen-detail");

    /* The real detail TRACK branch sends the frozen typed address to the
     * unchanged Fox Hunt API even after the source list has been replaced. */
    handle_ble_tap(30,300);assert(ble_mode==UI_BLE_MODE_FOX_HUNT&&track_begins==1);
    assert(!memcmp(track_address,expected.peer.address,6)&&track_type==expected.peer.address_type);
    ble_fox_hunt_tick(8500,false);assert(passive_starts==1&&owner==CERBERUS_S3_BLE_OWNER_SCANNER);
    assert(!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    clock_ms=8600;actual_back(440);hold_stop=true;ble_fox_hunt_tick(8800,false);
    assert(ble_fox_hunt_exit_pending&&ble_mode==UI_BLE_MODE_FOX_HUNT&&track_ends==0);
    owner=CERBERUS_S3_BLE_OWNER_NONE;ble_fox_hunt_tick(9100,false);
    assert(ble_mode==UI_BLE_MODE_DETAIL&&track_ends==1);
    assert(!memcmp(&ble_findmy_selected,&expected,sizeof(expected)));
    clock_ms=10100;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"ADV HINT 3082 WHITE CODE / 10s"));
    actual_back(440);assert(ble_mode==UI_BLE_MODE_LIST&&ble_filter==CERBERUS_S3_BLE_FILTER_FLIPPER);

    /* Render worst-case code mixtures, name/service-data provenance and long
     * ages. Do not silently truncate a response or imply physical colors. */
    ble_selected_focus=expected.focus;ble_selected_focus.discovery.flipper_advertisement=(cerberus_ble_flipper_hints_t){15,31};
    ble_selected_focus.discovery.flipper_scan_response=(cerberus_ble_flipper_hints_t){8,31};
    clock_ms=INT32_MAX;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"3080/81/82/83 CODES+NAME+DATA"));assert(strstr(drawn,"3083 TRANSPARENT CODE+NAME+DATA"));save(argv[1],"mixed-detail");
    char text[64];cerberus_ble_flipper_hints_t name_only={0,CERBERUS_BLE_FLIPPER_NAME};
    cerberus_flipper_hint_line(&name_only,false,0,100,text,sizeof(text));assert(!strcmp(text,"ADV NAME ONLY / NO UUID HINT"));
    cerberus_flipper_hint_line(&expected.focus.discovery.flipper_advertisement,false,UINT32_MAX-100,1100,text,sizeof(text));assert(strstr(text," / 1s"));
    check_meta_ui(argv[1]);
    check_ibeacon_ui(argv[1]);
    check_flock_ui(argv[1]);
    check_hc_ui(argv[1]);
    check_recent_ui(argv[1]);
    check_start_publication_lifecycle();
    check_wifi_recent_ui(argv[1]);
    check_analyzer_window_ui(argv[1]);
    puts("BLE discovery UI: production filter/sort/age/cards/hit-test, timed completed/paused expiry without RF start, publication ceiling/wrap, frozen typed selection through Fox Hunt/Back, source ages, priorities, privacy, RSSI and unchanged Sound gate passed");
    return 0;
}
