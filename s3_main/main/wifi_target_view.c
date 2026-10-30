#include "wifi_target_view.h"
#include "display.h"
#include <stdio.h>
#include <string.h>

#define TRY(call) do { esp_err_t err_=(call); if(err_!=ESP_OK)return err_; } while(0)
static esp_err_t line(uint16_t y,const char *text,uint16_t color,
    const cerberus_ui_widget_theme_t *theme)
{
    char bounded[49];
    snprintf(bounded,sizeof(bounded),"%.48s",text?text:"");
    for(char *p=bounded;*p;++p)if((unsigned char)*p<32||(unsigned char)*p>126)*p='?';
    return cerberus_display_draw_text((uint16_t)((320-strlen(bounded)*6)/2),y,
        bounded,color,theme->background,1);
}
esp_err_t cerberus_wifi_target_draw(const cerberus_wifi_target_view_t *v,
    const cerberus_ui_widget_theme_t *theme)
{
    if(!v||!theme)return ESP_ERR_INVALID_ARG;
    TRY(line(56,v->selected?v->ssid:"NO NETWORK SELECTED",theme->text,theme));
    TRY(line(78,v->selected?v->identifier:"CHOOSE A NETWORK FROM THE SCAN",theme->muted,theme));
    TRY(line(100,v->selected?v->metadata:"",theme->text,theme));
    const char *labels[]={"ATTACK THIS AP","CAPTURE THIS AP",
        v->connection_active?"CONNECTION":"CONNECT","NETWORK / SERVICE SCANS",
        v->station_label ? v->station_label : "STATIONS / CLIENTS","DETAILS / TRUST / VENDOR",
        "PACKET MONITOR","CHANGE AP / SCAN"};
    const bool enabled[]={v->selected,v->selected&&v->can_capture,
        v->selected&&v->can_connect,v->selected&&v->can_scan_ports,
        v->selected&&v->can_scan_stations,v->selected,
        v->selected&&v->can_packet_monitor,true};
    for(unsigned i=0;i<8;++i) {
        if(i==6U) {
            TRY(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,330,144,30},
                labels[i],1,enabled[i],theme));
            TRY(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){164,330,144,30},
                "CSI MOTION",1,v->selected&&v->can_csi,theme));
        } else TRY(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,126+i*34,296,30},
            labels[i],1,enabled[i],theme));
    }
    return line(404,v->message,theme->muted,theme);
}
cerberus_wifi_target_action_t cerberus_wifi_target_tap(
    const cerberus_wifi_target_view_t *v,uint16_t x,uint16_t y)
{
    if(!v||x<12||x>=308||y<126||y>=398)return CERBERUS_WIFI_TARGET_NONE;
    unsigned row=(y-126)/34;
    if((y-126)%34>=30)return CERBERUS_WIFI_TARGET_NONE;
    if(row==7)return CERBERUS_WIFI_TARGET_CHANGE;
    if(!v->selected)return CERBERUS_WIFI_TARGET_NONE;
    if(row==0)return CERBERUS_WIFI_TARGET_ATTACK;
    if(row==1&&v->can_capture)return CERBERUS_WIFI_TARGET_CAPTURE;
    if(row==2&&v->can_connect)return CERBERUS_WIFI_TARGET_CONNECT;
    if(row==3&&v->can_scan_ports)return CERBERUS_WIFI_TARGET_PORTS;
    if(row==4&&v->can_scan_stations)return CERBERUS_WIFI_TARGET_STATIONS;
    if(row==5)return CERBERUS_WIFI_TARGET_DETAILS;
    if(row==6&&x<156&&v->can_packet_monitor)return CERBERUS_WIFI_TARGET_PACKET_MONITOR;
    if(row==6&&x>=164&&v->can_csi)return CERBERUS_WIFI_TARGET_CSI;
    return CERBERUS_WIFI_TARGET_NONE;
}
