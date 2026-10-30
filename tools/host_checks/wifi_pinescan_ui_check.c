#define cerberus_display_draw_text framebuffer_draw_text
#include "pinescan_framebuffer.h"
#undef cerberus_display_draw_text
#include "wifi_pinescan_session.h"
#include "sentinel_hub.h"
#include "c5_spi_link.h"
#include "privacy.h"

static char drawn[16384];
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *s,uint16_t fg,uint16_t bg,uint8_t scale)
{
    size_t used = strlen(drawn);
    assert(used + strlen(s) + 2U < sizeof(drawn));
    snprintf(drawn + used, sizeof(drawn) - used, "%s\n", s);
    return framebuffer_draw_text(x,y,s,fg,bg,scale);
}
#define UI_BACKGROUND 0x0042
#define UI_PANEL 0x08A4
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define UI_GOOD 0x471B
#define UI_BAD 0xF440
#define TAG "host"
#define MALLOC_CAP_SPIRAM 4U
#define MALLOC_CAP_8BIT 2U
#define ESP_ERROR_CHECK(call) assert((call)==ESP_OK)
static const cerberus_ui_widget_theme_t theme={UI_BACKGROUND,UI_PANEL,0x3F1B,UI_TEXT,UI_MUTED};
#define UI_WIDGET_THEME (&theme)
#ifdef PINESCAN_NAVIGATION_CHECK
#include "wifi_navigation_types.h"
#else
enum {UI_PAGE_WIFI, UI_PAGE_MAIN};
enum {UI_WIFI_MODE_MENU, UI_WIFI_MODE_PINESCAN};
#endif
static unsigned current_page=UI_PAGE_WIFI,wifi_mode=UI_WIFI_MODE_MENU;
static bool c5_online=true,mounted,masked,allocation_fail;
static cerberus_c5_status_t c5_status={.firmware_version=CERBERUS_WIFI_CAPTURE_HOP_MIN_FIRMWARE};
static unsigned allocations,frees,starts,stops,follows,releases,status_reads,page_reads,draws;
static uint32_t now=1000;
static cerberus_s3_pinescan_status_t remote;
static cerberus_s3_pinescan_follow_t follow;
static cerberus_s3_pinescan_row_t source[8], followed;
static uint16_t source_count;
static esp_err_t status_error,page_error,start_error,follow_error,release_error;
static bool bad_generation,bad_reason,bad_ssid,bad_match_address;
static bool last_save;
static void *heap_caps_calloc(size_t count,size_t size,unsigned caps)
{
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(allocation_fail)return NULL;
    ++allocations;return calloc(count,size);
}
static void heap_caps_free(void *p){if(p){++frees;free(p);}}
static bool cerberus_storage_is_mounted(void){return mounted;}
static bool privacy_addresses_masked(void){return masked;}
int64_t esp_timer_get_time(void){return (int64_t)now*1000;}
static const char *esp_err_to_name(esp_err_t err)
{
    switch(err){case ESP_ERR_NO_MEM:return "ESP_ERR_NO_MEM";case ESP_ERR_INVALID_STATE:return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_TIMEOUT:return "ESP_ERR_TIMEOUT";case ESP_ERR_INVALID_RESPONSE:return "ESP_ERR_INVALID_RESPONSE";
    default:return "ESP_FAIL";}
}
static esp_err_t draw_centered(uint16_t y,const char *s,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320U-strlen(s)*6U*scale)/2U),y,s,color,UI_BACKGROUND,scale);}
static esp_err_t draw_detail_page(void){++draws;return ESP_OK;}
static esp_err_t draw_current_page(void){++draws;return ESP_OK;}

esp_err_t cerberus_s3_pinescan_status(cerberus_s3_pinescan_status_t *out)
{++status_reads;if(status_error)return status_error;*out=remote;return ESP_OK;}
esp_err_t cerberus_s3_pinescan_page(uint16_t offset,cerberus_s3_pinescan_row_t *rows,size_t capacity,cerberus_s3_pinescan_page_t *page)
{
    ++page_reads;assert(capacity==4);
    size_t copied=offset<source_count?source_count-offset:0U;if(copied>capacity)copied=capacity;
    memcpy(rows,source+offset,copied*sizeof(*rows));
    if(copied&&bad_generation)++rows[0].generation;
    if(copied&&bad_reason)rows[0].evidence.last_match.reasons=0;
    if(copied&&bad_ssid)rows[0].evidence.last_match.beacon.ssid_length=33;
    if(copied&&bad_match_address)rows[0].evidence.last_match.beacon.transmitter[5]^=1;
    *page=(cerberus_s3_pinescan_page_t){.generation=remote.generation,.total=source_count,.copied=(uint16_t)copied};
    return page_error;
}
esp_err_t cerberus_s3_pinescan_start(bool save)
{
    ++starts;last_save=save;
    if(start_error)return start_error;
    assert(!remote.capture.busy);
    remote.capture.busy=true;remote.capture.hopping=true;remote.capture.save_pcap=save;
    remote.capture.state=CERBERUS_S3_CAPTURE_STARTING;remote.following=false;
    return ESP_OK;
}
void cerberus_s3_pinescan_stop(void)
{++stops;if(remote.capture.busy)remote.capture.state=CERBERUS_S3_CAPTURE_STOPPING;}
esp_err_t cerberus_s3_pinescan_follow(const cerberus_s3_pinescan_row_t *selected,bool save)
{
    ++follows;assert(!remote.capture.busy);followed=*selected;last_save=save;
    if(follow_error)return follow_error;
    ++remote.capture.generation;
    remote.capture.channel=selected->evidence.latest.rx_channel;remote.following=true;remote.capture.busy=true;remote.capture.hopping=false;remote.capture.save_pcap=save;
    remote.capture.state=CERBERUS_S3_CAPTURE_STARTING;return ESP_OK;
}
esp_err_t cerberus_s3_pinescan_follow_status(cerberus_s3_pinescan_follow_t *out){*out=follow;return ESP_OK;}
esp_err_t cerberus_s3_pinescan_release(void)
{++releases;assert(!remote.capture.busy);return release_error;}

#include "wifi_pinescan_ui.h"
#include "pinescan_routes.h"

static void seed(void)
{
    memset(&remote,0,sizeof(remote));memset(&follow,0,sizeof(follow));memset(source,0,sizeof(source));
    source_count=5;remote.generation=17;remote.candidates=5;remote.capacity=32;remote.valid_beacons=123;
    remote.matching_beacons=19;remote.received_24ghz=100;remote.received_5ghz=23;
    remote.capture.generation=17;remote.capture.state=CERBERUS_S3_CAPTURE_RUNNING;
    remote.capture.busy=true;remote.capture.hopping=true;
    for(unsigned i=0;i<source_count;++i){
        source[i].generation=17;source[i].first_received_age_ms=12000;source[i].last_received_age_ms=2000;
        cerberus_wifi_pinescan_record_t *r=&source[i].evidence;
        r->latest.rx_channel=36;r->latest.advertised_channel=149;r->latest.rssi=-70+(int)i;r->peak_rssi=-34;
        const uint8_t address[6]={0,0xc0,0xca,1,2,(uint8_t)(i+1)};memcpy(r->latest.transmitter,address,6);
        r->latest.bssid[0]=4;r->latest.bssid[5]=(uint8_t)(i+2);r->multiple_bssids=true;r->frames=55;r->matching_frames=5;
        r->latest.observed_us=UINT64_C(8000000000012000);r->first_observed_us=UINT64_C(8000000000000000);
        r->latest.ssid_length=32;memset(r->latest.ssid,0xff,32);r->latest.capabilities=0x11;
        r->last_match.beacon=r->latest;
        r->last_match.beacon.ssid_length=5;memcpy(r->last_match.beacon.ssid,"A\0B\\\xff",5);
        r->last_match.beacon.rx_channel=6;r->last_match.beacon.advertised_channel=11;r->last_match.beacon.rssi=-50;
        r->last_match.beacon.bssid[0]=6;r->last_match.beacon.capabilities=1;
        r->last_match.beacon.observed_us=UINT64_C(8000000000004000);
        r->last_match.reasons=CERBERUS_WIFI_PINESCAN_REASON_OUI|CERBERUS_WIFI_PINESCAN_REASON_MINIMAL;
        r->last_match.profile=CERBERUS_WIFI_PINESCAN_PROFILE_ALFA;
        r->last_match.condition=CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR;
        r->last_match.ie_count=2;r->reasons_seen=r->last_match.reasons;
    }
}
static void poll(void){now+=500;actual_tick(now,false);}
static void render(void)
{
    drawn[0]='\0';assert(cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND)==ESP_OK);
    assert(actual_draw()==ESP_OK);
}
static void save(const char *prefix,const char *suffix)
{
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,suffix);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv)
{
    assert(argc==2);
    const uint8_t binary[]={'A',0,'B','\\',0xff};char escaped[129];
    assert(pinescan_escape(binary,sizeof(binary),escaped,sizeof(escaped))==12&&!strcmp(escaped,"A\\x00B\\\\\\xFF"));
    assert(pinescan_escape(binary,sizeof(binary),escaped,4)==0&&escaped[0]=='\0');
    assert(sentinel_hub_hit_test(SENTINEL_HUB_WIFI,88,364)==9);
    assert(sentinel_hub_hit_test(SENTINEL_HUB_WIFI,155,405)==9);
    assert(sentinel_hub_hit_test(SENTINEL_HUB_WIFI,87,380)==-1);
    assert(sentinel_hub_hit_test(SENTINEL_HUB_WIFI,156,380)==-1);
    assert(sentinel_hub_hit_test(SENTINEL_HUB_WIFI,164,380)==8);
    assert(sentinel_hub_draw(SENTINEL_HUB_WIFI,&theme)==ESP_OK);save(argv[1],"menu");
    actual_menu(122,380);assert(wifi_mode==UI_WIFI_MODE_PINESCAN&&pinescan_ui_open&&!pinescan_ui_save);
    assert(!starts&&!follows&&!stops&&!allocations);render();save(argv[1],"ready-no-sd");
    assert(strstr(drawn,"NO AP SELECTION REQUIRED")&&strstr(drawn,"NO SD CARD REQUIRED"));
    assert(strstr(drawn,"ORDINARY ADAPTERS CAN MATCH THESE RULES"));
    actual_tap(250,380);assert(!pinescan_ui_save);
    c5_online=false;actual_tap(100,380);assert(!starts&&strstr(pinescan_ui_message,"UNAVAILABLE"));
    c5_online=true;actual_tap(100,380);assert(starts==1&&!last_save&&remote.capture.busy);
    mounted=true;actual_tap(250,380);assert(!pinescan_ui_save);
    seed();poll();assert(pinescan_ui_page.copied==4&&pinescan_ui_page.total==5&&allocations==1);
    render();save(argv[1],"running");assert(strstr(drawn,"LAST MATCH: OUI + MINIMAL LAYOUT")&&strstr(drawn,"RX 36 / ADV 149"));
    assert(strstr(drawn,"2.4G 100 / 5G 23 / HINT FRAMES 19"));
    masked=true;render();assert(!strstr(drawn,"00:C0:CA:")&&strstr(drawn,"TX WIFI-"));masked=false;
    actual_tap(180,380);assert(pinescan_ui_offset==4&&pinescan_ui_page.copied==1);
    actual_tap(180,380);assert(pinescan_ui_offset==4);actual_tap(20,380);assert(pinescan_ui_offset==0);
    cerberus_s3_pinescan_row_t painted=pinescan_ui_store->rows[0];
    bad_generation=true;poll();assert(!memcmp(&painted,&pinescan_ui_store->rows[0],sizeof(painted)));bad_generation=false;
    bad_reason=true;poll();assert(!memcmp(&painted,&pinescan_ui_store->rows[0],sizeof(painted)));bad_reason=false;
    bad_ssid=true;poll();assert(!memcmp(&painted,&pinescan_ui_store->rows[0],sizeof(painted)));bad_ssid=false;
    bad_match_address=true;poll();assert(!memcmp(&painted,&pinescan_ui_store->rows[0],sizeof(painted)));bad_match_address=false;
    page_error=ESP_ERR_TIMEOUT;poll();assert(!memcmp(&painted,&pinescan_ui_store->rows[0],sizeof(painted)));page_error=ESP_OK;
    poll();assert(!pinescan_ui_message[0]);
    actual_tap(20,183);assert(!pinescan_ui_detail); /* First row's lower gap. */
    source[0].evidence.latest.transmitter[5]=99; /* Different source after paint. */
    actual_tap(20,150);assert(pinescan_ui_detail&&!memcmp(&painted,&pinescan_ui_store->selected,sizeof(painted))&&!follows);
    render();assert(strstr(drawn,"LAST MATCH / RETAINED QUALIFYING BEACON"));
    assert(strstr(drawn,"BSSID 06:00:00:00:00:02")&&strstr(drawn,"CAP 0001 / PRIVACY CLEAR / IES 2"));
    assert(strstr(drawn,"SSID 5B / RX 6 / ADV 11 / -50 DBM")&&strstr(drawn,"A\\x00B\\\\\\xFF"));
    assert(strstr(drawn,"PREFIX RULE REQUIRES PRIVACY BIT CLEAR")&&strstr(drawn,"LAYOUT: ONLY SSID THEN DS / CAP 0001"));
    assert(strstr(drawn,"FOLLOW USES LATEST RX CH 36"));
    assert(strstr(drawn,"HINTS DO NOT IDENTIFY A PHYSICAL DEVICE")&&!strstr(drawn,"DETECTED PINEAPPLE"));save(argv[1],"matched-binary");
    actual_tap(30,380);render();assert(strstr(drawn,"LATEST BEACON / NOT A NEW CLASSIFICATION"));
    assert(strstr(drawn,"BSSID 04:00:00:00:00:02")&&strstr(drawn,"SOURCE SPAN 12 MS"));
    assert(strstr(drawn,"S3 RXD AGE")&&strstr(drawn,"SSID 32B / RX 36 / ADV 149 / -70 DBM"));
    assert(!strstr(drawn,"CAP 0001")&&!strstr(drawn,"PREFIX RULE:"));save(argv[1],"latest-32-binary");
    masked=true;render();assert(!strstr(drawn,"00:C0:CA:")&&!strstr(drawn,"04:00:00:00:00:02")&&strstr(drawn,"TX CLAIM WIFI-"));
    save(argv[1],"masked");masked=false;
    /* Exact current proof and historical OR are distinct: do not borrow a
     * previously seen minimal-layout reason for an OUI-only latest match. */
    pinescan_ui_store->selected.evidence.last_match.reasons=CERBERUS_WIFI_PINESCAN_REASON_OUI;
    pinescan_ui_store->selected.evidence.last_match.ie_count=5;
    pinescan_ui_store->selected.evidence.last_match.beacon.capabilities=0x11;
    pinescan_ui_store->selected.evidence.last_match.privacy_bit=true;
    pinescan_ui_store->selected.evidence.last_match.condition=CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_SET;
    actual_tap(30,380);render();assert(strstr(drawn,"HINT: OUI RULE")&&!strstr(drawn,"HINT: OUI + MINIMAL"));
    assert(strstr(drawn,"MINIMAL LAYOUT RULE NOT MATCHED")&&strstr(drawn,"PRIVACY BIT SET"));
    actual_tap(30,380);render();assert(strstr(drawn,"HISTORICAL HINTS: OUI + MINIMAL LAYOUT"));
    pinescan_ui_store->selected.evidence.last_match.reasons=CERBERUS_WIFI_PINESCAN_REASON_MINIMAL;
    actual_tap(30,380);render();assert(strstr(drawn,"NO MATCHED PREFIX RULE")&&strstr(drawn,"PRIVACY BIT N/A"));
    pinescan_ui_store->selected=painted;
    unsigned before=stops;actual_tap(150,380);assert(stops==before+1&&pinescan_ui_follow_pending&&!follows);
    poll();assert(!follows&&!memcmp(&painted,&pinescan_ui_store->selected,sizeof(painted)));
    remote.capture.busy=false;remote.capture.state=CERBERUS_S3_CAPTURE_COMPLETE;poll();
    assert(follows==1&&remote.following&&remote.capture.busy&&!last_save&&!memcmp(&followed,&painted,sizeof(painted)));
    remote.capture.state=CERBERUS_S3_CAPTURE_RUNNING;render();assert(strstr(drawn,"LISTENING RX CH 36 / WAITING FOR THIS TX"));
    follow.valid=true;follow.generation=18;follow.matching_beacons=7;follow.peak_rssi=-40;follow.last_received_age_ms=3000;
    follow.latest=painted.evidence.latest;follow.latest.rssi=-50;
    follow.generation=17;poll();assert(!pinescan_ui_signal.valid);follow.generation=18;
    poll();render();assert(strstr(drawn,"LIVE -50 DBM / PEAK -40 / BEACONS 7")&&strstr(drawn,"UPDATED 3S AGO"));
    assert(!strstr(drawn,"MATCHES 7"));save(argv[1],"follow");
    assert(!memcmp(&painted,&pinescan_ui_store->selected,sizeof(painted)));
    follow.latest.transmitter[5]=99;poll();assert(!pinescan_ui_signal.valid);
    actual_back();assert(pinescan_ui_list_pending&&wifi_mode==UI_WIFI_MODE_PINESCAN&&pinescan_ui_detail);
    before=releases;poll();assert(releases==before&&!frees);
    remote.capture.busy=false;remote.capture.state=CERBERUS_S3_CAPTURE_SAVED;poll();assert(!pinescan_ui_detail&&!pinescan_ui_list_pending&&!frees);
    actual_tap(250,380);assert(pinescan_ui_save);actual_tap(100,380);assert(starts==2&&last_save);
    actual_back();assert(pinescan_ui_exit_pending&&wifi_mode==UI_WIFI_MODE_PINESCAN&&!frees);
    remote.capture.state=CERBERUS_S3_CAPTURE_ERROR;remote.capture.error_code=ESP_ERR_TIMEOUT;
    poll();assert(wifi_mode==UI_WIFI_MODE_PINESCAN&&!frees);
    remote.capture.busy=false;release_error=ESP_ERR_INVALID_STATE;poll();assert(wifi_mode==UI_WIFI_MODE_PINESCAN&&!frees);
    release_error=ESP_OK;poll();assert(wifi_mode==UI_WIFI_MODE_MENU&&!pinescan_ui_open&&frees==1);
    actual_menu(122,380);assert(pinescan_ui_save);seed();allocation_fail=true;poll();assert(!pinescan_ui_store&&!pinescan_ui_page.copied&&strstr(pinescan_ui_message,"NO_MEM"));
    allocation_fail=false;poll();assert(pinescan_ui_store&&allocations==2&&!pinescan_ui_message[0]);
    now+=500;actual_tick(now,true);assert(pinescan_ui_exit_pending&&remote.capture.state==CERBERUS_S3_CAPTURE_STOPPING);
    assert(frees==1);remote.capture.busy=false;now+=500;actual_tick(now,true);assert(frees==2&&!pinescan_ui_open&&wifi_mode==UI_WIFI_MODE_MENU);
    actual_menu(122,380);status_error=ESP_ERR_TIMEOUT;assert(pinescan_busy());actual_back();assert(pinescan_ui_open);
    status_error=ESP_OK;poll();assert(!pinescan_ui_open);
    actual_menu(122,380);seed();poll();actual_tap(20,150);actual_tap(150,380);assert(pinescan_ui_follow_pending);
    actual_tap(250,380);assert(!pinescan_ui_follow_pending);remote.capture.busy=false;poll();assert(follows==1);
    current_page=UI_PAGE_MAIN;poll();assert(!pinescan_ui_open&&allocations==frees);
    current_page=UI_PAGE_WIFI;remote.capture.busy=false;actual_menu(122,380);
    start_error=ESP_ERR_INVALID_STATE;actual_tap(100,380);assert(!remote.capture.busy&&strstr(pinescan_ui_message,"START:"));start_error=ESP_OK;
    poll();assert(strstr(pinescan_ui_message,"START:"));
    seed();remote.capture.busy=false;poll();actual_tap(20,150);painted=pinescan_ui_store->selected;
    follow_error=ESP_ERR_TIMEOUT;actual_tap(150,380);assert(strstr(pinescan_ui_message,"FOLLOW:")&&!remote.capture.busy);
    assert(!memcmp(&painted,&pinescan_ui_store->selected,sizeof(painted)));follow_error=ESP_OK;
    current_page=UI_PAGE_MAIN;poll();assert(!pinescan_ui_open&&allocations==frees);
    puts("PineScan UI passed: exact retained-match/latest separation, source/local clock distinction, binary SSID/privacy, real route/hit regions, explicit save/start, immutable selection, generation-bound Follow and asynchronous cleanup.");
    return 0;
}
