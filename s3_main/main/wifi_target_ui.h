/* The selected scan record is a copy, never a pointer into a sorted live row. */
static char wifi_target_message[49];
static bool wifi_attack_selecting;

static bool wifi_target_toggle_selection(const cerberus_c5_wifi_result_t *ap,uint8_t index)
{
    if(!wifi_targets_toggle(ap,index))return false;
    wifi_selection_valid=wifi_target_count!=0;
    if(wifi_selection_valid){
        int selected=wifi_targets_find(ap);
        const wifi_target_entry_t *entry=&wifi_targets[selected<0?wifi_target_count-1:(unsigned)selected];
        wifi_selected_result=entry->ap;wifi_selected_index=entry->index;
    }
    return true;
}

static void wifi_target_sync_menu(void)
{
    if(!wifi_target_count&&wifi_selection_valid)(void)wifi_targets_add(&wifi_selected_result,wifi_selected_index);
    if(wifi_target_count&&(!wifi_selection_valid||wifi_targets_find(&wifi_selected_result)<0)){
        wifi_selected_result=wifi_targets[0].ap;wifi_selected_index=wifi_targets[0].index;wifi_selection_valid=true;
    }
    field_menu.wifi_target_valid=wifi_target_count!=0;
    field_menu.wifi_deauth_available=c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_AP_DEAUTH);
    field_menu.wifi_probe_available=c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_AP_PROBE);
    field_menu.wifi_bad_message_available=c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_AP_BAD_MESSAGE);
    field_menu.wifi_assoc_sleep_available=c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_AP_ASSOC_SLEEP);
    field_menu.wifi_channel_switch_available=c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_AP_CHANNEL_SWITCH);
    field_menu.wifi_quiet_available=c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_AP_QUIET);
    field_menu.wifi_ap_clone_available=c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_AP_CLONE_SPAM);
    field_menu.wifi_sae_available=c5_online&&c5_status.firmware_version>=44&&
        (c5_status.capabilities&CERBERUS_C5_CAP_AP_SAE_COMMIT);
    field_menu.wifi_portal_available=c5_online&&wifi_selected_result.ssid[0]&&
        (c5_status.capabilities&(CERBERUS_C5_CAP_WIFI_2_4_GHZ|CERBERUS_C5_CAP_WIFI_5_GHZ));
    field_menu.wifi_target[0]=field_menu.wifi_target_detail[0]=0;
    if(!wifi_selection_valid)return;
    snprintf(field_menu.wifi_target,sizeof(field_menu.wifi_target),"%s",
        wifi_selected_result.ssid[0]?wifi_selected_result.ssid:"<HIDDEN SSID>");
    for(char *p=field_menu.wifi_target;*p;++p)if((unsigned char)*p<32||(unsigned char)*p>126)*p='?';
    char identifier[24];
    cerberus_privacy_format_wifi_identifier(wifi_selected_result.bssid,
        privacy_addresses_masked(),identifier,sizeof(identifier));
    snprintf(field_menu.wifi_target_detail,sizeof(field_menu.wifi_target_detail),
        "%s / CH %u / %s",identifier,wifi_selected_result.channel,
        wifi_selected_result.channel>14?"5 GHZ":"2.4 GHZ");
    if(wifi_target_count>1){
        snprintf(field_menu.wifi_target,sizeof(field_menu.wifi_target),"%u NETWORKS SELECTED",wifi_target_count);
        snprintf(field_menu.wifi_target_detail,sizeof(field_menu.wifi_target_detail),"FOCUS: %.32s",wifi_selected_result.ssid[0]?wifi_selected_result.ssid:"<HIDDEN SSID>");
    }
}
static void wifi_target_open_attacks(void)
{
    if(!wifi_selection_valid)return;
    wifi_attack_selecting=false;
    wifi_target_sync_menu();
    field_menu.attack=true;field_menu.bluetooth=false;field_menu.page=0;
    current_page=UI_PAGE_FIELD_MENU;
}
static void wifi_target_finish_selection(void)
{
    wifi_target_message[0]=0;
    wifi_mode=UI_WIFI_MODE_TARGET;
    if(wifi_attack_selecting)wifi_mode=UI_WIFI_MODE_LIST;
}
static bool wifi_target_connection_matches(void)
{
    return wifi_selection_valid && wifi_connection_target_valid &&
        memcmp(wifi_connection_target.bssid,wifi_selected_result.bssid,6)==0 &&
        wifi_connection_target.security==wifi_selected_result.security &&
        strcmp(wifi_connection_target.ssid,wifi_selected_result.ssid)==0;
}
static cerberus_wifi_target_view_t wifi_target_view(char identifier[24],char metadata[64])
{
    cerberus_csi_target_t csi_target={.channel=wifi_selected_result.channel};
    memcpy(csi_target.bssid,wifi_selected_result.bssid,sizeof(csi_target.bssid));
    cerberus_privacy_format_wifi_identifier(wifi_selected_result.bssid,
        privacy_addresses_masked(),identifier,24);
    snprintf(metadata,64,"CH %u / %s / %d DBM / %s",wifi_selected_result.channel,
        wifi_selected_result.channel>14?"5 GHZ":"2.4 GHZ",wifi_selected_result.rssi,
        wifi_security_text(wifi_selected_result.security));
    bool connected=wifi_connection_error==ESP_OK&&wifi_connection_status.state==CERBERUS_WIFI_CONNECTION_CONNECTED;
    bool active=connected||wifi_connection_status.state==CERBERUS_WIFI_CONNECTION_CONNECTING;
    bool same=wifi_target_connection_matches();
    const char *message=wifi_target_message;
    if(!message[0])message=active&&!same?"CONNECTED ELSEWHERE / PORT SCAN DISABLED":
        connected&&same?"C5 CONNECTED TO THIS NETWORK":
        wifi_scan_status.state==CERBERUS_WIFI_SCAN_RUNNING?"FINISHING SCAN / YOUR TARGET IS HELD":
        "CONNECT TO THIS NETWORK FOR PORT SCANS";
    return (cerberus_wifi_target_view_t){
        .ssid=wifi_selected_result.ssid[0]?wifi_selected_result.ssid:"<HIDDEN SSID>",
        .identifier=identifier,.metadata=metadata,.message=message,.selected=wifi_selection_valid,
        .connection_matches=same,.connection_active=active,
        .can_connect=c5_online&&(active||(wifi_scan_status.state==CERBERUS_WIFI_SCAN_COMPLETE&&wifi_selected_result.ssid[0])),
        .can_scan_ports=c5_online&&connected&&same&&(c5_status.capabilities&CERBERUS_C5_CAP_NETWORK_SCAN),
        .station_label=wifi_relation_supported()?"OBSERVED CLIENTS":"STATIONS / CLIENTS",
        .can_scan_stations=wifi_relation_supported()||(c5_online&&(c5_status.capabilities&CERBERUS_C5_CAP_WIFI_STATIONS)),
        .can_capture=c5_online&&c5_status.firmware_version>=CERBERUS_WIFI_CAPTURE_MIN_FIRMWARE,
        .can_packet_monitor=c5_online&&
            c5_status.firmware_version>=CERBERUS_WIFI_PACKET_MONITOR_MIN_FIRMWARE&&
            cerberus_wifi_packet_monitor_channel_valid(wifi_selected_result.channel),
        .can_csi=c5_online&&c5_status.firmware_version>=CERBERUS_CSI_MIN_FIRMWARE&&
            cerberus_csi_target_valid(&csi_target),
    };
}
static esp_err_t draw_wifi_target(void)
{
    char identifier[24],metadata[64];
    cerberus_wifi_target_view_t view=wifi_target_view(identifier,metadata);
    return cerberus_wifi_target_draw(&view,UI_WIDGET_THEME);
}
static void wifi_target_connect(void)
{
    wifi_target_message[0]=0;
    wifi_connection_error=c5_spi_link_get_wifi_connection_status(&wifi_connection_status);
    if(wifi_connection_error!=ESP_OK){
        snprintf(wifi_target_message,sizeof(wifi_target_message),"CONNECTION STATUS FAILED");return;
    }
    if(wifi_connection_status.state==CERBERUS_WIFI_CONNECTION_CONNECTED||
        wifi_connection_status.state==CERBERUS_WIFI_CONNECTION_CONNECTING){wifi_mode=UI_WIFI_MODE_CONNECTION;return;}
    if(!live_connection_ready()){
        snprintf(wifi_target_message,sizeof(wifi_target_message),"WAIT FOR THE C5 SCAN TO FINISH");return;
    }
    if(wifi_selected_result.security==CERBERUS_WIFI_SECURITY_OPEN){
        wifi_connection_error=live_connect_selected_wifi("");wifi_mode=UI_WIFI_MODE_CONNECTION;
    }else{
        wifi_connection_error=cerberus_keyboard_begin(&wifi_keyboard,wifi_connection_password,
            sizeof(wifi_connection_password),"");
        if(wifi_connection_error==ESP_OK){cerberus_keyboard_set_masked(&wifi_keyboard,true);wifi_mode=UI_WIFI_MODE_PASSWORD;}
    }
}
static void wifi_target_handle_tap(uint16_t x,uint16_t y)
{
    char identifier[24],metadata[64];
    cerberus_wifi_target_view_t view=wifi_target_view(identifier,metadata);
    cerberus_wifi_target_action_t action=cerberus_wifi_target_tap(&view,x,y);
    if(action==CERBERUS_WIFI_TARGET_ATTACK){
        if(wifi_targets_single(&wifi_selected_result,wifi_selected_index))wifi_target_open_attacks();
        else snprintf(wifi_target_message,sizeof(wifi_target_message),"NO MEMORY FOR NETWORK SELECTION");
    }else if(action==CERBERUS_WIFI_TARGET_CONNECT)wifi_target_connect();
    else if(action==CERBERUS_WIFI_TARGET_PORTS){
        /* Refresh before opening. Never relabel another connection as this AP. */
        wifi_connection_error=c5_spi_link_get_wifi_connection_status(&wifi_connection_status);
        if(wifi_connection_error==ESP_OK&&wifi_connection_status.state==CERBERUS_WIFI_CONNECTION_CONNECTED&&wifi_target_connection_matches()){
            net_enter();wifi_mode=UI_WIFI_MODE_NETWORK_SCAN;
        }else snprintf(wifi_target_message,sizeof(wifi_target_message),"CONNECTION CHANGED / CHECK TARGET");
    }else if(action==CERBERUS_WIFI_TARGET_STATIONS){
        if(wifi_relation_supported())wifi_relation_open();
        else{station_enter();wifi_mode=UI_WIFI_MODE_STATIONS;}
    }
    else if(action==CERBERUS_WIFI_TARGET_CAPTURE){capture_enter();wifi_mode=UI_WIFI_MODE_CAPTURE;}
    else if(action==CERBERUS_WIFI_TARGET_PACKET_MONITOR){
        packet_monitor_channel=wifi_selected_result.channel;
        packet_monitor_from_target=true;
        packet_monitor_attempted=false;
        packet_monitor_last_poll=0U;
        wifi_mode=UI_WIFI_MODE_PACKET_MONITOR;
        packet_monitor_start_ui();
    }
    else if(action==CERBERUS_WIFI_TARGET_CSI){
        esp_err_t error=csi_open_selected();
        if(error!=ESP_OK)snprintf(wifi_target_message,sizeof(wifi_target_message),"CSI: %s",esp_err_to_name(error));
    }
    else if(action==CERBERUS_WIFI_TARGET_DETAILS)wifi_mode=UI_WIFI_MODE_DETAIL;
    else if(action==CERBERUS_WIFI_TARGET_CHANGE){wifi_target_message[0]=0;wifi_mode=UI_WIFI_MODE_LIST;}
}
