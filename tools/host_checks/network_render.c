#include "network_framebuffer.h"
#include "cerberus_protocol.h"
#include "keyboard.h"
#include "ui_widgets.h"
#define UI_BACKGROUND 0x0042
#define UI_PANEL 0x08A4
#define UI_MUTED 0x7CB5
#define UI_TEXT 0xE77F
#define UI_GOOD 0x471B
#define TAG "network-test"
static const cerberus_ui_widget_theme_t theme={UI_BACKGROUND,UI_PANEL,UI_MUTED,UI_TEXT,UI_MUTED};
#define UI_WIDGET_THEME (&theme)
static cerberus_net_scan_status_t remote;
static struct {uint32_t capabilities;} c5_status={.capabilities=CERBERUS_C5_CAPABILITIES};
static unsigned starts,stops,polls,saves;
static int link_error;
static int save_error;
static uint8_t connection_index=2;
static uint8_t remote_mode;
static char saved_csv[8192];
static const char *esp_err_to_name(int error){return error?"ESP_ERR_INVALID_STATE":"ESP_OK";}
static esp_err_t draw_centered(uint16_t y,const char *s,uint16_t color,uint8_t scale)
{return cerberus_display_draw_text((uint16_t)((320-strlen(s)*6*scale)/2),y,s,color,UI_BACKGROUND,scale);}
static esp_err_t draw_small_button(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const char *s)
{return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){x,y,w,h},s,1,true,&theme);}
static esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *s)
{*s=(cerberus_wifi_connection_status_payload_t){.state=CERBERUS_WIFI_CONNECTION_CONNECTED,.generation=7,.index=connection_index,.ipv4={192,168,1,42},.gateway={192,168,1,1},.netmask={255,255,255,0}};return link_error;}
static esp_err_t c5_spi_link_network_status(cerberus_net_scan_status_t *s){++polls;*s=remote;return link_error;}
static esp_err_t c5_spi_link_network_start(const cerberus_net_scan_request_t *r,cerberus_net_scan_status_t *s)
{++starts;remote_mode=r->mode;assert(r->connection_generation==7&&r->connection_index==2);remote=(cerberus_net_scan_status_t){.job_id=r->job_id,.state=CERBERUS_NET_SCAN_RUNNING,.total=16};*s=remote;return ESP_OK;}
static esp_err_t c5_spi_link_network_stop(uint16_t job,cerberus_net_scan_status_t *s)
{++stops;assert(job==remote.job_id);if(link_error)return link_error;remote.state=CERBERUS_NET_SCAN_CANCELLED;*s=remote;return ESP_OK;}
static esp_err_t c5_spi_link_network_result(uint16_t job,uint8_t index,cerberus_net_scan_result_t *r)
{*r=(cerberus_net_scan_result_t){.job_id=job,.index=index,.port=(uint16_t)(index?65535:22),.ipv4={192,168,1,1},.flags=index?0:CERBERUS_NET_SCAN_SSH_BANNER,.connect_ms=12};
 if(remote_mode==CERBERUS_NET_SCAN_PING_LAN){r->port=0;r->flags=CERBERUS_NET_SCAN_ICMP_REPLY;}
 else if(remote_mode==CERBERUS_NET_SCAN_ARP_LAN){r->port=0;r->flags=CERBERUS_NET_SCAN_ARP_RESOLVED|(index?0:CERBERUS_NET_SCAN_ARP_CACHED);memcpy(r->banner,"\x02\x12\x34\xAB\xCD\xEF",6);}
 else memcpy(r->banner,"SSH-2.0-X",9);return ESP_OK;}
static esp_err_t cerberus_storage_write_wifi_report(const char *data,size_t length,char *name,size_t cap)
{assert(length<4096);if(save_error)return save_error;memcpy(saved_csv,data,length);saved_csv[length]=0;snprintf(name,cap,"WIFI0001.CSV");++saves;return ESP_OK;}
esp_err_t cerberus_keyboard_begin(cerberus_keyboard_t *k,char *buffer,size_t capacity,const char *initial)
{snprintf(buffer,capacity,"%s",initial);*k=(cerberus_keyboard_t){.buffer=buffer,.capacity=capacity};return ESP_OK;}
esp_err_t cerberus_keyboard_draw(const cerberus_keyboard_t *k,const char *prompt){(void)k;(void)prompt;return ESP_OK;}
esp_err_t cerberus_keyboard_handle_tap(cerberus_keyboard_t *k,uint16_t x,uint16_t y,cerberus_keyboard_action_t *a)
{(void)k;(void)x;(void)y;*a=CERBERUS_KEYBOARD_ACTION_SUBMIT;return ESP_OK;}
#include "network_scan_ui.h"
static void save_image(const char *path)
{
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(unsigned i=0;i<320*480;++i){uint16_t p=screen[i];uint8_t rgb[]={(uint8_t)((p>>11)*255/31),(uint8_t)(((p>>5)&63)*255/63),(uint8_t)((p&31)*255/31)};assert(fwrite(rgb,1,3,f)==3);}assert(!fclose(f));
}
int main(int argc,char **argv)
{
    assert(argc==2);net_enter();assert(!strcmp(net_target,"192.168.1.1"));
    net_tap(100,390);assert(starts==0); /* Button gap. */
    strcpy(net_target,"192.168.1.255");net_start();assert(starts==0);
    strcpy(net_target,"192.168.1.1");net_tap(30,390);assert(starts==1&&net_busy());
    net_tap(210,120);assert(net_mode==0); /* Cannot change job while running. */
    assert(net_draw()==ESP_OK);
    remote.state=CERBERUS_NET_SCAN_COMPLETE;remote.total=remote.tested=16;
    remote.open=4;remote.closed=12;remote.result_count=4;
    net_poll(1000,true);assert(net_loaded==4&&!net_busy());
    assert(net_draw()==ESP_OK);save_image(argv[1]);
    net_save();assert(saves==1&&strstr(saved_csv,"192.168.1.1,22,12,1,5353482D322E302D58"));
    save_error=ESP_FAIL;net_save();assert(net_save_error==ESP_FAIL);
    net_poll(1500,true);assert(net_save_error==ESP_FAIL&&net_error==ESP_OK);
    assert(net_draw()==ESP_OK);save_error=0;net_save();assert(net_save_error==ESP_OK&&saves==2);
    net_tap(210,120);assert(net_mode==1);net_tap(20,170);assert(net_editing==2);
    net_tap(20,170);assert(!net_editing);strcpy(net_ports,"0-10");net_start();assert(starts==1);
    strcpy(net_ports,"22");net_start();assert(starts==2);
    unsigned before=polls;link_error=ESP_FAIL;net_poll(2000,false);
    assert(stops==1&&polls==before&&net_busy()); /* Hidden UI must not renew lease. */
    link_error=0;net_poll(3000,false);assert(!net_busy());
    for(unsigned mode=0;mode<CERBERUS_NET_SCAN_MODE_COUNT;++mode){net_mode=(uint8_t)mode;assert(net_draw()==ESP_OK);}
    net_status.open=65535;net_status.total=net_status.tested=65535;assert(net_draw()==ESP_OK);
    net_error=ESP_ERR_INVALID_STATE;assert(net_draw()==ESP_OK);
    strcpy(net_target,"192.168.1.77");net_enter();assert(!strcmp(net_target,"192.168.1.77"));
    connection_index=3;net_enter();assert(!net_request.job_id&&!net_loaded&&!net_status.result_count&&!strcmp(net_target,"192.168.1.1"));
    connection_index=2;net_error=ESP_OK;
    strcpy(net_target,"invalid");strcpy(net_ports,"bad range");
    for(unsigned mode=CERBERUS_NET_SCAN_SMTP_LAN;mode<CERBERUS_NET_SCAN_MODE_COUNT;++mode){
        net_mode=(uint8_t)mode;unsigned old_starts=starts;
        net_tap(20,120);assert(!net_editing); /* LAN modes have no individual host entry. */
        net_start();assert(starts==old_starts+1&&net_request.mode==mode);
        assert(net_request.first_port==cerberus_net_scan_service_port((uint8_t)mode));
        assert(net_request.last_port==net_request.first_port);
        remote.state=CERBERUS_NET_SCAN_COMPLETE;remote.open=remote.result_count=4;
        net_poll(4000+mode*1000,true);assert(!net_busy());
        assert(net_draw()==ESP_OK);
        net_save();assert(strstr(saved_csv,"scope=connected_subnet; target=192.168.1.0; netmask=255.255.255.0"));
        if(mode==CERBERUS_NET_SCAN_PING_LAN){
            assert(strstr(saved_csv,"ipv4,echo_reply,rtt_ms\n192.168.1.1,1,12\n"));
            assert(!strstr(saved_csv,"tcp_port"));
        }
        if(mode==CERBERUS_NET_SCAN_ARP_LAN){
            assert(strstr(saved_csv,"ipv4,mac,resolution_ms,was_cached\n192.168.1.1,02:12:34:AB:CD:EF,12,1\n"));
            assert(!strstr(saved_csv,"tcp_port"));
        }
    }
    c5_status.capabilities&=~CERBERUS_C5_CAP_HOST_DISCOVERY;
    unsigned before_host=starts;net_mode=CERBERUS_NET_SCAN_PING_LAN;net_start();assert(starts==before_host);
    net_enter();assert(net_mode==CERBERUS_NET_SCAN_COMMON);
    net_mode=CERBERUS_NET_SCAN_RDP_LAN;net_tap(210,120);assert(net_mode==CERBERUS_NET_SCAN_COMMON);
    c5_status.capabilities&=~CERBERUS_C5_CAP_SERVICE_SCANS;
    unsigned old_starts=starts;net_mode=CERBERUS_NET_SCAN_HTTP_LAN;net_start();
    assert(starts==old_starts&&strstr(net_message,"DOES NOT SUPPORT"));
    net_enter();assert(net_mode==CERBERUS_NET_SCAN_COMMON);
    net_mode=CERBERUS_NET_SCAN_TELNET_LAN;net_tap(210,120);assert(net_mode==CERBERUS_NET_SCAN_COMMON);
    puts("Native network UI checks passed: layout, touch gaps, validation, CSV, progress and hidden-screen cancellation.");
}
