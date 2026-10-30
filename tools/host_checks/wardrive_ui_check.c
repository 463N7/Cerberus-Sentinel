#undef NDEBUG
#include "wardrive_service.h"
#include "ui_widgets.h"
#include "display.h"
#include "esp_check.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display_font.h"

static unsigned checks, calls, fail_at, status_reads, stop_requests, page_draws;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);abort(); } } while (0)
#define ESP_ERROR_CHECK(x) CHECK((x)==ESP_OK)
#define UI_HEADER_HEIGHT 48U
enum { UI_PAGE_BLE=1, UI_PAGE_IEEE802154=2 };
static unsigned current_page;
static const char *TAG="wardrive_host";
static uint16_t pixels[480][320];
static char drawn[8192], lines[480][128];
static cerberus_wardrive_status_t runtime;
static bool wardrive_start_pending,wardrive_return_pending,wardrive_gps_enabled,wardrive_include_expansions;
static esp_err_t wardrive_start_error;
static char wardrive_blocked_reason[64];
static uint32_t wardrive_start_requested_ms,wardrive_last_start_attempt_ms,wardrive_last_draw_ms;
static int64_t clock_us=1234000;
static esp_err_t draw_detail_page(void);
static int64_t esp_timer_get_time(void){return clock_us;}
static const char *esp_err_to_name(esp_err_t e)
{return e==ESP_ERR_TIMEOUT?"ESP_ERR_TIMEOUT":e==ESP_ERR_NO_MEM?"ESP_ERR_NO_MEM":"ESP_FAIL";}
void cerberus_wardrive_get_status(cerberus_wardrive_status_t *out){++status_reads;*out=runtime;}
void cerberus_wardrive_request_stop(void){++stop_requests;}

esp_err_t cerberus_display_fill_rect(uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t color)
{
    CHECK((unsigned)x+w<=320U&&(unsigned)y+h<=480U);
    if(++calls==fail_at)return ESP_FAIL;
    for(unsigned iy=y;iy<(unsigned)y+h;++iy)for(unsigned ix=x;ix<(unsigned)x+w;++ix)pixels[iy][ix]=color;
    return ESP_OK;
}
esp_err_t cerberus_display_draw_text(uint16_t x,uint16_t y,const char *text,uint16_t fg,uint16_t bg,uint8_t scale)
{
    CHECK(scale==1U||scale==2U);
    CHECK(x+strlen(text)*6U*scale<=320U&&(unsigned)y+7U*scale<=480U);
    if(++calls==fail_at)return ESP_FAIL;
    CHECK(strlen(drawn)+strlen(text)+2U<sizeof(drawn));
    strcat(drawn,text);strcat(drawn,"\n");
    snprintf(lines[y],sizeof(lines[y]),"%s",text);
    for(size_t i=0;text[i];++i){const uint8_t *font=font5x7((unsigned char)text[i]);
        for(unsigned cy=0;cy<7U;++cy)for(unsigned cx=0;cx<6U;++cx)
            for(unsigned sy=0;sy<scale;++sy)for(unsigned sx=0;sx<scale;++sx)
                pixels[y+cy*scale+sy][x+(i*6U+cx)*scale+sx]=cx<5U&&(font[cy]&(1U<<(4U-cx)))?fg:bg;
    }
    return ESP_OK;
}
#include "wardrive_ui_impl.h"

static esp_err_t draw_detail_page(void)
{
    ++page_draws;drawn[0]='\0';memset(lines,0,sizeof(lines));
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND),TAG,"clear");
    ESP_RETURN_ON_ERROR(draw_header("WARDRIVING"),TAG,"header");
    return draw_wardrive();
}
static void reset(void)
{
    memset(&runtime,0,sizeof(runtime));wardrive_start_pending=wardrive_return_pending=false;
    wardrive_gps_enabled=wardrive_include_expansions=false;wardrive_start_error=ESP_OK;
    wardrive_blocked_reason[0]='\0';calls=fail_at=status_reads=stop_requests=page_draws=0;
    wardrive_last_draw_ms=999;clock_us=1234000;
}
static void draw(void)
{
    unsigned before=status_reads,stops=stop_requests;
    cerberus_wardrive_status_t frozen=runtime;
    CHECK(draw_detail_page()==ESP_OK);
    CHECK(status_reads==before+1U&&stop_requests==stops&&!memcmp(&frozen,&runtime,sizeof(runtime)));
}
static void save(const char *prefix,const char *name)
{
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name);
    FILE *f=fopen(path,"wb");CHECK(f!=NULL);CHECK(fprintf(f,"P6\n320 480\n255\n")>0);
    for(unsigned y=0;y<480;++y)for(unsigned x=0;x<320;++x){
        uint16_t p=pixels[y][x];uint8_t rgb[]={(uint8_t)(((p>>11)&31)*255/31),
            (uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};
        if(fwrite(rgb,1,3,f)!=3){fclose(f);abort();}
    }
    CHECK(fclose(f)==0);
}
static void check_views(const char *prefix)
{
    reset();draw();CHECK(!strcmp(lines[80],"GPS OFF / UNLOC CSV"));
    CHECK(!strcmp(lines[178],"PROBE WAIT / BLE HINTS 0")&&strstr(drawn,"START\n"));save(prefix,"idle-no-gps");
    runtime=(cerberus_wardrive_status_t){.state=CERBERUS_WARDRIVE_RUNNING,.c5_wifi_sweeps=12,.c5_wifi_observations=96,
        .s3_ble_sweeps=9,.s3_ble_observations=173,.s3_ble_flock_observations=23,
        .s3_ble_flock_clue=3,.s3_ble_flock_signature=15,.s3_ble_flock_corroborated=5,
        .c5_probe_state=CERBERUS_WARDRIVE_PROBE_AVAILABLE,.c5_probe_observations=4,
        .c5_wifi_logged=31,.s3_ble_logged=48,.sd_rows=79,.sd_bytes=32768,.unlocated_rows=79,
        .last_file_name="UNLOC0001.CSV"};
    draw();CHECK(!strcmp(lines[132],"C5 WI-FI: 12 SWEEPS / 96 AP"));
    CHECK(!strcmp(lines[156],"S3 BLE: 9 SWEEPS / 173 OBS"));
    CHECK(!strcmp(lines[178],"HINT OBS: PROBE 4 / BLE 23")&&strstr(drawn,"STOP\n"));
    CHECK(!strcmp(lines[300],"GEO 0 / UNLOC 79 ROWS"));save(prefix,"running-no-gps");
    runtime.s3_ble_flock_observations=runtime.c5_probe_observations=UINT32_MAX;draw();
    CHECK(!strcmp(lines[178],"HINT OBS: PROBE 4294967295 / BLE 4294967295"));save(prefix,"max-flock-count");
    runtime.c5_probe_state=CERBERUS_WARDRIVE_PROBE_UNAVAILABLE;draw();
    CHECK(!strcmp(lines[178],"PROBE UNAVAILABLE / BLE HINTS 4294967295"));save(prefix,"probe-unavailable");
    runtime.c5_probe_state=CERBERUS_WARDRIVE_PROBE_UNSUPPORTED;draw();
    CHECK(!strcmp(lines[178],"PROBE UNSUPPORTED / BLE HINTS 4294967295"));
    runtime.state=CERBERUS_WARDRIVE_STOPPING;wardrive_return_pending=true;draw();
    CHECK(!strcmp(lines[105],"STATE: STOPPING / SAVING"));
    CHECK(!strcmp(lines[324],"CLOSING SD FILE BEFORE EXIT")&&strstr(drawn,"STOP\n"));save(prefix,"stopping");
    runtime.state=CERBERUS_WARDRIVE_STOPPED;wardrive_return_pending=false;draw();
    CHECK(!strcmp(lines[105],"STATE: STOPPED")&&strstr(drawn,"START\n"));
    runtime.state=CERBERUS_WARDRIVE_ERROR;runtime.error_code=ESP_ERR_TIMEOUT;runtime.s3_ble_error=ESP_ERR_TIMEOUT;
    draw();CHECK(!strcmp(lines[324],"S3 BLE: ESP_ERR_TIMEOUT"));save(prefix,"error");
    runtime=(cerberus_wardrive_status_t){.state=CERBERUS_WARDRIVE_RUNNING,.gps_requested=true,
        .gps={.state=CERBERUS_WARDRIVE_GPS_FRESH,.utc_valid=true},.expansion_present_mask=0x3f};
    wardrive_gps_enabled=wardrive_include_expansions=true;draw();
    CHECK(!strcmp(lines[80],"GPS FIX + UTC / WIGLE CSV"));
    CHECK(!strcmp(lines[240],"EXPANSION R1-R6: 6 ONLINE"));save(prefix,"gps-format-preview");
    runtime.gps.utc_valid=false;draw();CHECK(!strcmp(lines[80],"GPS FIX / UTC UNVERIFIED"));
    runtime.gps.state=CERBERUS_WARDRIVE_GPS_STALE;draw();CHECK(!strcmp(lines[80],"GPS FIX STALE / UNLOC CSV"));
    runtime.gps_error=ESP_FAIL;draw();CHECK(!strcmp(lines[80],"GPS UART ERROR / UNLOC CSV"));
}
static void check_routes(void)
{
    reset();wardrive_handle_tap(24,346);CHECK(wardrive_include_expansions&&!wardrive_gps_enabled);
    wardrive_handle_tap(164,346);CHECK(wardrive_gps_enabled);
    unsigned old=status_reads;wardrive_handle_tap(23,390);wardrive_handle_tap(296,390);CHECK(status_reads==old);
    unsigned draws=page_draws;wardrive_handle_tap(200,178);CHECK(page_draws==draws&&!stop_requests);
    wardrive_start_error=ESP_FAIL;strcpy(wardrive_blocked_reason,"old failure");
    wardrive_handle_tap(160,380);CHECK(wardrive_start_pending&&!stop_requests&&!wardrive_start_error&&!wardrive_blocked_reason[0]);
    CHECK(wardrive_start_requested_ms==1234&&wardrive_last_start_attempt_ms==984&&!wardrive_last_draw_ms);
    wardrive_handle_tap(24,346);CHECK(wardrive_include_expansions); /* No configuration changes while pending. */
    wardrive_handle_tap(160,411);CHECK(!wardrive_start_pending&&!stop_requests);
    for(unsigned state=CERBERUS_WARDRIVE_STARTING;state<=CERBERUS_WARDRIVE_STOPPING;++state){
        runtime.state=(cerberus_wardrive_state_t)state;unsigned stops=stop_requests;
        wardrive_handle_tap(160,380);CHECK(stop_requests==stops+1&&!wardrive_start_pending);
        wardrive_handle_tap(164,346);CHECK(wardrive_gps_enabled);
    }
    /* A failed paint is returned, not reported as successful. */
    reset();fail_at=1;CHECK(draw_wardrive()==ESP_FAIL);
}
int main(int argc,char **argv)
{
    CHECK(argc==2);check_views(argv[1]);check_routes();
    printf("Wardriving production UI: %u checks; one status snapshot per draw, Flock hint counts, GPS/no-GPS labels, Stop/config routes and bounded native rendering pass.\n",checks);
}
