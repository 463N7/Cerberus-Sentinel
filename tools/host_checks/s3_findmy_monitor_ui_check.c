#undef NDEBUG
#include "s3_findmy_monitor.h"
#include "s3_flipper_hint_view.h"
#include "s3_meta_hint_view.h"
#include "s3_ibeacon_hint_view.h"
#include "s3_flock_hint_view.h"
#include "s3_hc_hint_view.h"
#include "ble_rssi.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#define cerberus_display_draw_text framebuffer_draw_text
#include "findmy_framebuffer.h"
#undef cerberus_display_draw_text
#include "privacy.h"
static char drawn[8192];
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,uint8_t scale)
{
    size_t used=strlen(drawn);assert(used+strlen(s)+2U<sizeof(drawn));
    snprintf(drawn+used,sizeof(drawn)-used,"%s\n",s);
    return framebuffer_draw_text(x,y,s,fg,bg,scale);
}

#define UI_WIFI_VISIBLE_ROWS 6U
#define UI_WIFI_LIST_Y 84U
#define UI_WIFI_ROW_HEIGHT 44U
#define UI_WIFI_CONTROLS_Y 356U
#define UI_BLE_REFRESH_MS 500U
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_8BIT 2U
#define UI_BACKGROUND 0x0042
#define UI_PANEL 0x08A4
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define UI_GOOD 0x471B
#define UI_BAD 0xF440
#define TAG "host"
#define ESP_ERROR_CHECK(x) assert((x) == ESP_OK)
typedef enum { UI_PAGE_BLE, UI_PAGE_OTHER, UI_PAGE_BLE_ATTACK, UI_PAGE_WIFI, UI_PAGE_IEEE802154, UI_PAGE_WATCH } ui_page_t;
enum { UI_BLE_MODE_LIST, UI_BLE_MODE_FINDMY_MONITOR, UI_BLE_MODE_DETAIL, UI_BLE_MODE_MENU, UI_BLE_MODE_ANALYZER, UI_BLE_MODE_FOX_HUNT };
enum { CERBERUS_S3_BLE_OWNER_NONE, CERBERUS_S3_BLE_OWNER_SCANNER, CERBERUS_S3_BLE_OWNER_CENTRAL };
enum { CERBERUS_S3_WIFI_NONE, CERBERUS_S3_WIFI_RECEIVER };
static unsigned current_page, ble_mode, owner, wifi_owner;
static int64_t clock_ms;
static bool fail_alloc, hold_stop;
static unsigned starts, stops, getters;
static cerberus_ble_scan_status_payload_t scan, ble_scan_status;
static cerberus_findmy_monitor_t *ble_findmy_monitor;
static cerberus_findmy_monitor_record_t ble_findmy_selected, fixture;
static bool ble_selected_from_monitor, ble_selected_raw_frozen, ble_attack_selecting;
static bool ble_selected_original_time64_valid;
static uint64_t ble_selected_original_ms64;
static uint64_t ble_selected_source_ceiling_ms64;
static bool ble_findmy_running, ble_findmy_scan_owned, ble_findmy_stopping;
static bool ble_findmy_exit_pending, ble_findmy_select_pending;
static uint8_t ble_findmy_generation;
static uint32_t ble_findmy_last_tick, ble_findmy_last_draw, ble_findmy_wait_since, ble_findmy_sweeps;
static size_t ble_findmy_page, ble_page_offset;
static esp_err_t ble_findmy_error;
static cerberus_c5_ble_result_t ble_selected_result;
static cerberus_s3_ble_focus_t ble_selected_focus;
static uint8_t ble_selected_scan_index;
static uint32_t ble_selected_scan_ms;
static cerberus_findmy_profile_t ble_selected_sound_profile;
static bool ble_selection_valid, ble_attack_capture_valid, ble_connection_checked;
static bool ble_service_checked, ble_characteristic_checked, ble_read_checked, ble_scan_checked;
static cerberus_s3_ble_filter_t ble_filter;
static bool masked;
static bool privacy_addresses_masked(void){return masked;}
static const char *esp_err_to_name(esp_err_t err){(void)err;return "ESP_ERR_INVALID_STATE";}
static esp_err_t draw_centered(uint16_t y,const char *s,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320U-strlen(s)*6U*scale)/2U),y,s,color,UI_BACKGROUND,scale);}
static esp_err_t draw_small_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *s)
{
    cerberus_display_fill_rect(x,y,w,h,UI_PANEL);
    return cerberus_display_draw_text((uint16_t)(x+(w-strlen(s)*6U)/2U),(uint16_t)(y+(h-7U)/2U),s,UI_TEXT,UI_PANEL,1);
}
static int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
static void *heap_caps_calloc(size_t n, size_t s, unsigned cap)
{ (void)cap; return fail_alloc ? NULL : calloc(n,s); }
static void heap_caps_free(void *p) { free(p); }
static esp_err_t draw_detail_page(void) { return ESP_OK; }
static void make_ble_name_safe(char *name) { (void)name; }
static void set_ble_action_message(const char *s, unsigned c) { (void)s;(void)c; }
static bool ble_attack_capture_selected_ad(uint8_t i) { (void)i; return true; }
static void open_tool_page(unsigned p) { current_page=p; }
static esp_err_t refresh_ble_scan_status(void) { ble_scan_status=scan; return ESP_OK; }
static unsigned cerberus_s3_ble_host_owner(void) { return owner; }
static unsigned cerberus_s3_wifi_owner(void) { return wifi_owner; }
static esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *s)
{ *s=scan; return ESP_OK; }
static esp_err_t cerberus_s3_ble_scanner_start_active(cerberus_ble_scan_status_payload_t *s)
{ assert(owner==CERBERUS_S3_BLE_OWNER_NONE); ++starts; ++scan.generation; scan.state=CERBERUS_BLE_SCAN_RUNNING; scan.stored_count=0; owner=CERBERUS_S3_BLE_OWNER_SCANNER; *s=scan; return ESP_OK; }
static esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *s)
{ ++stops; scan.state=CERBERUS_BLE_SCAN_IDLE; if(!hold_stop)owner=CERBERUS_S3_BLE_OWNER_NONE; *s=scan; return ESP_OK; }
static bool valid_get(uint8_t gen,uint8_t i)
{ ++getters; return gen==scan.generation && i==0 && scan.state==CERBERUS_BLE_SCAN_COMPLETE; }
static esp_err_t cerberus_s3_ble_scanner_get_result(uint8_t g,uint8_t i,cerberus_c5_ble_result_t *p)
{ if(!valid_get(g,i))return ESP_ERR_INVALID_STATE; *p=fixture.peer; return ESP_OK; }
static esp_err_t cerberus_s3_ble_scanner_get_advertisement(uint8_t g,uint8_t i,cerberus_c5_ble_advertisement_t *p)
{ if(!valid_get(g,i))return ESP_ERR_INVALID_STATE; *p=fixture.advertisement; return ESP_OK; }
static esp_err_t cerberus_s3_ble_scanner_get_advertisement_time(uint8_t g,uint8_t i,uint32_t *t)
{ if(!valid_get(g,i))return ESP_ERR_INVALID_STATE; *t=fixture.observed_ms; return ESP_OK; }
static esp_err_t cerberus_s3_ble_scanner_get_focus(uint8_t g,uint8_t i,cerberus_s3_ble_focus_t *p)
{ if(!valid_get(g,i))return ESP_ERR_INVALID_STATE; *p=fixture.focus; return ESP_OK; }

#include "findmy_selected_impl.h"
#include "findmy_monitor_ui_impl.h"
#include "ble_display_age_impl.h"
#include "findmy_detail_draw_impl.h"
static cerberus_findmy_monitor_record_t *live_ble_evidence;
#include "findmy_badge_capacity.h"
static char live_badges[4][FINDMY_BADGE_CAPACITY];
#include "findmy_list_badge_impl.h"

static void bind_fixture(uint8_t generation)
{
    cerberus_s3_ble_discovery_evidence_t *h=&fixture.focus.discovery;
    memset(h,0,sizeof(*h));h->generation=generation;
    memcpy(h->address,fixture.peer.address,6);h->address_type=fixture.peer.address_type;
    h->advertisement_sequence=1;h->advertisement_observed_ms=fixture.observed_ms;
    h->flags=CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE|CERBERUS_S3_BLE_EVIDENCE_ADV_VALID|CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
    assert(cerberus_ble_findmy_hints_parse(fixture.advertisement.data,fixture.advertisement.length,&h->findmy_advertisement));
}
static void save(const char *prefix,const char *suffix)
{
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,suffix);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}

static void tick(uint32_t now) { clock_ms=now; ble_findmy_monitor_tick(now,false); }
static void reset(void)
{
    free(ble_findmy_monitor); ble_findmy_monitor=NULL;
    memset(&scan,0,sizeof(scan)); memset(&fixture,0,sizeof(fixture));
    fixture.peer.address[0]=7; fixture.peer.address_type=1;
    fixture.peer.flags=CERBERUS_BLE_RESULT_CONNECTABLE;
    fixture.advertisement.address[0]=7; fixture.advertisement.address_type=1;
    fixture.advertisement.data[0]=30; fixture.advertisement.data[1]=0xFF;
    fixture.advertisement.data[2]=0x4C; fixture.advertisement.data[4]=0x12;
    fixture.advertisement.data[5]=0x19; fixture.advertisement.length=31;
    fixture.observed_ms=100;
    bind_fixture(1U);
    current_page=UI_PAGE_BLE; owner=CERBERUS_S3_BLE_OWNER_NONE;
    wifi_owner=CERBERUS_S3_WIFI_NONE; clock_ms=100; starts=stops=getters=0;
    fail_alloc=hold_stop=false; ble_selected_from_monitor=ble_selected_raw_frozen=false;
    ble_selected_original_time64_valid=false; ble_selected_original_ms64=0;
    ble_selected_source_ceiling_ms64=0;
    ble_attack_selecting=false;
}

static void check_display_age64(void)
{
    const int64_t wrap=INT64_C(1)<<32;
    char line[64];
    /* Time zero, a nearby low32 wrap, a late first copy, and future clocks. */
    strcpy(line,"ADV HINT FMNA / 0s");
    ble_hint_age64_line(line,sizeof(line),0,0,30000);
    assert(!strcmp(line,"ADV HINT FMNA / 30s"));
    strcpy(line,"IBEACON ADV EARLIER / 0s");
    ble_hint_age64_line(line,sizeof(line),UINT32_MAX,wrap+50,wrap+999);
    assert(!strcmp(line,"IBEACON ADV EARLIER / 1s"));
    strcpy(line,"ADV HINT FMNA / 0s");
    ble_hint_age64_line(line,sizeof(line),0,wrap-1,wrap);
    assert(!strcmp(line,"ADV HINT FMNA / >49d"));
    strcpy(line,"RSP HINT FMNA / 0s");
    ble_hint_age64_line(line,sizeof(line),110,100,100);
    assert(!strcmp(line,"RSP HINT FMNA / AGE ?"));
    strcpy(line,"RSP HINT FMNA / 0s");
    ble_hint_age64_line(line,sizeof(line),50,100,99);
    assert(!strcmp(line,"RSP HINT FMNA / AGE ?"));
    strcpy(line,"RSP HINT FMNA / 0s");
    ble_hint_age64_line(line,sizeof(line),50,100,-1);
    assert(!strcmp(line,"RSP HINT FMNA / AGE ?"));
    strcpy(line,"ADV NAME ONLY / NO UUID HINT");
    ble_hint_age64_line(line,sizeof(line),0,0,wrap);
    assert(!strcmp(line,"ADV NAME ONLY / NO UUID HINT"));
    char bounded[12];memset(bounded,'X',sizeof(bounded));
    ble_hint_age64_line(bounded,8,0,0,wrap);
    for(unsigned i=0;i<sizeof(bounded);++i)assert(bounded[i]=='X');

    reset();
    ble_selected_original_time64_valid=true;
    ble_selected_original_ms64=1000;
    ble_selected_source_ceiling_ms64=3000;
    memset(&ble_selected_focus,0,sizeof(ble_selected_focus));
    ble_selected_focus.category_bits=0x3f;
    ble_selected_focus.discovery.advertisement_observed_ms=1000;
    ble_selected_focus.discovery.scan_response_observed_ms=2000;
    ble_selected_focus.discovery.findmy_advertisement.bits=CERBERUS_BLE_FINDMY_HINT_FMNA;
    ble_selected_focus.discovery.findmy_scan_response.bits=CERBERUS_BLE_FINDMY_HINT_DULT;
    ble_selected_focus.discovery.flipper_advertisement.bits=CERBERUS_BLE_FLIPPER_HINT_WHITE;
    ble_selected_focus.discovery.flipper_scan_response.bits=CERBERUS_BLE_FLIPPER_HINT_BLACK;
    assert(cerberus_ble_meta_evidence_add(&ble_selected_focus.discovery.meta_advertisement,cerberus_ble_meta_id_value(0),CERBERUS_BLE_META_LIST16));
    assert(cerberus_ble_meta_evidence_add(&ble_selected_focus.discovery.meta_scan_response,cerberus_ble_meta_id_value(1),CERBERUS_BLE_META_DATA16));
    ble_selected_focus.discovery.flock_advertisement.facts=CERBERUS_BLE_FLOCK_FS_BATTERY;
    ble_selected_focus.discovery.flock_scan_response.facts=CERBERUS_BLE_FLOCK_PENGUIN;
    ble_selected_focus.discovery.hc_advertisement=CERBERUS_BLE_HC_05;
    ble_selected_focus.discovery.hc_scan_response=CERBERUS_BLE_HC_06;
    ble_selected_focus.ibeacon_valid=true;
    ble_selected_focus.ibeacon_sequence=1;
    ble_selected_focus.ibeacon_observed_ms=500;
    cerberus_s3_ble_focus_t proof=ble_selected_focus;
    for(unsigned family=1;family<CERBERUS_S3_BLE_FILTER_COUNT;++family){
        ble_filter=(cerberus_s3_ble_filter_t)family;
        clock_ms=wrap+3000;drawn[0]=0;
        assert(actual_hint_detail()==ESP_OK&&strstr(drawn," / >49d"));
        assert(!strstr(drawn," / 0s")&&!strstr(drawn," / 1s")&&!strstr(drawn," / 2s"));
        clock_ms=2999;drawn[0]=0;
        assert(actual_hint_detail()==ESP_OK&&strstr(drawn," / AGE ?"));
        assert(!memcmp(&proof,&ble_selected_focus,sizeof(proof)));
    }
    /* A response is newer than the original but keeps its own exact age. */
    ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;clock_ms=4000;drawn[0]=0;
    assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV HINT FMNA / 3s")&&strstr(drawn,"RSP HINT DULT / 2s"));
    ble_selected_original_time64_valid=false;
}
int main(int argc,char **argv)
{
    assert(argc==2);
    check_display_age64();
    reset(); ble_findmy_monitor_open(); tick(100);
    assert(starts==1 && ble_findmy_scan_owned);
    scan.state=CERBERUS_BLE_SCAN_COMPLETE; scan.stored_count=1; owner=CERBERUS_S3_BLE_OWNER_NONE;
    tick(8200);
    assert(starts==2 && ble_findmy_monitor->count==1 && ble_findmy_sweeps==1);
    /* Production renderer freezes the identity used by the real row route. */
    drawn[0]='\0';assert(draw_ble_findmy_monitor()==ESP_OK);
    assert(strstr(drawn,"ACTIVE SCAN / RSP=SCAN RESPONSE / HINTS ONLY"));
    assert(strstr(drawn,"ADV HINT APPLE / 8s"));
    assert(strstr(drawn,"RSP NO MATCHED FIND MY HINT"));
    ble_findmy_monitor_tap(30,90); hold_stop=true; tick(8500);
    assert(stops==1 && ble_findmy_stopping && ble_mode==UI_BLE_MODE_FINDMY_MONITOR);
    tick(8800); assert(ble_findmy_stopping); /* idle snapshot is not host release */
    owner=CERBERUS_S3_BLE_OWNER_NONE; tick(9100);
    assert(ble_mode==UI_BLE_MODE_DETAIL && ble_selected_from_monitor && ble_selected_scan_ms==100);
    unsigned before=getters; ++scan.generation; memset(&fixture,0,sizeof(fixture));
    uint32_t observed=0;
    assert(selected_findmy_profile(0,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_AIRTAG);
    assert(observed==100 && getters==before && !cerberus_findmy_ui_fresh(observed,30101));
    /* The new ordinary-list frozen proof guards the original in full-width
     * time; response freshness or a complete low32 wrap cannot revive Sound. */
    ble_selected_original_time64_valid=true; ble_selected_original_ms64=100;
    clock_ms=30100;
    assert(selected_findmy_profile(0,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_AIRTAG);
    clock_ms=30101;
    assert(selected_findmy_profile(0,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_NONE && observed==0);
    clock_ms=99;
    assert(selected_findmy_profile(0,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_NONE);
    clock_ms=(INT64_C(1)<<32)+100;
    assert(selected_findmy_profile(0,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_NONE);
    ble_selected_original_ms64=(UINT64_C(1)<<32)+100;
    assert(selected_findmy_profile(0,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_AIRTAG);
    assert(observed==100 && getters==before);
    ble_selected_original_time64_valid=false;
    /* Existing central connection does not trap Back from a stopped monitor. */
    ble_mode=UI_BLE_MODE_FINDMY_MONITOR; owner=CERBERUS_S3_BLE_OWNER_CENTRAL;
    ble_findmy_request_stop(true,false); tick(9400);
    assert(ble_mode==UI_BLE_MODE_LIST && !ble_findmy_monitor && owner==CERBERUS_S3_BLE_OWNER_CENTRAL);
    reset(); fail_alloc=true; ble_findmy_monitor_open(); ble_findmy_request_stop(true,false); tick(100);
    assert(ble_mode==UI_BLE_MODE_LIST && !ble_findmy_stopping);
    reset(); ble_findmy_monitor_open(); tick(100); hold_stop=true;
    ble_findmy_request_stop(false,true); ble_findmy_request_stop(true,false); tick(400);
    owner=CERBERUS_S3_BLE_OWNER_NONE; tick(700);
    assert(ble_mode==UI_BLE_MODE_LIST && !ble_findmy_monitor && !ble_findmy_select_pending);
    reset(); owner=CERBERUS_S3_BLE_OWNER_CENTRAL; ble_findmy_monitor_open(); tick(100); tick(12200);
    assert(!ble_findmy_running && starts==0 && ble_findmy_error==ESP_ERR_INVALID_STATE);
    ble_findmy_request_stop(true,false); tick(12500); assert(!ble_findmy_monitor);
    reset(); ble_findmy_monitor_open(); tick(100); ++scan.generation;
    tick(500); assert(!ble_findmy_running && !ble_findmy_scan_owned);
    ble_findmy_request_stop(true,false); tick(800); assert(stops==0 && !ble_findmy_monitor);
    reset(); fixture.observed_ms=100; fixture.generation=4; fixture.index=3;
    owner=CERBERUS_S3_BLE_OWNER_SCANNER; scan.generation=5;
    ble_select_frozen_live(&fixture); tick(500);
    assert(ble_findmy_stopping && !ble_selected_raw_frozen && stops==0);
    memset(&fixture,0,sizeof(fixture)); owner=CERBERUS_S3_BLE_OWNER_NONE; tick(800);
    assert(ble_mode==UI_BLE_MODE_DETAIL && ble_selected_raw_frozen && !ble_selected_from_monitor);
    assert(ble_selected_scan_index==3 && ble_selected_scan_ms==100 && ble_selected_result.address[0]==7);
    before=getters; assert(selected_findmy_profile(3,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_AIRTAG);
    assert(getters==before && observed==100 && !cerberus_findmy_ui_fresh(observed,30101));

    /* Response-only FMNA128 is discoverable and selectable, without becoming
     * an active Sound profile or replacing original advertisement time. */
    reset();fixture.advertisement.length=3;fixture.advertisement.data[0]=2;
    fixture.advertisement.data[1]=1;fixture.advertisement.data[2]=6;bind_fixture(1U);
    fixture.focus.discovery.findmy_scan_response=(cerberus_ble_findmy_hints_t){CERBERUS_BLE_FINDMY_HINT_FMNA,CERBERUS_BLE_FINDMY_UUID128};
    fixture.focus.discovery.scan_response_sequence=2;fixture.focus.discovery.scan_response_observed_ms=1100;
    fixture.focus.discovery.flags|=CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    fixture.focus.category_bits=CERBERUS_S3_BLE_FOCUS_FIND_MY;
    ble_findmy_monitor_open();tick(100);scan.state=CERBERUS_BLE_SCAN_COMPLETE;scan.stored_count=1;owner=CERBERUS_S3_BLE_OWNER_NONE;tick(8200);
    assert(ble_findmy_monitor->count==1);masked=true;drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);
    assert(draw_ble_findmy_monitor()==ESP_OK);assert(strstr(drawn,"ADV NO FIND MY HINT"));assert(strstr(drawn,"RSP HINT FMNA128 / 7s"));
    assert(!strstr(drawn,"07:00:00:00:00:00"));save(argv[1],"response-monitor");
    ble_findmy_monitor_tap(30,90);tick(8500);assert(ble_mode==UI_BLE_MODE_DETAIL && ble_selected_raw_frozen);
    assert(ble_selected_sound_profile==CERBERUS_FINDMY_PROFILE_NONE);
    assert(ble_findmy_selected.observed_ms==100 && ble_findmy_selected.focus.discovery.scan_response_observed_ms==1100);
    cerberus_s3_ble_focus_t frozen=ble_selected_focus;
    ++scan.generation;memset(&fixture,0,sizeof(fixture));clock_ms=12100;ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;
    drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(strstr(drawn,"RSP HINT FMNA128 / 11s"));
    assert(!memcmp(&frozen,&ble_selected_focus,sizeof(frozen)));
    assert(selected_findmy_profile(ble_selected_scan_index,&ble_selected_result,&observed)==CERBERUS_FINDMY_PROFILE_NONE);

    /* Mixed profile/UUID-width provenance fits every native row and both
     * detail lines. A six-row render exercises the actual screen limits. */
    ble_mode=UI_BLE_MODE_FINDMY_MONITOR;ble_findmy_running=false;ble_findmy_monitor->count=6;
    for(unsigned i=0;i<6;++i){ble_findmy_monitor->records[i]=ble_findmy_selected;
        ble_findmy_monitor->records[i].peer.address[0]=(uint8_t)(7+i);
        ble_findmy_monitor->records[i].focus.discovery.findmy_advertisement=(cerberus_ble_findmy_hints_t){7,7};
        ble_findmy_monitor->records[i].focus.discovery.findmy_scan_response=(cerberus_ble_findmy_hints_t){7,7};}
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(draw_ble_findmy_monitor()==ESP_OK);
    assert(strstr(drawn,"ADV HINT APPLE FMNA16+32+128 DULT / 12s"));save(argv[1],"mixed-monitor");
    ble_selected_focus=ble_findmy_monitor->records[0].focus;
    drawn[0]='\0';cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND);assert(actual_hint_detail()==ESP_OK);
    assert(strstr(drawn,"ADV HINT APPLE FMNA16+32+128 DULT / 12s"));assert(strstr(drawn,"RSP HINT APPLE FMNA16+32+128 DULT / 11s"));save(argv[1],"mixed-detail");
    /* Other focused detail categories preserve their existing rendering. */
    ble_filter=CERBERUS_S3_BLE_FILTER_META_HINT;drawn[0]='\0';assert(actual_hint_detail()==ESP_OK);assert(!drawn[0]);
    ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;live_ble_evidence=ble_findmy_monitor->records;actual_hint_badge();
    assert(!strcmp(live_badges[0],"ADV MIXED / RSP MIXED / HINTS"));
    ble_filter=CERBERUS_S3_BLE_FILTER_ALL;strcpy(live_badges[0],"CONNECTABLE");actual_hint_badge();assert(!strcmp(live_badges[0],"CONNECTABLE"));
    free(ble_findmy_monitor);ble_findmy_monitor=NULL;
    puts("Find My Monitor UI: production source/age rendering, frozen response-only selection, unchanged raw Sound gate, active sweeps, Stop/Back and owner isolation passed");
    return 0;
}
