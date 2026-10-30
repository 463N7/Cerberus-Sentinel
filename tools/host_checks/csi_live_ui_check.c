/* Reuse the deterministic transport boundary; test the actual private UI
 * header here, not a second imitation of the session controller. */
#define main csi_session_fixture_main
#include "csi_session_check.c"
#undef main
#include "csi_ui.h"
#include "csi_link.h"

enum {UI_PAGE_WIFI,UI_PAGE_HOME,UI_WIFI_MODE_TARGET,UI_WIFI_MODE_CSI};
enum {MALLOC_CAP_SPIRAM=4,MALLOC_CAP_8BIT=2};
static unsigned current_page=UI_PAGE_WIFI,wifi_mode=UI_WIFI_MODE_TARGET;
static bool wifi_selection_valid,allocation_fail;
static struct {uint8_t bssid[6],channel;char ssid[33];} wifi_selected_result;
static unsigned allocations,frees,detail_draws,draw_calls,draw_errors;
static void *allocated;
static void *heap_caps_calloc(size_t count,size_t size,unsigned caps)
{
    CHECK(count==1 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(allocation_fail)return NULL;
    CHECK(!allocated);allocated=calloc(count,size);CHECK(allocated!=NULL);++allocations;return allocated;
}
static void monitored_free(void *memory)
{CHECK(memory && memory==allocated);++frees;free(memory);allocated=NULL;}
static int64_t esp_timer_get_time(void){return (int64_t)fake.now;}
static bool privacy_addresses_masked(void){return true;}
static void cerberus_privacy_format_wifi_identifier(const uint8_t *bssid,bool masked,char *out,size_t capacity)
{CHECK(masked);CHECK(bssid[5]==7);snprintf(out,capacity,"AP-MASKED");}
static esp_err_t draw_detail_page(void){++detail_draws;return ESP_OK;}
void cerberus_csi_link_ops(cerberus_csi_ops_t *out){*out=ops;}
esp_err_t cerberus_display_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color)
{(void)color;CHECK((unsigned)x+w<=320 && (unsigned)y+h<=480);++draw_calls;return ESP_OK;}
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *text,uint16_t fg,uint16_t bg,uint8_t scale)
{(void)fg;(void)bg;CHECK(x+strlen(text)*6U*scale<=320 && y+7U*scale<=480);++draw_calls;return ESP_OK;}
#define ESP_ERROR_CHECK(e) do{esp_err_t error=(e);if(error){++draw_errors;CHECK(error==ESP_OK);}}while(0)
#define free monitored_free
#include "csi_live_ui.h"
#undef free

static void reset_live(void)
{
    CHECK(!csi_live && !allocated);setup();
    current_page=UI_PAGE_WIFI;wifi_mode=UI_WIFI_MODE_TARGET;allocation_fail=false;
    cerberus_csi_target_t t=target();wifi_selection_valid=true;
    memcpy(wifi_selected_result.bssid,t.bssid,6);wifi_selected_result.channel=t.channel;
    memset(wifi_selected_result.ssid,0,sizeof(wifi_selected_result.ssid));memcpy(wifi_selected_result.ssid,t.ssid,t.ssid_length);
}
static void live_tick(uint64_t elapsed,bool visible)
{fake.now+=elapsed;fake.source+=elapsed;csi_live_tick(visible);}
static void open_live(void)
{CHECK(csi_open_selected()==ESP_OK);CHECK(csi_live&&wifi_mode==UI_WIFI_MODE_CSI);CHECK(csi_draw(true)==ESP_OK);}
static void start_live(void)
{
    unsigned starts=fake.calls[START];csi_handle_tap(100,440);CHECK(fake.calls[START]==starts);
    live_tick(1,true);CHECK(fake.calls[START]==starts+1 && csi_live->session.owned);
    CHECK(csi_draw(false)==ESP_OK);
}
static void close_live(void)
{
    csi_handle_tap(20,440);if(csi_live)live_tick(250000,true);
    CHECK(!csi_live && !allocated && wifi_mode==UI_WIFI_MODE_TARGET);
}

int main(void)
{
    reset_live();wifi_selection_valid=false;CHECK(csi_open_selected()==ESP_ERR_INVALID_STATE && !allocations);
    wifi_selection_valid=true;wifi_selected_result.channel=0;CHECK(csi_open_selected()==ESP_ERR_INVALID_ARG && !allocations);
    wifi_selected_result.channel=6;allocation_fail=true;CHECK(csi_open_selected()==ESP_ERR_NO_MEM && !allocations);
    allocation_fail=false;open_live();CHECK(allocations==1 && !fake.calls[RESERVE] && !fake.calls[START]);
    wifi_selected_result.bssid[5]=99;wifi_selected_result.channel=36;strcpy(wifi_selected_result.ssid,"OTHER AP");
    CHECK(csi_live->session.target.bssid[5]==7 && csi_live->session.target.channel==6);
    CHECK(csi_draw(false)==ESP_OK);CHECK(!fake.calls[START]);start_live();
    CHECK(fake.target.bssid[5]==7 && fake.target.channel==6);
    CHECK(csi_open_selected()==ESP_ERR_INVALID_STATE && allocations==1);
    uint64_t epoch=csi_live->session.binding.epoch;unsigned starts=fake.calls[START];
    csi_handle_tap(200,440);CHECK(csi_live->session.recalibrate_requested && fake.calls[START]==starts);
    fake.failure[STOP]=-7;live_tick(1,true);CHECK(csi_live && csi_live->session.owned && allocations==1);
    live_tick(250000,true);CHECK(csi_live && csi_live->session.start_requested && fake.calls[START]==starts);
    live_tick(1,true);CHECK(csi_live->session.binding.epoch>epoch && fake.calls[START]==starts+1 && allocations==1);
    CHECK(csi_draw(false)==ESP_OK);fake.stop_held=true;csi_handle_tap(20,440);live_tick(1,true);
    CHECK(csi_live && csi_live->exit_pending && csi_live->session.owned && !frees);
    fake.stop_held=false;fake.radio_error=0;live_tick(250000,true);
    CHECK(!csi_live && frees==1 && detail_draws==1 && wifi_mode==UI_WIFI_MODE_TARGET);

    reset_live();open_live();start_live();fake.failure[STOP]=-9;
    live_tick(1,false);CHECK(csi_live && csi_live->session.stop_requested);
    live_tick(250000,false);CHECK(csi_live && cerberus_csi_session_released(&csi_live->session));
    CHECK(wifi_mode==UI_WIFI_MODE_CSI); /* Asleep on this page keeps only stopped presentation. */
    CHECK(csi_draw(false)==ESP_OK);close_live();

    reset_live();open_live();start_live();current_page=UI_PAGE_HOME;unsigned old_draws=detail_draws;
    fake.stop_held=true;live_tick(1,false);CHECK(csi_live && csi_live->session.owned);
    fake.stop_held=false;fake.radio_error=0;live_tick(250000,false);
    CHECK(!csi_live && !allocated && detail_draws==old_draws && current_page==UI_PAGE_HOME);

    reset_live();open_live();fake.failure[RESERVE]=ESP_ERR_NOT_SUPPORTED;
    csi_handle_tap(100,440);live_tick(1,true);CHECK(!fake.calls[START] && !csi_live->session.owned);
    CHECK(csi_live->session.error==ESP_ERR_NOT_SUPPORTED);close_live();

    reset_live();open_live();fake.corrupt=3;csi_handle_tap(100,440);live_tick(1,true);
    CHECK(!csi_live->session.owned && fake.owned && !csi_live->session.binding.epoch);
    CHECK(csi_live->session.error==CERBERUS_CSI_SESSION_BAD_SOURCE);
    CHECK(csi_draw(false)==ESP_OK);close_live();
    CHECK(!fake.calls[STOP] && !fake.calls[RENEW] && !fake.calls[READ]);

    reset_live();open_live();start_live();
    for(unsigned i=0;i<320;++i){
        fake.now+=100000;fake.source+=100000;enqueue(fake.source);csi_live_tick(true);
    }
    CHECK(csi_live->session.model.view.calibrated);
    fake.now+=1000;fake.source+=1000;enqueue(fake.source);fake.failure[ACK]=-8;csi_live_tick(true);
    cerberus_csi_motion_model_t retained=csi_live->session.model;
    for(unsigned i=0;i<20;++i){fake.failure[ACK]=-8;live_tick(250000,true);}
    CHECK(csi_draw(false)==ESP_OK && !csi_live->view.link_stale && csi_live->view.source_stale);
    CHECK(!strcmp(csi_live->cache.painted.lines[2],"SAMPLE STALE / LOW DATA"));
    CHECK(!strcmp(csi_live->cache.painted.lines[4],"SCORE -- / REL 0% / RATE --"));
    CHECK(!memcmp(&retained,&csi_live->session.model,sizeof(retained)));
    live_tick(250000,true);enqueue(fake.source);live_tick(1,true);live_tick(250000,true);
    CHECK(csi_draw(false)==ESP_OK && !csi_live->view.source_stale);
    CHECK(strcmp(csi_live->cache.painted.lines[4],"SCORE -- / REL 0% / RATE --"));close_live();

    for(unsigned i=0;i<40;++i){reset_live();open_live();start_live();close_live();}
    CHECK(allocations==frees && !allocated && !draw_errors);
    printf("CSI actual live UI: %u checks passed; %u balanced PSRAM lifetimes; store %zu bytes; no RF on open/paint\n",checks,allocations,sizeof(csi_live_store_t));
    return 0;
}
