#include "mac_monitor_ui.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define BG UINT16_C(0x0042)
#define PANEL UINT16_C(0x08A4)
#define BORDER UINT16_C(0x21A9)
#define TEXT UINT16_C(0xE77F)
#define MUTED UINT16_C(0x7CB5)
#define CYAN UINT16_C(0x471B)
static void clean(char *out,size_t size,const char *s,size_t maximum)
{size_t n=0;if(s)while(n<maximum && n+1<size && s[n]){unsigned char c=(unsigned char)s[n];out[n]=c>=32 && c<=126?(char)c:'?';++n;}out[n]=0;}
static const char *radio(const cerberus_mac_monitor_record_t *r){return r->row.radio==CERBERUS_MAC_BLE?"BLE":"WI-FI";}
static void age(char out[49],const char *label,const cerberus_mac_monitor_record_t *r,uint64_t now)
{
    uint64_t ms;
    if(!cerberus_mac_monitor_age(r,now,&ms))snprintf(out,49,"%s UNAVAILABLE",label);
    else snprintf(out,49,"%s <=%" PRIu64 "s",label,ms/1000U+(ms%1000U!=0));
}
static void signal(char out[24],const cerberus_mac_monitor_record_t *r)
{if(r->row.flags&CERBERUS_MAC_RSSI_VALID)snprintf(out,24,"%d dBm",r->row.rssi);else snprintf(out,24,"UNAVAILABLE");}
static const char *sat(uint32_t n){return n==UINT32_MAX?">=":"";}
static bool model(const cerberus_mac_monitor_ui_view_t *v,cerberus_mac_monitor_ui_model_t *m)
{
    if(!v || !m || v->state>CERBERUS_MAC_UI_ERROR || v->page.sort>CERBERUS_MAC_SORT_RECENT ||
        v->page.total>64 || v->page.copied>4 || v->page.offset>v->page.total ||
        v->page.copied>v->page.total-v->page.offset)return false;
    memset(m,0,sizeof(*m));m->detail_mode=v->detail;m->start=v->start_available && !v->stop_available;
    m->stop=v->stop_available;m->back=v->back_available;
    m->previous=!v->detail && v->page.offset>=4;m->next=!v->detail && v->page.offset+v->page.copied<v->page.total;
    char label[35];clean(label,sizeof(label),v->wifi_status?v->wifi_status:"UNAVAILABLE",34);
    snprintf(m->wifi,49,"C5 WI-FI: %s",label);clean(label,sizeof(label),v->ble_status?v->ble_status:"UNAVAILABLE",34);
    snprintf(m->ble,49,"S3 BLE: %s",label);
    static const char *states[]={"READY","MONITORING","STOPPING / WAIT","STOPPED","NEEDS ATTENTION"};
    snprintf(m->state,25,"%s",states[v->state]);
    static const char *sorts[]={"COUNT >","SIGNAL >","RECENT >"};snprintf(m->sort,13,"%s",sorts[v->page.sort]);
    snprintf(m->summary,37,"%u SOURCES / %u OF %u",v->page.total,
        v->page.total?(unsigned)(v->page.offset/4+1):0,(v->page.total+3U)/4U);
    if(!v->detail){
        for(unsigned n=0;n<v->page.copied;++n){
            const cerberus_mac_monitor_record_t *r=&v->page.rows[n];
            if(!r->valid || r->row.radio>1)return false;
            clean(label,sizeof(label),v->source_labels[n]?v->source_labels[n]:"ADDRESS HIDDEN",26);
            snprintf(m->rows[n][0],49,"%s %.26s%s",radio(r),label,r->present?"":" / EARLIER");
            char rssi[24];signal(rssi,r);
            snprintf(m->rows[n][1],49,"RX %s%" PRIu32 " / %s",sat(r->observations),r->observations,rssi);
            age(m->rows[n][2],"LAST SEEN",r,v->now_us);
        }
        if(!v->page.copied){snprintf(m->detail[0],49,"NO OBSERVED SOURCES");snprintf(m->detail[1],49,"%s",m->start?"PRESS START TO MONITOR":"WAITING FOR RADIO OBSERVATIONS");}
        return true;
    }
    const cerberus_mac_monitor_selection_t *s=&v->selection;
    if(!s->valid || !s->frozen.valid)return false;
    const cerberus_mac_monitor_record_t *f=&s->frozen,*r=&s->latest;
    if(!cerberus_mac_monitor_source_equal(f,r))return false;
    clean(label,sizeof(label),v->selected_source_label?v->selected_source_label:"ADDRESS HIDDEN",26);
    snprintf(m->detail[0],49,"%s %s",radio(f),label);
    snprintf(m->detail[1],49,"FROZEN RX %s%" PRIu32,sat(f->observations),f->observations);
    age(m->detail[2],"SELECTED LAST SEEN",f,v->now_us);
    snprintf(m->detail[3],49,"%s",s->unavailable?"RADIO UNAVAILABLE / RETAINED":s->present?"PRESENT IN LATEST SNAPSHOT":"NOT IN LATEST SNAPSHOT / RETAINED");
    snprintf(m->detail[4],49,"KNOWN RX %s%" PRIu32 " / RSP %s%" PRIu32,sat(r->observations),r->observations,sat(r->responses),r->responses);
    char rssi[24];signal(rssi,r);snprintf(m->detail[5],49,"LAST REPORT RSSI %s",rssi);
    age(m->detail[6],"LAST SEEN",r,v->now_us);
    if(r->row.radio==CERBERUS_MAC_WIFI){
        snprintf(m->detail[7],49,"LAST CHANNEL %u",r->row.channel);
        snprintf(m->detail[8],49,"FRAMES:%s%s%s",r->row.type_mask&CERBERUS_MAC_WIFI_MGMT?" MGMT":"",
            r->row.type_mask&CERBERUS_MAC_WIFI_CONTROL?" CTRL":"",r->row.type_mask&CERBERUS_MAC_WIFI_DATA?" DATA":"");
    }else{
        static const char *types[]={"PUBLIC","RANDOM","PUBLIC IDENTITY","RANDOM IDENTITY"};
        snprintf(m->detail[7],49,"ADDRESS: %s",r->row.address_type<4?types[r->row.address_type]:"UNKNOWN");
        snprintf(m->detail[8],49,"REPORTS:%s%s%s",r->row.type_mask&CERBERUS_MAC_BLE_ORIGINAL?" ORIGINAL":"",
            r->row.type_mask&CERBERUS_MAC_BLE_RESPONSE?" RESPONSE":"",r->row.type_mask&CERBERUS_MAC_BLE_PARTIAL?" PARTIAL":"");
    }
    snprintf(m->detail[9],49,"REPLACED W:%" PRIu32 " B:%" PRIu32,v->replacements[0],v->replacements[1]);
    snprintf(m->detail[10],49,"REJECTED W:%" PRIu32 " B:%" PRIu32,v->invalid[0],v->invalid[1]);
    snprintf(m->detail[11],49,"SIGNAL + COUNTS / NO MOVEMENT ESTIMATE");
    return true;
}
static esp_err_t button(unsigned x,unsigned w,const char *label,bool enabled)
{
    esp_err_t e=cerberus_display_fill_rect((uint16_t)x,422,(uint16_t)w,44,enabled?PANEL:BG);if(e)return e;
    return cerberus_display_draw_text((uint16_t)(x+(w-strlen(label)*6U)/2U),440,label,enabled?TEXT:MUTED,enabled?PANEL:BG,1);
}
esp_err_t cerberus_mac_monitor_ui_update(cerberus_mac_monitor_ui_cache_t *c,const cerberus_mac_monitor_ui_view_t *v,bool full)
{
    if(!c)return ESP_ERR_INVALID_ARG;
    if(!model(v,&c->staging)){c->valid=false;return ESP_ERR_INVALID_ARG;}
    full=full || !c->valid || c->painted.detail_mode!=c->staging.detail_mode;
    cerberus_mac_monitor_ui_model_t *m=&c->staging,*old=&c->painted;esp_err_t e;
#define TRY(call) do{e=(call);if(e){c->valid=false;return e;}}while(0)
    if(full){TRY(cerberus_display_fill_rect(0,0,320,480,BG));TRY(cerberus_display_draw_text(94,16,"MAC MONITOR",TEXT,BG,2));}
#define LINE(y,a,b) do{if(full || strcmp((a),(b))){TRY(cerberus_display_fill_rect(12,(y),296,9,BG));TRY(cerberus_display_draw_text(12,(y),(a),TEXT,BG,1));}}while(0)
    LINE(48,m->wifi,old->wifi);LINE(64,m->ble,old->ble);LINE(84,m->state,old->state);
    if(full || strcmp(m->summary,old->summary) || strcmp(m->sort,old->sort)){
        TRY(cerberus_display_fill_rect(12,103,296,20,BG));TRY(cerberus_display_draw_text(12,108,m->summary,TEXT,BG,1));
        if(!m->detail_mode)TRY(cerberus_display_draw_text(248,108,m->sort,CYAN,BG,1));
    }
    if(m->detail_mode){for(unsigned n=0;n<12;++n)LINE((uint16_t)(132+22*n),m->detail[n],old->detail[n]);}
    else{
        for(unsigned n=0;n<4;++n)if(full || memcmp(m->rows[n],old->rows[n],sizeof(m->rows[n]))){
            TRY(cerberus_display_fill_rect(12,(uint16_t)(132+68*n),296,62,PANEL));
            for(unsigned q=0;q<3;++q)TRY(cerberus_display_draw_text(18,(uint16_t)(140+68*n+16*q),m->rows[n][q],q?TEXT:CYAN,PANEL,1));
        }
        if(!v->page.copied){LINE(148,m->detail[0],old->detail[0]);LINE(170,m->detail[1],old->detail[1]);}
    }
    if(full || old->back!=m->back)TRY(button(8,68,"BACK",m->back));
    if(full || old->previous!=m->previous)TRY(button(84,62,"PREV",m->previous));
    if(full || old->stop!=m->stop || old->start!=m->start)TRY(button(154,72,m->stop?"STOP":"START",m->stop||m->start));
    if(full || old->next!=m->next)TRY(button(234,78,"NEXT",m->next));
#undef LINE
#undef TRY
    c->painted=*m;c->row_count=m->detail_mode?1:v->page.copied;
    if(m->detail_mode)c->rows[0]=v->selection.frozen;
    else if(c->row_count)memcpy(c->rows,v->page.rows,c->row_count*sizeof(c->rows[0]));
    c->valid=true;return ESP_OK;
}
void cerberus_mac_monitor_ui_invalidate(cerberus_mac_monitor_ui_cache_t *c){if(c)c->valid=false;}
cerberus_mac_monitor_ui_event_t cerberus_mac_monitor_ui_tap(const cerberus_mac_monitor_ui_cache_t *c,uint16_t x,uint16_t y)
{
    cerberus_mac_monitor_ui_event_t event={0};if(!c || !c->valid)return event;
    const cerberus_mac_monitor_ui_model_t *m=&c->painted;
    if(y>=422 && y<466){
        if(x>=8 && x<76 && m->back)event.action=CERBERUS_MAC_UI_BACK;
        else if(x>=84 && x<146 && m->previous)event.action=CERBERUS_MAC_UI_PREV;
        else if(x>=154 && x<226)event.action=m->stop?CERBERUS_MAC_UI_STOP:m->start?CERBERUS_MAC_UI_START:CERBERUS_MAC_UI_NONE;
        else if(x>=234 && x<312 && m->next)event.action=CERBERUS_MAC_UI_NEXT;
    }else if(!m->detail_mode && x>=240 && x<312 && y>=100 && y<124)event.action=CERBERUS_MAC_UI_SORT;
    else if(!m->detail_mode && x>=12 && x<308 && y>=132 && y<398){
        unsigned row=(y-132U)/68U;if(row<c->row_count && (y-132U)%68U<62U){event.action=CERBERUS_MAC_UI_SELECT;event.selected=c->rows[row];}
    }
    return event;
}
