#include "csi_ui.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CSI_BG UINT16_C(0x0042)
#define CSI_PANEL UINT16_C(0x08A4)
#define CSI_BORDER UINT16_C(0x21A9)
#define CSI_TEXT UINT16_C(0xE77F)
#define CSI_MUTED UINT16_C(0x7CB5)
#define CSI_CYAN UINT16_C(0x471B)
#define CSI_AMBER UINT16_C(0xFDC4)
#define CSI_RED UINT16_C(0xF38B)

static void safe_text(char *out, size_t capacity, const uint8_t *text, size_t length)
{
    size_t n=length<capacity-1U?length:capacity-1U;
    for(size_t i=0;i<n;++i)out[i]=text[i]>=32U && text[i]<=126U?(char)text[i]:'?';
    out[n]=0;
}

static bool make_model(const cerberus_csi_ui_view_t *view, cerberus_csi_ui_model_t *out)
{
    if(!view || !view->session || !out)return false;
    const cerberus_csi_session_view_t *s=view->session;
    const cerberus_csi_motion_view_t *m=&s->motion;
    if(s->target.ssid_length>32U || m->history_count>CERBERUS_CSI_MOTION_HISTORY ||
        m->history_head>=CERBERUS_CSI_MOTION_HISTORY || m->score>100U || m->reliability>100U ||
        m->calibration_percent>100U || m->calibration_windows>CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS ||
        s->state>CERBERUS_CSI_SESSION_ERROR || m->state>CERBERUS_CSI_MOTION_DETECTED)return false;
    for(size_t i=0;i<CERBERUS_CSI_MOTION_FEATURES;++i)if(m->feature_scores[i]>100U)return false;
    memset(out,0,sizeof(*out));
    char name[33],source[36];
    safe_text(name,sizeof(name),s->target.ssid,s->target.ssid_length);
    const char *label=view->source_label?view->source_label:"SELECTED AP";
    size_t length=0;while(label[length] && length<35U)++length;
    safe_text(source,sizeof(source),(const uint8_t *)label,length);
    snprintf(out->lines[0],49,"AP %s",s->target.ssid_length?name:"<HIDDEN>");
    snprintf(out->lines[1],49,"%s / CH %u",source,s->target.channel);
    const char *state="READY";out->state_color=CSI_CYAN;
    if(s->state==CERBERUS_CSI_SESSION_STARTING)state="STARTING";
    else if(s->state==CERBERUS_CSI_SESSION_STOPPING)state="STOPPING";
    else if(s->state==CERBERUS_CSI_SESSION_STOPPED)state="STOPPED";
    else if(s->state==CERBERUS_CSI_SESSION_ERROR){state=s->owned?"CLEANUP REQUIRED":"ERROR";out->state_color=CSI_RED;}
    else if(s->state==CERBERUS_CSI_SESSION_RUNNING){
        if(s->link_stale){state="LINK STALE / LOW DATA";out->state_color=CSI_AMBER;}
        else if(s->source_stale){state="SAMPLE STALE / LOW DATA";out->state_color=CSI_AMBER;}
        else switch(m->state){
            case CERBERUS_CSI_MOTION_CALIBRATING:state="CALIBRATING";break;
            case CERBERUS_CSI_MOTION_LOW_DATA:state="LOW DATA";out->state_color=CSI_AMBER;break;
            case CERBERUS_CSI_MOTION_STILL:state="STILL";break;
            case CERBERUS_CSI_MOTION_POSSIBLE:state="POSSIBLE MOTION";out->state_color=CSI_AMBER;break;
            case CERBERUS_CSI_MOTION_DETECTED:state="MOTION";out->state_color=CSI_RED;break;
        }
    }
    snprintf(out->lines[2],49,"%s",state);
    if(s->recalibrating)snprintf(out->lines[3],49,"RECALIBRATION WAITS FOR CAPTURE RELEASE");
    else if(!m->calibrated)snprintf(out->lines[3],49,"KEEP STILL / CAL %u/30 GOOD SECONDS",m->calibration_windows);
    else snprintf(out->lines[3],49,"CALIBRATED / 160 SECOND HISTORY");
    if(s->state==CERBERUS_CSI_SESSION_RUNNING && (s->link_stale || s->source_stale)){
        snprintf(out->lines[4],49,"SCORE -- / REL 0%% / RATE --");
        snprintf(out->lines[5],49,"AMPLITUDE -- / SHAPE --");
        snprintf(out->lines[6],49,"PHASE -- / TEMPORAL --");
    }else{
        snprintf(out->lines[4],49,"SCORE %u / REL %u%% / RATE %u/s",m->score,m->reliability,m->samples_per_second);
        snprintf(out->lines[5],49,"AMPLITUDE %u / SHAPE %u",m->feature_scores[0],m->feature_scores[1]);
        snprintf(out->lines[6],49,"PHASE %u / TEMPORAL %u",m->feature_scores[2],m->feature_scores[3]);
    }
    snprintf(out->lines[7],49,"C5 ACCEPTED %" PRIu32,s->accepted);
    snprintf(out->lines[8],49,"S3 USED %" PRIu32 " / REJECTED %" PRIu32,m->samples_used,m->samples_rejected);
    snprintf(out->lines[9],49,"C5 FULL %" PRIu32 " / INVALID %" PRIu32,s->overflow,s->invalid);
    if(s->error)snprintf(out->lines[10],49,"ERROR %" PRId32 " / %s",s->error,s->owned?"STOP RETRIES CLEANUP":"START TO RETRY");
    else if(s->receive_age_valid){
        uint64_t seconds=s->receive_age_ms/1000U+(s->receive_age_ms%1000U!=0U);
        snprintf(out->lines[10],49,"RX CALLBACK AGE <=%" PRIu64 "s",seconds);
    }else snprintf(out->lines[10],49,"RX CALLBACK AGE UNAVAILABLE");
    snprintf(out->lines[11],49,"PASSIVE / SIGNAL VARIATION");
    out->start=!s->busy;
    out->stop=s->busy;
    out->recalibrate=s->state==CERBERUS_CSI_SESSION_RUNNING && s->owned && !s->recalibrating;
    out->history_count=m->history_count;
    for(size_t i=0;i<m->history_count;++i){
        size_t index=(m->history_head+CERBERUS_CSI_MOTION_HISTORY-m->history_count+i)%CERBERUS_CSI_MOTION_HISTORY;
        uint8_t value=m->history[index];
        if(value>100U && value!=CERBERUS_CSI_MOTION_HISTORY_INVALID)return false;
        out->history[i]=value;
    }
    return true;
}

static esp_err_t button(uint16_t x,uint16_t width,const char *label,bool enabled)
{
    esp_err_t error=cerberus_display_fill_rect(x,422,width,44,enabled?CSI_PANEL:CSI_BG);
    if(error!=ESP_OK)return error;
    error=cerberus_display_fill_rect(x,422,width,1,CSI_BORDER);
    if(error!=ESP_OK)return error;
    uint16_t text_x=(uint16_t)(x+(width-strlen(label)*6U)/2U);
    return cerberus_display_draw_text(text_x,440,label,enabled?CSI_TEXT:CSI_MUTED,enabled?CSI_PANEL:CSI_BG,1);
}

esp_err_t cerberus_csi_ui_update(cerberus_csi_ui_cache_t *cache,
    const cerberus_csi_ui_view_t *view,bool force)
{
    if(!cache)return ESP_ERR_INVALID_ARG;
    if(!make_model(view,&cache->staging)){cache->valid=false;return ESP_ERR_INVALID_ARG;}
    const cerberus_csi_ui_model_t *next=&cache->staging,*old=&cache->painted;
    force=force||!cache->valid;
    esp_err_t error;
#define TRY(call) do{error=(call);if(error!=ESP_OK){cache->valid=false;return error;}}while(0)
    if(force){
        TRY(cerberus_display_fill_rect(0,0,320,480,CSI_BG));
        TRY(cerberus_display_draw_text(100,16,"CSI MOTION",CSI_TEXT,CSI_BG,2));
        TRY(cerberus_display_draw_text(16,230,"SCORE / GAPS MEAN INSUFFICIENT DATA",CSI_MUTED,CSI_BG,1));
    }
    static const uint16_t rows[12]={48,64,88,111,129,147,162,180,196,212,374,390};
    for(size_t i=0;i<12U;++i)if(force || strcmp(old->lines[i],next->lines[i]) || (i==2U && old->state_color!=next->state_color)){
        uint8_t scale=i==2U?2U:1U;
        TRY(cerberus_display_fill_rect(16,rows[i],288,(uint16_t)(7U*scale),CSI_BG));
        TRY(cerberus_display_draw_text(16,rows[i],next->lines[i],i==2U?next->state_color:CSI_TEXT,CSI_BG,scale));
    }
    if(force || old->history_count!=next->history_count || memcmp(old->history,next->history,sizeof(next->history))){
        TRY(cerberus_display_fill_rect(16,246,288,114,CSI_PANEL));
        TRY(cerberus_display_fill_rect(16,358,288,1,CSI_BORDER));
        for(size_t i=0;i<next->history_count;++i){
            uint8_t value=next->history[i];
            uint16_t x=(uint16_t)(16U+(CERBERUS_CSI_MOTION_HISTORY-next->history_count+i)*288U/CERBERUS_CSI_MOTION_HISTORY);
            if(value==CERBERUS_CSI_MOTION_HISTORY_INVALID){TRY(cerberus_display_fill_rect(x,358,1,1,CSI_MUTED));continue;}
            uint16_t height=(uint16_t)(1U+(unsigned)value*110U/100U);
            TRY(cerberus_display_fill_rect(x,(uint16_t)(358U-height),1,height,CSI_CYAN));
        }
    }
    if(force)TRY(button(8,68,"BACK",true));
    if(force || old->start!=next->start || old->stop!=next->stop)TRY(button(84,88,next->stop?"STOP":"START",next->start||next->stop));
    if(force || old->recalibrate!=next->recalibrate)TRY(button(180,132,"RECALIBRATE",next->recalibrate));
#undef TRY
    cache->painted=*next;cache->valid=true;
    return ESP_OK;
}

void cerberus_csi_ui_invalidate(cerberus_csi_ui_cache_t *cache)
{if(cache)cache->valid=false;}

cerberus_csi_ui_action_t cerberus_csi_ui_tap(const cerberus_csi_ui_cache_t *cache,uint16_t x,uint16_t y)
{
    if(!cache || !cache->valid || y<422U || y>=466U)return CERBERUS_CSI_UI_NONE;
    if(x>=8U && x<76U)return CERBERUS_CSI_UI_BACK;
    if(x>=84U && x<172U)return cache->painted.stop?CERBERUS_CSI_UI_STOP:
        cache->painted.start?CERBERUS_CSI_UI_START:CERBERUS_CSI_UI_NONE;
    if(x>=180U && x<312U && cache->painted.recalibrate)return CERBERUS_CSI_UI_RECALIBRATE;
    return CERBERUS_CSI_UI_NONE;
}
