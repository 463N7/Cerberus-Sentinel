/* Private to wifi_scanner.c. Reuses its scan worker, RX callback and locks.
 * scanner_mutex reserves radio ownership; defense_capture_mux protects the
 * bounded RX table. Lock ordering is scanner_mutex then capture mux. */
static cerberus_wifi_station_request_t station_request;
static cerberus_wifi_station_table_t station_table;
static bool station_reserved,station_receiving,station_stop;
static uint32_t station_started,station_contact;
static bool another_radio_operation_is_active(void);
static bool wifi_connection_is_active_locked(void);
static uint32_t station_now(void){return (uint32_t)(esp_timer_get_time()/1000);}
static uint16_t station_elapsed(void){uint32_t n=station_now()-station_started;return n>UINT16_MAX?UINT16_MAX:(uint16_t)n;}
#include "wifi_station_evidence.h"

/* Radio-changing commands share the SPI owner task. No TX lock is acquired here. */
static esp_err_t ap_target_snapshot(const cerberus_wifi_ap_job_t *r,uint8_t *ssid,uint8_t *length,bool *short_eapol)
{
    if(!cerberus_wifi_ap_job_valid(r))return ESP_ERR_INVALID_ARG;
    if(!scanner_mutex)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    bool valid=scanner_ready&&!station_reserved&&!atomic_load_explicit(&mac_radio_reserved,memory_order_acquire)&&
        !atomic_load_explicit(&csi_radio_reserved,memory_order_acquire)&&
        !wifi_connection_is_active_locked()&&
        defense_status.state!=CERBERUS_WIFI_DEFENSE_RUNNING&&scan_status.state==CERBERUS_WIFI_SCAN_COMPLETE&&
        r->generation==scan_status.generation&&r->index<scan_status.stored_count&&
        r->channel==scan_results[r->index].primary&&!memcmp(r->bssid,scan_results[r->index].bssid,6);
    if(valid&&r->scope==CERBERUS_AP_ONE_STATION){
        valid=false;
        portENTER_CRITICAL(&defense_capture_mux);
        valid=station_evidence_valid(r);
        portEXIT_CRITICAL(&defense_capture_mux);
    }
    if(valid&&ssid&&length){
        *length=(uint8_t)strnlen((const char*)scan_results[r->index].ssid,32);
        memcpy(ssid,scan_results[r->index].ssid,*length);
    }
    if(valid&&short_eapol){
        wifi_auth_mode_t auth=scan_results[r->index].authmode;
        *short_eapol=auth==WIFI_AUTH_WPA3_PSK||auth==WIFI_AUTH_WPA2_WPA3_PSK||auth==WIFI_AUTH_WPA3_ENTERPRISE||
            auth==WIFI_AUTH_WPA2_WPA3_ENTERPRISE||auth==WIFI_AUTH_WPA3_ENT_192||auth==WIFI_AUTH_WAPI_PSK;
    }
    xSemaphoreGive(scanner_mutex);return valid?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t cerberus_wifi_ap_target_valid(const cerberus_wifi_ap_job_t *r)
{return ap_target_snapshot(r,NULL,NULL,NULL);}
esp_err_t cerberus_wifi_ap_target_ssid(const cerberus_wifi_ap_job_t *r,uint8_t ssid[32],uint8_t *length)
{return ssid&&length?ap_target_snapshot(r,ssid,length,NULL):ESP_ERR_INVALID_ARG;}
esp_err_t cerberus_wifi_ap_target_metadata(const cerberus_wifi_ap_job_t *r,cerberus_wifi_ap_metadata_t *out)
{return out?ap_target_snapshot(r,out->ssid,&out->ssid_length,&out->short_eapol):ESP_ERR_INVALID_ARG;}

bool cerberus_wifi_station_running(void)
{
    if(!scanner_mutex)return false;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    bool busy=station_reserved;
    xSemaphoreGive(scanner_mutex);
    return busy;
}
esp_err_t cerberus_wifi_station_start(const cerberus_wifi_station_request_t *r)
{
    if(!cerberus_wifi_station_request_valid(r))return ESP_ERR_INVALID_ARG;
    if(!scanner_mutex||cerberus_c5_test_running()||another_radio_operation_is_active())return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    portENTER_CRITICAL(&defense_capture_mux);
    bool duplicate=station_table.status.job_id==r->job_id;
    bool same=duplicate&&!memcmp(&station_request,r,sizeof(*r));
    if(same)station_contact=station_now();
    portEXIT_CRITICAL(&defense_capture_mux);
    if(duplicate){xSemaphoreGive(scanner_mutex);return same?ESP_OK:ESP_ERR_INVALID_ARG;}
    if(!scanner_ready||!scanner_task_handle||station_reserved||atomic_load_explicit(&mac_radio_reserved,memory_order_acquire) || atomic_load_explicit(&csi_radio_reserved,memory_order_acquire)||
        wifi_connection_is_active_locked()||
        scan_status.state!=CERBERUS_WIFI_SCAN_COMPLETE||defense_status.state==CERBERUS_WIFI_DEFENSE_RUNNING||
        r->generation!=scan_status.generation||r->index>=scan_status.stored_count||
        r->channel!=scan_results[r->index].primary||memcmp(r->bssid,scan_results[r->index].bssid,6)){
        xSemaphoreGive(scanner_mutex);return ESP_ERR_INVALID_STATE;
    }
    esp_err_t evidence_error=station_evidence_prepare(r);
    if(evidence_error!=ESP_OK){xSemaphoreGive(scanner_mutex);return evidence_error;}
    station_reserved=true;
    portENTER_CRITICAL(&defense_capture_mux);
    station_request=*r;memset(&station_table,0,sizeof(station_table));
    station_table.status.job_id=r->job_id;station_table.status.state=CERBERUS_STATION_RUNNING;
    station_stop=station_receiving=false;station_contact=station_started=station_now();
    portEXIT_CRITICAL(&defense_capture_mux);
    xSemaphoreGive(scanner_mutex);xTaskNotifyGive(scanner_task_handle);return ESP_OK;
}
esp_err_t cerberus_wifi_station_stop(uint16_t job_id)
{
    if(!job_id)return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&defense_capture_mux);
    bool match=job_id==station_table.status.job_id;
    if(match&&(station_table.status.state==CERBERUS_STATION_RUNNING||station_table.status.state==CERBERUS_STATION_STOPPING)){
        station_stop=true;station_table.status.state=CERBERUS_STATION_STOPPING;
    }
    portEXIT_CRITICAL(&defense_capture_mux);
    return match?ESP_OK:ESP_ERR_NOT_FOUND;
}
void cerberus_wifi_station_status(uint16_t lease_job_id,cerberus_wifi_station_status_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&defense_capture_mux);
    if(lease_job_id&&lease_job_id==station_table.status.job_id)station_contact=station_now();
    *out=station_table.status;
    if(out->state==CERBERUS_STATION_RUNNING||out->state==CERBERUS_STATION_STOPPING)out->elapsed_ms=station_elapsed();
    portEXIT_CRITICAL(&defense_capture_mux);
}
esp_err_t cerberus_wifi_station_result(const cerberus_wifi_station_query_t *q,cerberus_wifi_station_result_t *out)
{
    if(!q||!out)return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&defense_capture_mux);
    bool valid=q->job_id==station_table.status.job_id&&q->index<station_table.status.count;
    if(valid)*out=station_table.rows[q->index];
    portEXIT_CRITICAL(&defense_capture_mux);
    return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
static void station_receive(const wifi_promiscuous_pkt_t *packet)
{
    const wifi_pkt_rx_ctrl_t *rx=&packet->rx_ctrl;
    if(rx->rx_state||rx->sig_len<28)return;
    portENTER_CRITICAL(&defense_capture_mux);
    bool active=station_receiving&&rx->channel==station_request.channel;
    uint16_t job_id=station_request.job_id;uint8_t ap[6];memcpy(ap,station_request.bssid,6);
    portEXIT_CRITICAL(&defense_capture_mux);
    if(!active)return;
    cerberus_wifi_station_observation_t observation;
    bool matched=cerberus_wifi_station_decode(packet->payload,rx->sig_len-4U,ap,&observation);
    portENTER_CRITICAL(&defense_capture_mux);
    if(station_receiving&&job_id==station_request.job_id){
        cerberus_wifi_station_observe(&station_table,matched?&observation:NULL,rx->rssi,rx->channel,station_now()-station_started);
        if(matched)station_remember(observation.mac);
    }
    portEXIT_CRITICAL(&defense_capture_mux);
}
static bool station_should_stop(void)
{
    portENTER_CRITICAL(&defense_capture_mux);
    bool stop=station_stop||(uint32_t)(station_now()-station_contact)>5000U;
    portEXIT_CRITICAL(&defense_capture_mux);return stop;
}
static bool station_run(void)
{
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    bool reserved=station_reserved;
    xSemaphoreGive(scanner_mutex);
    if(!reserved)return false;
    portENTER_CRITICAL(&defense_capture_mux);
    cerberus_wifi_station_request_t request=station_request;
    bool pending=station_table.status.state==CERBERUS_STATION_RUNNING||station_table.status.state==CERBERUS_STATION_STOPPING;
    portEXIT_CRITICAL(&defense_capture_mux);
    if(!pending)return true; /* Cleanup failure keeps the radio reserved until reboot. */
    esp_err_t error=ESP_OK,cleanup=ESP_OK;
    bool enabled=false,cancelled=station_should_stop();
    if(!cancelled){
        wifi_promiscuous_filter_t filter={.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA};
        error=esp_wifi_set_band(request.channel<=14?WIFI_BAND_2G:WIFI_BAND_5G);
        if(error==ESP_OK)error=esp_wifi_set_channel(request.channel,WIFI_SECOND_CHAN_NONE);
        if(error==ESP_OK)error=esp_wifi_set_promiscuous_filter(&filter);
        if(error==ESP_OK){error=esp_wifi_set_promiscuous(true);enabled=error==ESP_OK;}
        portENTER_CRITICAL(&defense_capture_mux);station_receiving=enabled;portEXIT_CRITICAL(&defense_capture_mux);
    }
    while(error==ESP_OK&&!cancelled&&(uint32_t)(station_now()-station_started)<request.seconds*1000U){
        vTaskDelay(pdMS_TO_TICKS(50));cancelled=station_should_stop();
    }
    portENTER_CRITICAL(&defense_capture_mux);station_receiving=false;portEXIT_CRITICAL(&defense_capture_mux);
    if(enabled)cleanup=esp_wifi_set_promiscuous(false);
    esp_err_t restore=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    if(cleanup==ESP_OK)cleanup=restore;
    if(error==ESP_OK)error=cleanup;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    station_reserved=cleanup!=ESP_OK;
    portENTER_CRITICAL(&defense_capture_mux);
    station_table.status.elapsed_ms=station_elapsed();
    station_table.status.error_code=error;
    station_table.status.state=error!=ESP_OK?CERBERUS_STATION_ERROR:
        cancelled?CERBERUS_STATION_CANCELLED:CERBERUS_STATION_COMPLETE;
    portEXIT_CRITICAL(&defense_capture_mux);
    xSemaphoreGive(scanner_mutex);return true;
}
