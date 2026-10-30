#define cerberus_display_draw_text framebuffer_draw_text
#include "field_framebuffer.h"
#undef cerberus_display_draw_text
#include "field_menu.h"
#include "sentinel_dashboard.h"
static char drawn_text[8192];
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,uint8_t scale)
{
    size_t used=strlen(drawn_text);
    assert(used+strlen(s)+2<sizeof(drawn_text));
    snprintf(drawn_text+used,sizeof(drawn_text)-used,"%s\n",s);
    return framebuffer_draw_text(x,y,s,fg,bg,scale);
}
static const cerberus_ui_widget_theme_t theme={0x0062,0x08A4,0x3F1B,0xE77E,0x84B5};
static void save(const char *prefix,const char *name)
{
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
static void draw(cerberus_field_menu_t *menu)
{
    drawn_text[0]=0;
    assert(cerberus_display_fill_rect(0,0,320,480,theme.background)==ESP_OK);
    assert(cerberus_display_draw_text(14,16,"FIELD KIT",theme.text,theme.background,2)==ESP_OK);
    assert(cerberus_field_menu_draw(menu,&theme)==ESP_OK);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    sentinel_dashboard_status_t status={.c5_online=true,.touch_online=true};
    assert(sentinel_dashboard_draw(&status,0)==ESP_OK);save(argv[1],"home");
    assert(sentinel_dashboard_hit_test(50,380)==SENTINEL_DASHBOARD_SCAN);
    assert(sentinel_dashboard_hit_test(220,380)==SENTINEL_DASHBOARD_ATTACK);
    assert(sentinel_dashboard_hit_test(160,380)==SENTINEL_DASHBOARD_NONE);
    assert(sentinel_dashboard_hit_test(50,440)==SENTINEL_DASHBOARD_WATCH);
    uint16_t bottom[320*124];memcpy(bottom,screen+320*350,sizeof(bottom));
    status.lessons_complete=25;
    assert(sentinel_dashboard_animate(&status,1)==ESP_OK);
    assert(!memcmp(bottom,screen+320*350,sizeof(bottom)));
    cerberus_field_menu_t menu={0};draw(&menu);save(argv[1],"scan");
    const cerberus_field_action_t routes[]={CERBERUS_FIELD_WIFI,CERBERUS_FIELD_BLE,CERBERUS_FIELD_WARDRIVE,CERBERUS_FIELD_RADIO,CERBERUS_FIELD_SURVEY,CERBERUS_FIELD_NETWORK,CERBERUS_FIELD_REMOTE_ID,CERBERUS_FIELD_MAC_MONITOR};
    for(unsigned i=0;i<8;++i){assert(cerberus_field_menu_tap(&menu,20,116+i*37)==routes[i]);assert(cerberus_field_menu_tap(&menu,20,150+i*37)==CERBERUS_FIELD_NONE);}
    assert(cerberus_field_menu_tap(&menu,307,408)==CERBERUS_FIELD_MAC_MONITOR);
    assert(cerberus_field_menu_tap(&menu,100,419)==CERBERUS_FIELD_NONE);
    assert(cerberus_field_menu_tap(&menu,100,420)==CERBERUS_FIELD_HOME);
    assert(cerberus_field_menu_tap(&menu,109,70)==CERBERUS_FIELD_NONE);
    assert(cerberus_field_menu_tap(&menu,212,70)==CERBERUS_FIELD_NONE);
    assert(cerberus_field_menu_tap(&menu,150,70)==CERBERUS_FIELD_REDRAW&&menu.attack);
    for(unsigned category=0;category<2;++category){
        assert(cerberus_field_menu_tap(&menu,category?200:50,110)==CERBERUS_FIELD_REDRAW);
        assert(menu.bluetooth==(bool)category&&menu.page==0);
        assert(cerberus_field_menu_tap(&menu,30,350)==CERBERUS_FIELD_NONE);
        unsigned pages=3;
        assert(cerberus_field_menu_tap(&menu,25,160)==(category?CERBERUS_FIELD_BLE:CERBERUS_FIELD_WIFI_SELECT));
        for(unsigned p=0;p<pages;++p){
            draw(&menu);char name[32];snprintf(name,sizeof(name),"%s-%u",category?"bluetooth":"wifi",p+1);save(argv[1],name);
            /* No target: every attack row and gap is inert. */
            for(unsigned y=208;y<338;++y)for(unsigned x=0;x<320;++x)assert(cerberus_field_menu_tap(&menu,x,y)==CERBERUS_FIELD_NONE);
            if(!category){
                assert(strstr(drawn_text,"SELECT APS FROM SCAN"));
                assert(strstr(drawn_text,"SELECT AP FROM SCAN FIRST"));
                assert(!strstr(drawn_text,"BEACON SPAM")&&!strstr(drawn_text,"RICK ROLL")&&!strstr(drawn_text,"KARMA"));
            }
            cerberus_field_action_t next=cerberus_field_menu_tap(&menu,250,350);
            assert(next==(p+1<pages?CERBERUS_FIELD_REDRAW:CERBERUS_FIELD_NONE));
        }
        while(menu.page)assert(cerberus_field_menu_tap(&menu,30,350)==CERBERUS_FIELD_REDRAW);
        assert(cerberus_field_menu_tap(&menu,150,390)==(category?CERBERUS_FIELD_RADIO_LAB:CERBERUS_FIELD_NONE));
    }
    menu.bluetooth=true;
    menu.ble_advertiser_available=true;
    menu.ble_connectable_available=true;
    menu.ble_gatt_available=true;
    const char *ble_required[]={"SOUR APPLE","APPLE JUICE","SWIFT PAIR SPAM",
        "SAMSUNG BLE SPAM","GOOGLE BLE SPAM","FLIPPER BLE SPAM",
        "BLE SPAM ALL","SPOOF AIRTAG","FIND MY BEEP ALL"};
    const cerberus_field_action_t ble_expected[]={CERBERUS_FIELD_BLE_SOUR_APPLE,CERBERUS_FIELD_BLE_APPLE_JUICE,
        CERBERUS_FIELD_BLE_SWIFT_PAIR,CERBERUS_FIELD_BLE_SAMSUNG,CERBERUS_FIELD_BLE_GOOGLE,
        CERBERUS_FIELD_BLE_FLIPPER,CERBERUS_FIELD_BLE_ALL,CERBERUS_FIELD_BLE_SPOOF_AIRTAG,
        CERBERUS_FIELD_BLE_FIND_MY_SOUND};
    for(unsigned page=0;page<3;++page){
        menu.page=page;draw(&menu);
        for(unsigned row=0;row<3;++row){
            unsigned index=page*3+row;
            assert(strstr(drawn_text,ble_required[index]));
            assert(cerberus_field_menu_tap(&menu,24,208+row*42)==ble_expected[index]);
            assert(cerberus_field_menu_tap(&menu,24,244+row*42)==CERBERUS_FIELD_NONE);
        }
        assert(!strstr(drawn_text,"NEEDS S3 CONNECTABLE ROLE"));
        if(page==2){
            assert(strstr(drawn_text,"SELECT OBSERVED BLE DEVICE"));
            assert(strstr(drawn_text,"ENDLESS / S3 SCAN + GATT / STOP"));
            assert(cerberus_field_menu_tap(&menu,24,304)==CERBERUS_FIELD_BLE_FIND_MY_SOUND);
        }
        char name[32];snprintf(name,sizeof(name),"ble-active-%u",page+1);save(argv[1],name);
    }
    menu.page=2;
    menu.ble_connectable_available=false;
    menu.page=0;draw(&menu);
    assert(strstr(drawn_text,"NEEDS S3 CONNECTABLE ROLE"));
    assert(cerberus_field_menu_tap(&menu,24,250)==CERBERUS_FIELD_NONE);
    menu.page=2;draw(&menu);
    assert(cerberus_field_menu_tap(&menu,24,208)==CERBERUS_FIELD_NONE);
    menu.ble_connectable_available=true;
    menu.ble_capture_selected=true;
    snprintf(menu.ble_target,sizeof(menu.ble_target),"Test beacon");
    draw(&menu);
    assert(strstr(drawn_text,"SELECT RANDOM-STATIC DEVICE"));
    assert(strstr(drawn_text,"CHANGE BLE: Test beacon"));
    assert(cerberus_field_menu_tap(&menu,24,250)==CERBERUS_FIELD_BLE_SPOOF_AIRTAG);
    menu.ble_capture_replayable=true;
    draw(&menu);
    assert(strstr(drawn_text,"SELECTED RAW AD READY"));
    menu.ble_advertiser_available=false;
    draw(&menu);
    assert(strstr(drawn_text,"S3 BLE ADVERTISER UNAVAILABLE"));
    assert(cerberus_field_menu_tap(&menu,24,250)==CERBERUS_FIELD_NONE);
    assert(cerberus_field_menu_tap(&menu,24,304)==CERBERUS_FIELD_BLE_FIND_MY_SOUND);
    menu.ble_gatt_available=false;
    draw(&menu);
    assert(strstr(drawn_text,"S3 BLE CENTRAL UNAVAILABLE"));
    assert(cerberus_field_menu_tap(&menu,24,304)==CERBERUS_FIELD_NONE);
    menu.bluetooth=false;menu.wifi_target_valid=true;menu.wifi_deauth_available=true;menu.wifi_probe_available=true;
    snprintf(menu.wifi_target,sizeof(menu.wifi_target),"Cerberus Test AP");
    snprintf(menu.wifi_target_detail,sizeof(menu.wifi_target_detail),"02:00:00:00:00:01 / CH 36 / 5 GHZ");
    const char *required[]={"DEAUTH FLOOD","BAD MESSAGE","ASSOCIATION SLEEP","SAE COMMIT FLOOD","CHANNEL SWITCH","QUIET TIME","PROBE REQUEST FLOOD","AP CLONE SPAM","EVIL PORTAL / AP CLONE"};
    for(unsigned p=0;p<3;++p){
        menu.page=p;draw(&menu);
        for(unsigned i=p*3;i<(p+1)*3&&i<9;++i)assert(strstr(drawn_text,required[i]));
        assert(strstr(drawn_text,"CHANGE AP: Cerberus Test AP")&&strstr(drawn_text,"02:00:00:00:00:01 / CH 36 / 5 GHZ"));
        assert(strstr(drawn_text,"NOT PORTED / UNAVAILABLE")&&!strstr(drawn_text,"SELECT AP FROM SCAN FIRST"));
        if(!p)assert(strstr(drawn_text,"CONFIGURE / BROADCAST OR STATION"));
        if(p==2)assert(strstr(drawn_text,"CONFIGURE / AP-DIRECTED PROBES"));
        assert(!strstr(drawn_text,"RADIO LAB")&&!strstr(drawn_text,"BEACON SPAM"));
        char name[32];snprintf(name,sizeof(name),"selected-ap-%u",p+1);save(argv[1],name);
    }
    assert(cerberus_field_menu_tap(&menu,25,160)==CERBERUS_FIELD_WIFI_SELECT);
    assert(cerberus_field_menu_tap(&menu,25,390)==CERBERUS_FIELD_WIFI_TARGET);
    assert(cerberus_field_menu_tap(&menu,25,375)==CERBERUS_FIELD_NONE);
    menu.page=0;
    for(unsigned y=208;y<338;++y)for(unsigned x=0;x<320;++x)
        assert(cerberus_field_menu_tap(&menu,x,y)==(x>=12&&x<308&&y<244?CERBERUS_FIELD_WIFI_DEAUTH:CERBERUS_FIELD_NONE));
    menu.wifi_deauth_available=false;assert(cerberus_field_menu_tap(&menu,20,220)==CERBERUS_FIELD_NONE);
    menu.page=2;assert(cerberus_field_menu_tap(&menu,20,220)==CERBERUS_FIELD_WIFI_PROBE);
    menu.wifi_probe_available=false;assert(cerberus_field_menu_tap(&menu,20,220)==CERBERUS_FIELD_NONE);
    menu.wifi_deauth_available=menu.wifi_probe_available=true;
    menu.wifi_bad_message_available=menu.wifi_assoc_sleep_available=menu.wifi_channel_switch_available=menu.wifi_quiet_available=true;
    menu.wifi_sae_available=true;
    menu.wifi_ap_clone_available=true;
    menu.wifi_portal_available=true;
    const cerberus_field_action_t expected[]={CERBERUS_FIELD_WIFI_DEAUTH,CERBERUS_FIELD_WIFI_BAD_MESSAGE,CERBERUS_FIELD_WIFI_ASSOC_SLEEP,
        CERBERUS_FIELD_WIFI_SAE,CERBERUS_FIELD_WIFI_CHANNEL_SWITCH,CERBERUS_FIELD_WIFI_QUIET,CERBERUS_FIELD_WIFI_PROBE,CERBERUS_FIELD_WIFI_AP_CLONE,CERBERUS_FIELD_WIFI_PORTAL};
    for(unsigned page=0;page<3;++page){menu.page=page;draw(&menu);
        for(unsigned y=208;y<334;++y)for(unsigned x=0;x<320;++x){unsigned row=(y-208)/42;
            assert(cerberus_field_menu_tap(&menu,x,y)==(x>=12&&x<308&&(y-208)%42<36?expected[page*3+row]:CERBERUS_FIELD_NONE));}
        char name[32];snprintf(name,sizeof(name),"all-ap-modes-%u",page+1);save(argv[1],name);
    }
    menu.page=2;menu.wifi_portal_available=false;
    assert(cerberus_field_menu_tap(&menu,20,304)==CERBERUS_FIELD_NONE);
    menu.wifi_portal_available=true;
    assert(cerberus_field_menu_tap(&menu,20,304)==CERBERUS_FIELD_WIFI_PORTAL);
    menu.wifi_bad_message_available=menu.wifi_assoc_sleep_available=menu.wifi_channel_switch_available=menu.wifi_quiet_available=menu.wifi_sae_available=false;
    menu.page=0;assert(cerberus_field_menu_tap(&menu,20,260)==CERBERUS_FIELD_NONE&&cerberus_field_menu_tap(&menu,20,300)==CERBERUS_FIELD_NONE);
    menu.page=1;assert(cerberus_field_menu_tap(&menu,20,260)==CERBERUS_FIELD_NONE&&cerberus_field_menu_tap(&menu,20,300)==CERBERUS_FIELD_NONE);
    assert(cerberus_field_menu_tap(&menu,250,70)==CERBERUS_FIELD_RECON);
    assert(cerberus_field_menu_tap(&menu,150,440)==CERBERUS_FIELD_HOME);
    assert(cerberus_field_menu_tap(&menu,400,70)==CERBERUS_FIELD_NONE);
    assert(cerberus_field_menu_draw(NULL,&theme)==ESP_ERR_INVALID_ARG);
    menu.page=255;assert(cerberus_field_menu_draw(&menu,&theme)==ESP_ERR_INVALID_ARG);
    puts("Field menu checks passed: all pages within bounds, scan routes, tab gaps, paging, unavailable actions inert, home/recon reachable.");
}
