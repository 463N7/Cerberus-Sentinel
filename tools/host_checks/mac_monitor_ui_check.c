#include "mac_monitor_ui.h"
#include "display_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t pixels[480][320],reference[480][320];
static unsigned checks,calls,fail_at;static const char *prefix;
static cerberus_mac_monitor_ui_cache_t cache,reference_cache;
static cerberus_mac_monitor_ui_view_t view;
static cerberus_mac_monitor_inventory_t inventory;
static cerberus_mac_observation_t collector;
static cerberus_mac_snapshot_t snapshot;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line%u: %s\n",__LINE__,#c);abort();}}while(0)
esp_err_t cerberus_display_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color)
{CHECK((unsigned)x+w<=320 && (unsigned)y+h<=480);if(++calls==fail_at)return ESP_FAIL;
for(unsigned yy=y;yy<(unsigned)y+h;++yy)for(unsigned xx=x;xx<(unsigned)x+w;++xx)pixels[yy][xx]=color;return ESP_OK;}
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *text,uint16_t fg,uint16_t bg,uint8_t scale)
{CHECK(scale==1 || scale==2);CHECK(x+strlen(text)*6U*scale<=320 && y+7U*scale<=480);if(++calls==fail_at)return ESP_FAIL;
for(unsigned i=0;text[i];++i){const uint8_t *glyph=font5x7((unsigned char)text[i]);
for(unsigned yy=0;yy<7;++yy)for(unsigned xx=0;xx<6;++xx)for(unsigned sy=0;sy<scale;++sy)for(unsigned sx=0;sx<scale;++sx)
pixels[y+yy*scale+sy][x+(i*6U+xx)*scale+sx]=xx<5 && (glyph[yy]&(1U<<(4U-xx)))?fg:bg;}return ESP_OK;}
static void save(const char *name)
{char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name);FILE *file=fopen(path,"wb");CHECK(file!=NULL);fprintf(file,"P6\n320 480\n255\n");
for(unsigned y=0;y<480;++y)for(unsigned x=0;x<320;++x){uint16_t p=pixels[y][x];uint8_t rgb[3]={(uint8_t)(((p>>11)&31)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};CHECK(fwrite(rgb,1,3,file)==3);}CHECK(fclose(file)==0);}
static void update(bool force){CHECK(cerberus_mac_monitor_ui_update(&cache,&view,force)==ESP_OK);}
static void same_as_full(void)
{memcpy(reference,pixels,sizeof(reference));memset(&reference_cache,0,sizeof(reference_cache));CHECK(cerberus_mac_monitor_ui_update(&reference_cache,&view,true)==ESP_OK);CHECK(!memcmp(reference,pixels,sizeof(pixels)));}
static void populate(unsigned count)
{
    CHECK(cerberus_mac_observation_init(&collector,0,0));
    for(unsigned n=0;n<count;++n){uint8_t mac[6]={2,3,4,5,6,(uint8_t)n};CHECK(cerberus_mac_observation_observe(&collector,mac,0,(int)n-70,6,1,n));}
    CHECK(cerberus_mac_observation_snapshot(&collector,count,&snapshot));cerberus_mac_monitor_inventory_init(&inventory);
    CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,1,&snapshot,0,100,true)==CERBERUS_MAC_PUBLISHED);
    CHECK(cerberus_mac_monitor_inventory_page(&inventory,0,0,100,&view.page));
}
int main(int argc,char **argv)
{
    CHECK(argc==2);prefix=argv[1];memset(&view,0,sizeof(view));view.start_available=view.back_available=true;
    view.wifi_status="READY";view.ble_status="READY";view.now_us=100;
    for(unsigned n=0;n<4;++n)view.source_labels[n]="SOURCE-MASKED";
    view.selected_source_label="SOURCE-MASKED";
    CHECK(cerberus_mac_monitor_ui_tap(&cache,200,440).action==CERBERUS_MAC_UI_NONE);
    populate(0);update(true);save("ready");unsigned previous=calls;update(false);CHECK(calls==previous);
    for(unsigned x=0;x<320;++x){
        unsigned action=x>=8 && x<76?CERBERUS_MAC_UI_BACK:x>=154 && x<226?CERBERUS_MAC_UI_START:CERBERUS_MAC_UI_NONE;
        CHECK(cerberus_mac_monitor_ui_tap(&cache,(uint16_t)x,422).action==action);
        CHECK(cerberus_mac_monitor_ui_tap(&cache,(uint16_t)x,465).action==action);
        CHECK(cerberus_mac_monitor_ui_tap(&cache,(uint16_t)x,466).action==CERBERUS_MAC_UI_NONE);
    }
    populate(32);view.state=CERBERUS_MAC_UI_RUNNING;view.start_available=false;view.stop_available=true;
    view.wifi_status="RECEIVING / CHANNEL 177";view.ble_status="PASSIVE / RECEIVING";
    update(false);save("list");same_as_full();
    cerberus_mac_monitor_record_t painted=view.page.rows[0];
    CHECK(cerberus_mac_monitor_inventory_page(&inventory,CERBERUS_MAC_SORT_SIGNAL,4,100,&view.page));
    cerberus_mac_monitor_ui_event_t e=cerberus_mac_monitor_ui_tap(&cache,20,140);
    CHECK(e.action==CERBERUS_MAC_UI_SELECT && !memcmp(&painted,&e.selected,sizeof(painted)));
    update(false);same_as_full();CHECK(cerberus_mac_monitor_ui_tap(&cache,250,108).action==CERBERUS_MAC_UI_SORT);
    CHECK(cerberus_mac_monitor_ui_tap(&cache,239,108).action==CERBERUS_MAC_UI_NONE);
    CHECK(cerberus_mac_monitor_ui_tap(&cache,250,124).action==CERBERUS_MAC_UI_NONE);
    for(unsigned row=0;row<4;++row){unsigned top=132+68*row;
        CHECK(cerberus_mac_monitor_ui_tap(&cache,12,(uint16_t)top).action==CERBERUS_MAC_UI_SELECT);
        CHECK(cerberus_mac_monitor_ui_tap(&cache,307,(uint16_t)(top+61)).action==CERBERUS_MAC_UI_SELECT);
        CHECK(cerberus_mac_monitor_ui_tap(&cache,308,(uint16_t)top).action==CERBERUS_MAC_UI_NONE);
        CHECK(cerberus_mac_monitor_ui_tap(&cache,12,(uint16_t)(top+62)).action==CERBERUS_MAC_UI_NONE);
    }
    CHECK(cerberus_mac_monitor_inventory_select(&inventory,&painted));view.selection=inventory.selected;view.detail=true;
    update(false);save("detail");same_as_full();CHECK(cerberus_mac_monitor_ui_tap(&cache,250,108).action==CERBERUS_MAC_UI_NONE);
    CHECK(cerberus_mac_monitor_ui_tap(&cache,20,140).action==CERBERUS_MAC_UI_NONE);
    CHECK(!strcmp(cache.painted.detail[7],"LAST CHANNEL 6") && !strcmp(cache.painted.detail[8],"FRAMES: MGMT"));
    CHECK(strstr(cache.painted.detail[6],"LAST SEEN") && !strstr(cache.painted.detail[11],"GPS"));
    cerberus_mac_monitor_selection_t selected=view.selection;
    static const char *address_types[]={"PUBLIC","RANDOM","PUBLIC IDENTITY","RANDOM IDENTITY"};
    for(unsigned type=0;type<4;++type)for(unsigned mask=1;mask<=7;++mask){
        view.selection.frozen.row.radio=view.selection.latest.row.radio=CERBERUS_MAC_BLE;
        view.selection.frozen.row.address_type=view.selection.latest.row.address_type=(uint8_t)type;
        view.selection.latest.row.type_mask=(uint8_t)mask;
        update(false);same_as_full();
        char expected[49];snprintf(expected,sizeof(expected),"ADDRESS: %s",address_types[type]);
        CHECK(!strcmp(cache.painted.detail[7],expected));
        CHECK((strstr(cache.painted.detail[8],"ORIGINAL")!=NULL)==((mask&1)!=0));
        CHECK((strstr(cache.painted.detail[8],"RESPONSE")!=NULL)==((mask&2)!=0));
        CHECK((strstr(cache.painted.detail[8],"PARTIAL")!=NULL)==((mask&4)!=0));
        CHECK(!strstr(cache.painted.detail[8],"MASK") && !strstr(cache.painted.detail[8],"SNAPSHOT"));
    }
    save("ble-detail");view.selection=selected;update(false);same_as_full();
    view.selection.present=false;view.selection.unavailable=true;view.now_us=UINT64_MAX;
    view.selection.latest.observations=view.selection.latest.responses=UINT32_MAX;
    view.selection.latest.row.rssi=127;view.selection.latest.row.flags=0;
    view.selection.latest.row.incarnation=view.selection.latest.snapshot_id=UINT32_MAX;
    view.invalid[0]=view.invalid[1]=view.replacements[0]=view.replacements[1]=UINT32_MAX;
    view.selected_source_label="0123456789012345678901234567890123456789";
    update(false);save("maximum");same_as_full();
    CHECK(strstr(cache.painted.detail[4],">=4294967295") && strstr(cache.painted.detail[5],"UNAVAILABLE"));
    view.selection.latest.timing_valid=false;update(false);same_as_full();CHECK(strstr(cache.painted.detail[6],"UNAVAILABLE"));
    view.state=CERBERUS_MAC_UI_STOPPING;update(false);same_as_full();CHECK(cerberus_mac_monitor_ui_tap(&cache,180,440).action==CERBERUS_MAC_UI_STOP);
    view.detail=false;populate(0);view.state=CERBERUS_MAC_UI_STOPPED;view.stop_available=false;view.start_available=true;
    update(false);save("stopped");same_as_full();
    view.state=CERBERUS_MAC_UI_RUNNING;fail_at=calls+2;CHECK(cerberus_mac_monitor_ui_update(&cache,&view,false)!=ESP_OK);
    CHECK(!cache.valid && cerberus_mac_monitor_ui_tap(&cache,180,440).action==CERBERUS_MAC_UI_NONE);
    fail_at=0;update(false);same_as_full();
    view.page.copied=5;CHECK(cerberus_mac_monitor_ui_update(&cache,&view,true)==ESP_ERR_INVALID_ARG && !cache.valid);
    printf("MAC native UI: %u checks passed; view %zu cache %zu bytes\n",checks,sizeof(view),sizeof(cache));return 0;
}
