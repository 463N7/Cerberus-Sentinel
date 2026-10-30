#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sentinel_watch_view.h"
#include "sentinel_watch.h"
#include "display.h"
#include "esp_check.h"
#include "display_font.h"
static uint16_t screen[320*480];
static unsigned calls;
static void bounds(unsigned x,unsigned y,unsigned w,unsigned h)
{ assert(w && h && x+w<=320 && y+h<=480); ++calls; }
esp_err_t cerberus_display_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color)
{
    bounds(x,y,w,h);
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)screen[(y+row)*320+x+col]=color;
    return ESP_OK;
}
esp_err_t cerberus_display_draw_rgb565_bitmap(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *p)
{
    bounds(x,y,w,h);
    for(unsigned row=0;row<h;++row)memcpy(screen+(y+row)*320+x,p+row*w,w*2);
    return ESP_OK;
}
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *text,uint16_t fg,uint16_t bg,uint8_t scale)
{
    if(!*text)return ESP_OK;
    unsigned length=(unsigned)strlen(text);bounds(x,y,length*6*scale,7*scale);
    for(unsigned c=0;c<length;++c) {
        const uint8_t *glyph=font5x7(text[c]);
        for(unsigned row=0;row<7*scale;++row)for(unsigned col=0;col<6*scale;++col) {
            bool ink=col/scale<5 && (glyph[row/scale]&(1U<<(4-col/scale)));
            screen[(y+row)*320+x+c*6*scale+col]=ink?fg:bg;
        }
    }
    return ESP_OK;
}
/* Exercise the real private controls with framebuffer primitives in place of
 * the enclosing UI task. Font, geometry, handlers and content stay native. */
#define UI_BACKGROUND 0x0042
#define UI_PANEL 0x08A4
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define UI_GOOD 0x471B
#define TAG "host"
typedef enum {WATCH_LIVE,WATCH_HISTORY,WATCH_DETAILS,WATCH_DEMO,WATCH_FILTERS,WATCH_WHY,
    WATCH_OPTIONS,WATCH_ACTIONS,WATCH_SIGNAL} watch_page_t;
static watch_page_t watch_page;
static sentinel_watch_t watch_state;
static sentinel_watch_entry_t watch_detail;
static unsigned watch_filters_page,watch_history_page,watch_demo_scene,recon_confirm;
static bool watch_paused,c5_online=true;
static char recon_message[49];
static esp_err_t draw_centered(uint16_t y,const char *s,uint16_t color,uint8_t scale)
{ return cerberus_display_draw_text((320-strlen(s)*6*scale)/2,y,s,color,UI_BACKGROUND,scale); }
static esp_err_t draw_header(const char *s)
{ return draw_centered(20,s,UI_TEXT,2); }
static esp_err_t draw_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *s)
{
    cerberus_display_fill_rect(x,y,w,h,UI_PANEL);
    return cerberus_display_draw_text(x+(w-strlen(s)*6)/2,y+(h-7)/2,s,UI_TEXT,UI_PANEL,1);
}
#include "recon_controls_ui.h"
static void save(const char *prefix,const char *state)
{
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,state);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i) {
        uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(fclose(f)==0);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    sentinel_watch_view_t v={.radio_online=true,.observation_count=0,.match_count=0,
        .phase="BLUETOOTH LE / PASSIVE / SWEEP 1",.title="RECON / LISTENING",
        .evidence="LISTENING FOR DOCUMENTED DEVICE HINTS",.detail="WI-FI + BLE TAKE TURNS / NO CONNECTIONS"};
    assert(sentinel_watch_view_draw(&v,1600)==ESP_OK); save(argv[1],"live");
    calls=0;assert(sentinel_watch_view_update(&v,1600)==ESP_OK && calls==0);
    assert(sentinel_watch_view_hit_test(&v,250,440)==SENTINEL_WATCH_ACTION_DEMO);
    assert(sentinel_watch_view_hit_test(&v,78,440)==SENTINEL_WATCH_ACTION_NONE);
    assert(sentinel_watch_view_hit_test(&v,10,360)==SENTINEL_WATCH_ACTION_NONE);
    v.paused=true;v.phase="PAUSED / TAP RESUME TO SCAN";
    assert(sentinel_watch_view_update(&v,2000)==ESP_OK);save(argv[1],"paused");
    calls=0;assert(sentinel_watch_view_update(&v,6000)==ESP_OK && calls==0);
    v.paused=false;v.radio_online=false;v.phase="RADIO OFFLINE / WAITING FOR LINK";
    assert(sentinel_watch_view_update(&v,6000)==ESP_OK);save(argv[1],"offline");
    calls=0;assert(sentinel_watch_view_update(&v,10000)==ESP_OK && calls==0);
    v.radio_online=true;v.has_details=true;v.scene=SENTINEL_WATCH_SCENE_FINDMY;
    v.confidence=SENTINEL_WATCH_CONFIDENCE_HIGH;v.title="FIND MY BROADCAST HINT";
    v.phase="FIXTURE PREVIEW / NO LIVE DEVICE";v.evidence="SYNTHETIC TEST EVIDENCE";v.detail="BLE / -62 DBM / TAP FOR EVIDENCE";
    assert(sentinel_watch_view_update(&v,10400)==ESP_OK);save(argv[1],"finding");
    assert(sentinel_watch_view_hit_test(&v,10,360)==SENTINEL_WATCH_ACTION_DETAILS);
    sentinel_watch_init(&watch_state); watch_page=WATCH_OPTIONS;
    assert(recon_draw_options()==ESP_OK);save(argv[1],"options");
    assert(!recon_handle_control_tap(20,120,0)); /* vertical button gap */
    assert(recon_handle_control_tap(20,80,0) && watch_state.minimum_confidence==SENTINEL_WATCH_CONFIDENCE_HIGH);
    assert(recon_handle_control_tap(20,134,0) && watch_state.muted);
    sentinel_watch_match_t match={SENTINEL_WATCH_CAMERA,SENTINEL_WATCH_CONFIDENCE_HIGH,"fixture","Fixture camera","Synthetic evidence"};
    uint8_t mac[6]={0,1,2,3,4,5};
    sentinel_watch_observe(&watch_state,SENTINEL_WATCH_RADIO_BLE,mac,0,"unit",4,-62,&match,1000);
    watch_detail=*sentinel_watch_history(&watch_state,0);watch_page=WATCH_ACTIONS;
    assert(recon_draw_actions()==ESP_OK);save(argv[1],"actions");
    assert(!recon_handle_control_tap(159,250,1000)); /* horizontal snooze gap */
    assert(recon_handle_control_tap(20,170,1000) && sentinel_watch_is_ignored(&watch_state,&watch_detail));
    assert(recon_handle_control_tap(20,250,1000) && watch_state.policies[0].snooze_duration_ms==600000);
    assert(recon_handle_control_tap(200,310,1000) && watch_state.policies[0].snooze_duration_ms==14400000);
    assert(recon_handle_control_tap(20,110,1000) && watch_page==WATCH_SIGNAL);
    assert(recon_draw_signal(2000)==ESP_OK);save(argv[1],"signal");
    assert(recon_draw_signal(40000)==ESP_OK);save(argv[1],"stale");
    assert(recon_handle_control_tap(100,440,1000) && watch_page==WATCH_ACTIONS);
    watch_page=WATCH_OPTIONS;
    assert(recon_handle_control_tap(20,290,1000) && watch_state.history_count==1 && recon_confirm==5);
    assert(recon_handle_control_tap(20,290,1000) && watch_state.history_count==0);
    puts("PASS: native Recon rendering bounds, unchanged-frame caching, paused/offline animation and touch gaps.");
    puts("PASS: native options/actions/signal layouts and handlers; stale signal, snooze durations and clear confirmation.");
}
