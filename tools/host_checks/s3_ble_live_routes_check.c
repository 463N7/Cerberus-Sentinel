#undef NDEBUG
#include "s3_findmy_monitor.h"
#include "scan_display_store.h"
#include "s3_ble_list.h"
#include "s3_ble_recent.h"
#include "s3_ble_discovery_inventory.h"
#include "s3_ble_discovery_session.h"
#include "sentinel_scan_view.h"
#include "sentinel_scan_session.h"
#include <assert.h>
#include <stdio.h>
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
enum {UI_PAGE_WIFI,UI_PAGE_BLE,UI_PAGE_IEEE802154,UI_PAGE_WATCH};
enum {UI_WIFI_MODE_LIST,UI_WIFI_MODE_TARGET,UI_WIFI_MODE_MENU};
enum {UI_BLE_MODE_LIST,UI_BLE_MODE_FINDMY_MONITOR,UI_BLE_MODE_MENU,UI_BLE_MODE_ANALYZER};
enum {UI_IEEE802154_MODE_ENERGY,UI_IEEE802154_MODE_BEACONS,UI_IEEE802154_MODE_ACTIVITY,UI_IEEE802154_MODE_MENU};
static unsigned current_page=UI_PAGE_BLE,ble_mode=UI_BLE_MODE_LIST,wifi_mode,ieee802154_mode;
static bool wifi_attack_selecting,wifi_selection_valid,ble_attack_selecting;
static unsigned wifi_target_count,wifi_selected_index,wifi_analysis_generation;
static esp_err_t wifi_analysis_error,wifi_trust_error,wifi_connection_error;
static cerberus_c5_wifi_result_t wifi_selected_result;
static cerberus_scan_display_store_t store,*live_store=&store;
static cerberus_findmy_monitor_record_t evidence[32],*live_ble_evidence=evidence,selected;
static uint8_t live_ble_indexes[32];static size_t live_ble_filtered_count;
static bool live_wifi_recent_only;
static uint8_t live_wifi_indexes[32];
static size_t live_wifi_filtered_count;
static cerberus_c5_status_t c5_status;
static uint16_t live_page[SENTINEL_SCAN_TOOL_COUNT];
typedef struct {bool captured;uint16_t count;uint8_t saved_generation;} live_scan_record_t;
typedef live_scan_record_t live_test_record_t;
static live_test_record_t live_records[SENTINEL_SCAN_TOOL_COUNT];
static sentinel_scan_view_t live_view;
static char live_hint[52];static uint32_t live_frame,live_last_poll;
static cerberus_s3_ble_filter_t ble_filter,live_ble_order_filter;
static cerberus_s3_ble_sort_t live_ble_sort,live_ble_order_sort;
static bool live_ble_order_dirty=true;
static bool live_ble_order_discovery;
static uint16_t live_ble_order_count;
static uint64_t live_ble_order_epoch,live_ble_order_revision;
static bool live_ble_recent_only;
static uint32_t live_ble_visible_mask=UINT32_MAX;
static int64_t live_ble_published_ms,clock_ms;
static int64_t esp_timer_get_time(void){return clock_ms*1000LL;}
static uint8_t live_ble_focus_bits[32];
static char live_ble_filter_label[19];
static const char *ble_filter_name(cerberus_s3_ble_filter_t f){(void)f;return "FILTER";}
static sentinel_scan_session_t session;
static sentinel_view_action_t action;
static unsigned monitor_opened,monitor_tapped,selected_count,active_starts,passive_starts,stop_count;
static cerberus_ble_scan_status_payload_t ble_scan_status;
static sentinel_scan_tool_t live_page_tool(void) {return SENTINEL_SCAN_BLE;}
static void live_make_view(void);
static sentinel_scan_session_t *live_session_for(sentinel_scan_tool_t t){(void)t;return &session;}
void sentinel_scan_session_select(sentinel_scan_session_t *s,sentinel_scan_tool_t t){s->desired=t;}
void sentinel_scan_session_pause(sentinel_scan_session_t *s,bool p){s->paused=p;}
sentinel_view_action_t sentinel_scan_view_hit_test(const sentinel_scan_view_t*v,uint16_t x,uint16_t y){(void)v;(void)x;(void)y;return action;}
esp_err_t sentinel_scan_view_update(const sentinel_scan_view_t*v,uint32_t f){(void)v;(void)f;return ESP_OK;}
static esp_err_t draw_current_page(void){return ESP_OK;}
static void ble_findmy_monitor_open(void){++monitor_opened;ble_mode=UI_BLE_MODE_FINDMY_MONITOR;}
static void ble_findmy_monitor_tap(uint16_t x,uint16_t y){(void)x;(void)y;++monitor_tapped;}
static bool ble_connection_is_active(void){return false;}
static void ble_select_frozen_live(const cerberus_findmy_monitor_record_t*r){selected=*r;++selected_count;}
static void wifi_relation_bind_selection(bool ordinary)
{(void)ordinary;assert(!"Wi-Fi relationship binding from a BLE-only route");}
#define wifi_targets_clear() ((void)0)
#define wifi_target_open_attacks() ((void)0)
#define wifi_target_toggle_selection(...) true
#define wifi_target_finish_selection() ((void)0)
#define c5_spi_link_get_wifi_connection_status(...) ESP_OK
static esp_err_t cerberus_s3_ble_scanner_start_active(cerberus_ble_scan_status_payload_t*s){(void)s;++active_starts;return ESP_OK;}
static esp_err_t cerberus_s3_ble_scanner_start(cerberus_ble_scan_status_payload_t*s){(void)s;++passive_starts;return ESP_OK;}
static esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t*s){(void)s;++stop_count;return ESP_OK;}
/* This fixture exercises the retained completed-bank route. The separate
 * discovery-live suite compiles the actual active controller and adapter. */
static struct {
    cerberus_s3_ble_discovery_inventory_t inventory;
    cerberus_ble_discovery_session_t session;
    bool selection_pending;
    uint8_t destination;
} *ble_discovery_live;
static bool ble_discovery_presenting(void){return false;}
static bool ble_discovery_page(void){return false;}
static uint32_t ble_discovery_visible_mask(uint64_t now){(void)now;assert(!"live mask in legacy fixture");return 0U;}
static void ble_discovery_toggle(void){assert(!"live toggle in legacy fixture");}
static void ble_discovery_select_painted(unsigned row){(void)row;assert(!"live selection in legacy fixture");}
static esp_err_t live_paint(bool full){(void)full;return sentinel_scan_view_update(&live_view,live_frame);}
#include "ble_live_route_impl.h"
int main(void)
{
    assert(live_is_view());
    for(unsigned i=0;i<CERBERUS_S3_BLE_FILTER_COUNT;++i){handle_ble_tap(100,60);assert(ble_filter==(i+1)%CERBERUS_S3_BLE_FILTER_COUNT);}
    ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;action=SENTINEL_VIEW_TOOLS;handle_ble_tap(250,380);
    assert(monitor_opened==1 && !live_is_view());handle_ble_tap(20,90);assert(monitor_tapped==1);
    ble_mode=UI_BLE_MODE_LIST;live_ble_filtered_count=2;live_ble_indexes[0]=9;live_ble_indexes[1]=2;
    live_records[SENTINEL_SCAN_BLE].saved_generation=7;
    evidence[9].generation=7;evidence[9].index=9;evidence[9].observed_ms=100;evidence[9].peer.address[0]=77;
    live_store->ble[9]=evidence[9].peer;action=SENTINEL_VIEW_ROW0;handle_ble_tap(20,130);
    assert(selected_count==1 && selected.index==9 && selected.peer.address[0]==77 && selected.observed_ms==100);
    evidence[9].peer.address[0]=1;handle_ble_tap(20,130);assert(selected_count==1);
    action=SENTINEL_VIEW_NEXT;handle_ble_tap(180,380);assert(live_page[SENTINEL_SCAN_BLE]==0); /* A stale Next hit cannot exceed the filtered page. */
    action=SENTINEL_VIEW_PREV;handle_ble_tap(20,380);assert(live_page[SENTINEL_SCAN_BLE]==0);
    /* Exercise the actual ordering block followed by the production tap path.
     * Sorting changes only the index map: selected raw evidence stays bound. */
    ble_filter=CERBERUS_S3_BLE_FILTER_ALL;live_ble_order_dirty=true;
    live_records[SENTINEL_SCAN_BLE].count=3;live_page[SENTINEL_SCAN_BLE]=0;
    memset(store.ble,0,sizeof(store.ble));
    strcpy(store.ble[0].name,"Zeta");store.ble[0].rssi=-70;
    strcpy(store.ble[1].name,"alpha");store.ble[1].rssi=-30;
    strcpy(store.ble[2].name,"Beta");store.ble[2].rssi=-80;
    for(unsigned i=0;i<3;++i){store.ble[i].address[0]=(uint8_t)(80+i);evidence[i].peer=store.ble[i];
        evidence[i].generation=7;evidence[i].index=(uint8_t)i;evidence[i].observed_ms=100+i;}
    cerberus_c5_ble_result_t before[3];memcpy(before,store.ble,sizeof(before));
    live_make_view();assert(live_ble_indexes[0]==1&&live_ble_indexes[1]==0&&live_ble_indexes[2]==2);
    assert(!strcmp(live_view.sort_label,"SIGNAL >"));
    live_page[SENTINEL_SCAN_BLE]=2;action=SENTINEL_VIEW_SORT;handle_ble_tap(250,65);
    assert(live_ble_sort==CERBERUS_S3_BLE_SORT_NAME_ASC&&live_page[SENTINEL_SCAN_BLE]==0);
    assert(live_ble_indexes[0]==1&&live_ble_indexes[1]==2&&live_ble_indexes[2]==0);
    action=SENTINEL_VIEW_ROW1;handle_ble_tap(20,206);
    assert(selected_count==2&&selected.index==2&&selected.peer.address[0]==82&&selected.observed_ms==102);
    assert(!memcmp(before,store.ble,sizeof(before)));
    action=SENTINEL_VIEW_SORT;handle_ble_tap(250,65);
    assert(live_ble_sort==CERBERUS_S3_BLE_SORT_DISCOVERY&&live_ble_indexes[0]==0&&live_ble_indexes[1]==1);
    handle_ble_tap(250,65);assert(live_ble_sort==CERBERUS_S3_BLE_SORT_SIGNAL_DESC);
    live_ble_focus_bits[0]=CERBERUS_S3_BLE_FOCUS_FIND_MY;live_ble_focus_bits[2]=CERBERUS_S3_BLE_FOCUS_FIND_MY;
    ble_filter=CERBERUS_S3_BLE_FILTER_FIND_MY;live_make_view();
    assert(live_ble_filtered_count==2&&live_ble_indexes[0]==0&&live_ble_indexes[1]==2);
    assert(actual_ble_start(true)==ESP_OK && active_starts==1);
    current_page=UI_PAGE_WATCH;assert(actual_ble_start(true)==ESP_OK && passive_starts==1);
    assert(actual_ble_start(false)==ESP_OK && stop_count==1);
    puts("Actual BLE live dispatch: filters, Monitor entry, mapped frozen selection, pagination, sorted/focused original indexes, original timestamp, active-list/passive-Watch scan gate passed");
}
