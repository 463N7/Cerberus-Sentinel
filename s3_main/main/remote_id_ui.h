#pragma once

/* Caller-owned, allocation-free Remote ID presentation. No radio/scheduler
 * operation occurs here. SELECT returns the row copied after successful paint,
 * never a mutable inventory index. Receipt ages and conservative qualifying
 * receive-callback age bounds are separate; neither dates individual fields. */
#include "remote_id_inventory.h"
#include "sentinel_watch_protocol.h"
#include "display.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    CERBERUS_REMOTE_ID_UI_IDLE,
    CERBERUS_REMOTE_ID_UI_RUNNING,
    CERBERUS_REMOTE_ID_UI_STOPPING,
    CERBERUS_REMOTE_ID_UI_STOPPED,
    CERBERUS_REMOTE_ID_UI_ERROR,
} cerberus_remote_id_ui_state_t;

typedef struct {
    cerberus_remote_id_ui_state_t state;
    bool detail, start_available, stop_available, back_available;
    bool follow_available, follow_enabled, motion_detail;
    uint64_t now_ms;
    cerberus_remote_id_page_t page;
    cerberus_remote_id_selection_t selection;
    /* Already privacy-formatted identifiers; raw MAC bytes are never drawn. */
    const char *source_labels[CERBERUS_REMOTE_ID_PAGE_ROWS];
    const char *selected_source_label;
    const char *wifi_status, *ble_status, *message;
    uint32_t wifi_dropped_reports, ble_dropped_reports;
} cerberus_remote_id_ui_view_t;

typedef enum {
    CERBERUS_REMOTE_ID_UI_ACTION_NONE,
    CERBERUS_REMOTE_ID_UI_ACTION_START,
    CERBERUS_REMOTE_ID_UI_ACTION_STOP,
    CERBERUS_REMOTE_ID_UI_ACTION_BACK,
    CERBERUS_REMOTE_ID_UI_ACTION_SELECT,
    CERBERUS_REMOTE_ID_UI_ACTION_PREV,
    CERBERUS_REMOTE_ID_UI_ACTION_NEXT,
    CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW,
    CERBERUS_REMOTE_ID_UI_ACTION_UNFOLLOW,
    CERBERUS_REMOTE_ID_UI_ACTION_MOTION,
    CERBERUS_REMOTE_ID_UI_ACTION_DETAILS,
} cerberus_remote_id_ui_action_t;

typedef struct {
    cerberus_remote_id_ui_action_t action;
    cerberus_remote_id_record_t selected;
} cerberus_remote_id_ui_event_t;

typedef struct {
    char wifi[49], ble[49], state[25], summary[49];
    char rows[4][3][49], detail[12][49], footer[2][49];
    uint16_t row_colors[4];
    bool detail_mode, back, previous, next, start, stop;
    bool follow_visible, follow_available, follow_enabled;
    bool motion_mode, motion_available;
} cerberus_remote_id_ui_model_t;

typedef struct {
    /* Keep this caller-owned storage in the same PSRAM state as the inventory.
     * Staging avoids placing the formatted page on the UI task's stack. */
    cerberus_remote_id_ui_model_t painted, staging;
    cerberus_remote_id_record_t rows[CERBERUS_REMOTE_ID_PAGE_ROWS];
    uint8_t row_count;
    bool valid;
} cerberus_remote_id_ui_cache_t;

#define RID_UI_BG UINT16_C(0x0042)
#define RID_UI_PANEL UINT16_C(0x08A4)
#define RID_UI_BORDER UINT16_C(0x21A9)
#define RID_UI_TEXT UINT16_C(0xE77F)
#define RID_UI_MUTED UINT16_C(0x7CB5)
#define RID_UI_CYAN UINT16_C(0x471B)
#define RID_UI_VIOLET UINT16_C(0xACDF)

static inline void cerberus_remote_id_ui_text(char *out, size_t capacity,
    const char *text, size_t maximum)
{
    if (!capacity) return;
    size_t used=0U;
    if (text) while (used<maximum && used+1U<capacity && text[used]) {
        unsigned char value=(unsigned char)text[used];
        out[used]=value>=32U && value<=126U ? (char)value : '?';
        ++used;
    }
    out[used]='\0';
}

static inline const char *cerberus_remote_id_ui_transport(uint8_t transport)
{
    if (transport==SENTINEL_WATCH_TRANSPORT_BLE) return "BLE";
    if (transport==SENTINEL_WATCH_TRANSPORT_WIFI_BEACON) return "WI-FI BEACON";
    if (transport==SENTINEL_WATCH_TRANSPORT_WIFI_NAN) return "WI-FI NAN";
    return "UNKNOWN";
}

static inline void cerberus_remote_id_ui_age(char out[49], const char *label,
    uint64_t receipt_ms, uint64_t now_ms, bool valid)
{
    if (!valid || now_ms<receipt_ms) snprintf(out,49,"%s UNAVAILABLE",label);
    else snprintf(out,49,"%s %" PRIu64 "s",label,(now_ms-receipt_ms)/1000U);
}

static inline void cerberus_remote_id_ui_receive_age(char out[49], const char *label,
    const cerberus_watch_receive_timing_t *timing, uint64_t now_ms, bool valid)
{
    uint64_t age_ms;
    if (!valid || !cerberus_watch_receive_age_at_ms(timing, now_ms, &age_ms))
        snprintf(out,49,"%s UNAVAILABLE",label);
    else {
        /* Round upward again for presentation: <=0s must never hide a
         * positive millisecond bound. Division avoids an overflowing +999. */
        uint64_t seconds=age_ms/1000U+(age_ms%1000U!=0U);
        snprintf(out,49,"%s <=%" PRIu64 "s",label,seconds);
    }
}

static inline void cerberus_remote_id_ui_id(const cerberus_watch_observation_t *row,
    char out[24])
{
    if (!(row->flags&SENTINEL_WATCH_PROTOCOL_HAS_ID) || !row->id[0]) {
        snprintf(out,24,"NOT RECEIVED"); return;
    }
    cerberus_remote_id_ui_text(out,24,row->id,SENTINEL_WATCH_PROTOCOL_ID_MAX);
}

static inline void cerberus_remote_id_ui_coordinate(char out[49], const char *label,
    int32_t value)
{
    int64_t signed_value=value;
    uint64_t magnitude=(uint64_t)(signed_value<0 ? -signed_value : signed_value);
    snprintf(out,49,"%s %s%" PRIu64 ".%07" PRIu64,label,
        signed_value<0?"-":"",magnitude/10000000U,magnitude%10000000U);
}

static inline bool cerberus_remote_id_ui_model(const cerberus_remote_id_ui_view_t *view,
    cerberus_remote_id_ui_model_t *out)
{
    if (!view || !out || view->page.copied>CERBERUS_REMOTE_ID_PAGE_ROWS ||
        view->page.total>CERBERUS_REMOTE_ID_CAPACITY || view->page.offset>view->page.total ||
        view->page.copied>view->page.total-view->page.offset) return false;
    memset(out,0,sizeof(*out));
    out->detail_mode=view->detail;
    out->motion_mode=view->detail && view->motion_detail;
    out->motion_available=view->detail && view->selection.valid && view->back_available;
    out->back=view->back_available;
    out->stop=view->stop_available;
    out->start=!out->stop && view->start_available;
    out->previous=!view->detail && view->page.offset>=CERBERUS_REMOTE_ID_PAGE_ROWS;
    out->next=!view->detail && view->page.copied &&
        view->page.offset+view->page.copied<view->page.total;
    out->follow_visible=view->detail && view->selection.valid &&
        view->selection.frozen.radio==CERBERUS_WATCH_RADIO_WIFI;
    out->follow_available=out->follow_visible && view->follow_available;
    out->follow_enabled=out->follow_visible && view->follow_enabled;
    char clean[35];
    cerberus_remote_id_ui_text(clean,sizeof(clean),view->wifi_status?view->wifi_status:"UNAVAILABLE",34);
    snprintf(out->wifi,sizeof(out->wifi),"C5 WI-FI: %s",clean);
    cerberus_remote_id_ui_text(clean,sizeof(clean),view->ble_status?view->ble_status:"UNAVAILABLE",34);
    snprintf(out->ble,sizeof(out->ble),"S3 BLE: %s",clean);
    const char *state=view->state==CERBERUS_REMOTE_ID_UI_RUNNING?"SCANNING":
        view->state==CERBERUS_REMOTE_ID_UI_STOPPING?"STOPPING / WAIT":
        view->state==CERBERUS_REMOTE_ID_UI_STOPPED?"STOPPED":
        view->state==CERBERUS_REMOTE_ID_UI_ERROR?"NEEDS ATTENTION":"READY";
    snprintf(out->state,sizeof(out->state),"%s",state);
    if (view->wifi_dropped_reports || view->ble_dropped_reports)
        snprintf(out->footer[0],49,"DROPPED REPORTS W:%" PRIu32 " B:%" PRIu32,
            view->wifi_dropped_reports,view->ble_dropped_reports);
    else snprintf(out->footer[0],49,"SOURCE COUNTS ARE NOT AIRCRAFT COUNTS");
    cerberus_remote_id_ui_text(out->footer[1],49,view->message ? view->message :
        "SELF-REPORTED BROADCAST DATA",48);
    if (!view->detail) {
        unsigned page=view->page.total?(unsigned)(view->page.offset/4U+1U):0U;
        snprintf(out->summary,49,"%u SOURCES / PAGE %u OF %u",view->page.total,page,
            (unsigned)((view->page.total+3U)/4U));
        for (unsigned i=0;i<view->page.copied;++i) {
            const cerberus_remote_id_record_t *record=&view->page.rows[i];
            char id[24],source[27];
            cerberus_remote_id_ui_id(&record->observation,id);
            snprintf(out->rows[i][0],49,"ID %s",id);
            cerberus_remote_id_ui_text(source,sizeof(source),view->source_labels[i]?view->source_labels[i]:"ADDRESS HIDDEN",26);
            snprintf(out->rows[i][1],49,"%s / %s",
                cerberus_remote_id_ui_transport(record->observation.transport),source);
            cerberus_remote_id_ui_receive_age(out->rows[i][2],"RX CALLBACK AGE",&record->timing,
                view->now_ms,record->sweep_token!=0U);
            out->row_colors[i]=record->radio==CERBERUS_WATCH_RADIO_BLE?RID_UI_VIOLET:RID_UI_CYAN;
        }
        if (!view->page.copied) {
            snprintf(out->detail[0],49,"NO REMOTE ID SOURCES");
            snprintf(out->detail[1],49,"%s",view->state==CERBERUS_REMOTE_ID_UI_RUNNING?
                "WAITING FOR COMPLETED SWEEPS":view->start_available?"START TO SCAN":"CHECK RADIO STATUS ABOVE");
        }
        return true;
    }
    const cerberus_remote_id_selection_t *selection=&view->selection;
    if (!selection->valid) {
        snprintf(out->detail[0],49,"NO SELECTED SOURCE");
        return true;
    }
    const cerberus_remote_id_record_t *frozen=&selection->frozen;
    char source[27],id[24];
    cerberus_remote_id_ui_text(source,sizeof(source),view->selected_source_label?view->selected_source_label:"ADDRESS HIDDEN",26);
    snprintf(out->detail[0],49,"SOURCE %s / %s",cerberus_remote_id_ui_transport(frozen->observation.transport),source);
    cerberus_remote_id_ui_id(&frozen->observation,id);
    snprintf(out->detail[1],49,"SELECTED ID %s",id);
    cerberus_remote_id_ui_age(out->detail[2],"SELECTED SWEEP RECEIPT AGE",
        frozen->receipt_ms,view->now_ms,frozen->sweep_token!=0U);
    bool latest=selection->latest.sweep_token &&
        cerberus_remote_id_source_equal(frozen,&selection->latest);
    snprintf(out->detail[3],49,"%s",selection->radio_unavailable?"RADIO UNAVAILABLE / RETAINED EVIDENCE":
        selection->present&&latest?"PRESENT IN LAST COMPLETED SWEEP":"ABSENT FROM LAST COMPLETED SWEEP");
    cerberus_remote_id_ui_age(out->detail[4],"LAST MATCH RECEIPT AGE",selection->latest.receipt_ms,view->now_ms,latest);
    snprintf(out->detail[5],49,"MATCHING SWEEPS %" PRIu32,selection->matching_sweeps);
    const cerberus_watch_observation_t *row=&selection->latest.observation;
    if(out->motion_mode){
        const sentinel_watch_motion_t *motion=&selection->latest.motion;
        bool location=latest && sentinel_watch_motion_valid(motion) &&
            (motion->flags&SENTINEL_WATCH_MOTION_LOCATION_PRESENT);
        snprintf(out->detail[2],49,"ADVERTISED MOTION");
        if(latest)cerberus_remote_id_ui_id(row,id);else snprintf(id,sizeof(id),"UNAVAILABLE");
        snprintf(out->detail[4],49,"LAST MATCH ID %s",id);
        snprintf(out->detail[5],49,"HORIZONTAL SPEED");
        if(location && (motion->flags&SENTINEL_WATCH_MOTION_SPEED))
            snprintf(out->detail[6],49,"%u.%02u M/S",(unsigned)(motion->speed_qms/4U),
                (unsigned)(motion->speed_qms%4U)*25U);
        else snprintf(out->detail[6],49,"UNAVAILABLE");
        if(location && (motion->flags&SENTINEL_WATCH_MOTION_DIRECTION))
            snprintf(out->detail[7],49,"DIRECTION %u DEG",(unsigned)motion->direction_deg);
        else snprintf(out->detail[7],49,"DIRECTION UNAVAILABLE");
        if(location && (motion->flags&SENTINEL_WATCH_MOTION_HEIGHT)){
            int32_t height=(int32_t)motion->height_encoded*5-10000;
            uint32_t magnitude=(uint32_t)(height<0?-height:height);
            snprintf(out->detail[8],49,"HEIGHT %s%" PRIu32 ".%" PRIu32 " M / %s",
                height<0?"-":"",magnitude/10U,magnitude%10U,
                motion->flags&SENTINEL_WATCH_MOTION_HEIGHT_AGL?"ABOVE GROUND":"FROM TAKEOFF");
        }else snprintf(out->detail[8],49,"HEIGHT UNAVAILABLE");
        cerberus_remote_id_ui_receive_age(out->detail[9],"LOCATION CALLBACK AGE",
            &selection->latest.motion_timing,view->now_ms,location);
        cerberus_remote_id_ui_receive_age(out->detail[10],"SOURCE CALLBACK AGE",
            &selection->latest.timing,view->now_ms,latest);
        snprintf(out->detail[11],49,"%s",location?"VALUES REPORTED BY THIS SOURCE":"NO LOCATION GROUP IN THIS EVIDENCE");
        return true;
    }
    if (latest) cerberus_remote_id_ui_id(row,id);
    else snprintf(id,sizeof(id),"UNAVAILABLE");
    snprintf(out->detail[6],49,"%s",id);
    bool position=latest&&(row->flags&SENTINEL_WATCH_PROTOCOL_HAS_POSITION)&&
        row->latitude_e7>=-900000000 && row->latitude_e7<=900000000 &&
        row->longitude_e7>=-1800000000 && row->longitude_e7<=1800000000;
    if (position) {
        cerberus_remote_id_ui_coordinate(out->detail[7],"LAT",row->latitude_e7);
        cerberus_remote_id_ui_coordinate(out->detail[8],"LON",row->longitude_e7);
    } else snprintf(out->detail[7],49,"POSITION NOT RECEIVED IN THIS EVIDENCE");
    if (latest && (row->flags&SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE) &&
        row->altitude_dm>-10000 && row->altitude_dm<=317675 &&
        (row->altitude_kind==SENTINEL_WATCH_ALTITUDE_GEODETIC ||
         row->altitude_kind==SENTINEL_WATCH_ALTITUDE_PRESSURE)) {
        int64_t altitude=row->altitude_dm;
        uint64_t magnitude=(uint64_t)(altitude<0?-altitude:altitude);
        snprintf(out->detail[9],49,"ALT %s%" PRIu64 ".%" PRIu64 " M / %s",
            altitude<0?"-":"",magnitude/10U,magnitude%10U,
            row->altitude_kind==SENTINEL_WATCH_ALTITUDE_GEODETIC?"WGS84 HAE":"PRESSURE");
    } else snprintf(out->detail[9],49,"ALTITUDE NOT RECEIVED IN THIS EVIDENCE");
    cerberus_remote_id_ui_age(out->detail[10],"LAST CHECK RECEIPT AGE",
        selection->checked_receipt_ms,view->now_ms,selection->checked_sweep_token!=0U);
    cerberus_remote_id_ui_receive_age(out->detail[11],"LAST MATCH RX CALLBACK AGE",
        &selection->latest.timing,view->now_ms,latest);
    return true;
}

static inline esp_err_t cerberus_remote_id_ui_draw_text(uint16_t x,uint16_t y,
    const char *text,uint16_t color,uint16_t background,uint8_t scale,size_t maximum)
{
    char clean[49];cerberus_remote_id_ui_text(clean,sizeof(clean),text,maximum);
    return clean[0]?cerberus_display_draw_text(x,y,clean,color,background,scale):ESP_OK;
}

static inline esp_err_t cerberus_remote_id_ui_button(uint16_t x,uint16_t width,
    const char *label,bool enabled)
{
    esp_err_t error=cerberus_display_fill_rect(x,422,width,44,enabled?RID_UI_PANEL:RID_UI_BG);
    if(error!=ESP_OK)return error;
    error=cerberus_display_fill_rect(x,465,width,1,enabled?RID_UI_CYAN:RID_UI_BORDER);
    if(error!=ESP_OK)return error;
    return cerberus_remote_id_ui_draw_text(x+(uint16_t)((width-strlen(label)*6U)/2U),440,
        label,enabled?RID_UI_TEXT:RID_UI_MUTED,enabled?RID_UI_PANEL:RID_UI_BG,1,12);
}

static inline esp_err_t cerberus_remote_id_ui_update(cerberus_remote_id_ui_cache_t *cache,
    const cerberus_remote_id_ui_view_t *view,bool force)
{
    if(!cache)return ESP_ERR_INVALID_ARG;
    if(!cerberus_remote_id_ui_model(view,&cache->staging)){cache->valid=false;return ESP_ERR_INVALID_ARG;}
    cerberus_remote_id_ui_model_t *next=&cache->staging,*old=&cache->painted;
    force|=!cache->valid || old->detail_mode!=next->detail_mode || old->motion_mode!=next->motion_mode ||
        (!next->detail_mode && ((cache->row_count==0U)!=(view->page.copied==0U)));
    esp_err_t error=ESP_OK;
#define RID_UI_TRY(call) do{error=(call);if(error!=ESP_OK){cache->valid=false;return error;}}while(0)
    if(force)RID_UI_TRY(cerberus_display_fill_rect(0,0,320,480,RID_UI_BG));
    if(force || strcmp(old->wifi,next->wifi) || strcmp(old->ble,next->ble) || strcmp(old->state,next->state)){
        RID_UI_TRY(cerberus_display_fill_rect(0,0,320,104,RID_UI_BG));
        RID_UI_TRY(cerberus_remote_id_ui_draw_text(106,16,"REMOTE ID",RID_UI_TEXT,RID_UI_BG,2,24));
        RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,42,"SELF-REPORTED BROADCASTS",RID_UI_MUTED,RID_UI_BG,1,48));
        RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,58,next->wifi,RID_UI_CYAN,RID_UI_BG,1,48));
        RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,73,next->ble,RID_UI_VIOLET,RID_UI_BG,1,48));
        RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,91,next->state,RID_UI_TEXT,RID_UI_BG,1,48));
    }
    if(!next->detail_mode){
        if(force || strcmp(old->summary,next->summary)){
            RID_UI_TRY(cerberus_display_fill_rect(8,104,304,24,RID_UI_BG));
            RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,112,next->summary,RID_UI_TEXT,RID_UI_BG,1,48));
        }
        for(unsigned i=0;i<4;++i)if(force || memcmp(old->rows[i],next->rows[i],sizeof(next->rows[i])) ||
            old->row_colors[i]!=next->row_colors[i]){
            uint16_t y=(uint16_t)(132U+58U*i);bool filled=next->rows[i][0][0]!=0;
            RID_UI_TRY(cerberus_display_fill_rect(8,y,304,54,filled?RID_UI_PANEL:RID_UI_BG));
            if(filled){
                RID_UI_TRY(cerberus_display_fill_rect(8,y,2,54,next->row_colors[i]));
                RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,y+4,next->rows[i][0],RID_UI_TEXT,RID_UI_PANEL,2,24));
                RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,y+24,next->rows[i][1],RID_UI_MUTED,RID_UI_PANEL,1,48));
                RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,y+40,next->rows[i][2],RID_UI_MUTED,RID_UI_PANEL,1,48));
            }
        }
        if(!view->page.copied && (force || memcmp(old->detail,next->detail,sizeof(next->detail)))){
            RID_UI_TRY(cerberus_display_fill_rect(8,132,304,228,RID_UI_BG));
            RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,210,next->detail[0],RID_UI_TEXT,RID_UI_BG,1,48));
            RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,235,next->detail[1],RID_UI_MUTED,RID_UI_BG,1,48));
        }
    }else if(force || memcmp(old->detail,next->detail,sizeof(next->detail))){
        RID_UI_TRY(cerberus_display_fill_rect(8,104,304,258,RID_UI_BG));
        static const uint16_t y[]={110,129,146,174,194,214,251,293,307,328,350,228};
        static const uint16_t motion_y[]={110,129,146,174,194,216,239,267,291,315,335,352};
        for(unsigned i=0;i<12;++i)RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,next->motion_mode?motion_y[i]:y[i],next->detail[i],
            i==0||i==3?RID_UI_CYAN:RID_UI_TEXT,RID_UI_BG,i==6?2:1,i==6?24:48));
        if(view->selection.valid && !next->motion_mode){
            RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,238,"ADVERTISED ID",RID_UI_MUTED,RID_UI_BG,1,48));
            RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,278,"AIRCRAFT POSITION",RID_UI_MUTED,RID_UI_BG,1,48));
        }
    }
    if(force || memcmp(old->footer,next->footer,sizeof(next->footer))){
        RID_UI_TRY(cerberus_display_fill_rect(8,366,304,46,RID_UI_BG));
        RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,374,next->footer[0],RID_UI_MUTED,RID_UI_BG,1,48));
        RID_UI_TRY(cerberus_remote_id_ui_draw_text(16,394,next->footer[1],RID_UI_MUTED,RID_UI_BG,1,48));
    }
    if(force || old->back!=next->back)RID_UI_TRY(cerberus_remote_id_ui_button(8,68,"BACK",next->back));
    if(force || old->previous!=next->previous || old->follow_visible!=next->follow_visible ||
        old->follow_available!=next->follow_available || old->follow_enabled!=next->follow_enabled)
        RID_UI_TRY(cerberus_remote_id_ui_button(84,68,next->follow_visible?
            (next->follow_enabled?"UNFOLLOW":"FOLLOW"):"PREV",
            next->follow_visible?next->follow_available:next->previous));
    if(force || old->start!=next->start || old->stop!=next->stop)
        RID_UI_TRY(cerberus_remote_id_ui_button(160,68,next->stop?"STOP":"START",next->stop||next->start));
    if(force || old->next!=next->next || old->motion_available!=next->motion_available || old->motion_mode!=next->motion_mode)
        RID_UI_TRY(cerberus_remote_id_ui_button(236,76,next->motion_available?
            (next->motion_mode?"DETAILS":"MOTION"):"NEXT",next->motion_available||next->next));
#undef RID_UI_TRY
    cache->painted=*next;
    cache->row_count=view->detail?0U:view->page.copied;
    if(cache->row_count)memcpy(cache->rows,view->page.rows,cache->row_count*sizeof(cache->rows[0]));
    else if(view->detail && view->selection.valid)cache->rows[0]=view->selection.frozen;
    cache->valid=true;
    return ESP_OK;
}

static inline void cerberus_remote_id_ui_invalidate(cerberus_remote_id_ui_cache_t *cache)
{if(cache)cache->valid=false;}

static inline cerberus_remote_id_ui_event_t cerberus_remote_id_ui_tap(
    const cerberus_remote_id_ui_cache_t *cache,uint16_t x,uint16_t y)
{
    cerberus_remote_id_ui_event_t event={0};
    if(!cache || !cache->valid)return event;
    const cerberus_remote_id_ui_model_t *painted=&cache->painted;
    if(y>=422U && y<466U){
        if(x>=8U && x<76U && painted->back)event.action=CERBERUS_REMOTE_ID_UI_ACTION_BACK;
        else if(x>=84U && x<152U){
            if(painted->follow_available){
                event.action=painted->follow_enabled?CERBERUS_REMOTE_ID_UI_ACTION_UNFOLLOW:CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW;
                event.selected=cache->rows[0];
            }else if(painted->previous)event.action=CERBERUS_REMOTE_ID_UI_ACTION_PREV;
        }
        else if(x>=160U && x<228U)event.action=painted->stop?CERBERUS_REMOTE_ID_UI_ACTION_STOP:
            painted->start?CERBERUS_REMOTE_ID_UI_ACTION_START:CERBERUS_REMOTE_ID_UI_ACTION_NONE;
        else if(x>=236U && x<312U){
            if(painted->motion_available){
                event.action=painted->motion_mode?CERBERUS_REMOTE_ID_UI_ACTION_DETAILS:CERBERUS_REMOTE_ID_UI_ACTION_MOTION;
                event.selected=cache->rows[0];
            }else if(painted->next)event.action=CERBERUS_REMOTE_ID_UI_ACTION_NEXT;
        }
    }else if(!painted->detail_mode && x>=8U && x<312U && y>=132U){
        unsigned row=(y-132U)/58U,within=(y-132U)%58U;
        if(row<cache->row_count && within<54U){
            event.action=CERBERUS_REMOTE_ID_UI_ACTION_SELECT;
            event.selected=cache->rows[row];
        }
    }
    return event;
}
