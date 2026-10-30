#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "remote_id_ui.h"
#include "display_font.h"

static uint16_t pixels[480][320], reference[480][320];
static unsigned calls, text_calls, fail_at;
static char drawn[16384];
static cerberus_remote_id_ui_cache_t cache, reference_cache;
static cerberus_remote_id_inventory_t inventory;
static cerberus_remote_id_ui_view_t view;
static const char *prefix;
static unsigned checks;
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
    memset(&cache,0,sizeof(cache));memset(&view,0,sizeof(view));
    memset(pixels,0xA5,sizeof(pixels));drawn[0]=0;calls=text_calls=fail_at=0;
    cerberus_remote_id_inventory_init(&inventory);
    view.start_available=view.back_available=true;
    view.wifi_status="READY / PASSIVE";view.ble_status="READY / PASSIVE";
    view.selected_source_label="BLE-01AB23 / TYPE 3";
    for(unsigned i=0;i<4;++i)view.source_labels[i]="SOURCE-01AB23";
}

static cerberus_watch_observation_t row(uint8_t radio,unsigned index)
{
    cerberus_watch_observation_t result={.address={0x02,0x23,0x45,0x67,0x89,0},
        .address_type=radio==CERBERUS_WATCH_RADIO_BLE?3U:0U,.rssi=-42,
        .channel=radio==CERBERUS_WATCH_RADIO_BLE?0U:36U,
        .kind=SENTINEL_WATCH_PROTOCOL_DRONE,
        .transport=radio==CERBERUS_WATCH_RADIO_BLE?SENTINEL_WATCH_TRANSPORT_BLE:
            SENTINEL_WATCH_TRANSPORT_WIFI_BEACON,
        .flags=SENTINEL_WATCH_PROTOCOL_HAS_ID};
    result.address[5]=(uint8_t)index;
    snprintf(result.id,sizeof(result.id),"ID-%02u",index);
    return result;
}

static void publish(uint8_t radio,uint64_t token,uint64_t now,
    const cerberus_watch_observation_t *rows,size_t count,uint16_t flags)
{
    cerberus_watch_scan_status_payload_t status={.radio=radio,
        .generation=(uint8_t)token,.state=CERBERUS_WATCH_COMPLETE,
        .stored_count=(uint8_t)count,.reports=(uint32_t)count,.flags=flags};
    CHECK(cerberus_remote_id_inventory_publish(&inventory,token,&status,&status,
        rows,count,now)==CERBERUS_REMOTE_ID_PUBLISHED);
}

static void page(size_t offset)
{CHECK(cerberus_remote_id_inventory_page(&inventory,offset,&view.page));}

static void update(bool force)
{drawn[0]=0;CHECK(cerberus_remote_id_ui_update(&cache,&view,force)==ESP_OK);}

static void action(unsigned x,unsigned y,cerberus_remote_id_ui_action_t expected)
{CHECK(cerberus_remote_id_ui_tap(&cache,(uint16_t)x,(uint16_t)y).action==expected);}

/* A differential repaint must exactly match a fresh rendering of that state. */
static void equals_full_render(void)
{
    memcpy(reference,pixels,sizeof(reference));memset(&reference_cache,0,sizeof(reference_cache));
    drawn[0]=0;CHECK(cerberus_remote_id_ui_update(&reference_cache,&view,true)==ESP_OK);
    CHECK(!memcmp(reference,pixels,sizeof(pixels)));
}

static void controls(void)
{
    reset();update(true);save("empty-idle");
    CHECK(strstr(drawn,"0 SOURCES")&&strstr(drawn,"NO REMOTE ID SOURCES"));
    action(8,422,CERBERUS_REMOTE_ID_UI_ACTION_BACK);
    action(75,465,CERBERUS_REMOTE_ID_UI_ACTION_BACK);
    action(160,422,CERBERUS_REMOTE_ID_UI_ACTION_START);
    action(227,465,CERBERUS_REMOTE_ID_UI_ACTION_START);
    action(76,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(159,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(228,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(180,421,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(180,466,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(250,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    unsigned before=calls;update(false);CHECK(calls==before);
    view.start_available=false;view.stop_available=true;view.back_available=false;
    view.state=CERBERUS_REMOTE_ID_UI_STOPPING;view.wifi_status="STOPPING";
    update(false);action(180,440,CERBERUS_REMOTE_ID_UI_ACTION_STOP);
    action(20,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);equals_full_render();
    view.state=CERBERUS_REMOTE_ID_UI_ERROR;view.wifi_status="ERROR / CLEANUP REQUIRED";
    view.ble_status="UNAVAILABLE";view.wifi_dropped_reports=view.ble_dropped_reports=UINT32_MAX;
    update(false);action(180,440,CERBERUS_REMOTE_ID_UI_ACTION_STOP);save("cleanup-error");
    CHECK(strstr(drawn,"4294967295")&&strstr(drawn,"UNAVAILABLE"));
    cerberus_remote_id_ui_invalidate(&cache);action(180,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
}

static void full_pages_and_transitions(void)
{
    reset();update(true);
    cerberus_watch_observation_t rows[12];
    for(unsigned i=0;i<12;++i)rows[i]=row(CERBERUS_WATCH_RADIO_WIFI,i);
    memcpy(rows[0].id,"ABCDEFGHIJKLMNOPQRST",20);rows[0].id[20]=0;
    publish(CERBERUS_WATCH_RADIO_WIFI,1,0,rows,1,0);page(0);view.now_ms=1000;
    update(false);equals_full_render(); /* Empty text must not survive a one-row page. */
    for(unsigned i=0;i<12;++i)rows[i]=row(CERBERUS_WATCH_RADIO_WIFI,i);
    publish(CERBERUS_WATCH_RADIO_WIFI,2,2000,rows,12,0);
    for(unsigned i=0;i<12;++i)rows[i]=row(CERBERUS_WATCH_RADIO_BLE,i);
    /* Same address/ID as Wi-Fi is deliberately a separate source. */
    publish(CERBERUS_WATCH_RADIO_BLE,1,1000,rows,12,0);
    view.now_ms=5000;view.state=CERBERUS_REMOTE_ID_UI_RUNNING;
    view.stop_available=true;view.start_available=false;
    view.wifi_status="SWEEP COMPLETE";view.ble_status="SWEEP COMPLETE";
    for(size_t offset=0;offset<24;offset+=4){
        page(offset);update(false);CHECK(view.page.total==24&&view.page.copied==4);
        CHECK(strstr(cache.painted.summary,"24 SOURCES"));
        action(100,440,offset?CERBERUS_REMOTE_ID_UI_ACTION_PREV:CERBERUS_REMOTE_ID_UI_ACTION_NONE);
        action(270,440,offset<20?CERBERUS_REMOTE_ID_UI_ACTION_NEXT:CERBERUS_REMOTE_ID_UI_ACTION_NONE);
        for(unsigned i=0;i<4;++i){
            cerberus_remote_id_ui_event_t event=cerberus_remote_id_ui_tap(&cache,10,(uint16_t)(132+i*58));
            CHECK(event.action==CERBERUS_REMOTE_ID_UI_ACTION_SELECT);
            CHECK(!memcmp(&event.selected,&view.page.rows[i],sizeof(event.selected)));
            action(311,185+i*58,CERBERUS_REMOTE_ID_UI_ACTION_SELECT);
            action(312,150+i*58,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
            action(10,186+i*58,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
        }
        equals_full_render();
    }
    save("full-ble-page");
    publish(CERBERUS_WATCH_RADIO_WIFI,3,6000,NULL,0,0);
    publish(CERBERUS_WATCH_RADIO_BLE,2,6000,NULL,0,0);
    view.now_ms=6000;page(0);update(false);equals_full_render();
    action(20,150,CERBERUS_REMOTE_ID_UI_ACTION_NONE);save("empty-running");
    view.page.copied=5;CHECK(cerberus_remote_id_ui_update(&cache,&view,false)==ESP_ERR_INVALID_ARG);
    CHECK(!cache.valid);action(180,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
}

static void frozen_selection_and_details(void)
{
    reset();cerberus_watch_observation_t rows[2]={row(CERBERUS_WATCH_RADIO_BLE,1),row(CERBERUS_WATCH_RADIO_BLE,2)};
    rows[0].flags|=SENTINEL_WATCH_PROTOCOL_HAS_POSITION|SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE;
    rows[0].latitude_e7=-1;rows[0].longitude_e7=1800000000;rows[0].altitude_dm=-1;
    rows[0].altitude_kind=SENTINEL_WATCH_ALTITUDE_GEODETIC;
    memcpy(rows[0].id,"ABCDEFGHIJKLMNOPQRST",20);rows[0].id[20]=0;
    publish(CERBERUS_WATCH_RADIO_BLE,1,1000,rows,2,0);page(0);view.now_ms=5000;update(true);
    cerberus_remote_id_record_t expected=view.page.rows[0];
    /* Publication, reordering and rebuilding the caller's view precede the tap. */
    cerberus_watch_observation_t swap=rows[0];rows[0]=rows[1];rows[1]=swap;
    memset(rows[1].id,0,sizeof(rows[1].id));strcpy(rows[1].id,"NEW ADVERTISED ID");
    publish(CERBERUS_WATCH_RADIO_BLE,2,4000,rows,2,0);page(0);
    cerberus_remote_id_ui_event_t event=cerberus_remote_id_ui_tap(&cache,20,150);
    CHECK(event.action==CERBERUS_REMOTE_ID_UI_ACTION_SELECT);
    CHECK(!memcmp(&event.selected,&expected,sizeof(expected)));
    CHECK(cerberus_remote_id_inventory_select(&inventory,&event.selected));
    CHECK(!memcmp(&inventory.selected.frozen,&expected,sizeof(expected)));
    CHECK(inventory.selected.latest.receipt_ms==4000&&inventory.selected.matching_sweeps==2);
    view.detail=true;view.selection=inventory.selected;update(false);
    CHECK(strstr(drawn,"SELECTED ID ABCDEFGHIJKLMNOPQRST"));
    CHECK(strstr(drawn,"SELECTED SWEEP RECEIPT AGE 4s"));
    CHECK(!strcmp(cache.painted.detail[4],"LAST MATCH RECEIPT AGE 1s"));
    CHECK(strstr(drawn,"LAT -0.0000001")&&strstr(drawn,"LON 180.0000000"));
    CHECK(strstr(drawn,"ALT -0.1 M / WGS84 HAE"));
    CHECK(strstr(drawn,"MATCHING SWEEPS 2"));
    CHECK(!strstr(drawn,"02:23:45:67:89:01"));save("selected-position");
    action(20,150,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(270,440,CERBERUS_REMOTE_ID_UI_ACTION_MOTION);
    equals_full_render();
    /* Absent and unavailable checks retain the last matching payload's age. */
    publish(CERBERUS_WATCH_RADIO_BLE,3,9000,NULL,0,0);
    view.selection=inventory.selected;view.now_ms=10000;update(false);
    CHECK(strstr(drawn,"ABSENT FROM LAST COMPLETED SWEEP"));
    CHECK(!strcmp(cache.painted.detail[4],"LAST MATCH RECEIPT AGE 6s")&&strstr(drawn,"LAST CHECK RECEIPT AGE 1s"));
    save("selected-absent");
    publish(CERBERUS_WATCH_RADIO_BLE,4,11000,NULL,0,CERBERUS_WATCH_CAPTURE_UNAVAILABLE);
    view.selection=inventory.selected;view.now_ms=12000;update(false);
    CHECK(strstr(drawn,"RADIO UNAVAILABLE / RETAINED EVIDENCE"));
    CHECK(!strcmp(cache.painted.detail[4],"LAST MATCH RECEIPT AGE 8s"));save("selected-unavailable");
    /* A current Basic ID-only row replaces old coordinates/altitude entirely. */
    rows[0]=row(CERBERUS_WATCH_RADIO_BLE,1);strcpy(rows[0].id,"BASIC ID ONLY");
    publish(CERBERUS_WATCH_RADIO_BLE,5,12000,rows,1,0);
    view.selection=inventory.selected;view.now_ms=13000;update(false);
    CHECK(strstr(drawn,"POSITION NOT RECEIVED IN THIS EVIDENCE"));
    CHECK(strstr(drawn,"ALTITUDE NOT RECEIVED IN THIS EVIDENCE"));
    CHECK(!strstr(drawn,"LAT -0.0000001")&&!strstr(drawn,"WGS84 HAE"));
    CHECK(!memcmp(&view.selection.frozen,&expected,sizeof(expected)));save("selected-id-only");
    /* Same six address bytes with a different BLE address type is not this source. */
    rows[0].address_type=2;
    publish(CERBERUS_WATCH_RADIO_BLE,6,14000,rows,1,0);
    view.selection=inventory.selected;view.now_ms=15000;update(false);
    CHECK(!view.selection.present && view.selection.latest.receipt_ms==12000);
    CHECK(strstr(drawn,"ABSENT FROM LAST COMPLETED SWEEP"));
    /* A foreign typed source must never donate location to this selection. */
    view.selection.latest.observation.address_type=2;
    update(false);CHECK(!strcmp(cache.painted.detail[4],"LAST MATCH RECEIPT AGE UNAVAILABLE"));
    CHECK(strstr(drawn,"UNAVAILABLE"));
    /* Returning to a current list changes only the painted selection mapping. */
    view.detail=false;page(0);update(false);equals_full_render();
    event=cerberus_remote_id_ui_tap(&cache,20,150);
    CHECK(event.selected.receipt_ms==14000&&event.selected.observation.address_type==2);
}

static void age_limits_and_paint_failure(void)
{
    reset();cerberus_watch_observation_t r=row(CERBERUS_WATCH_RADIO_WIFI,1);
    publish(CERBERUS_WATCH_RADIO_WIFI,1,0,&r,1,0);page(0);view.now_ms=UINT64_MAX;
    update(true);CHECK(strstr(drawn,"RX CALLBACK AGE UNAVAILABLE"));
    CHECK(cerberus_remote_id_inventory_select(&inventory,&view.page.rows[0]));
    view.detail=true;view.selection=inventory.selected;
    view.selection.matching_sweeps=UINT32_MAX;
    view.selected_source_label="01:23:45:67:89:AB / TYPE 000";
    view.selection.latest.observation.flags|=SENTINEL_WATCH_PROTOCOL_HAS_POSITION|SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE;
    view.selection.latest.observation.latitude_e7=-900000000;
    view.selection.latest.observation.longitude_e7=-1800000000;
    view.selection.latest.observation.altitude_dm=317675;
    view.selection.latest.observation.altitude_kind=SENTINEL_WATCH_ALTITUDE_PRESSURE;
    update(false);CHECK(strstr(drawn,"MATCHING SWEEPS 4294967295"));
    CHECK(strstr(drawn,"18446744073709551s")); /* Receipt age remains full-width. */
    CHECK(strstr(drawn,"ALT 31767.5 M / PRESSURE"));save("max-fields");
    view.now_ms=0;view.selection.latest.receipt_ms=1;
    update(false);CHECK(!strcmp(cache.painted.detail[4],"LAST MATCH RECEIPT AGE UNAVAILABLE"));
    view.detail=false;view.now_ms=1;update(false);
    cerberus_remote_id_record_t painted=cache.rows[0];
    view.page.rows[0].observation.address[5]=9;
    strcpy(view.page.rows[0].observation.id,"CHANGED");
    fail_at=calls+2U;
    CHECK(cerberus_remote_id_ui_update(&cache,&view,false)==ESP_FAIL);
    CHECK(!cache.valid&&!memcmp(&cache.rows[0],&painted,sizeof(painted)));
    action(20,150,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    fail_at=0;update(false);equals_full_render();
    CHECK(cache.rows[0].observation.address[5]==9);
    /* Bounded sanitization; malformed text cannot escape a visual row. */
    view.wifi_status="READY\nSECOND LINE";
    view.source_labels[0]="MASKED\tSOURCE";
    update(false);CHECK(strstr(drawn,"READY?SECOND LINE")&&strstr(drawn,"MASKED?SOURCE"));
    CHECK(cerberus_remote_id_ui_update(NULL,&view,false)==ESP_ERR_INVALID_ARG);
}

static void qualifying_receive_ages(void)
{
    reset();
    cerberus_watch_observation_t r=row(CERBERUS_WATCH_RADIO_BLE,1U);
    cerberus_watch_scan_status_payload_t status={.radio=CERBERUS_WATCH_RADIO_BLE,
        .generation=1U,.state=CERBERUS_WATCH_COMPLETE,.stored_count=1U,.reports=1U};
    cerberus_watch_receive_timing_t timing={.request_started_us=999001U,.source_age_ms=5U,.valid=true};
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,1U,&status,&status,&r,&timing,1U,1000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    page(0);view.now_ms=5000U;update(true);
    CHECK(!strcmp(cache.painted.rows[0][2],"RX CALLBACK AGE <=5s"));
    save("callback-age-list");equals_full_render();
    const cerberus_remote_id_record_t painted=cache.rows[0];
    /* A newer publication must not redirect an old painted row or its age. */
    timing.request_started_us=3999500U;timing.source_age_ms=1U;status.generation=2U;
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,2U,&status,&status,&r,&timing,1U,4000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    cerberus_remote_id_ui_event_t event=cerberus_remote_id_ui_tap(&cache,20,150);
    CHECK(event.action==CERBERUS_REMOTE_ID_UI_ACTION_SELECT && !memcmp(&event.selected,&painted,sizeof(painted)));
    CHECK(cerberus_remote_id_inventory_select(&inventory,&event.selected));
    view.detail=true;view.selection=inventory.selected;update(false);
    CHECK(!memcmp(&view.selection.frozen,&painted,sizeof(painted)));
    CHECK(!strcmp(cache.painted.detail[4],"LAST MATCH RECEIPT AGE 1s"));
    CHECK(!strcmp(cache.painted.detail[11],"LAST MATCH RX CALLBACK AGE <=2s"));
    save("callback-age-selected");equals_full_render();
    publish(CERBERUS_WATCH_RADIO_BLE,3U,6000U,NULL,0U,0U);
    view.selection=inventory.selected;view.now_ms=7000U;update(false);
    CHECK(strstr(drawn,"ABSENT FROM LAST COMPLETED SWEEP"));
    CHECK(!strcmp(cache.painted.detail[11],"LAST MATCH RX CALLBACK AGE <=4s"));
    CHECK(!strcmp(cache.painted.detail[4],"LAST MATCH RECEIPT AGE 3s"));
    CHECK(!memcmp(&view.selection.frozen,&painted,sizeof(painted)));
    save("callback-age-absent");equals_full_render();
    publish(CERBERUS_WATCH_RADIO_BLE,4U,7000U,&r,1U,0U);
    view.selection=inventory.selected;update(false);
    CHECK(!strcmp(cache.painted.detail[11],"LAST MATCH RX CALLBACK AGE UNAVAILABLE"));
    CHECK(!memcmp(&view.selection.frozen,&painted,sizeof(painted)));
    save("callback-age-unavailable");
    /* At time zero, upward rounding covers the upper end of the current
     * millisecond bin. A future anchor is not a zero-age measurement. */
    view.selection.latest.timing=(cerberus_watch_receive_timing_t){.valid=true};view.now_ms=0U;
    update(false);CHECK(!strcmp(cache.painted.detail[11],"LAST MATCH RX CALLBACK AGE <=1s"));
    view.selection.latest.timing.request_started_us=1000U;
    update(false);CHECK(!strcmp(cache.painted.detail[11],"LAST MATCH RX CALLBACK AGE UNAVAILABLE"));
    view.selection.latest.timing.request_started_us=0U;
    view.selection.latest.timing.source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS;
    view.now_ms=(UINT64_MAX-999U)/1000U;
    update(false);CHECK(strlen(cache.painted.detail[11])<=48U);
    CHECK(strstr(cache.painted.detail[11],"<=36893488147420s"));
    save("callback-age-max");equals_full_render();
    view.now_ms=UINT64_MAX;
    update(false);CHECK(!strcmp(cache.painted.detail[11],"LAST MATCH RX CALLBACK AGE UNAVAILABLE"));
    view.now_ms=0U;view.selection.latest.timing.source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS+1U;
    update(false);CHECK(!strcmp(cache.painted.detail[11],"LAST MATCH RX CALLBACK AGE UNAVAILABLE"));
}

static void follow_controls_and_frozen_evidence(void)
{
    reset();cerberus_watch_observation_t rows[2]={row(CERBERUS_WATCH_RADIO_WIFI,1),row(CERBERUS_WATCH_RADIO_WIFI,2)};
    rows[0].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    publish(CERBERUS_WATCH_RADIO_WIFI,1,1000,rows,2,0);page(0);
    CHECK(cerberus_remote_id_inventory_select(&inventory,&view.page.rows[0]));
    view.detail=true;view.selection=inventory.selected;view.now_ms=4000;
    view.follow_available=true;view.selected_source_label="WI-FI-123ABC";
    view.message="FOLLOW READY / START TO RESUME";update(true);
    CHECK(strstr(drawn,"FOLLOW")&&strstr(drawn,"SELECTED ID ID-01"));
    const cerberus_remote_id_record_t frozen=view.selection.frozen;
    cerberus_remote_id_ui_event_t event=cerberus_remote_id_ui_tap(&cache,84,422);
    CHECK(event.action==CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW&&!memcmp(&event.selected,&frozen,sizeof(frozen)));
    action(151,465,CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW);
    action(83,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);action(152,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(100,421,CERBERUS_REMOTE_ID_UI_ACTION_NONE);action(100,466,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(185,440,CERBERUS_REMOTE_ID_UI_ACTION_START);save("follow-ready");equals_full_render();
    /* A changed caller selection cannot redirect the already painted action. */
    CHECK(cerberus_remote_id_inventory_select(&inventory,&view.page.rows[1]));view.selection=inventory.selected;
    event=cerberus_remote_id_ui_tap(&cache,100,440);
    CHECK(event.action==CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW&&!memcmp(&event.selected,&frozen,sizeof(frozen)));
    view.follow_enabled=true;view.stop_available=true;view.start_available=false;
    view.state=CERBERUS_REMOTE_ID_UI_RUNNING;view.message="FOLLOW STARTS AT NEXT WI-FI WINDOW";
    update(false);CHECK(strstr(drawn,"UNFOLLOW")&&strstr(drawn,"NEXT WI-FI WINDOW"));
    event=cerberus_remote_id_ui_tap(&cache,100,440);
    CHECK(event.action==CERBERUS_REMOTE_ID_UI_ACTION_UNFOLLOW&&!memcmp(&event.selected,&view.selection.frozen,sizeof(event.selected)));
    action(185,440,CERBERUS_REMOTE_ID_UI_ACTION_STOP);save("follow-waiting");equals_full_render();
    view.message="FOLLOWING WITH BACKGROUND COVERAGE";update(false);save("follow-active");equals_full_render();
    view.message="SEARCHING FOR SELECTED WI-FI SOURCE";update(false);save("follow-search");equals_full_render();
    view.follow_enabled=false;view.follow_available=false;view.message="FOLLOW REQUIRES C5 UPDATE";
    update(false);action(100,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);save("follow-unavailable");equals_full_render();
    view.follow_available=true;fail_at=calls+1;view.follow_enabled=true;
    CHECK(cerberus_remote_id_ui_update(&cache,&view,false)==ESP_FAIL);
    CHECK(!cache.valid);action(100,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    fail_at=0;update(false);action(100,440,CERBERUS_REMOTE_ID_UI_ACTION_UNFOLLOW);
    cerberus_watch_observation_t ble=row(CERBERUS_WATCH_RADIO_BLE,3);
    publish(CERBERUS_WATCH_RADIO_BLE,1,4000,&ble,1,0);
    cerberus_remote_id_page_t combined;CHECK(cerberus_remote_id_inventory_page(&inventory,0,&combined));
    CHECK(cerberus_remote_id_inventory_select(&inventory,&combined.rows[2]));view.selection=inventory.selected;
    update(false);CHECK(!cache.painted.follow_visible&&!cache.painted.follow_available&&!cache.painted.follow_enabled);
    action(100,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);save("follow-ble-hidden");equals_full_render();
}

static void motion_panel_evidence_and_paint(void)
{
    reset();
    view.selected_source_label="WI-FI-01AB23";
    cerberus_watch_observation_t r=row(CERBERUS_WATCH_RADIO_WIFI,1);
    cerberus_watch_scan_status_payload_t status={.radio=CERBERUS_WATCH_RADIO_WIFI,.generation=1,
        .state=CERBERUS_WATCH_COMPLETE,.stored_count=1,.reports=1};
    cerberus_watch_receive_timing_t source_time={100000000,1,true},location_time={100000000,60000,true};
    sentinel_watch_motion_t motion={.speed_qms=1017,.direction_deg=360,.height_encoded=1,.flags=31};
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,1,&status,&status,&r,&source_time,&motion,&location_time,1,100000)==CERBERUS_REMOTE_ID_PUBLISHED);
    page(0);view.now_ms=100000;update(true);
    cerberus_remote_id_record_t frozen=cache.rows[0];
    CHECK(cerberus_remote_id_inventory_select(&inventory,&frozen));
    view.selection=inventory.selected;view.detail=true;view.follow_available=true;update(false);
    action(236,422,CERBERUS_REMOTE_ID_UI_ACTION_MOTION);action(311,465,CERBERUS_REMOTE_ID_UI_ACTION_MOTION);
    action(235,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);action(312,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    action(270,421,CERBERUS_REMOTE_ID_UI_ACTION_NONE);action(270,466,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
    cerberus_remote_id_ui_event_t event=cerberus_remote_id_ui_tap(&cache,270,440);
    CHECK(!memcmp(&event.selected,&frozen,sizeof(frozen)));
    view.motion_detail=true;update(false);equals_full_render();
    CHECK(!strcmp(cache.painted.detail[6],"254.25 M/S"));
    CHECK(!strcmp(cache.painted.detail[7],"DIRECTION 360 DEG"));
    CHECK(!strcmp(cache.painted.detail[8],"HEIGHT -999.5 M / ABOVE GROUND"));
    CHECK(!strcmp(cache.painted.detail[9],"LOCATION CALLBACK AGE <=61s"));
    CHECK(!strcmp(cache.painted.detail[10],"SOURCE CALLBACK AGE <=1s"));
    action(270,440,CERBERUS_REMOTE_ID_UI_ACTION_DETAILS);action(100,440,CERBERUS_REMOTE_ID_UI_ACTION_FOLLOW);
    action(20,440,CERBERUS_REMOTE_ID_UI_ACTION_BACK);save("motion-old-location");
    unsigned previous_calls=calls;update(false);CHECK(calls==previous_calls);
    view.now_ms+=1000;update(false);equals_full_render();
    CHECK(!strcmp(cache.painted.detail[9],"LOCATION CALLBACK AGE <=62s"));
    CHECK(!memcmp(&view.selection.frozen,&frozen,sizeof(frozen)));
    /* New publication is separate from the immutable painted selection. */
    status.generation=2;source_time.request_started_us=101000000;location_time.request_started_us=101000000;
    location_time.source_age_ms=0;motion.height_encoded=UINT16_MAX;motion.flags=15;motion.speed_qms=0;motion.direction_deg=0;
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,2,&status,&status,&r,&source_time,&motion,&location_time,1,101000)==CERBERUS_REMOTE_ID_PUBLISHED);
    event=cerberus_remote_id_ui_tap(&cache,270,440);
    CHECK(event.action==CERBERUS_REMOTE_ID_UI_ACTION_DETAILS&&!memcmp(&event.selected,&frozen,sizeof(frozen)));
    view.selection=inventory.selected;view.follow_enabled=true;view.stop_available=true;view.start_available=false;
    update(false);equals_full_render();
    CHECK(!strcmp(cache.painted.detail[6],"0.00 M/S")&&!strcmp(cache.painted.detail[7],"DIRECTION 0 DEG"));
    CHECK(!strcmp(cache.painted.detail[8],"HEIGHT 31767.5 M / FROM TAKEOFF"));
    CHECK(!strcmp(cache.painted.detail[9],"LOCATION CALLBACK AGE <=1s"));
    CHECK(!memcmp(&view.selection.frozen.motion,&frozen.motion,sizeof(motion)));
    CHECK(cerberus_watch_receive_timing_equal(&view.selection.frozen.motion_timing,&frozen.motion_timing));
    action(100,440,CERBERUS_REMOTE_ID_UI_ACTION_UNFOLLOW);action(185,440,CERBERUS_REMOTE_ID_UI_ACTION_STOP);
    save("motion-max-height");
    publish(CERBERUS_WATCH_RADIO_WIFI,3,102000,NULL,0,0);view.selection=inventory.selected;view.now_ms=103000;
    update(false);equals_full_render();CHECK(strstr(cache.painted.detail[3],"ABSENT"));
    CHECK(!strcmp(cache.painted.detail[9],"LOCATION CALLBACK AGE <=3s"));save("motion-absent-retained");
    /* A new Location with unavailable values clears all displayed components. */
    status.generation=4;motion=(sentinel_watch_motion_t){.flags=1};
    source_time.request_started_us=location_time.request_started_us=103000000;
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,4,&status,&status,&r,&source_time,&motion,&location_time,1,103000)==CERBERUS_REMOTE_ID_PUBLISHED);
    view.selection=inventory.selected;update(false);equals_full_render();
    CHECK(!strcmp(cache.painted.detail[6],"UNAVAILABLE")&&!strcmp(cache.painted.detail[7],"DIRECTION UNAVAILABLE")&&
        !strcmp(cache.painted.detail[8],"HEIGHT UNAVAILABLE"));
    CHECK(strstr(cache.painted.detail[11],"VALUES REPORTED"));save("motion-components-unavailable");
    /* Old peers/old publishers explicitly replace extras with absent fields. */
    publish(CERBERUS_WATCH_RADIO_WIFI,5,104000,&r,1,0);view.selection=inventory.selected;view.now_ms=104000;
    update(false);equals_full_render();CHECK(strstr(cache.painted.detail[11],"NO LOCATION GROUP"));
    CHECK(!strcmp(cache.painted.detail[9],"LOCATION CALLBACK AGE UNAVAILABLE"));save("motion-legacy-unavailable");
    view.selection.latest.motion=(sentinel_watch_motion_t){.speed_qms=255,.direction_deg=181,.height_encoded=2000,.flags=15};
    update(false);CHECK(!strcmp(cache.painted.detail[6],"63.75 M/S")&&!strcmp(cache.painted.detail[8],"HEIGHT 0.0 M / FROM TAKEOFF"));
    CHECK(!strcmp(cache.painted.detail[9],"LOCATION CALLBACK AGE UNAVAILABLE"));
    view.selection.latest.motion_timing=(cerberus_watch_receive_timing_t){0,CERBERUS_WATCH_MAX_SOURCE_AGE_MS,true};
    view.now_ms=(UINT64_MAX-999U)/1000U;update(false);equals_full_render();
    CHECK(strstr(cache.painted.detail[9],"<=36893488147420s"));save("motion-max-age");
    view.now_ms=UINT64_MAX;update(false);CHECK(!strcmp(cache.painted.detail[9],"LOCATION CALLBACK AGE UNAVAILABLE"));
    view.selection.latest.observation.address[5]++;update(false);equals_full_render();
    CHECK(strstr(cache.painted.detail[11],"NO LOCATION GROUP")); /* No foreign motion donation. */
    view.motion_detail=false;update(false);equals_full_render();action(270,440,CERBERUS_REMOTE_ID_UI_ACTION_MOTION);
    view.motion_detail=true;fail_at=calls+1;
    CHECK(cerberus_remote_id_ui_update(&cache,&view,false)==ESP_FAIL&&!cache.valid);
    action(270,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);fail_at=0;update(false);equals_full_render();
    view.back_available=false;update(false);action(270,440,CERBERUS_REMOTE_ID_UI_ACTION_NONE);
}

int main(int argc,char **argv)
{
    CHECK(argc==2);prefix=argv[1];
    controls();full_pages_and_transitions();frozen_selection_and_details();age_limits_and_paint_failure();qualifying_receive_ages();
    follow_controls_and_frozen_evidence();
    motion_panel_evidence_and_paint();
    printf("Remote ID production UI: %u checks; cache=%zu, view=%zu bytes\n",checks,sizeof(cache),sizeof(view));
    return 0;
}
