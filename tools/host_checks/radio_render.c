#include "radio_framebuffer.h"
#include "cerberus_protocol.h"
#include "ui_widgets.h"
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT 0x107
#define UI_BACKGROUND 0x0042
#define UI_PANEL 0x08A4
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define UI_GOOD 0x471B
#define TAG "radio-ui-test"
static const cerberus_ui_widget_theme_t theme={UI_BACKGROUND,UI_PANEL,UI_MUTED,UI_TEXT,UI_MUTED};
#define UI_WIDGET_THEME (&theme)
static bool c5_online=true;
static struct {uint32_t capabilities;uint8_t radio_state;} c5_status={CERBERUS_C5_CAP_RADIO_TEST,0};
static cerberus_radio_test_status_t remote;
static unsigned starts,stops,polls,saves,leases;
static int link_error,start_error,save_error,profile_error=ESP_ERR_NOT_FOUND;
static bool start_delivered=true;
static const char *profile="CERBERUS-LAB\n";
static char saved_csv[512];
static const char *esp_err_to_name(int error){return error?"ESP_ERR_INVALID_STATE":"ESP_OK";}
static esp_err_t draw_centered(uint16_t y,const char *s,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320-strlen(s)*6*scale)/2),y,s,color,UI_BACKGROUND,scale);}
static esp_err_t draw_small_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *s)
{return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){x,y,w,h},s,1,true,&theme);}
static esp_err_t cerberus_storage_read_file_chunk(const char *path,const char *name,uint32_t offset,uint8_t *data,size_t cap,size_t *got,uint64_t *size)
{assert(!strcmp(path,"/CERBERUS/TESTS")&&(!strcmp(name,"WIFI.TXT")||!strcmp(name,"BLE.TXT"))&&!offset);*size=strlen(profile);*got=*size<cap?*size:cap;memcpy(data,profile,*got);return profile_error;}
static esp_err_t c5_spi_link_test_status(uint16_t id,cerberus_radio_test_status_t *out)
{++polls;if(id)++leases;if(link_error)return link_error;*out=remote;return ESP_OK;}
static esp_err_t c5_spi_link_test_start(const cerberus_radio_test_config_t *c,cerberus_radio_test_status_t *out)
{++starts;remote=(cerberus_radio_test_status_t){.job_id=c->job_id,.mode=c->mode,.channel=c->channel,.seconds=c->seconds,.state=start_delivered?CERBERUS_TEST_RUNNING:CERBERUS_TEST_STAGED,.counts_valid=1};if(start_error){remote.error_code=start_error;remote.state=CERBERUS_TEST_STAGED;}if(!link_error)*out=remote;return link_error?link_error:start_error;}
static esp_err_t c5_spi_link_test_stop(uint16_t id,cerberus_radio_test_status_t *out)
{++stops;assert(id==remote.job_id);if(link_error)return link_error;remote.state=CERBERUS_TEST_CANCELLED;*out=remote;return ESP_OK;}
static esp_err_t cerberus_ble_test_start(const cerberus_radio_test_config_t *c)
{cerberus_radio_test_status_t out;return c5_spi_link_test_start(c,&out);}
static void cerberus_ble_test_status(uint16_t id,cerberus_radio_test_status_t *out)
{++polls;if(id)++leases;*out=remote;}
static void cerberus_ble_test_stop(uint16_t id){assert(id==remote.job_id);++stops;remote.state=CERBERUS_TEST_CANCELLED;}
static esp_err_t cerberus_storage_write_diagnostic_report(const char *data,size_t length,char *name,size_t cap)
{assert(length<sizeof(saved_csv));if(save_error)return save_error;memcpy(saved_csv,data,length);saved_csv[length]=0;snprintf(name,cap,"DIAG0001.CSV");++saves;return ESP_OK;}
#include "radio_lab_ui.h"
static void save_image(const char *prefix,const char *suffix)
{
    /* The real draw_detail_page clears before drawing this private body. */
    assert(cerberus_display_fill_rect(0,0,320,480,UI_BACKGROUND)==ESP_OK);
    assert(lab_draw()==ESP_OK);
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,suffix);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv)
{
    assert(argc==2);assert(lab_load_name()==ESP_OK&&!strcmp(lab_name,"CERBERUS-LAB"));
    assert(lab_draw()==ESP_OK);save_image(argv[1],"ready");
    lab_tap(160,390);assert(!starts); /* Button gap. */
    c5_online=false;lab_start();assert(lab_error==ESP_ERR_NOT_SUPPORTED&&!starts);c5_online=true;
    c5_status.radio_state=CERBERUS_C5_RADIO_WIFI_CONNECTED;lab_start();assert(!starts&&strstr(lab_message,"DISCONNECT"));c5_status.radio_state=0;
    profile_error=ESP_OK;profile="Bad\nName";lab_start();assert(lab_error==ESP_ERR_INVALID_ARG&&!starts);
    profile="01234567890123456789012345678901";lab_start();assert(starts==1&&lab_busy());
    lab_tap(20,120);assert(lab_mode==CERBERUS_TEST_WIFI_BEACON); /* No mode changes while running. */
    remote.elapsed_ms=1250;remote.accepted_frames=13;lab_tick(1000,true);assert(lab_draw()==ESP_OK);save_image(argv[1],"running");
    unsigned before=leases;link_error=ESP_ERR_TIMEOUT;lab_tick(1500,false);assert(lab_busy()&&leases==before&&stops==1);
    link_error=0;lab_tick(2000,false);assert(!lab_busy()&&leases==before&&stops==2);
    lab_save();assert(saves==1&&strstr(saved_csv,",C5,1,6,10,5,"));
    save_error=ESP_FAIL;lab_save();lab_tick(2500,true);assert(lab_save_error==ESP_FAIL);assert(lab_draw()==ESP_OK);
    save_error=0;lab_save();assert(lab_save_error==ESP_OK);
    start_error=ESP_ERR_NO_MEM;lab_start();assert(!lab_busy()&&lab_status.state==CERBERUS_TEST_ERROR);lab_tick(3000,true);assert(lab_error==ESP_ERR_NO_MEM);
    start_error=0;remote.state=CERBERUS_TEST_IDLE;
    /* A dropped START response can mean the worker is running or only staged. */
    lab_start();lab_pending=true;memset(&lab_status,0,sizeof(lab_status));lab_error=ESP_ERR_TIMEOUT;
    lab_tick(3500,true);assert(lab_busy()&&!lab_pending&&lab_status.state==CERBERUS_TEST_RUNNING);
    lab_stop();start_delivered=false;lab_start();lab_pending=true;memset(&lab_status,0,sizeof(lab_status));lab_error=ESP_ERR_TIMEOUT;
    lab_tick(4000,true);assert(!lab_busy()&&lab_status.state==CERBERUS_TEST_ERROR&&lab_status.error_code==ESP_ERR_TIMEOUT);
    start_delivered=true;profile_error=ESP_ERR_NOT_FOUND;lab_tap(20,120);assert(lab_mode==CERBERUS_TEST_BLE_ADVERT&&!lab_config.job_id&&!strcmp(lab_name,"Cerberus Lab"));
    assert(lab_draw()==ESP_OK);save_image(argv[1],"ble");
    lab_start();assert(lab_busy()&&lab_config.channel==0);remote.state=CERBERUS_TEST_COMPLETE;lab_tick(4500,true);assert(!lab_busy());
    lab_save();assert(strstr(saved_csv,",S3,2,0,10,4,"));
    for(unsigned s=0;s<=CERBERUS_TEST_ERROR;++s){lab_status.state=(uint8_t)s;lab_status.elapsed_ms=UINT32_MAX;lab_status.accepted_frames=UINT32_MAX;assert(lab_draw()==ESP_OK);}
    puts("Native radio UI checks passed: bounds, profiles, busy controls, lost acknowledgements, hidden-screen stops, saved CSV and persistent errors.");
}
