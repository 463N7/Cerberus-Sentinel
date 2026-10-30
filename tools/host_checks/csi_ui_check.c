#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csi_ui.h"
#include "display_font.h"
static uint16_t pixels[480][320], reference[480][320];
static unsigned calls, text_calls, fail_at, checks;
static char drawn[16384];
static cerberus_csi_ui_cache_t cache, reference_cache;
static cerberus_csi_session_view_t session;
static cerberus_csi_ui_view_t view;
static const char *prefix;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr,"CHECK failed at %s:%d: %s\n",__FILE__,__LINE__,#condition); abort(); } } while (0)
esp_err_t cerberus_display_fill_rect(uint16_t x, uint16_t y, uint16_t width,
    uint16_t height, uint16_t color)
{
    CHECK((unsigned)x+width<=320U && (unsigned)y+height<=480U);
    if (++calls==fail_at) return ESP_FAIL;
    for(unsigned iy=y;iy<(unsigned)y+height;++iy)
        for(unsigned ix=x;ix<(unsigned)x+width;++ix) pixels[iy][ix]=color;
    return ESP_OK;
}

esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *text,
    uint16_t fg,uint16_t bg,uint8_t scale)
{
    CHECK(scale==1U || scale==2U);
    CHECK((unsigned)x+strlen(text)*6U*scale<=320U && (unsigned)y+7U*scale<=480U);
    if(++calls==fail_at) return ESP_FAIL;
    ++text_calls;
    size_t used=strlen(drawn);
    CHECK(used+strlen(text)+2U<sizeof(drawn));
    snprintf(drawn+used,sizeof(drawn)-used,"%s\n",text);
    for(size_t i=0;text[i];++i){
        const uint8_t *font=font5x7((unsigned char)text[i]);
        for(unsigned cy=0;cy<7;++cy)for(unsigned cx=0;cx<6;++cx)
            for(unsigned sy=0;sy<scale;++sy)for(unsigned sx=0;sx<scale;++sx)
                pixels[y+cy*scale+sy][x+(i*6U+cx)*scale+sx]=
                    cx<5U && (font[cy]&(1U<<(4U-cx)))?fg:bg;
    }
    return ESP_OK;
}

static void save(const char *name)
{
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name);
    FILE *file=fopen(path,"wb");CHECK(file!=NULL);
    CHECK(fprintf(file,"P6\n320 480\n255\n")>0);
    for(unsigned y=0;y<480;++y)for(unsigned x=0;x<320;++x){
        uint16_t p=pixels[y][x];
        uint8_t rgb[3]={(uint8_t)(((p>>11)&31U)*255U/31U),
            (uint8_t)(((p>>5)&63U)*255U/63U),(uint8_t)((p&31U)*255U/31U)};
        if(fwrite(rgb,1,3,file)!=3U){fprintf(stderr,"Cannot write preview %s\n",path);abort();}
    }
    CHECK(fclose(file)==0);
}

static void reset(void)
{
    memset(&cache,0,sizeof(cache));memset(&session,0,sizeof(session));
    memset(pixels,0xA5,sizeof(pixels));drawn[0]=0;calls=text_calls=fail_at=0;
    session.target=(cerberus_csi_target_t){.bssid={2,3,4,5,6,7},.channel=6,.ssid_length=7,.ssid={'T','E','S','T',' ','A','P'}};
    view=(cerberus_csi_ui_view_t){.session=&session,.source_label="AP-01AB23"};
}

static void update(bool force)
{drawn[0]=0;CHECK(cerberus_csi_ui_update(&cache,&view,force)==ESP_OK);}
static void action(unsigned x,unsigned y,cerberus_csi_ui_action_t expected)
{CHECK(cerberus_csi_ui_tap(&cache,(uint16_t)x,(uint16_t)y)==expected);}
static void equals_full_render(void)
{
    memcpy(reference,pixels,sizeof(reference));memset(&reference_cache,0,sizeof(reference_cache));
    drawn[0]=0;CHECK(cerberus_csi_ui_update(&reference_cache,&view,true)==ESP_OK);
    CHECK(!memcmp(reference,pixels,sizeof(pixels)));
}

static void controls(void)
{
    reset();action(100,440,CERBERUS_CSI_UI_NONE);update(true);save("ready");
    CHECK(strstr(drawn,"TEST AP")&&strstr(drawn,"READY")&&strstr(drawn,"KEEP STILL"));
    action(8,422,CERBERUS_CSI_UI_BACK);action(75,465,CERBERUS_CSI_UI_BACK);
    action(84,422,CERBERUS_CSI_UI_START);action(171,465,CERBERUS_CSI_UI_START);
    action(200,440,CERBERUS_CSI_UI_NONE);
    for(unsigned x=0;x<320;++x){
        cerberus_csi_ui_action_t expected=(x>=8&&x<76)?CERBERUS_CSI_UI_BACK:
            (x>=84&&x<172)?CERBERUS_CSI_UI_START:CERBERUS_CSI_UI_NONE;
        action(x,421,CERBERUS_CSI_UI_NONE);action(x,422,expected);
        action(x,465,expected);action(x,466,CERBERUS_CSI_UI_NONE);
    }
    unsigned before=calls;update(false);CHECK(calls==before);
    session.state=CERBERUS_CSI_SESSION_RUNNING;session.busy=session.owned=true;
    session.motion.calibration_windows=12;session.motion.calibration_percent=40;
    session.motion.samples_per_second=20;session.motion.reliability=100;
    update(false);save("calibrating");CHECK(strstr(drawn,"12/30 GOOD SECONDS"));
    action(100,440,CERBERUS_CSI_UI_STOP);action(180,422,CERBERUS_CSI_UI_RECALIBRATE);
    action(311,465,CERBERUS_CSI_UI_RECALIBRATE);action(312,440,CERBERUS_CSI_UI_NONE);
    equals_full_render();
    session.state=CERBERUS_CSI_SESSION_STOPPING;session.recalibrating=true;
    /* A tap refers to the controls actually painted, not the mutated input. */
    action(200,440,CERBERUS_CSI_UI_RECALIBRATE);
    update(false);CHECK(strstr(drawn,"WAITS FOR CAPTURE RELEASE"));
    action(200,440,CERBERUS_CSI_UI_NONE);action(100,440,CERBERUS_CSI_UI_STOP);equals_full_render();
    session.state=CERBERUS_CSI_SESSION_ERROR;session.error=INT32_MIN;
    update(false);save("cleanup");CHECK(strstr(drawn,"CLEANUP REQUIRED")&&strstr(drawn,"STOP RETRIES CLEANUP"));
    action(100,440,CERBERUS_CSI_UI_STOP);equals_full_render();
    session.state=CERBERUS_CSI_SESSION_STOPPED;session.busy=session.owned=session.recalibrating=false;session.error=0;
    update(false);action(100,440,CERBERUS_CSI_UI_START);equals_full_render();
}

static void history_and_extremes(void)
{
    reset();session.state=CERBERUS_CSI_SESSION_RUNNING;session.busy=session.owned=true;
    session.motion.calibrated=true;session.motion.calibration_windows=30;session.motion.calibration_percent=100;
    session.motion.state=CERBERUS_CSI_MOTION_DETECTED;session.motion.score=85;session.motion.reliability=92;
    session.motion.samples_per_second=120;session.motion.samples_used=41200;
    session.accepted=41232;session.invalid=8;session.overflow=24;
    session.motion.history_count=160;session.motion.history_head=17;
    for(unsigned i=0;i<160;++i)session.motion.history[i]=(uint8_t)(i%101U);
    session.motion.history[24]=CERBERUS_CSI_MOTION_HISTORY_INVALID;
    session.receive_age_valid=true;session.receive_age_ms=1;
    update(true);save("motion");CHECK(strstr(drawn,"AGE <=1s"));
    for(unsigned head=0;head<160;++head){session.motion.history_head=(uint16_t)head;update(false);equals_full_render();}
    for(unsigned state=0;state<=CERBERUS_CSI_MOTION_DETECTED;++state){session.motion.state=(uint8_t)state;update(false);equals_full_render();}
    session.link_stale=true;update(false);save("low-data");CHECK(strstr(drawn,"LINK STALE / LOW DATA"));
    CHECK(strstr(cache.painted.lines[4],"SCORE -- / REL 0% / RATE --"));
    session.link_stale=false;session.source_stale=true;session.receive_age_ms=5001;
    for(unsigned prior=CERBERUS_CSI_MOTION_STILL;prior<=CERBERUS_CSI_MOTION_DETECTED;++prior){
        session.motion.state=(uint8_t)prior;session.motion.score=85;session.motion.reliability=92;
        update(false);CHECK(!strcmp(cache.painted.lines[2],"SAMPLE STALE / LOW DATA"));
        CHECK(!strcmp(cache.painted.lines[4],"SCORE -- / REL 0% / RATE --"));
        CHECK(!strcmp(cache.painted.lines[5],"AMPLITUDE -- / SHAPE --"));
        CHECK(!strcmp(cache.painted.lines[6],"PHASE -- / TEMPORAL --"));
        CHECK(session.motion.state==prior && session.motion.score==85 && session.motion.reliability==92);
        CHECK(cache.painted.history_count==160);equals_full_render();
    }
    save("sample-stale");session.source_stale=false;session.receive_age_ms=1;update(false);
    CHECK(!strcmp(cache.painted.lines[2],"MOTION") && strstr(cache.painted.lines[4],"SCORE 85 / REL 92%"));
    equals_full_render();
    session.target.ssid_length=32;memset(session.target.ssid,'W',32);session.target.ssid[4]=0;session.target.ssid[5]=255;
    view.source_label="012345678901234567890123456789012345678901234567890123456789";
    session.accepted=session.invalid=session.overflow=session.acked=UINT32_MAX;
    session.motion.samples_used=session.motion.samples_rejected=UINT32_MAX;
    session.motion.samples_per_second=UINT16_MAX;session.motion.score=session.motion.reliability=100;
    for(unsigned i=0;i<4;++i)session.motion.feature_scores[i]=100;
    session.receive_age_ms=UINT64_MAX;session.target.channel=177;
    update(false);save("maximum");equals_full_render();
    session.link_stale=true;update(false);save("maximum-stale");
    CHECK(!strcmp(cache.painted.lines[4],"SCORE -- / REL 0% / RATE --"));equals_full_render();
    session.motion.history_count=0;session.motion.history_head=0;memset(session.motion.history,0,sizeof(session.motion.history));
    session.target.ssid_length=0;session.receive_age_valid=false;update(false);equals_full_render();
}

static void failed_paint(void)
{
    reset();update(true);session.state=CERBERUS_CSI_SESSION_RUNNING;session.busy=session.owned=true;
    fail_at=calls+3;CHECK(cerberus_csi_ui_update(&cache,&view,false)!=ESP_OK);
    CHECK(!cache.valid);action(100,440,CERBERUS_CSI_UI_NONE);fail_at=0;update(false);equals_full_render();
    session.motion.history_count=161;CHECK(cerberus_csi_ui_update(&cache,&view,false)==ESP_ERR_INVALID_ARG);
    CHECK(!cache.valid);session.motion.history_count=1;session.motion.history[0]=101;
    session.motion.history_head=1;CHECK(cerberus_csi_ui_update(&cache,&view,true)==ESP_ERR_INVALID_ARG);
    session.motion.history[0]=0;update(true);cerberus_csi_ui_invalidate(&cache);action(8,440,CERBERUS_CSI_UI_NONE);
    session.motion.feature_scores[3]=101;CHECK(cerberus_csi_ui_update(&cache,&view,true)==ESP_ERR_INVALID_ARG);
    CHECK(cerberus_csi_ui_update(NULL,&view,true)==ESP_ERR_INVALID_ARG);
    CHECK(cerberus_csi_ui_update(&cache,NULL,true)==ESP_ERR_INVALID_ARG);
}

int main(int argc,char **argv)
{
    CHECK(argc==2);prefix=argv[1];controls();history_and_extremes();failed_paint();
    printf("CSI UI: %u checks passed; cache %zu bytes; render/hit tests have no radio operations\n",checks,sizeof(cache));
    return 0;
}
