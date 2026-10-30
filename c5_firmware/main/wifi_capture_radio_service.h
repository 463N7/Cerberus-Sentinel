/* Private to wifi_scanner.c: scanner mutex and existing worker own the radio. */

static const uint8_t capture_hop_channels[]={
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,
    36,40,44,48,52,56,60,64,100,104,108,112,116,
    120,124,128,132,136,140,144,149,153,157,161,165,
};
static bool capture_hopping,capture_restore_pending,capture_disable_pending;
static wifi_band_mode_t capture_previous_band_mode;
static uint8_t capture_previous_channel,capture_hop_index;
static wifi_second_chan_t capture_previous_second;
static uint32_t capture_next_hop_ms;
static esp_err_t capture_terminal_reason;

/* All radio helpers below run under scanner_mutex. Gate admission before
 * disabling RX; callbacks may finish their already admitted copy. No channel
 * changes are made until these callbacks drain. The ring stays live/pinned. */
static esp_err_t capture_hop_pause(void)
{
    cerberus_wifi_capture_service_set_channel(0U);
    if(capture_disable_pending){
        esp_err_t error=esp_wifi_set_promiscuous(false);
        if(error!=ESP_OK)return error;
        capture_disable_pending=false;
    }
    uint32_t started=(uint32_t)(esp_timer_get_time()/1000LL);
    while(cerberus_wifi_capture_service_inflight()){
        if((uint32_t)((esp_timer_get_time()/1000LL)-started)>=50U)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

static esp_err_t capture_hop_restore(void)
{
    esp_err_t error=capture_hop_pause();
    if(error!=ESP_OK)return error;
    if(capture_restore_pending){
        /* Restore the actual previous channel before restoring its mode.
         * AUTO permits selecting either band, including after a 5 GHz hop. */
        error=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        if(error==ESP_OK&&capture_previous_channel){
            error=esp_wifi_set_band(capture_previous_channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
            if(error==ESP_OK)error=esp_wifi_set_channel(capture_previous_channel,capture_previous_second);
        }
        if(error==ESP_OK)error=esp_wifi_set_band_mode(capture_previous_band_mode);
        if(error!=ESP_OK)return error;
        capture_restore_pending=false;
    }
    atomic_store_explicit(&capture_radio_reserved,false,memory_order_release);
    return ESP_OK;
}

static esp_err_t capture_hop_next(void)
{
    esp_err_t error=capture_hop_pause();
    if(error!=ESP_OK)return error;
    esp_err_t first_error=ESP_OK;
    for(size_t attempt=0;attempt<sizeof(capture_hop_channels);++attempt){
        uint8_t channel=capture_hop_channels[capture_hop_index];
        capture_hop_index=(uint8_t)((capture_hop_index+1U)%sizeof(capture_hop_channels));
        error=esp_wifi_set_band(channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
        if(error==ESP_OK)error=esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
        if(error!=ESP_OK){
            if(first_error==ESP_OK)first_error=error;
            continue; /* Country/SDK constraints are authoritative. */
        }
        capture_disable_pending=true; /* Also clean up a failed enable. */
        error=esp_wifi_set_promiscuous(true);
        if(error!=ESP_OK)return error;
        cerberus_wifi_capture_service_set_channel(channel);
        capture_next_hop_ms=(uint32_t)(esp_timer_get_time()/1000LL)+
            CERBERUS_WIFI_CAPTURE_HOP_DWELL_MS;
        return ESP_OK;
    }
    return first_error==ESP_OK?ESP_ERR_NOT_SUPPORTED:first_error;
}

static esp_err_t capture_hop_finish(esp_err_t reason)
{
    if(cerberus_wifi_capture_service_active())capture_terminal_reason=reason;
    cerberus_wifi_capture_service_stop(capture_terminal_reason);
    esp_err_t cleanup=capture_hop_restore();
    if(cleanup!=ESP_OK)cerberus_wifi_capture_service_cleanup_error(cleanup);
    return cleanup;
}

static esp_err_t capture_fixed_finish(esp_err_t reason)
{
    if(cerberus_wifi_capture_service_active())capture_terminal_reason=reason;
    cerberus_wifi_capture_service_stop(capture_terminal_reason);
    esp_err_t cleanup=capture_hop_pause();
    if(cleanup==ESP_OK)
        atomic_store_explicit(&capture_radio_reserved,false,memory_order_release);
    else cerberus_wifi_capture_service_cleanup_error(cleanup);
    return cleanup;
}

esp_err_t cerberus_wifi_capture_start(const cerberus_wifi_capture_start_request_t *request)
{
    if(!request)return ESP_ERR_INVALID_ARG;
    if(!scanner_mutex||!scanner_task_handle)return ESP_ERR_INVALID_STATE;
    if(atomic_load_explicit(&mac_radio_reserved,memory_order_acquire))return ESP_ERR_INVALID_STATE;
    esp_err_t preempt_error=cerberus_wifi_channel_survey_preempt();
    if(preempt_error!=ESP_OK)return preempt_error;
    if(cerberus_c5_test_running())return ESP_ERR_INVALID_STATE;
    if(!atomic_load_explicit(&capture_radio_reserved,memory_order_acquire)&&
       another_radio_operation_is_active())return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    if(atomic_load_explicit(&capture_radio_reserved,memory_order_acquire)){
        bool same=cerberus_wifi_capture_service_same_request(request)&&
            cerberus_wifi_capture_service_active();
        xSemaphoreGive(scanner_mutex);
        return same?ESP_OK:ESP_ERR_INVALID_STATE;
    }
    if(!scanner_ready||station_reserved||atomic_load_explicit(&mac_radio_reserved,memory_order_acquire) || atomic_load_explicit(&csi_radio_reserved,memory_order_acquire)||
       scan_status.state==CERBERUS_WIFI_SCAN_RUNNING||
       defense_status.state==CERBERUS_WIFI_DEFENSE_RUNNING||wifi_connection_is_active_locked()){
        xSemaphoreGive(scanner_mutex);return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err=cerberus_wifi_capture_service_start(request);
    if(err!=ESP_OK){xSemaphoreGive(scanner_mutex);return err;}
    atomic_store_explicit(&capture_radio_reserved,true,memory_order_release);
    capture_hopping=(request->flags&CERBERUS_WIFI_CAPTURE_FLAG_HOP)!=0U;
    capture_terminal_reason=ESP_OK;
    capture_restore_pending=false;
    capture_disable_pending=false;
    wifi_promiscuous_filter_t filter={.filter_mask=0U};
    if(request->mode_mask&CERBERUS_WIFI_CAPTURE_MODE_RAW)
        filter.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT|
            WIFI_PROMIS_FILTER_MASK_CTRL|WIFI_PROMIS_FILTER_MASK_DATA;
    else{
        if(request->mode_mask&(CERBERUS_WIFI_CAPTURE_MODE_PROBE|
            CERBERUS_WIFI_CAPTURE_MODE_DEAUTH|
            CERBERUS_WIFI_CAPTURE_MODE_BEACON|
            CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT))filter.filter_mask|=WIFI_PROMIS_FILTER_MASK_MGMT;
        if(request->mode_mask&CERBERUS_WIFI_CAPTURE_MODE_EAPOL)
            filter.filter_mask|=WIFI_PROMIS_FILTER_MASK_DATA;
    }
    if(capture_hopping){
        err=esp_wifi_get_band_mode(&capture_previous_band_mode);
        if(err==ESP_OK)err=esp_wifi_get_channel(&capture_previous_channel,&capture_previous_second);
        if(err==ESP_OK){
            capture_restore_pending=true;
            capture_disable_pending=true;
            err=capture_hop_pause();
        }
        if(err==ESP_OK)err=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        if(err==ESP_OK)err=esp_wifi_set_promiscuous_filter(&filter);
        capture_hop_index=0U;
        if(err==ESP_OK)err=capture_hop_next();
        if(err==ESP_OK)cerberus_wifi_capture_service_set_deadline(
            (uint32_t)(esp_timer_get_time()/1000LL)+request->lease_ms);
        else (void)capture_hop_finish(err);
        xSemaphoreGive(scanner_mutex);
        if(err==ESP_OK||atomic_load_explicit(&capture_radio_reserved,memory_order_acquire))
            xTaskNotifyGive(scanner_task_handle);
        return err;
    }
    err=esp_wifi_set_band(request->channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
    if(err==ESP_OK)err=esp_wifi_set_channel(request->channel,WIFI_SECOND_CHAN_NONE);
    if(err==ESP_OK)err=esp_wifi_set_promiscuous_filter(&filter);
    bool enable_attempted=err==ESP_OK;
    if(enable_attempted){
        capture_disable_pending=true;
        err=esp_wifi_set_promiscuous(true);
    }
    if(err==ESP_OK){
        cerberus_wifi_capture_service_set_deadline(
            (uint32_t)(esp_timer_get_time()/1000LL)+request->lease_ms);
    }else{
        esp_err_t release=capture_fixed_finish(err);
        if(release!=ESP_OK)cerberus_wifi_capture_service_set_deadline(
            (uint32_t)(esp_timer_get_time()/1000LL)+1000U);
    }
    xSemaphoreGive(scanner_mutex);
    if(err==ESP_OK||atomic_load_explicit(&capture_radio_reserved,memory_order_acquire))
        xTaskNotifyGive(scanner_task_handle);
    return err;
}

static esp_err_t wifi_capture_stop_internal(uint32_t generation,esp_err_t reason)
{
    if(!scanner_mutex||!generation)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    if(generation!=cerberus_wifi_capture_service_generation()){
        xSemaphoreGive(scanner_mutex);return ESP_ERR_INVALID_STATE;
    }
    if(!atomic_load_explicit(&capture_radio_reserved,memory_order_acquire)){
        cerberus_wifi_capture_service_confirm_stopped();
        xSemaphoreGive(scanner_mutex);return ESP_OK; /* lost reply retry */
    }
    if(capture_hopping){
        esp_err_t error=capture_hop_finish(reason);
        if(error==ESP_OK)cerberus_wifi_capture_service_confirm_stopped();
        xSemaphoreGive(scanner_mutex);
        return error;
    }
    /* Fixed reception keeps its old channel behavior, but shares the bounded
     * callback drain so STOPPED cannot race a late ring publication. */
    esp_err_t err=capture_fixed_finish(reason);
    if(err==ESP_OK)cerberus_wifi_capture_service_confirm_stopped();
    xSemaphoreGive(scanner_mutex);
    return err;
}

static void wifi_capture_worker_tick(void)
{
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    if(!atomic_load_explicit(&capture_radio_reserved,memory_order_acquire)){
        xSemaphoreGive(scanner_mutex);return;
    }
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000LL);
    uint32_t deadline=cerberus_wifi_capture_service_deadline_ms();
    bool expired=deadline&&(int32_t)(now-deadline)>=0;
    if(capture_hopping){
        if(!cerberus_wifi_capture_service_active()||expired)
            (void)capture_hop_finish(expired?ESP_ERR_TIMEOUT:capture_terminal_reason);
        else if((int32_t)(now-capture_next_hop_ms)>=0){
            esp_err_t error=capture_hop_next();
            if(error!=ESP_OK)(void)capture_hop_finish(error);
        }
        xSemaphoreGive(scanner_mutex);return;
    }
    bool cleanup_pending=!cerberus_wifi_capture_service_active();
    uint32_t generation=cerberus_wifi_capture_service_generation();
    xSemaphoreGive(scanner_mutex);
    if(expired||cleanup_pending)(void)wifi_capture_stop_internal(generation,
        expired?ESP_ERR_TIMEOUT:capture_terminal_reason);
}

esp_err_t cerberus_wifi_capture_stop(uint32_t generation)
{return wifi_capture_stop_internal(generation,ESP_OK);}

void cerberus_wifi_capture_status(cerberus_wifi_capture_status_payload_t *out)
{
    if(!out)return;
    if(!scanner_mutex){memset(out,0,sizeof(*out));out->error_code=ESP_ERR_INVALID_STATE;return;}
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    cerberus_wifi_capture_service_status(out);
    xSemaphoreGive(scanner_mutex);
}
