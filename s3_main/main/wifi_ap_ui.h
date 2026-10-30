/* S3 controls the job and receives on 2.4 GHz; C5 owns the TX worker. */
static bool ap_batch_active;
static cerberus_wifi_ap_job_t ap_job;
static cerberus_wifi_ap_station_set_t ap_active_stations;
static const wifi_target_entry_t *ap_source_target;
static cerberus_radio_test_status_t ap_status;
static cerberus_s3_rx_status_t ap_rx;
static uint8_t ap_scope,ap_mode=CERBERUS_AP_DEAUTH;
static bool ap_pending,ap_stopping,ap_station_selecting,ap_wait_rx,ap_c5_started;
static esp_err_t ap_error,ap_save_error;
static uint32_t ap_last_poll;
static char ap_message[49],ap_ssid[33];
static uint8_t ap_bound_bssid[6],ap_bound_channel;
static const char *ap_name(uint8_t mode)
{
    switch(mode){
    case CERBERUS_AP_PROBE_REQUEST:return "PROBE REQUESTS";
    case CERBERUS_AP_BAD_MESSAGE:return "BAD MESSAGE";
    case CERBERUS_AP_ASSOC_SLEEP:return "ASSOCIATION SLEEP";
    case CERBERUS_AP_CHANNEL_SWITCH:return "CHANNEL SWITCH";
    case CERBERUS_AP_QUIET:return "QUIET TIME";
    case CERBERUS_AP_CLONE_SPAM:return "AP CLONE SPAM";
    case CERBERUS_AP_SAE_COMMIT:return "SAE COMMIT FLOOD";
    default:return "DEAUTHENTICATION";
    }
}
static const char *ap_report_mode(uint8_t mode)
{
    switch(mode){
    case CERBERUS_AP_PROBE_REQUEST:return "directed_probe";
    case CERBERUS_AP_BAD_MESSAGE:return "bad_message";
    case CERBERUS_AP_ASSOC_SLEEP:return "association_sleep";
    case CERBERUS_AP_CHANNEL_SWITCH:return "channel_switch";
    case CERBERUS_AP_QUIET:return "quiet_time";
    case CERBERUS_AP_CLONE_SPAM:return "ap_clone_spam";
    case CERBERUS_AP_SAE_COMMIT:return "sae_commit";
    default:return "deauth";
    }
}
static const char *ap_report_scope(const cerberus_wifi_ap_job_t *job)
{return job->mode==CERBERUS_AP_CLONE_SPAM?"ssid_clone":
    job->mode==CERBERUS_AP_SAE_COMMIT?"ap_auth_commit":
    job->mode==CERBERUS_AP_PROBE_REQUEST?"ap_directed":
    job->scope==CERBERUS_AP_STATION_LIST?"station_list":
    job->scope==CERBERUS_AP_ONE_STATION?"one_station":"broadcast";}
static bool ap_csv_clients(char *csv,size_t capacity,size_t *used,unsigned target,
    const cerberus_wifi_ap_station_set_t *set)
{
    for(unsigned i=0;i<set->count;++i){
        const uint8_t *s=set->mac[i];
        if(*used>=capacity)return false;
        int n=snprintf(csv+*used,capacity-*used,"%u,%u,%02X:%02X:%02X:%02X:%02X:%02X\n",
            target,i+1,s[0],s[1],s[2],s[3],s[4],s[5]);
        if(n<0||(size_t)n>=capacity-*used)return false;
        *used+=(size_t)n;
    }
    return true;
}
static uint32_t ap_capability(uint8_t mode)
{
    switch(mode){
    case CERBERUS_AP_DEAUTH:return CERBERUS_C5_CAP_AP_DEAUTH;
    case CERBERUS_AP_PROBE_REQUEST:return CERBERUS_C5_CAP_AP_PROBE;
    case CERBERUS_AP_BAD_MESSAGE:return CERBERUS_C5_CAP_AP_BAD_MESSAGE;
    case CERBERUS_AP_ASSOC_SLEEP:return CERBERUS_C5_CAP_AP_ASSOC_SLEEP;
    case CERBERUS_AP_CHANNEL_SWITCH:return CERBERUS_C5_CAP_AP_CHANNEL_SWITCH;
    case CERBERUS_AP_QUIET:return CERBERUS_C5_CAP_AP_QUIET;
    case CERBERUS_AP_CLONE_SPAM:return CERBERUS_C5_CAP_AP_CLONE_SPAM;
    case CERBERUS_AP_SAE_COMMIT:return CERBERUS_C5_CAP_AP_SAE_COMMIT;
    default:return 0;
    }
}
static bool ap_c5_busy(void)
{return ap_pending||ap_status.state==CERBERUS_TEST_RUNNING||ap_status.state==CERBERUS_TEST_STOPPING;}
static uint32_t ap_frames_per_second(uint32_t count,uint32_t elapsed_ms)
{
    if(!elapsed_ms)return 0;
    uint64_t rate=((uint64_t)count*1000U+elapsed_ms/2U)/elapsed_ms;
    return rate>UINT32_MAX?UINT32_MAX:(uint32_t)rate;
}
static bool ap_rx_busy(void)
{return ap_rx.reserved&&ap_rx.state!=CERBERUS_TEST_ERROR;}
static bool ap_one_busy(void)
{return ap_wait_rx||ap_c5_busy()||ap_rx_busy();}
static bool ap_clone_identity_valid(const uint8_t mac[6],const uint8_t bssid[6])
{return (mac[0]&3)==2&&(mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5])&&memcmp(mac,bssid,6);}
static void ap_clone_identity_new(const uint8_t bssid[6],uint8_t out[6])
{
    esp_fill_random(out,6);out[0]=(out[0]&0xfcU)|2U;
    if(!memcmp(out,bssid,6))out[5]^=0x80U;
}
static void ap_clear_result(void)
{
    memset(&ap_job,0,sizeof(ap_job));memset(&ap_status,0,sizeof(ap_status));
    memset(&ap_active_stations,0,sizeof(ap_active_stations));
    memset(&ap_rx,0,sizeof(ap_rx));ap_wait_rx=ap_c5_started=ap_pending=ap_stopping=false;
    ap_error=ap_save_error=ESP_OK;ap_message[0]=0;
}
static bool ap_has_station(void)
{
    return station_selected_valid&&wifi_selection_valid&&
        !memcmp(station_selected_ap,wifi_selected_result.bssid,6)&&
        station_selected.channel==wifi_selected_result.channel;
}
static void ap_one_choose(uint8_t mode)
{
    if(ap_one_busy()||ap_mode==mode)return;
    ap_clear_result();ap_mode=mode;
    int selected=wifi_targets_find(&wifi_selected_result);
    ap_scope=wifi_targets_scope(selected>=0?&wifi_targets[selected]:NULL,mode);
}
static void ap_enter(void)
{
    if(ap_one_busy())return;
    if(!ap_batch_active){
        int selected=wifi_targets_find(&wifi_selected_result);
        if(selected>=0){
            if(!wifi_targets[selected].station_valid&&ap_has_station())
                wifi_targets_station(station_selected_ap,&station_selected,true);
            station_selected=wifi_targets[selected].station;station_selected_valid=wifi_targets[selected].station_valid;
            memcpy(station_selected_ap,wifi_targets[selected].ap.bssid,6);
            ap_scope=wifi_targets_scope(&wifi_targets[selected],ap_mode);
        }
    }
    if(!wifi_selection_valid||memcmp(ap_bound_bssid,wifi_selected_result.bssid,6)||ap_bound_channel!=wifi_selected_result.channel){
        ap_clear_result();if(!ap_batch_active){
            int selected=wifi_targets_find(&wifi_selected_result);
            ap_scope=selected>=0?wifi_targets_scope(&wifi_targets[selected],ap_mode):
                cerberus_wifi_ap_needs_station(ap_mode)?WIFI_TARGET_PICK_STATIONS:
                ap_has_station()?WIFI_TARGET_PICK_STATIONS:WIFI_TARGET_BROADCAST;
        }
    }
    if(cerberus_wifi_ap_needs_station(ap_mode)){
        if(ap_scope==WIFI_TARGET_BROADCAST)ap_scope=WIFI_TARGET_PICK_STATIONS;
    }else if(ap_mode!=CERBERUS_AP_DEAUTH)ap_scope=WIFI_TARGET_BROADCAST;
    memcpy(ap_bound_bssid,wifi_selected_result.bssid,6);ap_bound_channel=wifi_selected_result.channel;
    snprintf(ap_ssid,sizeof(ap_ssid),"%s",wifi_selected_result.ssid[0]?wifi_selected_result.ssid:"<HIDDEN SSID>");
    for(char *p=ap_ssid;*p;++p)if((uint8_t)*p<32||(uint8_t)*p>126)*p='?';
}
static void ap_dispatch(void)
{
    ap_wait_rx=false;
    if(ap_job.scope==CERBERUS_AP_STATION_LIST){
        ap_error=c5_spi_link_ap_stage(&ap_job,&ap_active_stations);
        if(ap_error!=ESP_OK){
            ap_pending=ap_c5_started=false;
            ap_status=(cerberus_radio_test_status_t){.job_id=ap_job.job_id,.mode=CERBERUS_TEST_WIFI_AP,
                .state=CERBERUS_TEST_ERROR,.error_code=ap_error};
            cerberus_s3_rx_stop(ap_job.job_id);return;
        }
    }
    ap_pending=true;ap_c5_started=true;
    ap_error=c5_spi_link_ap_start(&ap_job,&ap_status);
    if(ap_status.job_id==ap_job.job_id&&ap_status.mode==CERBERUS_TEST_WIFI_AP)ap_pending=false;
    if(!ap_c5_busy())cerberus_s3_rx_stop(ap_job.job_id);
}
static void ap_one_start_with_seconds(uint8_t dwell_seconds)
{
    if(ap_one_busy())return;
    ap_enter();
    ap_error=ap_save_error=ESP_OK;ap_message[0]=0;
    uint32_t capability=ap_capability(ap_mode);
    if(!wifi_selection_valid||!c5_online||!(c5_status.capabilities&capability)){
        snprintf(ap_message,sizeof(ap_message),"SELECT AP / C5 FEATURE SUPPORT REQUIRED");return;
    }
    if(ap_mode==CERBERUS_AP_SAE_COMMIT&&c5_status.firmware_version<44){
        snprintf(ap_message,sizeof(ap_message),"C5 REVISION 44 REQUIRED FOR SAE COMMIT");return;
    }
    const wifi_target_entry_t *target=ap_source_target;
    if(!target){int selected=wifi_targets_find(&wifi_selected_result);
        if(selected>=0)target=&wifi_targets[selected];}
    cerberus_wifi_ap_station_set_t stations={0};
    if(ap_mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(ap_mode)){
        if(ap_scope!=WIFI_TARGET_BROADCAST){
            if(target){
                if(!wifi_targets_station_set(target,ap_scope,&stations)){
                    snprintf(ap_message,sizeof(ap_message),"FIND AND SELECT STATIONS ON THIS AP");return;
                }
            }else if(ap_scope==WIFI_TARGET_PICK_STATIONS&&ap_has_station()){
                stations.count=1;memcpy(stations.mac[0],station_selected.mac,6);
            }else{snprintf(ap_message,sizeof(ap_message),"FIND AND SELECT STATIONS ON THIS AP");return;}
            if(stations.count>1&&c5_status.firmware_version<42){
                snprintf(ap_message,sizeof(ap_message),"C5 REVISION 42 REQUIRED FOR CLIENT LIST");return;
            }
        }
    }
    cerberus_wifi_connection_status_payload_t connection;
    ap_error=c5_spi_link_get_wifi_connection_status(&connection);if(ap_error!=ESP_OK)return;
    if(connection.state!=CERBERUS_WIFI_CONNECTION_IDLE&&connection.state!=CERBERUS_WIFI_CONNECTION_ERROR){
        snprintf(ap_message,sizeof(ap_message),"DISCONNECT WIFI BEFORE STARTING");return;
    }
    cerberus_wifi_scan_status_payload_t scan;
    ap_error=c5_spi_link_get_wifi_scan_status(&scan);if(ap_error!=ESP_OK)return;
    if(scan.state!=CERBERUS_WIFI_SCAN_COMPLETE){snprintf(ap_message,sizeof(ap_message),"WAIT FOR THE AP SCAN TO FINISH");return;}
    unsigned limit=scan.stored_count<CERBERUS_WIFI_SCAN_MAX_RESULTS?scan.stored_count:CERBERUS_WIFI_SCAN_MAX_RESULTS;
    unsigned hint=wifi_selected_index,index=hint;bool found=false;cerberus_c5_wifi_result_t candidate;
    /* Usually the selected row is still at this index. Verify it once instead
     * of transferring all preceding SSID records over SPI on every Start. */
    if(hint<limit){
        ap_error=c5_spi_link_get_wifi_scan_result(scan.generation,(uint8_t)hint,&candidate);if(ap_error!=ESP_OK)return;
        found=!memcmp(candidate.bssid,wifi_selected_result.bssid,6)&&candidate.channel==wifi_selected_result.channel;
    }
    if(!found)for(index=0;index<limit;++index){
        if(index==hint)continue;
        ap_error=c5_spi_link_get_wifi_scan_result(scan.generation,(uint8_t)index,&candidate);if(ap_error!=ESP_OK)return;
        if(!memcmp(candidate.bssid,wifi_selected_result.bssid,6)&&candidate.channel==wifi_selected_result.channel){found=true;break;}
    }
    if(!found){
        snprintf(ap_message,sizeof(ap_message),"TARGET CHANGED / RESCAN AND SELECT AP");return;
    }
    if(ap_mode!=CERBERUS_AP_DEAUTH&&strcmp(candidate.ssid,wifi_selected_result.ssid)){
        snprintf(ap_message,sizeof(ap_message),"SSID CHANGED / RESCAN AND SELECT AP");return;
    }
    wifi_selected_index=(uint8_t)index;
    cerberus_radio_test_status_t prior;
    ap_error=c5_spi_link_test_status(0,&prior);if(ap_error!=ESP_OK)return;
    if(prior.state==CERBERUS_TEST_RUNNING||prior.state==CERBERUS_TEST_STOPPING){
        snprintf(ap_message,sizeof(ap_message),"C5 TRANSMIT WORKER IS BUSY");return;
    }
    cerberus_wifi_ap_job_t next={.job_id=(uint16_t)(prior.job_id+1U),.generation=scan.generation,
        .index=(uint8_t)index,.channel=candidate.channel,.seconds=dwell_seconds,
        .scope=stations.count>1?CERBERUS_AP_STATION_LIST:stations.count?CERBERUS_AP_ONE_STATION:CERBERUS_AP_BROADCAST,.mode=ap_mode};
    if(!next.job_id)next.job_id=1;
    cerberus_s3_rx_status_t previous_rx;cerberus_s3_rx_status(0,&previous_rx);
    if(next.job_id==previous_rx.job_id){++next.job_id;if(!next.job_id)next.job_id=1;}
    memcpy(next.bssid,candidate.bssid,6);
    if(stations.count==1)memcpy(next.station,stations.mac[0],6);
    if(ap_mode==CERBERUS_AP_CLONE_SPAM){
        if(ap_source_target&&ap_clone_identity_valid(ap_source_target->clone_mac,candidate.bssid))
            memcpy(next.station,ap_source_target->clone_mac,6);
        else ap_clone_identity_new(candidate.bssid,next.station);
    }
    if(!cerberus_wifi_ap_job_valid(&next)||
       (stations.count>1&&!cerberus_wifi_ap_station_set_valid(&next,&stations))){
        snprintf(ap_message,sizeof(ap_message),"TARGET CHANNEL / SCOPE NOT SUPPORTED");return;
    }
    ap_clear_result();ap_job=next;ap_active_stations=stations;
    if(next.channel<=11){
        ap_error=next.scope==CERBERUS_AP_STATION_LIST?
            cerberus_s3_rx_start_stations(&ap_job,&ap_active_stations):
            next.mode==CERBERUS_AP_CLONE_SPAM?
                cerberus_s3_rx_start_clone(&ap_job,(const uint8_t*)candidate.ssid,
                    strnlen(candidate.ssid,32)):cerberus_s3_rx_start(&ap_job);
        cerberus_s3_rx_status(0,&ap_rx);
        if(ap_error!=ESP_OK){
            /* A prior receiver's state must never become this job's report. */
            memset(&ap_rx,0,sizeof(ap_rx));
            ap_status=(cerberus_radio_test_status_t){.job_id=ap_job.job_id,.mode=CERBERUS_TEST_WIFI_AP,
                .state=CERBERUS_TEST_ERROR,.error_code=ap_error};return;
        }
        ap_wait_rx=true;
    }else ap_dispatch();
}
static void ap_one_start(void){ap_one_start_with_seconds(0);}
static void ap_one_stop(void)
{
    if(!ap_one_busy()||!ap_job.job_id)return;
    ap_stopping=true;cerberus_s3_rx_stop(ap_job.job_id);
    if(ap_wait_rx){
        ap_wait_rx=false;
        ap_status=(cerberus_radio_test_status_t){.job_id=ap_job.job_id,.mode=CERBERUS_TEST_WIFI_AP,.state=CERBERUS_TEST_CANCELLED};
    }
    if(ap_c5_started&&ap_c5_busy()){
        cerberus_radio_test_status_t next;
        ap_error=c5_spi_link_test_stop(ap_job.job_id,&next);
        if(ap_error==ESP_OK){
            ap_pending=false;
            if(next.job_id==ap_job.job_id&&next.mode==CERBERUS_TEST_WIFI_AP)ap_status=next;
            else {
                /* A fresh valid response proves that this job is no longer
                 * owned by C5, including a Start that never reached it. */
                ap_error=ESP_ERR_INVALID_STATE;
                ap_status=(cerberus_radio_test_status_t){.job_id=ap_job.job_id,.mode=CERBERUS_TEST_WIFI_AP,
                    .state=CERBERUS_TEST_ERROR,.error_code=ap_error};
            }
        }
    }
    if(ap_job.channel<=11)cerberus_s3_rx_status(0,&ap_rx);
}
static void ap_one_poll(uint32_t now,bool visible)
{
    if(!ap_job.job_id||!ap_one_busy()||(uint32_t)(now-ap_last_poll)<250)return;
    ap_last_poll=now;
    if(!visible){ap_one_stop();return;}
    if(ap_job.channel<=11){
        cerberus_s3_rx_status(!ap_stopping&&(ap_wait_rx||ap_c5_busy())?ap_job.job_id:0,&ap_rx);
        if(ap_wait_rx){
            if(ap_rx.job_id==ap_job.job_id&&ap_rx.state==CERBERUS_TEST_RUNNING)ap_dispatch();
            else if(!ap_rx_busy()||ap_rx.job_id!=ap_job.job_id){
                ap_wait_rx=false;ap_error=ap_rx.error_code?ap_rx.error_code:ESP_ERR_INVALID_STATE;
                ap_status=(cerberus_radio_test_status_t){.job_id=ap_job.job_id,.mode=CERBERUS_TEST_WIFI_AP,
                    .state=CERBERUS_TEST_ERROR,.error_code=ap_error};
            }
            return;
        }
        if(ap_c5_busy()&&!ap_stopping&&(!ap_rx_busy()||ap_rx.job_id!=ap_job.job_id)){
            ap_one_stop();snprintf(ap_message,sizeof(ap_message),"S3 RECEIVER STOPPED / C5 STOP REQUESTED");return;
        }
    }
    if(!ap_c5_started||!ap_c5_busy())return;
    cerberus_radio_test_status_t next;
    ap_error=c5_spi_link_test_status(ap_stopping?0:ap_job.job_id,&next);if(ap_error!=ESP_OK)return;
    if(next.job_id!=ap_job.job_id||next.mode!=CERBERUS_TEST_WIFI_AP){
        ap_pending=false;ap_status.state=CERBERUS_TEST_ERROR;ap_error=ESP_ERR_INVALID_STATE;
        cerberus_s3_rx_stop(ap_job.job_id);return;
    }
    ap_status=next;ap_pending=false;
    if(ap_stopping&&next.state==CERBERUS_TEST_RUNNING)ap_one_stop();
    if(!ap_c5_busy())cerberus_s3_rx_stop(ap_job.job_id);
}
static void ap_one_save(void)
{
    if(ap_one_busy()||!ap_job.job_id||ap_status.job_id!=ap_job.job_id)return;
    size_t capacity=2048U+(size_t)ap_active_stations.count*32U;
    char *csv=CERBERUS_UI_JOB_ALLOC(capacity);char name[32];
    if(!csv){ap_save_error=ESP_ERR_NO_MEM;return;}
    int n=snprintf(csv,capacity,
        "# Cerberus selected-AP job; driver acceptance is not over-air success\n"
        "job,mode,bssid,station_or_clone_bssid,scope,channel,seconds,state,elapsed_ms,driver_accepted_frames,error,min_free_heap_kib\n"
        "%u,%s,%02X:%02X:%02X:%02X:%02X:%02X,%02X:%02X:%02X:%02X:%02X:%02X,%s,%u,%u,%u,%lu,%lu,%ld,%u\n"
        "# S3 matching frames do not identify the physical transmitter or prove disconnection\n"
        "s3_rx_available,s3_state,s3_observed,s3_ap_beacons,s3_matching_frames,s3_last_match_rssi,s3_error,s3_min_heap_kib\n"
        "%u,%u,%lu,%lu,%lu,%d,%ld,%u\n",
        ap_job.job_id,ap_report_mode(ap_job.mode),ap_job.bssid[0],ap_job.bssid[1],ap_job.bssid[2],ap_job.bssid[3],ap_job.bssid[4],ap_job.bssid[5],
        ap_job.station[0],ap_job.station[1],ap_job.station[2],ap_job.station[3],ap_job.station[4],ap_job.station[5],
        ap_report_scope(&ap_job),ap_job.channel,ap_job.seconds,ap_status.state,
        (unsigned long)ap_status.elapsed_ms,(unsigned long)ap_status.accepted_frames,(long)ap_status.error_code,ap_status.minimum_heap_kib,
        ap_job.channel<=11,ap_rx.state,(unsigned long)ap_rx.observed_frames,(unsigned long)ap_rx.target_beacons,
        (unsigned long)ap_rx.matching_frames,ap_rx.last_match_rssi,(long)ap_rx.error_code,ap_rx.minimum_heap_kib);
    ap_save_error=n>0&&(size_t)n<capacity?ESP_OK:ESP_ERR_INVALID_SIZE;
    size_t used=n>0?(size_t)n:0;
    if(ap_save_error==ESP_OK){
        int audit=snprintf(csv+used,capacity-used,"# Frozen client targets; observed address does not prove association\n"
            "target,client_index,client_mac\n");
        if(audit<0||(size_t)audit>=capacity-used)ap_save_error=ESP_ERR_INVALID_SIZE;
        else used+=(size_t)audit;
    }
    if(ap_save_error==ESP_OK&&!ap_csv_clients(csv,capacity,&used,1,&ap_active_stations))ap_save_error=ESP_ERR_INVALID_SIZE;
    if(ap_save_error==ESP_OK)ap_save_error=cerberus_storage_write_wifi_report(csv,used,name,sizeof(name));
    free(csv);
    if(ap_save_error==ESP_OK)snprintf(ap_message,sizeof(ap_message),"SAVED %s",name);
}
static esp_err_t ap_one_draw(void)
{
    /* The text renderer is ASCII-only. Draw the requested infinity mark as a
     * 15x7 bitmap beside the manual-Stop label. */
    static const uint8_t infinity_mark[14]={
        0x38,0x38,0x44,0x44,0x82,0x82,0x81,0x02,
        0x82,0x82,0x44,0x44,0x38,0x38,
    };
    char line[64],identifier[24];
    bool probe=ap_mode==CERBERUS_AP_PROBE_REQUEST;
    bool station_mode=ap_mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(ap_mode);
    bool announcement=ap_mode==CERBERUS_AP_CHANNEL_SWITCH||ap_mode==CERBERUS_AP_QUIET;
    bool clone=ap_mode==CERBERUS_AP_CLONE_SPAM;
    bool sae=ap_mode==CERBERUS_AP_SAE_COMMIT;
    int selected=wifi_targets_find(&wifi_selected_result);
    const wifi_target_entry_t *target=ap_source_target?ap_source_target:selected>=0?&wifi_targets[selected]:NULL;
    uint8_t count=target?wifi_targets_scope_count(target,ap_scope):
        ap_scope==WIFI_TARGET_PICK_STATIONS&&ap_has_station()?1:0;
    ESP_RETURN_ON_ERROR(draw_centered(66,ap_name(ap_mode),UI_TEXT,2),TAG,"AP title");
    ESP_RETURN_ON_ERROR(draw_centered(94,ap_ssid,UI_TEXT,1),TAG,"AP SSID");
    cerberus_privacy_format_wifi_identifier(ap_bound_bssid,privacy_addresses_masked(),identifier,sizeof(identifier));
    snprintf(line,sizeof(line),"%s / CH %u",identifier,ap_bound_channel);
    ESP_RETURN_ON_ERROR(draw_centered(112,line,UI_MUTED,1),TAG,"AP identity");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,134,296,36},
        !station_mode?(clone?"SCOPE: CLONE SELECTED SSID":"SCOPE: SELECTED AP"):ap_scope==WIFI_TARGET_BROADCAST?"SCOPE: AP BROADCAST":
        ap_scope==WIFI_TARGET_ALL_STATIONS?"SCOPE: ALL OBSERVED CLIENTS":"SCOPE: PICKED CLIENTS",1,
        station_mode,UI_WIDGET_THEME),TAG,"AP scope");
    if(sae)snprintf(line,sizeof(line),"GROUP 19 / NEW LOCAL SOURCE + POINT EACH FRAME");
    else if(probe)snprintf(line,sizeof(line),"SCANNED SSID / RANDOM LOCAL SOURCE MAC");
    else if(clone)snprintf(line,sizeof(line),"%s",ap_ssid[0]=='<'?"HIDDEN NAME UNKNOWN / BLANK CLONE":"SCANNED SSID / NEW LOCAL CLONE BSSID");
    else if(announcement)snprintf(line,sizeof(line),"%s",ap_mode==CERBERUS_AP_CHANNEL_SWITCH?"ANNOUNCE RANDOM OTHER CHANNEL":"QUIET INTERVAL / AP BEACON IDENTITY");
    else if(ap_scope==WIFI_TARGET_BROADCAST)snprintf(line,sizeof(line),"AP BROADCAST ADDRESS");
    else snprintf(line,sizeof(line),"%u %s / %u OBSERVED",count,
        ap_scope==WIFI_TARGET_ALL_STATIONS?"TARGET CLIENTS":"PICKED CLIENTS",target?target->observed_count:count);
    ESP_RETURN_ON_ERROR(draw_centered(182,line,UI_MUTED,1),TAG,"AP station");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,202,296,34},
        station_mode?"FIND / PICK CLIENTS":sae?"AUTH COMMIT / SELECTED AP BSSID":probe?"DESTINATION: THIS AP BSSID":clone?"READ AP BEACON / CLONE SSID":"READ AP BEACON BEFORE TRANSMITTING",1,station_mode,UI_WIDGET_THEME),TAG,"AP stations");
    ESP_RETURN_ON_ERROR(draw_centered(258,"RUNS UNTIL STOP",UI_TEXT,1),TAG,"AP manual stop");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_mono_bitmap(92,258,15,7,
        infinity_mark,UI_TEXT,UI_BACKGROUND),TAG,"AP infinity mark");
    const char *state=ap_wait_rx?"STARTING S3 RECEIVER":ap_pending?"WAITING FOR C5":ap_status.state==CERBERUS_TEST_RUNNING?"TRANSMITTING":
        ap_status.state==CERBERUS_TEST_STOPPING?"STOPPING":ap_status.state==CERBERUS_TEST_COMPLETE?"COMPLETE":
        ap_status.state==CERBERUS_TEST_CANCELLED?"CANCELLED":ap_status.state==CERBERUS_TEST_ERROR?"STOPPED WITH ERROR":"READY";
    snprintf(line,sizeof(line),"%s / %lu MS",state,(unsigned long)ap_status.elapsed_ms);
    ESP_RETURN_ON_ERROR(draw_centered(292,line,UI_TEXT,1),TAG,"AP state");
    snprintf(line,sizeof(line),"C5 ACCEPTED %lu / %lu FPS",
        (unsigned long)ap_status.accepted_frames,
        (unsigned long)ap_frames_per_second(ap_status.accepted_frames,ap_status.elapsed_ms));
    ESP_RETURN_ON_ERROR(draw_centered(312,line,UI_MUTED,1),TAG,"AP counts");
    if(ap_bound_channel<=11)snprintf(line,sizeof(line),"S3 M%lu B%lu / %lu FPS",
        (unsigned long)ap_rx.matching_frames,(unsigned long)ap_rx.target_beacons,
        (unsigned long)ap_frames_per_second(ap_rx.matching_frames,ap_rx.elapsed_ms));
    else snprintf(line,sizeof(line),"S3 CONTROLS / C5 5 GHZ / NO S3 RX");
    ESP_RETURN_ON_ERROR(draw_centered(334,line,UI_MUTED,1),TAG,"AP receiver");
    esp_err_t error=ap_save_error!=ESP_OK?ap_save_error:ap_error!=ESP_OK?ap_error:ap_status.error_code?ap_status.error_code:ap_rx.error_code;
    if(ap_status.state==CERBERUS_TEST_ERROR&&ap_status.mode==CERBERUS_TEST_WIFI_AP&&
       ap_status.name_length&&ap_status.error_code)
        snprintf(line,sizeof(line),"C5 %s: %s",cerberus_ap_stage_name(ap_status.name_length),
            esp_err_to_name(ap_status.error_code));
    else snprintf(line,sizeof(line),"%.48s",ap_save_error!=ESP_OK?esp_err_to_name(ap_save_error):ap_message[0]?ap_message:
        error!=ESP_OK?esp_err_to_name(error):"START TRANSMITS / LEAVING STOPS");
    ESP_RETURN_ON_ERROR(draw_centered(350,line,UI_MUTED,1),TAG,"AP note");
    ESP_RETURN_ON_ERROR(draw_small_button(12,366,144,40,ap_one_busy()?"STOP":"START"),TAG,"AP start");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){164,366,144,40},"SAVE REPORT",1,
        !ap_one_busy()&&ap_job.job_id&&ap_status.job_id==ap_job.job_id,UI_WIDGET_THEME);
}
static void ap_one_tap(uint16_t x,uint16_t y)
{
    if(y>=366&&y<406){if(x>=12&&x<156){if(ap_one_busy())ap_one_stop();else ap_one_start();}else if(x>=164&&x<308)ap_one_save();return;}
    if(ap_one_busy()||x<12||x>=308)return;
    if(y>=134&&y<170&&(ap_mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(ap_mode))){
        ap_clear_result();
        ap_scope=ap_mode==CERBERUS_AP_DEAUTH?
            (ap_scope==WIFI_TARGET_BROADCAST?WIFI_TARGET_ALL_STATIONS:
             ap_scope==WIFI_TARGET_ALL_STATIONS?WIFI_TARGET_PICK_STATIONS:WIFI_TARGET_BROADCAST):
            ap_scope==WIFI_TARGET_ALL_STATIONS?WIFI_TARGET_PICK_STATIONS:WIFI_TARGET_ALL_STATIONS;
        int selected=wifi_targets_find(&wifi_selected_result);
        if(selected<0&&wifi_selection_valid&&wifi_targets_add(&wifi_selected_result,wifi_selected_index))
            selected=wifi_targets_find(&wifi_selected_result);
        if(selected>=0)wifi_targets_set_scope(&wifi_targets[selected],ap_mode,ap_scope);
    }
    else if(y>=202&&y<236&&(ap_mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(ap_mode))){ap_clear_result();ap_station_selecting=true;station_enter();wifi_mode=UI_WIFI_MODE_STATIONS;}
}

#include "wifi_ap_batch_ui.h"
