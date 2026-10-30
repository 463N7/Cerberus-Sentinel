#include "picker_framebuffer.h"
#include "sentinel_scan_view.h"
#include "sentinel_scan_session.h"
static void save_frame(const char *path)
{
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}

static void assert_drawn_text(unsigned x,unsigned y,const char *label)
{
    /* Inspect the actual framebuffer using the production font: labels must
     * be fully visible at the expected position, not merely issue a draw. */
    uint16_t background=screen[y*320+x+5U],foreground=background;
    const uint8_t *first=font5x7(label[0]);
    for(unsigned row=0;row<7U;++row)for(unsigned col=0;col<5U;++col)
        if(first[row]&(1U<<(4U-col)))foreground=screen[(y+row)*320+x+col];
    assert(foreground!=background);
    for(unsigned c=0;c<strlen(label);++c){
        const uint8_t *glyph=font5x7(label[c]);
        for(unsigned row=0;row<7U;++row)for(unsigned col=0;col<6U;++col){
            bool ink=col<5U&&(glyph[row]&(1U<<(4U-col)));
            assert(screen[(y+row)*320+x+c*6U+col]==(ink?foreground:background));
        }
    }
}

static void check_age_controls(sentinel_scan_view_t *view,const char *all_path,
    const char *recent_path,const char *empty_path)
{
    _Static_assert(SENTINEL_VIEW_AGE==SENTINEL_VIEW_SORT+1,"Append action without renumbering old routes");
    static uint16_t before_screen[320*480];
    char age[24]="ALL AGES >";
    view->count_label="FIND MY >";view->sort_label="SCAN ORDER >";view->age_label=age;
    view->total_count=32U;view->page_count=8U;view->page=0U;view->has_next=true;
    view->hint="FROZEN SWEEP / 32 RETAINED OBSERVATIONS";
    assert(sentinel_scan_view_update(view,6U)==ESP_OK);
    assert_drawn_text(232U,58U,"SCAN ORDER >");
    assert_drawn_text(238U,83U,"ALL AGES >");
    assert_drawn_text(181U,64U,"1/8");
    save_frame(all_path);
    for(unsigned edge=0;edge<2U;++edge){
        uint16_t x=edge?311U:224U;
        assert(sentinel_scan_view_hit_test(view,x,48U)==SENTINEL_VIEW_SORT);
        assert(sentinel_scan_view_hit_test(view,x,73U)==SENTINEL_VIEW_SORT);
        assert(sentinel_scan_view_hit_test(view,x,74U)==SENTINEL_VIEW_AGE);
        assert(sentinel_scan_view_hit_test(view,x,97U)==SENTINEL_VIEW_AGE);
        assert(sentinel_scan_view_hit_test(view,x,47U)==SENTINEL_VIEW_NONE);
        assert(sentinel_scan_view_hit_test(view,x,98U)==SENTINEL_VIEW_NONE);
    }
    for(unsigned x=215U;x<224U;++x){
        assert(sentinel_scan_view_hit_test(view,(uint16_t)x,65U)==SENTINEL_VIEW_NONE);
        assert(sentinel_scan_view_hit_test(view,(uint16_t)x,85U)==SENTINEL_VIEW_NONE);
    }
    assert(sentinel_scan_view_hit_test(view,312U,85U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(view,250U,113U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(view,250U,126U)==SENTINEL_VIEW_ROW0);
    memcpy(before_screen,screen,sizeof(screen));unsigned before=calls;
    strcpy(age,"WITHIN 30S >"); /* Same pointer: the cache must own the old text. */
    assert(sentinel_scan_view_update(view,7U)==ESP_OK&&calls>before);
    for(unsigned y=0;y<480U;++y)if(y<48U||y>=98U)
        assert(!memcmp(screen+y*320U,before_screen+y*320U,320U*sizeof(screen[0])));
    assert_drawn_text(232U,83U,"WITHIN 30S >");
    before=calls;assert(sentinel_scan_view_update(view,8U)==ESP_OK&&calls==before);
    view->page=7U;view->has_previous=true;view->has_next=false;
    assert(sentinel_scan_view_update(view,9U)==ESP_OK);
    assert_drawn_text(181U,64U,"8/8");save_frame(recent_path);
    view->total_count=0U;view->row_count=0U;view->page=0U;view->page_count=1U;
    view->has_previous=false;view->message="NO RECENT HINTS";
    view->hint="0 WITHIN 30S / 32 RETAINED IN SWEEP";
    assert(sentinel_scan_view_update(view,10U)==ESP_OK);
    assert_drawn_text(232U,83U,"WITHIN 30S >");
    assert(sentinel_scan_view_hit_test(view,250U,126U)==SENTINEL_VIEW_NONE);
    save_frame(empty_path);
    before=calls;assert(sentinel_scan_view_update(view,11U)==ESP_OK&&calls==before);

    view->state=SENTINEL_VIEW_OFFLINE;
    assert(sentinel_scan_view_update(view,12U)==ESP_OK);
    assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_NONE);
    assert_drawn_text(232U,83U,"WITHIN 30S >"); /* Visible but disabled. */
    view->state=SENTINEL_VIEW_ERROR;
    assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_AGE); /* Retained rows can be filtered. */
    view->state=SENTINEL_VIEW_COMPLETE;
    view->selection_mode=true;view->selection_count=0U;
    assert(sentinel_scan_view_update(view,13U)==ESP_OK);
    assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_NONE);
    view->selection_count=1U;
    assert(sentinel_scan_view_hit_test(view,250U,65U)==SENTINEL_VIEW_CLEAR);
    assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_NONE);
    view->selection_mode=false;
    const sentinel_view_kind_t unsupported[]={SENTINEL_VIEW_ENERGY,SENTINEL_VIEW_RADIO_LIST};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);++i){
        view->kind=unsupported[i];assert(sentinel_scan_view_draw(view,14U)==ESP_OK);
        assert(sentinel_scan_view_hit_test(view,250U,65U)==SENTINEL_VIEW_NONE);
        assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_NONE);
        before=calls;strcpy(age,"ALL AGES >");
        assert(sentinel_scan_view_update(view,15U)==ESP_OK&&calls==before);
        strcpy(age,"WITHIN 30S >");
    }
    view->kind=SENTINEL_VIEW_BLE;view->sort_label=NULL;
    assert(sentinel_scan_view_draw(view,16U)==ESP_OK);
    assert(sentinel_scan_view_hit_test(view,250U,65U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_AGE);
    view->sort_label="SIGNAL >";view->age_label="";
    assert(sentinel_scan_view_update(view,17U)==ESP_OK);
    assert(sentinel_scan_view_hit_test(view,224U,48U)==SENTINEL_VIEW_SORT);
    assert(sentinel_scan_view_hit_test(view,311U,97U)==SENTINEL_VIEW_SORT);
    view->age_label=NULL;before=calls;
    assert(sentinel_scan_view_update(view,18U)==ESP_OK&&calls==before);
    assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_SORT);
    view->state=(sentinel_view_state_t)255;
    assert(sentinel_scan_view_hit_test(view,250U,85U)==SENTINEL_VIEW_NONE);
    puts("BLE age controls passed: exact labels/paging, split bounds, cached in-place update, unchanged body, empty/disabled/unsupported views and legacy sort.");
}

static void check_wifi_age_controls(const char *all_path,const char *recent_path,
    const char *empty_path)
{
    static uint16_t before_screen[320*480];
    char age[24]="ALL AGES >";
    sentinel_scan_view_t view={.kind=SENTINEL_VIEW_WIFI,.state=SENTINEL_VIEW_COMPLETE,
        .age_label=age,.subtitle="SWEEP COMPLETE / TAP AGAIN TO RESCAN",
        .hint="32 RETAINED NETWORKS / AGE FILTER ABOVE",
        .total_count=32U,.page_count=8U,.has_next=true,.row_count=4U,
        .rows={{.name="Yasin Family Home",.detail="02:11:22:33:44:50 / CH 6",.badge="2.4G / WPA2",.rssi=-42,.signal_valid=true},
               {.name="Yasin Family Home",.detail="02:11:22:33:44:51 / CH 36",.badge="5G / WPA2",.rssi=-49,.signal_valid=true},
               {.name="Workshop network",.detail="02:11:22:33:44:52 / CH 11",.badge="2.4G / WPA2",.rssi=-70,.signal_valid=true},
               {.name="Hidden network",.detail="02:11:22:33:44:53 / CH 44",.badge="5G / WPA2",.rssi=-82,.signal_valid=true}}};
    assert(sentinel_scan_view_draw(&view,0U)==ESP_OK);
    assert_drawn_text(244U,58U,"CAPTURED");
    assert_drawn_text(238U,83U,"ALL AGES >");
    assert_drawn_text(181U,64U,"1/8");save_frame(all_path);
    for(unsigned edge=0;edge<2U;++edge){
        uint16_t x=edge?311U:224U;
        assert(sentinel_scan_view_hit_test(&view,x,47U)==SENTINEL_VIEW_NONE);
        assert(sentinel_scan_view_hit_test(&view,x,48U)==SENTINEL_VIEW_NONE);
        assert(sentinel_scan_view_hit_test(&view,x,73U)==SENTINEL_VIEW_NONE);
        assert(sentinel_scan_view_hit_test(&view,x,74U)==SENTINEL_VIEW_AGE);
        assert(sentinel_scan_view_hit_test(&view,x,97U)==SENTINEL_VIEW_AGE);
        assert(sentinel_scan_view_hit_test(&view,x,98U)==SENTINEL_VIEW_NONE);
    }
    for(unsigned x=215U;x<224U;++x)
        assert(sentinel_scan_view_hit_test(&view,(uint16_t)x,85U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&view,312U,85U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&view,250U,113U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&view,250U,126U)==SENTINEL_VIEW_ROW0);
    unsigned before=calls;
    view.sort_label="SIGNAL >"; /* Wi-Fi has no sort route or sort label. */
    assert(sentinel_scan_view_update(&view,1U)==ESP_OK&&calls==before);
    assert_drawn_text(244U,58U,"CAPTURED");
    assert(sentinel_scan_view_hit_test(&view,250U,58U)==SENTINEL_VIEW_NONE);
    memcpy(before_screen,screen,sizeof(screen));
    strcpy(age,"WITHIN 30S >");
    assert(sentinel_scan_view_update(&view,2U)==ESP_OK&&calls>before);
    for(unsigned y=0U;y<480U;++y)if(y<48U||y>=98U)
        assert(!memcmp(screen+y*320U,before_screen+y*320U,320U*sizeof(screen[0])));
    assert_drawn_text(244U,58U,"CAPTURED");
    assert_drawn_text(232U,83U,"WITHIN 30S >");
    before=calls;assert(sentinel_scan_view_update(&view,3U)==ESP_OK&&calls==before);
    view.page=7U;view.has_previous=true;view.has_next=false;
    assert(sentinel_scan_view_update(&view,4U)==ESP_OK);
    assert_drawn_text(181U,64U,"8/8");save_frame(recent_path);
    view.total_count=UINT16_MAX; /* Max summary width cannot collide with page. */
    assert(sentinel_scan_view_update(&view,5U)==ESP_OK);
    assert_drawn_text(181U,64U,"8/8");
    assert_drawn_text(244U,58U,"CAPTURED");

    static const char *const states[]={"READY","LIVE","PAUSED","CAPTURED","ERROR","OFFLINE"};
    for(unsigned state=0;state<sizeof(states)/sizeof(states[0]);++state){
        view.state=(sentinel_view_state_t)state;
        assert(sentinel_scan_view_update(&view,6U)==ESP_OK);
        assert_drawn_text(228U+(80U-(unsigned)strlen(states[state])*6U)/2U,58U,states[state]);
        assert(sentinel_scan_view_hit_test(&view,250U,58U)==SENTINEL_VIEW_NONE);
        assert(sentinel_scan_view_hit_test(&view,250U,85U)==
            (state==SENTINEL_VIEW_OFFLINE?SENTINEL_VIEW_NONE:SENTINEL_VIEW_AGE));
    }
    view.state=SENTINEL_VIEW_COMPLETE;view.total_count=0U;view.row_count=0U;
    view.page=0U;view.page_count=1U;view.has_previous=false;
    view.message="NO RECENT NETWORKS";view.hint="0 WITHIN 30S / 32 RETAINED IN SWEEP";
    assert(sentinel_scan_view_update(&view,7U)==ESP_OK);
    assert_drawn_text(244U,58U,"CAPTURED");
    assert_drawn_text(232U,83U,"WITHIN 30S >");save_frame(empty_path);
    assert(sentinel_scan_view_hit_test(&view,250U,126U)==SENTINEL_VIEW_NONE);
    before=calls;assert(sentinel_scan_view_update(&view,8U)==ESP_OK&&calls==before);
    view.selection_mode=true;view.selection_count=1U;
    assert(sentinel_scan_view_update(&view,9U)==ESP_OK);
    assert_drawn_text(253U,65U,"CLEAR");
    assert(sentinel_scan_view_hit_test(&view,224U,57U)==SENTINEL_VIEW_CLEAR);
    assert(sentinel_scan_view_hit_test(&view,311U,79U)==SENTINEL_VIEW_CLEAR);
    assert(sentinel_scan_view_hit_test(&view,250U,80U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&view,250U,85U)==SENTINEL_VIEW_NONE);
    before=calls;strcpy(age,"ALL AGES >");
    assert(sentinel_scan_view_update(&view,10U)==ESP_OK&&calls==before);
    view.selection_count=0U;
    assert(sentinel_scan_view_hit_test(&view,250U,65U)==SENTINEL_VIEW_NONE);
    view.selection_mode=false;view.age_label="";
    assert(sentinel_scan_view_update(&view,11U)==ESP_OK);
    assert_drawn_text(244U,65U,"CAPTURED");
    assert(sentinel_scan_view_hit_test(&view,224U,48U)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&view,311U,97U)==SENTINEL_VIEW_NONE);
    view.age_label=NULL;before=calls;
    assert(sentinel_scan_view_update(&view,12U)==ESP_OK&&calls==before);
    puts("Wi-Fi age controls passed: state-only top, exact labels/paging, split edges, same-pointer cache, unchanged body, state changes, empty/offline/selection and legacy no-age views.");
}

esp_err_t cerberus_display_draw_mono_bitmap(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *data,uint16_t fg,uint16_t bg)
{
    bounds(x,y,w,h);
    for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)
        screen[(y+row)*320+x+col]=data[row*((w+7)/8)+col/8]&(0x80>>(col%8))?fg:bg;
    return ESP_OK;
}
int main(int argc,char **argv)
{
    assert(argc==9);
    sentinel_scan_view_t v={.kind=SENTINEL_VIEW_WIFI,.state=SENTINEL_VIEW_COMPLETE,
        .count_label="NETWORKS SELECTED",.subtitle="TAP NETWORKS TO TOGGLE / THEN ATTACKS",
        .hint="0 SELECTED / 7 IN SWEEP / CLEAR ABOVE",.tools_label="ATTACKS",.tools_disabled=true,.selection_mode=true,
        .total_count=7,.page_count=2,.has_next=true,.row_count=4,
        .rows={{.name="Yasin Family Home",.detail="02:11:22:33:44:50 / CH 6",.badge="2.4G / WPA2 / TAP TO SELECT",.rssi=-42,.signal_valid=true},
               {.name="Yasin Family Home",.detail="02:11:22:33:44:51 / CH 36",.badge="5G / WPA2 / TAP TO SELECT",.rssi=-49,.signal_valid=true},
               {.name="A full thirty-two character SSID",.detail="02:11:22:33:44:52 / CH 11",.badge="2.4G / WPA2 / TAP TO SELECT",.rssi=-70,.signal_valid=true},
               {.name="Hidden network",.detail="02:11:22:33:44:53 / CH 44",.badge="5G / WPA2 / TAP TO SELECT",.rssi=-82,.signal_valid=true}}};
    assert(sentinel_scan_view_draw(&v,0)==ESP_OK);
    unsigned before=calls;assert(sentinel_scan_view_update(&v,1)==ESP_OK&&calls==before);
    assert(sentinel_scan_view_hit_test(&v,250,380)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&v,250,65)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&v,20,150)==SENTINEL_VIEW_ROW0);
    assert(sentinel_scan_view_hit_test(&v,20,206)==SENTINEL_VIEW_ROW1);
    assert(sentinel_scan_view_hit_test(&v,20,179)==SENTINEL_VIEW_NONE);
    v.rows[0].selected=true;v.rows[0].badge="2.4G / WPA2 / SELECTED";v.tools_disabled=false;v.selection_count=2;
    v.rows[1].selected=true;v.rows[1].badge="5G / WPA2 / SELECTED";v.hint="2 SELECTED / 7 IN SWEEP / CLEAR ABOVE";
    assert(sentinel_scan_view_update(&v,2)==ESP_OK&&calls>before);
    before=calls;assert(sentinel_scan_view_update(&v,3)==ESP_OK&&calls==before);
    assert(sentinel_scan_view_hit_test(&v,250,380)==SENTINEL_VIEW_TOOLS);
    assert(sentinel_scan_view_hit_test(&v,250,65)==SENTINEL_VIEW_CLEAR);
    assert(sentinel_scan_view_hit_test(&v,250,80)==SENTINEL_VIEW_NONE);
    v.rows[2].name="A full thirty-two character name";assert(sentinel_scan_view_update(&v,4)==ESP_OK&&calls>before);
    save_frame(argv[1]);
    char sorting[13]="SIGNAL >";
    v=(sentinel_scan_view_t){.kind=SENTINEL_VIEW_BLE,.state=SENTINEL_VIEW_COMPLETE,
        .count_label="ALL >",.sort_label=sorting,.subtitle="SWEEP COMPLETE / TAP AGAIN TO RESCAN",
        .hint="FILTER LEFT / SORT RIGHT / 7 IN SWEEP",.tools_label="TOOLS",
        .total_count=7,.page_count=2,.has_next=true,.row_count=4,
        .rows={{.name="Lab temperature",.detail="C0:11:22:33:44:55",.badge="BROADCAST",.rssi=-28,.signal_valid=true},
               {.name="Desk sensor",.detail="C0:11:22:33:44:66",.badge="BROADCAST",.rssi=-43,.signal_valid=true},
               {.name="Workshop beacon",.detail="C0:11:22:33:44:77",.badge="BROADCAST",.rssi=-61,.signal_valid=true},
               {.name="C0:11:22:33:44:88",.detail="C0:11:22:33:44:88",.badge="BROADCAST",.rssi=-87,.signal_valid=true}}};
    assert(sentinel_scan_view_draw(&v,0)==ESP_OK);
    before=calls;assert(sentinel_scan_view_update(&v,1)==ESP_OK&&calls==before);
    assert(sentinel_scan_view_hit_test(&v,250,65)==SENTINEL_VIEW_SORT);
    assert(sentinel_scan_view_hit_test(&v,224,48)==SENTINEL_VIEW_SORT);
    assert(sentinel_scan_view_hit_test(&v,311,97)==SENTINEL_VIEW_SORT);
    assert(sentinel_scan_view_hit_test(&v,223,65)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&v,312,65)==SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&v,250,98)==SENTINEL_VIEW_NONE);
    strcpy(sorting,"NAME A-Z >");
    assert(sentinel_scan_view_update(&v,2)==ESP_OK&&calls>before);
    before=calls;assert(sentinel_scan_view_update(&v,3)==ESP_OK&&calls==before);
    strcpy(sorting,"SCAN ORDER >");
    assert(sentinel_scan_view_update(&v,4)==ESP_OK&&calls>before);
    save_frame(argv[2]);
    v.sort_label=NULL;assert(sentinel_scan_view_update(&v,5)==ESP_OK);
    assert(sentinel_scan_view_hit_test(&v,250,65)==SENTINEL_VIEW_NONE);
    check_age_controls(&v,argv[3],argv[4],argv[5]);
    check_wifi_age_controls(argv[6],argv[7],argv[8]);
    sentinel_scan_session_t s;sentinel_scan_session_init(&s);
    sentinel_scan_session_select_snapshot(&s,SENTINEL_SCAN_WIFI);
    sentinel_scan_observation_t idle={.valid=true};
    assert(sentinel_scan_session_step(&s,0,idle).kind==SENTINEL_SCAN_COMMAND_NONE&&s.completed);
    sentinel_scan_session_select(&s,SENTINEL_SCAN_NONE);
    sentinel_scan_session_select_snapshot(&s,SENTINEL_SCAN_WIFI);
    assert(sentinel_scan_session_step(&s,100,idle).kind==SENTINEL_SCAN_COMMAND_NONE);
    sentinel_scan_session_pause(&s,false);
    assert(sentinel_scan_session_step(&s,200,idle).kind==SENTINEL_SCAN_COMMAND_START);
    puts("Network picker passed: full names, same-SSID selections, selected count/card redraw, Clear hit targets, idle redraw suppression, disabled actions, cached return and explicit rescan.");
}
