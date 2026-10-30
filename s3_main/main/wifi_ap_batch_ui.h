/* Main-task coordinator over the existing one-AP service. At most one C5 job
 * and one S3 receiver own RF. Results/targets are bounded PSRAM allocations. */
#include "storage_report_limits.h"
#define AP_BATCH_DWELL_SECONDS 5U
typedef struct {
    wifi_target_entry_t target;
    cerberus_wifi_ap_job_t job;
    cerberus_radio_test_status_t tx;
    cerberus_s3_rx_status_t rx;
    esp_err_t error;
    bool attempted;
    uint32_t visits,total_driver_frames,total_s3_matches,total_s3_beacons,total_elapsed_ms;
} ap_batch_result_t;
static ap_batch_result_t *ap_batch;
static cerberus_c5_wifi_result_t *ap_preflight_inventory;
static cerberus_wifi_scan_status_payload_t ap_preflight_scan;
static uint32_t ap_preflight_read;
static uint16_t ap_preflight_checked,ap_preflight_expected;
static uint8_t ap_batch_count,ap_batch_index,ap_batch_state;
static uint32_t ap_batch_completed;
static uint8_t ap_preflight_candidate,ap_preflight_client;
static bool ap_batch_stop_requested,ap_batch_preflighting,ap_preflight_matching,ap_preflight_ready;
static bool ap_busy(void){return ap_batch_active||ap_one_busy();}
static uint32_t ap_batch_add(uint32_t left,uint32_t right)
{return UINT32_MAX-left<right?UINT32_MAX:left+right;}
static void ap_batch_release(void)
{
    free(ap_preflight_inventory);ap_preflight_inventory=NULL;
    free(ap_batch);ap_batch=NULL;ap_batch_count=ap_batch_index=ap_batch_completed=ap_batch_state=0;
    ap_preflight_read=0;ap_preflight_checked=ap_preflight_expected=0;
    ap_preflight_candidate=ap_preflight_client=0;
    ap_batch_stop_requested=ap_batch_preflighting=ap_preflight_ready=ap_preflight_matching=false;
    ap_source_target=NULL;
}
static void ap_choose(uint8_t mode)
{
    if(ap_busy()||ap_mode==mode)return;
    ap_batch_release();ap_one_choose(mode);
}
static void ap_focus_target(const wifi_target_entry_t *target)
{
    wifi_selected_result=target->ap;wifi_selected_index=target->index;wifi_selection_valid=true;
    station_selected=target->station;station_selected_valid=target->station_valid;
    memcpy(station_selected_ap,target->ap.bssid,6);
    ap_scope=wifi_targets_scope(target,ap_mode);
}
static bool ap_same_snapshot(const cerberus_c5_wifi_result_t *a,const cerberus_c5_wifi_result_t *b)
{return a->channel==b->channel&&!memcmp(a->bssid,b->bssid,6)&&!strcmp(a->ssid,b->ssid)&&a->security==b->security;}
static void ap_batch_start_next(void);
static void ap_batch_preflight_fail(esp_err_t error)
{
    ap_error=error;ap_batch_state=CERBERUS_TEST_ERROR;
    ap_batch_active=ap_batch_preflighting=false;
    if(ap_batch_index<ap_batch_count){
        ap_batch[ap_batch_index].error=error;
        snprintf(ap_message,sizeof(ap_message),"NETWORK %u / RESELECT OR REFRESH STATIONS",ap_batch_index+1);
    }
    free(ap_preflight_inventory);ap_preflight_inventory=NULL;
}
/* One SPI transaction per UI poll. The whole frozen queue is validated before
 * either radio starts, including every chosen client on each network. */
static void ap_batch_preflight_step(void)
{
    if(ap_preflight_ready){
        ap_batch_preflighting=ap_preflight_ready=false;
        ap_batch_index=0;ap_batch_start_next();return;
    }
    if(ap_batch_index>=ap_batch_count){
        cerberus_wifi_scan_status_payload_t end;
        esp_err_t error=c5_spi_link_get_wifi_scan_status(&end);
        if(error!=ESP_OK||end.state!=ap_preflight_scan.state||
           end.generation!=ap_preflight_scan.generation||end.stored_count!=ap_preflight_scan.stored_count){
            ap_batch_preflight_fail(error!=ESP_OK?error:ESP_ERR_INVALID_STATE);return;
        }
        free(ap_preflight_inventory);ap_preflight_inventory=NULL;
        ap_preflight_ready=true;return;
    }
    ap_batch_result_t *result=&ap_batch[ap_batch_index];
    if(ap_preflight_matching){
        for(;ap_preflight_candidate<=ap_preflight_scan.stored_count;){
            unsigned attempt=ap_preflight_candidate++;
            unsigned index=attempt?attempt-1:result->target.index;
            if(index>=ap_preflight_scan.stored_count||(attempt&&index==result->target.index))continue;
            if(!(ap_preflight_read&(UINT32_C(1)<<index))){
                esp_err_t error=c5_spi_link_get_wifi_scan_result(ap_preflight_scan.generation,
                    (uint8_t)index,&ap_preflight_inventory[index]);
                if(error!=ESP_OK){ap_batch_preflight_fail(error);return;}
                ap_preflight_read|=UINT32_C(1)<<index;
                /* End this poll after one SPI read, even if this AP differs. */
                if(!ap_same_snapshot(&result->target.ap,&ap_preflight_inventory[index]))return;
            }else if(!ap_same_snapshot(&result->target.ap,&ap_preflight_inventory[index]))continue;
            result->target.index=(uint8_t)index;
            ap_preflight_matching=false;ap_preflight_client=0;return;
        }
        ap_batch_preflight_fail(ESP_ERR_NOT_FOUND);return;
    }
    uint8_t scope=wifi_targets_scope(&result->target,ap_mode);
    cerberus_wifi_ap_station_set_t stations;
    if(!wifi_targets_station_set(&result->target,scope,&stations)){
        ap_batch_preflight_fail(ESP_ERR_INVALID_STATE);return;
    }
    if(stations.count>1&&c5_status.firmware_version<42){
        ap_batch_preflight_fail(ESP_ERR_NOT_SUPPORTED);return;
    }
    unsigned total=stations.count?stations.count:1;
    if(ap_preflight_client<total){
        cerberus_wifi_ap_job_t request={.job_id=1,.generation=ap_preflight_scan.generation,
            .index=result->target.index,.channel=result->target.ap.channel,.seconds=AP_BATCH_DWELL_SECONDS,
            .scope=stations.count?CERBERUS_AP_ONE_STATION:CERBERUS_AP_BROADCAST,.mode=ap_mode};
        memcpy(request.bssid,result->target.ap.bssid,6);
        if(ap_mode==CERBERUS_AP_CLONE_SPAM)
            memcpy(request.station,result->target.clone_mac,6);
        else if(stations.count)memcpy(request.station,stations.mac[ap_preflight_client],6);
        esp_err_t error=cerberus_wifi_ap_job_valid(&request)?
            c5_spi_link_ap_validate(&request):ESP_ERR_INVALID_ARG;
        if(error!=ESP_OK){ap_batch_preflight_fail(error);return;}
        ++ap_preflight_client;++ap_preflight_checked;
        return;
    }
    ++ap_batch_index;ap_preflight_candidate=ap_preflight_client=0;ap_preflight_matching=true;
}
static void ap_batch_start_next(void)
{
    ap_clear_result();ap_source_target=&ap_batch[ap_batch_index].target;
    ap_focus_target(ap_source_target);
    ap_one_start_with_seconds(AP_BATCH_DWELL_SECONDS);
    ap_batch[ap_batch_index].attempted=ap_job.job_id!=0;
}
static void ap_start(void)
{
    if(ap_busy())return;
    ap_batch_release();
    if(wifi_target_count<=1){ap_one_start();return;}
    ap_clear_result();ap_batch_count=wifi_target_count;
    ap_batch=CERBERUS_UI_JOB_ALLOC(sizeof(*ap_batch)*ap_batch_count);
    if(!ap_batch){ap_batch_count=0;ap_error=ESP_ERR_NO_MEM;return;}
    for(unsigned i=0;i<ap_batch_count;++i)ap_batch[i].target=wifi_targets[i];
    if(ap_mode==CERBERUS_AP_CLONE_SPAM)for(unsigned i=0;i<ap_batch_count;++i){
        bool valid=false;
        for(unsigned attempt=0;attempt<8&&!valid;++attempt){
            uint8_t *mac=ap_batch[i].target.clone_mac;
            ap_clone_identity_new(ap_batch[i].target.ap.bssid,mac);
            valid=ap_clone_identity_valid(mac,ap_batch[i].target.ap.bssid);
            for(unsigned j=0;j<ap_batch_count&&valid;++j)
                if(!memcmp(mac,ap_batch[j].target.ap.bssid,6))valid=false;
            for(unsigned j=0;j<i&&valid;++j)
                if(!memcmp(mac,ap_batch[j].target.clone_mac,6))valid=false;
        }
        if(!valid){
            ap_batch_preflight_fail(ESP_ERR_INVALID_STATE);
            snprintf(ap_message,sizeof(ap_message),"COULD NOT ALLOCATE UNIQUE CLONE IDENTITY");
            return;
        }
    }
    unsigned required_revision=ap_mode==CERBERUS_AP_SAE_COMMIT?44U:41U;
    if(!c5_online||c5_status.firmware_version<required_revision||!(c5_status.capabilities&ap_capability(ap_mode))){
        ap_batch_preflight_fail(ESP_ERR_NOT_SUPPORTED);
        snprintf(ap_message,sizeof(ap_message),"ONLINE C5 REVISION %u+ REQUIRED FOR BATCH",required_revision);return;
    }
    ap_error=c5_spi_link_get_wifi_scan_status(&ap_preflight_scan);
    if(ap_error!=ESP_OK||ap_preflight_scan.state!=CERBERUS_WIFI_SCAN_COMPLETE||
       ap_preflight_scan.stored_count>CERBERUS_WIFI_SCAN_MAX_RESULTS){
        ap_batch_preflight_fail(ap_error!=ESP_OK?ap_error:ESP_ERR_INVALID_STATE);return;
    }
    ap_preflight_inventory=CERBERUS_UI_JOB_ALLOC(sizeof(*ap_preflight_inventory)*CERBERUS_WIFI_SCAN_MAX_RESULTS);
    if(!ap_preflight_inventory){ap_batch_preflight_fail(ESP_ERR_NO_MEM);return;}
    for(unsigned i=0;i<ap_batch_count;++i){
        uint8_t scope=wifi_targets_scope(&ap_batch[i].target,ap_mode);
        unsigned n=wifi_targets_scope_count(&ap_batch[i].target,scope);
        ap_preflight_expected+=(uint16_t)(n?n:1);
    }
    ap_batch_active=ap_batch_preflighting=ap_preflight_matching=true;
    ap_batch_state=CERBERUS_TEST_RUNNING;
}
static void ap_stop(void)
{
    if(ap_batch_active)ap_batch_stop_requested=true;
    if(ap_batch_preflighting)return;
    ap_one_stop();
}
static void ap_poll(uint32_t now,bool visible)
{
    if(ap_batch_active&&!visible)ap_batch_stop_requested=true;
    if(ap_batch_preflighting){
        if(ap_batch_stop_requested){
            ap_batch_active=ap_batch_preflighting=false;ap_batch_state=CERBERUS_TEST_CANCELLED;
            free(ap_preflight_inventory);ap_preflight_inventory=NULL;
            snprintf(ap_message,sizeof(ap_message),"CANCELLED BEFORE TRANSMISSION");return;
        }
        ap_batch_preflight_step();ap_last_poll=now;return;
    }
    ap_one_poll(now,visible);
    if(!ap_batch_active||ap_one_busy())return;
    ap_last_poll=now; /* Also repaint immediate start failures and final Stop. */
    ap_batch_result_t *r=&ap_batch[ap_batch_index];
    r->job=ap_job;r->tx=ap_status;r->rx=ap_rx;
    r->error=ap_error?ap_error:ap_status.error_code?ap_status.error_code:ap_rx.error_code;
    if(r->attempted){
        r->total_driver_frames=ap_batch_add(r->total_driver_frames,r->tx.accepted_frames);
        r->total_s3_matches=ap_batch_add(r->total_s3_matches,r->rx.matching_frames);
        r->total_s3_beacons=ap_batch_add(r->total_s3_beacons,r->rx.target_beacons);
        r->total_elapsed_ms=ap_batch_add(r->total_elapsed_ms,r->tx.elapsed_ms);
    }
    bool success=r->attempted&&ap_status.state==CERBERUS_TEST_COMPLETE&&!r->error;
    if(success){
        r->visits=ap_batch_add(r->visits,1);
        ap_batch_completed=ap_batch_add(ap_batch_completed,1);
    }
    if(ap_batch_stop_requested||!success){
        ap_batch_state=ap_batch_stop_requested||ap_status.state==CERBERUS_TEST_CANCELLED?CERBERUS_TEST_CANCELLED:CERBERUS_TEST_ERROR;
        ap_batch_active=false;
        if(!r->error&&!success&&!ap_batch_stop_requested)r->error=ESP_ERR_INVALID_STATE;
        return;
    }
    ap_batch_index=(uint8_t)((ap_batch_index+1U)%ap_batch_count);
    /* Next radio start is permitted only after the prior RX cleanup is final. */
    ap_batch_start_next();
}
static void ap_save(void)
{
    if(ap_busy())return;
    if(!ap_batch){ap_one_save();return;}
    size_t clients=0;
    for(unsigned i=0;i<ap_batch_count;++i)
        clients+=wifi_targets_scope_count(&ap_batch[i].target,wifi_targets_scope(&ap_batch[i].target,ap_mode));
    const size_t capacity=2048U+(size_t)ap_batch_count*448U+clients*32U;
    if(capacity>CERBERUS_STORAGE_WIFI_REPORT_MAX_BYTES){ap_save_error=ESP_ERR_INVALID_SIZE;return;}
    char *csv=CERBERUS_UI_JOB_ALLOC(capacity);
    if(!csv){ap_save_error=ESP_ERR_NO_MEM;return;}
    int header=snprintf(csv,capacity,
        "# Cerberus AP round robin; Stop ends session; dwell=5s; state=%u; targets=%u; completed=%lu; driver/RX counts do not establish client effects\n"
        "target,attempted,mode,bssid,station_or_clone_bssid,scope,profile,client_count,channel,dwell_seconds,job,c5_state,c5_elapsed_ms,c5_driver_frames,s3_state,s3_beacons,s3_matches,error,visits,total_c5_frames,total_s3_matches,total_s3_beacons,total_c5_elapsed_ms\n",
        ap_batch_state,ap_batch_count,(unsigned long)ap_batch_completed);
    ap_save_error=header>=0&&(size_t)header<capacity?ESP_OK:ESP_ERR_INVALID_SIZE;
    size_t used=ap_save_error==ESP_OK?(size_t)header:0;
    for(unsigned i=0;i<ap_batch_count&&ap_save_error==ESP_OK;++i){
        const ap_batch_result_t *r=&ap_batch[i];const uint8_t *b=r->target.ap.bssid;
        uint8_t profile=wifi_targets_scope(&r->target,ap_mode);
        cerberus_wifi_ap_station_set_t stations;
        (void)wifi_targets_station_set(&r->target,profile,&stations);
        cerberus_wifi_ap_job_t job=r->job;
        if(!r->attempted){job.mode=ap_mode;job.scope=stations.count>1?CERBERUS_AP_STATION_LIST:
            stations.count?CERBERUS_AP_ONE_STATION:CERBERUS_AP_BROADCAST;}
        const uint8_t zero[6]={0};
        const uint8_t *s=ap_mode==CERBERUS_AP_CLONE_SPAM?
            r->attempted?r->job.station:r->target.clone_mac:
            stations.count==1?stations.mac[0]:zero;
        int n=snprintf(csv+used,capacity-used,
            "%u,%u,%s,%02X:%02X:%02X:%02X:%02X:%02X,%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%u,%u,%u,%u,%u,%lu,%lu,%u,%lu,%lu,%ld,%lu,%lu,%lu,%lu,%lu\n",
            i+1,r->attempted,ap_report_mode(ap_mode),b[0],b[1],b[2],b[3],b[4],b[5],
            s[0],s[1],s[2],s[3],s[4],s[5],ap_report_scope(&job),
            profile==WIFI_TARGET_ALL_STATIONS?"all_observed":profile==WIFI_TARGET_PICK_STATIONS?"picked":"broadcast",
            stations.count,r->target.ap.channel,AP_BATCH_DWELL_SECONDS,r->job.job_id,r->tx.state,(unsigned long)r->tx.elapsed_ms,
            (unsigned long)r->tx.accepted_frames,r->rx.state,(unsigned long)r->rx.target_beacons,(unsigned long)r->rx.matching_frames,(long)r->error,
            (unsigned long)r->visits,(unsigned long)r->total_driver_frames,
            (unsigned long)r->total_s3_matches,(unsigned long)r->total_s3_beacons,
            (unsigned long)r->total_elapsed_ms);
        if(n<0||(size_t)n>=capacity-used){ap_save_error=ESP_ERR_INVALID_SIZE;break;}used+=(size_t)n;
    }
    if(ap_save_error==ESP_OK){
        int audit=snprintf(csv+used,capacity-used,
            "# Frozen client targets; observed address does not prove association\n"
            "target,client_index,client_mac\n");
        if(audit<0||(size_t)audit>=capacity-used)ap_save_error=ESP_ERR_INVALID_SIZE;
        else used+=(size_t)audit;
    }
    for(unsigned i=0;i<ap_batch_count&&ap_save_error==ESP_OK;++i){
        cerberus_wifi_ap_station_set_t stations;
        (void)wifi_targets_station_set(&ap_batch[i].target,
            wifi_targets_scope(&ap_batch[i].target,ap_mode),&stations);
        if(!ap_csv_clients(csv,capacity,&used,i+1,&stations))ap_save_error=ESP_ERR_INVALID_SIZE;
    }
    char name[32];if(ap_save_error==ESP_OK)ap_save_error=cerberus_storage_write_wifi_report(csv,used,name,sizeof(name));
    free(csv);if(ap_save_error==ESP_OK)snprintf(ap_message,sizeof(ap_message),"SAVED %s",name);
}
static esp_err_t ap_draw(void)
{
    ESP_RETURN_ON_ERROR(ap_one_draw(),TAG,"AP job");
    char line[52];
    if(ap_batch_count){
        if(ap_batch_preflighting)
            snprintf(line,sizeof(line),"CHECKING %u/%u CLIENTS / NETWORK %u/%u",
                ap_preflight_checked,ap_preflight_expected,ap_batch_index+1,ap_batch_count);
        else {
            uint32_t total=0;
            for(unsigned i=0;i<ap_batch_count;++i)
                total=ap_batch_add(total,ap_batch[i].total_driver_frames);
            snprintf(line,sizeof(line),"AP %u/%u LOOPING / V%lu / C5 %lu",
                ap_batch_index<ap_batch_count?ap_batch_index+1:ap_batch_count,ap_batch_count,
                (unsigned long)ap_batch_completed,(unsigned long)total);
        }
        ESP_RETURN_ON_ERROR(draw_centered(122,line,UI_MUTED,1),TAG,"AP batch");
        if(ap_batch_preflighting){
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0,290,320,20,UI_BACKGROUND),TAG,"AP preflight clear");
            ESP_RETURN_ON_ERROR(draw_centered(292,"VALIDATING TARGETS BEFORE RF",UI_TEXT,1),TAG,"AP preflight");
            return draw_small_button(12,366,144,40,"STOP");
        }
        if(!ap_batch_active){
            const char *state=ap_batch_state==CERBERUS_TEST_CANCELLED?
                "ROUND ROBIN STOPPED":"BATCH STOPPED WITH ERROR";
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0,290,320,20,UI_BACKGROUND),TAG,"AP batch state clear");
            ESP_RETURN_ON_ERROR(draw_centered(292,state,UI_TEXT,1),TAG,"AP batch state");
        }
        if(!ap_busy())return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){164,366,144,40},"SAVE REPORT",1,true,UI_WIDGET_THEME);
    }else if(wifi_target_count>1){
        int focus=wifi_targets_find(&wifi_selected_result);
        snprintf(line,sizeof(line),"AP %u/%u / TAP NAME / RUN UNTIL STOP",
            focus<0?1U:(unsigned)focus+1,wifi_target_count);
        ESP_RETURN_ON_ERROR(draw_centered(122,line,UI_MUTED,1),TAG,"AP targets");
    }
    return ESP_OK;
}
static void ap_tap(uint16_t x,uint16_t y)
{
    if(y>=366&&y<406){if(x>=12&&x<156){if(ap_busy())ap_stop();else ap_start();}else if(x>=164&&x<308)ap_save();return;}
    if(ap_busy())return;
    if(x>=12&&x<308&&y>=84&&y<111&&wifi_target_count>1){
        int focus=wifi_targets_find(&wifi_selected_result);unsigned next=focus<0?0:((unsigned)focus+1)%wifi_target_count;
        ap_batch_release();ap_clear_result();ap_focus_target(&wifi_targets[next]);ap_enter();return;
    }
    if(x>=12&&x<308&&((y>=134&&y<170&&(ap_mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(ap_mode)))||
       (y>=202&&y<236&&(ap_mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(ap_mode)))||
       (y>=246&&y<280)))ap_batch_release();
    ap_one_tap(x,y);
}
