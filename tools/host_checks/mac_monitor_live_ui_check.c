/* Actual UI header and routes; the platform adapters alone are simulated. */
#define main mac_session_fixture_main
#include "mac_monitor_session_check.c"
#undef main
#include "mac_monitor_ui.h"
#include "mac_monitor_link.h"
#include "s3_ble_mac_monitor.h"
#include "field_menu.h"
#include "privacy.h"
#include "esp_check.h"
#include "mac_route_types.h"

enum {MALLOC_CAP_SPIRAM=4,MALLOC_CAP_8BIT=2};
static ui_page_t current_page=UI_PAGE_FIELD_MENU,tool_parent_page;
static cerberus_field_menu_t field_menu;
static bool ui_capability_blocked,learning_tool_active,allocation_fail,takeover=true;
static const char *ui_capability_blocked_reason,*TAG="fixture";
static unsigned allocations,frees,draw_calls,other_routes;
static void *allocated;
static struct {bool session;} *remote_id_live,*csi_live;
static const cerberus_ui_widget_theme_t theme={0};
#define UI_WIDGET_THEME (&theme)
static bool remote_id_takeover_ready(void){return takeover;}
static bool cerberus_remote_id_session_released(const bool *s){return !*s;}
static bool cerberus_csi_session_released(const bool *s){return !*s;}
static bool privacy_addresses_masked(void){return true;}
static int64_t esp_timer_get_time(void){return (int64_t)fake.now;}
static const char *esp_err_to_name(esp_err_t e){return e==ESP_ERR_NO_MEM?"ESP_ERR_NO_MEM":"ERROR";}
static void *heap_caps_calloc(size_t count,size_t size,unsigned caps)
{CHECK(count==1 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(allocation_fail)return NULL;
CHECK(!allocated);allocated=calloc(count,size);CHECK(allocated);++allocations;return allocated;}
static void heap_caps_free(void *memory)
{if(!memory)return;CHECK(memory==allocated);CHECK(!fake.radios[0].owned && !fake.radios[1].owned);free(memory);allocated=NULL;++frees;}
void cerberus_mac_c5_link_init(cerberus_mac_c5_link_t *link,cerberus_mac_monitor_ops_t *out)
{CHECK(link && out);*out=ops;}
void cerberus_s3_ble_scanner_mac_ops(cerberus_s3_ble_mac_monitor_t *context,cerberus_mac_monitor_ops_t *out)
{CHECK(context && out);*out=ops;}
esp_err_t cerberus_display_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color)
{(void)color;CHECK((unsigned)x+w<=320 && (unsigned)y+h<=480);++draw_calls;return ESP_OK;}
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *text,uint16_t fg,uint16_t bg,uint8_t scale)
{(void)fg;(void)bg;CHECK(x+strlen(text)*6U*scale<=320 && y+7U*scale<=480);++draw_calls;return ESP_OK;}
#define ESP_ERROR_CHECK(call) do{CHECK((call)==ESP_OK);}while(0)
static esp_err_t draw_current_page(void){return cerberus_field_menu_draw(&field_menu,UI_WIDGET_THEME);}
static void open_main_menu(void){current_page=UI_PAGE_MAIN;}
static void watch_open(uint32_t time){(void)time;++other_routes;}
static void remote_id_open(void){++other_routes;}
#include "mac_monitor_live_ui.h"
#include "mac_routes.h"

static void reset_live(void)
{CHECK(!mac_monitor_live && !allocated);setup();memset(&field_menu,0,sizeof(field_menu));current_page=UI_PAGE_FIELD_MENU;
allocation_fail=false;takeover=true;fake.now=100000;}
static void advance_live(uint64_t elapsed,bool asleep)
{fake.now+=elapsed;for(unsigned n=0;n<2;++n)fake.radios[n].source+=elapsed;mac_monitor_tick((uint32_t)(fake.now/1000U),asleep);}
static void steps(unsigned count){for(unsigned n=0;n<count;++n)advance_live(100000,false);}
static unsigned radio_calls(void)
{unsigned count=0;for(unsigned n=0;n<2;++n)for(unsigned o=0;o<OPS;++o)count+=fake.radios[n].calls[o];return count;}
static void open_live(void)
{CHECK(draw_current_page()==ESP_OK);actual_mac_route(30,380);CHECK(mac_monitor_live && current_page==UI_PAGE_MAC_MONITOR);}
static void start_live(void)
{unsigned before=radio_calls();actual_mac_route(180,440);CHECK(radio_calls()==before);advance_live(100000,false);
CHECK(mac_monitor_live->session.radios[0].owned && mac_monitor_live->session.radios[1].owned);}
static void close_live(void)
{if(mac_monitor_live->detail)actual_mac_route(20,440);actual_mac_route(20,440);if(mac_monitor_live)advance_live(300000,false);
CHECK(!mac_monitor_live && !allocated && current_page==UI_PAGE_FIELD_MENU);}

int main(void)
{
    reset_live();CHECK(draw_current_page()==ESP_OK);
    for(unsigned n=0;n<8;++n){unsigned y=116+37*n;
        cerberus_field_action_t action=cerberus_field_menu_tap(&field_menu,30,(uint16_t)y);
        CHECK(action!=CERBERUS_FIELD_NONE && action!=CERBERUS_FIELD_HOME);
        CHECK(cerberus_field_menu_tap(&field_menu,30,(uint16_t)(y+33))==action);
        CHECK(cerberus_field_menu_tap(&field_menu,30,(uint16_t)(y+34))==CERBERUS_FIELD_NONE);}
    CHECK(cerberus_field_menu_tap(&field_menu,30,408)==CERBERUS_FIELD_MAC_MONITOR);
    CHECK(cerberus_field_menu_tap(&field_menu,30,409)==CERBERUS_FIELD_NONE);
    actual_mac_route(320,380);CHECK(!mac_monitor_live);allocation_fail=true;actual_mac_route(30,380);
    CHECK(!mac_monitor_live && !radio_calls() && mac_monitor_open_error==ESP_ERR_NO_MEM);
    actual_mac_route(100,440);CHECK(current_page==UI_PAGE_FIELD_MENU && !allocations);

    reset_live();open_live();CHECK(!radio_calls() && !mac_monitor_keeps_awake());
    unsigned before=radio_calls();mac_monitor_handle_tap(250,108);CHECK(!radio_calls());
    CHECK(mac_monitor_draw(false)==ESP_OK && !radio_calls());
    actual_mac_route(20,440);CHECK(!mac_monitor_live && !radio_calls());
    reset_live();open_live();start_live();CHECK(mac_monitor_keeps_awake());
    add(0,1);add(1,2);steps(3);CHECK(!mac_monitor_live->inventory.banks[0].published);
    CHECK(mac_monitor_live->inventory.banks[1].published && fake.radios[0].offset>0);
    steps(24);CHECK(mac_monitor_live->inventory.banks[0].header.count==1 && mac_monitor_live->inventory.banks[1].header.count==1);
    CHECK(mac_monitor_draw(false)==ESP_OK);
    cerberus_mac_monitor_record_t painted=mac_monitor_live->cache.rows[0];
    /* Publish a newer rank without painting it; the tap keeps the visible row. */
    unsigned other=painted.row.radio?0:1;for(unsigned n=0;n<9;++n)add(other,other?2:1);
    for(unsigned n=0;n<40;++n){fake.now+=100000;for(unsigned r=0;r<2;++r)fake.radios[r].source+=100000;
        cerberus_mac_monitor_session_tick(&mac_monitor_live->session,&mac_monitor_live->inventory,&mac_monitor_live->dispatch);}
    before=radio_calls();actual_mac_route(20,140);CHECK(radio_calls()==before && mac_monitor_live->detail);
    CHECK(!memcmp(&painted,&mac_monitor_live->inventory.selected.frozen,sizeof(painted)));
    CHECK(mac_monitor_live->inventory.selected.latest.valid);actual_mac_route(20,440);CHECK(!mac_monitor_live->detail && radio_calls()==before);
    CHECK(!mac_monitor_live->inventory.selected.valid);
    /* Lost ACK keeps exactly the pinned publication, while the other radio progresses. */
    while(!mac_monitor_live->session.radios[0].pending_ack)advance_live(100000,false);
    uint32_t publication=mac_monitor_live->inventory.banks[0].publications;
    unsigned reads=fake.radios[0].calls[READ];
    for(unsigned n=0;n<6;++n){fake.radios[0].fail[ACK]=-77;advance_live(100000,false);
        CHECK(mac_monitor_live->inventory.banks[0].publications==publication && fake.radios[0].calls[READ]==reads);}
    advance_live(100000,false);CHECK(!mac_monitor_live->session.radios[0].pending_ack);
    fake.radios[0].held=true;fake.radios[1].fail[STOP]=-8;before=frees;actual_mac_route(20,440);advance_live(100000,false);
    CHECK(mac_monitor_live && mac_monitor_live->exit_pending && frees==before);
    CHECK(mac_monitor_live->session.radios[0].owned && mac_monitor_live->session.radios[1].owned);
    advance_live(300000,false);CHECK(mac_monitor_live && !mac_monitor_live->session.radios[1].owned && frees==before);
    fake.radios[0].held=false;advance_live(300000,false);CHECK(!mac_monitor_live && frees==before+1);

    reset_live();open_live();start_live();fake.radios[1].held=true;current_page=UI_PAGE_MAIN;
    advance_live(100000,false);CHECK(mac_monitor_live && mac_monitor_live->exit_pending);
    fake.radios[1].held=false;advance_live(300000,false);CHECK(!mac_monitor_live && current_page==UI_PAGE_MAIN);
    reset_live();open_live();start_live();fake.radios[0].fail[STOP]=-7;
    advance_live(100000,true);CHECK(mac_monitor_live && mac_monitor_keeps_awake());advance_live(300000,true);
    CHECK(mac_monitor_live && cerberus_mac_monitor_session_released(&mac_monitor_live->session) && !mac_monitor_keeps_awake());
    CHECK(mac_monitor_draw(false)==ESP_OK);close_live();
    reset_live();open_live();takeover=false;CHECK(mac_monitor_draw(false)==ESP_OK);actual_mac_route(180,440);steps(3);CHECK(!radio_calls());close_live();
    for(unsigned n=0;n<20;++n){reset_live();open_live();start_live();close_live();}
    CHECK(allocations==frees && !allocated && !other_routes);
    printf("MAC actual live UI: %u checks passed; %u balanced PSRAM lifetimes; store %zu bytes\n",checks,allocations,sizeof(mac_monitor_live_store_t));
    return 0;
}
