/* Main-task UI; C5 owns the passive capture, S3 owns selection and SD export. */
static cerberus_wifi_station_request_t station_job;
static cerberus_wifi_station_status_t station_status;
static cerberus_wifi_station_result_t station_rows[CERBERUS_WIFI_STATION_LIMIT],station_selected;
static uint8_t station_loaded,station_page,station_seconds=15,station_selected_ap[6];
static bool station_pending,station_stopping,station_selected_valid,station_inventory_synced;
static esp_err_t station_error,station_save_error;
static uint32_t station_last_poll;
static char station_message[49],station_ssid[33];
static bool station_busy(void)
{return station_pending||station_status.state==CERBERUS_STATION_RUNNING||station_status.state==CERBERUS_STATION_STOPPING;}
static wifi_target_entry_t *station_target(void)
{
    int index=wifi_selection_valid?wifi_targets_find(&wifi_selected_result):-1;
    return index>=0?&wifi_targets[index]:NULL;
}
static void station_enter(void)
{
    if(station_busy())return;
    if(!wifi_selection_valid||memcmp(station_job.bssid,wifi_selected_result.bssid,6)||station_job.channel!=wifi_selected_result.channel){
        memset(&station_job,0,sizeof(station_job));memset(&station_status,0,sizeof(station_status));
        memset(station_rows,0,sizeof(station_rows));station_loaded=station_page=0;
        station_selected_valid=false;station_inventory_synced=false;
        station_message[0]=0;station_error=station_save_error=ESP_OK;
    }
    if(wifi_selection_valid&&!wifi_targets_add(&wifi_selected_result,wifi_selected_index))
        snprintf(station_message,sizeof(station_message),"NETWORK LIST FULL / REMOVE AN AP FIRST");
    wifi_target_entry_t *entry=station_target();
    if(entry){station_selected=entry->station;station_selected_valid=entry->station_valid;
        memcpy(station_selected_ap,entry->ap.bssid,6);}
    snprintf(station_ssid,sizeof(station_ssid),"%s",wifi_selected_result.ssid[0]?wifi_selected_result.ssid:"<HIDDEN SSID>");
    for(char *p=station_ssid;*p;++p)if((uint8_t)*p<32||(uint8_t)*p>126)*p='?';
}
static void station_start(void)
{
    if(station_busy())return;
    station_enter();
    station_message[0]=0;station_save_error=ESP_OK;
    if(!wifi_selection_valid||!(c5_status.capabilities&CERBERUS_C5_CAP_WIFI_STATIONS)){
        snprintf(station_message,sizeof(station_message),"SELECT AN AP / C5 STATION SUPPORT REQUIRED");return;
    }
    cerberus_wifi_connection_status_payload_t connection;
    station_error=c5_spi_link_get_wifi_connection_status(&connection);
    if(station_error!=ESP_OK)return;
    if(connection.state!=CERBERUS_WIFI_CONNECTION_IDLE&&connection.state!=CERBERUS_WIFI_CONNECTION_ERROR){
        snprintf(station_message,sizeof(station_message),"DISCONNECT WI-FI BEFORE PASSIVE CAPTURE");return;
    }
    cerberus_wifi_scan_status_payload_t scan;
    station_error=c5_spi_link_get_wifi_scan_status(&scan);
    if(station_error!=ESP_OK)return;
    if(scan.state!=CERBERUS_WIFI_SCAN_COMPLETE){snprintf(station_message,sizeof(station_message),"WAIT FOR THE AP SCAN TO FINISH");return;}
    /* Resolve by identity from the current C5 snapshot, never a sorted UI row. */
    unsigned index=0;cerberus_c5_wifi_result_t candidate;
    for(;index<scan.stored_count&&index<CERBERUS_WIFI_SCAN_MAX_RESULTS;++index){
        station_error=c5_spi_link_get_wifi_scan_result(scan.generation,(uint8_t)index,&candidate);
        if(station_error!=ESP_OK)return;
        if(!memcmp(candidate.bssid,wifi_selected_result.bssid,6)&&candidate.channel==wifi_selected_result.channel)break;
    }
    if(index>=scan.stored_count||index>=CERBERUS_WIFI_SCAN_MAX_RESULTS){
        snprintf(station_message,sizeof(station_message),"TARGET CHANGED / RESCAN AND SELECT AP");return;
    }
    cerberus_wifi_station_status_t prior;
    station_error=c5_spi_link_station_status(0,&prior);
    if(station_error!=ESP_OK)return;
    if(prior.state==CERBERUS_STATION_RUNNING||prior.state==CERBERUS_STATION_STOPPING){
        snprintf(station_message,sizeof(station_message),"PREVIOUS CAPTURE IS STILL STOPPING");return;
    }
    station_job=(cerberus_wifi_station_request_t){.job_id=(uint16_t)(prior.job_id+1U),
        .generation=scan.generation,.index=(uint8_t)index,.channel=candidate.channel,.seconds=station_seconds};
    if(!station_job.job_id)station_job.job_id=1;
    memcpy(station_job.bssid,candidate.bssid,6);
    memset(&station_status,0,sizeof(station_status));memset(station_rows,0,sizeof(station_rows));
    station_loaded=station_page=0;station_pending=true;station_stopping=false;
    station_inventory_synced=false;station_selected_valid=false;
    station_error=c5_spi_link_station_start(&station_job,&station_status);
    if(station_status.job_id==station_job.job_id)station_pending=false;
}
static void station_cancel(void)
{
    if(!station_busy()||!station_job.job_id)return;
    station_stopping=true;
    station_error=c5_spi_link_station_stop(station_job.job_id,&station_status);
    if(station_error==ESP_OK)station_pending=false;
}
static void station_poll(uint32_t now,bool visible)
{
    if(!station_job.job_id||(uint32_t)(now-station_last_poll)<500)return;
    station_last_poll=now;
    if(!visible){station_cancel();return;} /* STOP only; never renew a hidden job's lease. */
    cerberus_wifi_station_status_t status;
    station_error=c5_spi_link_station_status(station_job.job_id,&status);
    if(station_error!=ESP_OK)return;
    if(status.job_id!=station_job.job_id){
        station_pending=false;station_loaded=0;station_selected_valid=false;station_inventory_synced=false;
        station_status=(cerberus_wifi_station_status_t){.state=CERBERUS_STATION_ERROR};station_error=ESP_ERR_INVALID_STATE;return;
    }
    station_status=status;station_pending=false;
    if(station_stopping&&status.state==CERBERUS_STATION_RUNNING)station_cancel();
    /* Keep a consistent final table. Live status supplies frame/client counts. */
    if(station_busy())return;
    for(unsigned i=0;i<4&&station_loaded<station_status.count;++i){
        station_error=c5_spi_link_station_result(station_job.job_id,station_loaded,&station_rows[station_loaded]);
        if(station_error!=ESP_OK)return;
        ++station_loaded;
    }
    if(station_error==ESP_OK&&!station_inventory_synced&&station_loaded==station_status.count&&
       (station_status.state==CERBERUS_STATION_COMPLETE||station_status.state==CERBERUS_STATION_CANCELLED)){
        if(wifi_targets_inventory(station_job.bssid,station_job.channel,station_rows,station_loaded)){
            wifi_target_entry_t *entry=station_target();
            if(entry){station_selected=entry->station;station_selected_valid=entry->station_valid;
                memcpy(station_selected_ap,entry->ap.bssid,6);}
        }
        station_inventory_synced=true;
    }
}
static void station_save(void)
{
    if(station_busy()||!station_job.job_id||station_error!=ESP_OK||station_loaded!=station_status.count){
        snprintf(station_message,sizeof(station_message),"STOP AND LOAD ALL RESULTS BEFORE SAVING");return;
    }
    char *csv=malloc(4096);if(!csv){station_save_error=ESP_ERR_NO_MEM;return;}
    size_t used=(size_t)snprintf(csv,4096,
        "# Cerberus passive stations; bssid=%02X:%02X:%02X:%02X:%02X:%02X; channel=%u; state=%u; error=%ld; elapsed_ms=%lu; observed=%lu; matched=%lu; overflow_frames=%lu\n"
        "mac,flags,rssi,signal_source,frames,from_station,to_station,last_seen_ms\n",
        station_job.bssid[0],station_job.bssid[1],station_job.bssid[2],station_job.bssid[3],station_job.bssid[4],station_job.bssid[5],
        station_job.channel,station_status.state,(long)station_status.error_code,(unsigned long)station_status.elapsed_ms,
        (unsigned long)station_status.observed_frames,(unsigned long)station_status.matched_frames,(unsigned long)station_status.overflow_frames);
    station_save_error=ESP_OK;
    for(unsigned i=0;i<station_loaded;++i){
        const cerberus_wifi_station_result_t *r=&station_rows[i];
        int n=snprintf(csv+used,4096-used,"%02X:%02X:%02X:%02X:%02X:%02X,%u,%d,%s,%lu,%u,%u,%lu\n",
            r->mac[0],r->mac[1],r->mac[2],r->mac[3],r->mac[4],r->mac[5],r->flags,r->rssi,
            r->from_station?"station":"ap_only",(unsigned long)r->frames,r->from_station,r->to_station,(unsigned long)r->last_seen_ms);
        if(n<0||(size_t)n>=4096-used){station_save_error=ESP_ERR_INVALID_SIZE;break;}used+=(size_t)n;
    }
    char name[32];
    if(station_save_error==ESP_OK)station_save_error=cerberus_storage_write_wifi_report(csv,used,name,sizeof(name));
    free(csv);
    if(station_save_error==ESP_OK)snprintf(station_message,sizeof(station_message),"SAVED %s",name);
}
static esp_err_t station_draw(void)
{
    char line[64];
    ESP_RETURN_ON_ERROR(draw_centered(58,"AP STATIONS",UI_TEXT,2),TAG,"station title");
    ESP_RETURN_ON_ERROR(draw_centered(84,station_ssid,UI_MUTED,1),TAG,"station AP");
    const char *state=station_pending?"WAITING FOR C5":station_status.state==CERBERUS_STATION_RUNNING?"LISTENING":
        station_status.state==CERBERUS_STATION_STOPPING?"STOPPING":station_status.state==CERBERUS_STATION_COMPLETE?"COMPLETE":
        station_status.state==CERBERUS_STATION_CANCELLED?"CANCELLED":station_status.state==CERBERUS_STATION_ERROR?"ERROR":"READY";
    snprintf(line,sizeof(line),"%s / CH %u / %lu S",state,station_job.job_id?station_job.channel:wifi_selected_result.channel,
        (unsigned long)station_status.elapsed_ms/1000);
    ESP_RETURN_ON_ERROR(draw_centered(106,line,UI_GOOD,1),TAG,"station state");
    const wifi_target_entry_t *entry=station_target();
    snprintf(line,sizeof(line),"%u OBSERVED / %u PICKED / %lu FRAMES",station_status.count,
        wifi_targets_selected_count(entry),(unsigned long)station_status.matched_frames);
    ESP_RETURN_ON_ERROR(draw_centered(124,line,UI_TEXT,1),TAG,"station counts");
    for(unsigned row=0;row<4;++row){
        unsigned index=station_page*4U+row;if(index>=station_loaded)break;
        const cerberus_wifi_station_result_t *r=&station_rows[index];char mac[24];
        cerberus_privacy_format_wifi_identifier(r->mac,privacy_addresses_masked(),mac,sizeof(mac));
        int observed=wifi_targets_observed_index(entry,r->mac);
        bool selected=entry&&observed>=0&&(entry->selected_stations&(UINT32_C(1)<<observed));
        snprintf(line,sizeof(line),"%s %s / %lu",selected?">":" ",mac,(unsigned long)r->frames);
        ESP_RETURN_ON_ERROR(draw_small_button(8,(uint16_t)(148+row*42),304,36,line),TAG,"station row");
    }
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8,322,148,28},"PICK ALL",1,
        entry&&entry->observed_count&&!station_busy(),UI_WIDGET_THEME),TAG,"station pick all");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){164,322,148,28},"CLEAR PICKS",1,
        entry&&entry->selected_stations&&!station_busy(),UI_WIDGET_THEME),TAG,"station clear picks");
    if(station_save_error||station_error||station_status.error_code)snprintf(line,sizeof(line),"ERROR %s",esp_err_to_name(
        station_save_error?station_save_error:station_error?station_error:station_status.error_code));
    else if(station_message[0])snprintf(line,sizeof(line),"%s",station_message);
    else if(station_status.overflow_frames)snprintf(line,sizeof(line),"TABLE FULL / %lu FRAMES NOT RETAINED",(unsigned long)station_status.overflow_frames);
    else snprintf(line,sizeof(line),"OBSERVED ONLY / ASSOCIATION NOT VERIFIED");
    ESP_RETURN_ON_ERROR(draw_centered(356,line,UI_MUTED,1),TAG,"station note");
    ESP_RETURN_ON_ERROR(draw_small_button(8,378,76,36,station_busy()?"STOP":"START"),TAG,"station start");
    snprintf(line,sizeof(line),"%u SEC",station_seconds);
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){92,378,76,36},line,1,!station_busy(),UI_WIDGET_THEME),TAG,"station duration");
    ESP_RETURN_ON_ERROR(draw_small_button(176,378,60,36,"NEXT"),TAG,"station next");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){244,378,68,36},"SAVE SD",1,
        !station_busy()&&station_job.job_id&&station_error==ESP_OK&&station_loaded==station_status.count,UI_WIDGET_THEME);
}
static void station_tap(uint16_t x,uint16_t y)
{
    if(x>=8&&x<312&&y>=148&&y<316&&(y-148)%42<36){
        unsigned index=station_page*4U+(y-148)/42;
        if(index<station_loaded){
            wifi_target_entry_t *entry=station_target();
            int observed=wifi_targets_observed_index(entry,station_rows[index].mac);
            bool selected=entry&&observed>=0&&(entry->selected_stations&(UINT32_C(1)<<observed));
            if(entry&&wifi_targets_station(station_job.bssid,&station_rows[index],!selected)){
                station_selected=entry->station;station_selected_valid=entry->station_valid;
                memcpy(station_selected_ap,entry->ap.bssid,6);
            }
        }
    }else if(y>=322&&y<350&&!station_busy()){
        wifi_target_entry_t *entry=station_target();
        if(entry&&x>=8&&x<156)wifi_targets_select_all(entry);
        else if(entry&&x>=164&&x<312)wifi_targets_clear_stations(entry);
        if(entry){station_selected=entry->station;station_selected_valid=entry->station_valid;}
    }else if(y>=378&&y<414){
        if(x>=8&&x<84){if(station_busy())station_cancel();else station_start();}
        else if(x>=92&&x<168&&!station_busy())station_seconds=station_seconds==15?30:station_seconds==30?60:15;
        else if(x>=176&&x<236)station_page=(station_page+1U)*4U>=station_loaded?0:station_page+1;
        else if(x>=244&&x<312)station_save();
    }
}
