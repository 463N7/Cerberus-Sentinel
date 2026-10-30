/* Private to wifi_scanner.c. All SDK calls use the existing scanner worker;
 * SPI commands only reserve, inspect, cancel or renew an exact monotonic job. */
static const uint8_t remote_id_5g_channels[] = {
    36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,
    132,136,140,144,149,153,157,161,165,169,173,177,
};
static cerberus_remote_id_start_request_t remote_id_request;
static cerberus_remote_id_status_payload_t remote_id_status;
static cerberus_remote_id_channel_plan_t remote_id_plan;
static uint8_t remote_id_channels[CERBERUS_REMOTE_ID_CHANNEL_PLAN_MAX];
static uint8_t remote_id_channel_count,remote_id_probe_index,remote_id_accepted_count;
static uint8_t remote_id_next_background,remote_id_terminal_state;
static wifi_country_t remote_id_cached_country;
static bool remote_id_cache_valid,remote_id_saved,remote_id_disable_pending,remote_id_plan_ready;
static wifi_band_mode_t remote_id_previous_mode;
static uint8_t remote_id_previous_channel;
static wifi_second_chan_t remote_id_previous_second;
static wifi_promiscuous_filter_t remote_id_previous_filter;
static uint64_t remote_id_epoch,remote_id_lease_started_us,remote_id_window_started_us;
static uint64_t remote_id_follow_deadline_us;
static bool remote_id_follow_candidate,remote_id_follow_bound;

static bool remote_id_country_equal(const wifi_country_t *a,const wifi_country_t *b)
{
    return !memcmp(a->cc,b->cc,sizeof(a->cc)) && a->schan==b->schan && a->nchan==b->nchan &&
        a->policy==b->policy && a->wifi_5g_channel_mask==b->wifi_5g_channel_mask;
}

static void remote_id_rejected(uint64_t id,esp_err_t error,cerberus_remote_id_status_payload_t *out)
{
    memset(out,0,sizeof(*out));out->job_id=id;out->state=CERBERUS_REMOTE_ID_STATE_REJECTED;
    out->error_code=error;
}

static bool remote_id_valid_request(const cerberus_remote_id_start_request_t *request)
{
    if(!request||!request->job_id||
       request->duration_ms<CERBERUS_REMOTE_ID_WINDOW_MIN_MS||request->duration_ms>CERBERUS_REMOTE_ID_WINDOW_MAX_MS||
       request->lease_ms<CERBERUS_REMOTE_ID_LEASE_MIN_MS||request->lease_ms>CERBERUS_REMOTE_ID_LEASE_MAX_MS||
       request->dwell_ms<CERBERUS_REMOTE_ID_DWELL_MIN_MS||request->dwell_ms>CERBERUS_REMOTE_ID_DWELL_MAX_MS)return false;
    if(!request->flags){
        for(size_t i=0;i<sizeof(request->reserved);++i)if(request->reserved[i])return false;
        return true;
    }
    if(request->flags!=CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON&&
       request->flags!=CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN)return false;
    uint8_t any=0U;
    for(size_t i=0;i<sizeof(request->transmitter);++i)any|=request->transmitter[i];
    if(!any||(request->transmitter[0]&1U))return false;
    if(request->follow_channel<=14U)return true; /* Zero is explicit SEARCH. */
    for(size_t i=0;i<sizeof(remote_id_5g_channels);++i)
        if(request->follow_channel==remote_id_5g_channels[i])return true;
    return false;
}

static uint8_t remote_id_follow_transport(const cerberus_remote_id_start_request_t *request)
{
    return request->flags==CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON?
        SENTINEL_WATCH_TRANSPORT_WIFI_BEACON:SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
}

static void remote_id_status_locked(cerberus_remote_id_status_payload_t *out)
{
    *out=remote_id_status;out->flags=remote_id_request.flags?CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED:0U;
    if(remote_id_follow_bound)out->flags|=CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND;
    out->lease_remaining_ms=0U;
    bool owned=atomic_load_explicit(&remote_id_radio_reserved,memory_order_acquire);
    if(owned){
        out->flags|=CERBERUS_REMOTE_ID_FLAG_OWNED;
        int64_t now=esp_timer_get_time();
        if(now>=0&&(uint64_t)now>=remote_id_lease_started_us){
            uint64_t elapsed=((uint64_t)now-remote_id_lease_started_us)/1000U;
            if(elapsed<remote_id_request.lease_ms)out->lease_remaining_ms=(uint16_t)(remote_id_request.lease_ms-elapsed);
        }
    }
    portENTER_CRITICAL(&defense_capture_mux);
    if(owned&&out->state==CERBERUS_REMOTE_ID_STATE_RUNNING&&watch_capture_active&&watch_capture_channel){
        out->flags|=CERBERUS_REMOTE_ID_FLAG_RECEIVING;out->channel=watch_capture_channel;
    }else out->channel=0U;
    if(!owned&&out->state==CERBERUS_REMOTE_ID_STATE_COMPLETE&&
       scan_status.state!=CERBERUS_WIFI_SCAN_RUNNING&&
       !atomic_load_explicit(&mac_radio_reserved,memory_order_acquire)&&
       !atomic_load_explicit(&csi_radio_reserved,memory_order_acquire)&&
       !atomic_load_explicit(&capture_radio_reserved,memory_order_acquire)&&
       !atomic_load_explicit(&packet_monitor_radio_reserved,memory_order_acquire)&&
       !atomic_load_explicit(&channel_survey_radio_reserved,memory_order_acquire)&&!station_reserved&&
       watch_capture.capture_epoch==remote_id_epoch&&watch_capture.status.generation==out->generation&&
       watch_capture.status.state==CERBERUS_WATCH_COMPLETE)
        out->flags|=CERBERUS_REMOTE_ID_FLAG_DATA_READY;
    portEXIT_CRITICAL(&defense_capture_mux);
}

/* Caller holds scanner_mutex. Bind only the prior completed priority job's
 * actual collector, never an S3 age, claimed ID, stale index or ordinary scan.
 * Snapshot before replacing the retained request/status and collector epoch. */
static uint64_t remote_id_follow_proof_locked(const cerberus_remote_id_start_request_t *request,uint64_t now)
{
    if(!request->flags||!request->follow_channel)return 0U;
    cerberus_remote_id_status_payload_t prior;
    remote_id_status_locked(&prior);
    if(!(prior.flags&CERBERUS_REMOTE_ID_FLAG_DATA_READY))return 0U;
    uint64_t deadline=0U;
    portENTER_CRITICAL(&defense_capture_mux);
    if(remote_id_epoch&&watch_capture.capture_epoch==remote_id_epoch&&
       watch_capture.status.generation==prior.generation&&watch_capture.status.state==CERBERUS_WATCH_COMPLETE){
        for(uint8_t i=0;i<watch_capture.status.stored_count&&i<CERBERUS_WATCH_MAX_OBSERVATIONS;++i){
            const cerberus_watch_observation_t *row=&watch_capture.records[i];
            if(row->kind!=SENTINEL_WATCH_PROTOCOL_DRONE||row->transport!=remote_id_follow_transport(request)||
               row->address_type||row->channel!=request->follow_channel||
               memcmp(row->address,request->transmitter,6U)||!(watch_capture.qualifying_valid&(1U<<i)))continue;
            uint64_t source=watch_capture.qualifying_us[i];
            uint64_t limit=(uint64_t)CERBERUS_REMOTE_ID_FOLLOW_MAX_AGE_MS*1000U;
            if(source<=now&&now-source<limit&&source<=UINT64_MAX-limit)deadline=source+limit;
            break;
        }
    }
    portEXIT_CRITICAL(&defense_capture_mux);
    return deadline;
}

/* Before the first admitted RX only, expire pending proof into an explicit
 * generic SEARCH profile without resetting this job's elapsed finite budget. */
static bool remote_id_search_plan_locked(void)
{
    remote_id_follow_candidate=false;remote_id_follow_deadline_us=0U;
    bool valid=cerberus_remote_id_channel_plan_init(&remote_id_plan,remote_id_channels,
        remote_id_channel_count,remote_id_request.dwell_ms,remote_id_request.duration_ms,
        (uint32_t)(remote_id_window_started_us/1000U));
    if(valid)(void)cerberus_remote_id_channel_plan_seek_background(&remote_id_plan,remote_id_next_background);
    return valid;
}

/* Close admission before SDK disable; every admitted producer must leave
 * before retuning/restoring. Failed disable or drain retains ownership. */
static esp_err_t remote_id_pause_locked(void)
{
    portENTER_CRITICAL(&defense_capture_mux);
    watch_capture_active=false;watch_capture_channel=0U;watch_capture_deadline_us=0U;
    portEXIT_CRITICAL(&defense_capture_mux);
    remote_id_status.channel=0U;
    if(remote_id_disable_pending){
        esp_err_t error=esp_wifi_set_promiscuous(false);
        if(error!=ESP_OK)return error;
        remote_id_disable_pending=false;
    }
    int64_t started=esp_timer_get_time();
    for(;;){
        portENTER_CRITICAL(&defense_capture_mux);
        uint32_t inflight=watch_callback_inflight;
        portEXIT_CRITICAL(&defense_capture_mux);
        if(!inflight)return ESP_OK;
        int64_t now=esp_timer_get_time();
        if(started<0||now<started||now-started>=50000)return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void remote_id_end_locked(uint8_t terminal,esp_err_t error)
{
    int64_t now=esp_timer_get_time();
    if(remote_id_plan_ready&&now>=0&&(uint64_t)now>=remote_id_window_started_us){
        uint64_t elapsed=((uint64_t)now-remote_id_window_started_us)/1000U;
        remote_id_status.elapsed_ms=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
    }
    remote_id_terminal_state=terminal;
    remote_id_status.state=error==ESP_OK?CERBERUS_REMOTE_ID_STATE_STOPPING:CERBERUS_REMOTE_ID_STATE_ERROR;
    remote_id_status.error_code=error;
    portENTER_CRITICAL(&defense_capture_mux);
    watch_capture_active=false;watch_capture_channel=0U;
    portEXIT_CRITICAL(&defense_capture_mux);
    if(remote_id_plan_ready)remote_id_next_background=cerberus_remote_id_channel_plan_next_background(&remote_id_plan);
}

static void remote_id_cleanup_locked(void)
{
    esp_err_t error=remote_id_pause_locked();
    if(error==ESP_OK&&remote_id_saved){
        error=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        if(error==ESP_OK&&remote_id_previous_channel){
            error=esp_wifi_set_band(remote_id_previous_channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
            if(error==ESP_OK)error=esp_wifi_set_channel(remote_id_previous_channel,remote_id_previous_second);
        }
        if(error==ESP_OK)error=esp_wifi_set_band_mode(remote_id_previous_mode);
        if(error==ESP_OK)error=esp_wifi_set_promiscuous_filter(&remote_id_previous_filter);
        if(error==ESP_OK)remote_id_saved=false;
    }
    if(error!=ESP_OK){
        remote_id_status.error_code=error;remote_id_status.state=CERBERUS_REMOTE_ID_STATE_ERROR;
        remote_id_terminal_state=CERBERUS_REMOTE_ID_STATE_ERROR;return;
    }
    portENTER_CRITICAL(&defense_capture_mux);
    if(remote_id_epoch&&watch_capture.capture_epoch==remote_id_epoch)
        cerberus_watch_capture_finish(&watch_capture,
            remote_id_terminal_state==CERBERUS_REMOTE_ID_STATE_COMPLETE?CERBERUS_WATCH_COMPLETE:
            remote_id_terminal_state==CERBERUS_REMOTE_ID_STATE_STOPPED?CERBERUS_WATCH_IDLE:CERBERUS_WATCH_ERROR,
            (uint32_t)(remote_id_window_started_us/1000U)+remote_id_status.elapsed_ms);
    portEXIT_CRITICAL(&defense_capture_mux);
    remote_id_status.state=remote_id_terminal_state;
    atomic_store_explicit(&remote_id_radio_reserved,false,memory_order_release);
}

static esp_err_t remote_id_prepare_locked(void)
{
    bool promiscuous=false;wifi_country_t country={0};
    esp_err_t error=esp_wifi_get_promiscuous(&promiscuous);
    if(error!=ESP_OK)return error;
    if(promiscuous)return ESP_ERR_INVALID_STATE;
    error=remote_id_pause_locked();
    if(error!=ESP_OK)return error;
    error=esp_wifi_get_band_mode(&remote_id_previous_mode);
    if(error==ESP_OK)error=esp_wifi_get_channel(&remote_id_previous_channel,&remote_id_previous_second);
    if(error==ESP_OK)error=esp_wifi_get_promiscuous_filter(&remote_id_previous_filter);
    if(error!=ESP_OK)return error;
    remote_id_saved=true;
    error=esp_wifi_get_country(&country);
    if(error==ESP_OK)error=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    if(error!=ESP_OK)return error;
    wifi_promiscuous_filter_t filter={.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT};
    error=esp_wifi_set_promiscuous_filter(&filter);
    if(error!=ESP_OK)return error;
    if(remote_id_cache_valid&&remote_id_country_equal(&country,&remote_id_cached_country)){
        remote_id_probe_index=remote_id_channel_count;remote_id_accepted_count=remote_id_channel_count;
        return ESP_OK;
    }
    remote_id_cache_valid=false;remote_id_cached_country=country;
    remote_id_channel_count=remote_id_probe_index=remote_id_accepted_count=0U;
    if(!country.schan||country.schan>14U||!country.nchan||
       (unsigned)country.schan+country.nchan>15U)return ESP_ERR_INVALID_STATE;
    for(unsigned channel=country.schan;channel<(unsigned)country.schan+country.nchan;++channel)
        remote_id_channels[remote_id_channel_count++]=(uint8_t)channel;
    for(size_t i=0;i<sizeof(remote_id_5g_channels);++i){
        if(country.policy==WIFI_COUNTRY_POLICY_MANUAL&&country.wifi_5g_channel_mask&&
           !(country.wifi_5g_channel_mask&(UINT32_C(1)<<(i+1U))))continue;
        remote_id_channels[remote_id_channel_count++]=remote_id_5g_channels[i];
    }
    return ESP_OK;
}

static void remote_id_worker_tick(void)
{
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    if(!atomic_load_explicit(&remote_id_radio_reserved,memory_order_acquire)){
        xSemaphoreGive(scanner_mutex);return;
    }
    int64_t clock=esp_timer_get_time();
    uint64_t now=clock>=0?(uint64_t)clock:0U;
    if(remote_id_status.state==CERBERUS_REMOTE_ID_STATE_STARTING||remote_id_status.state==CERBERUS_REMOTE_ID_STATE_RUNNING){
        if(clock<0||now<remote_id_lease_started_us||now-remote_id_lease_started_us>=(uint64_t)remote_id_request.lease_ms*1000U)
            remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_ERROR,ESP_ERR_TIMEOUT);
        else if(remote_id_follow_bound&&now>=remote_id_follow_deadline_us)
            remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_COMPLETE,ESP_OK);
    }
    if(remote_id_status.state==CERBERUS_REMOTE_ID_STATE_STOPPING||remote_id_status.state==CERBERUS_REMOTE_ID_STATE_ERROR){
        remote_id_cleanup_locked();xSemaphoreGive(scanner_mutex);return;
    }
    esp_err_t error=ESP_OK;
    if(remote_id_status.state==CERBERUS_REMOTE_ID_STATE_STARTING){
        if(!remote_id_saved)error=remote_id_prepare_locked();
        if(error==ESP_OK&&remote_id_probe_index<remote_id_channel_count){
            uint8_t channel=remote_id_channels[remote_id_probe_index++];
            error=esp_wifi_set_band(channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
            if(error==ESP_OK)error=esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
            if(error==ESP_OK)remote_id_channels[remote_id_accepted_count++]=channel;
            /* Rejection is authoritative during RX-off discovery. No receive
             * coverage is claimed for that channel. Runtime failures abort. */
            if(error==ESP_OK||error==ESP_ERR_INVALID_ARG||error==ESP_ERR_NOT_SUPPORTED){
                xSemaphoreGive(scanner_mutex);return;
            }
            remote_id_cache_valid=false;
        }
        if(error==ESP_OK){
            remote_id_channel_count=remote_id_accepted_count;
            remote_id_cache_valid=remote_id_channel_count!=0U;
            clock=esp_timer_get_time();now=clock>=0?(uint64_t)clock:0U;
            if(clock<0||now<remote_id_lease_started_us||
               now-remote_id_lease_started_us>=(uint64_t)remote_id_request.lease_ms*1000U)error=ESP_ERR_TIMEOUT;
            remote_id_window_started_us=now;
            uint32_t budget=remote_id_request.duration_ms;
            remote_id_follow_candidate=false;
            if(remote_id_follow_deadline_us>now){
                uint64_t remaining=(remote_id_follow_deadline_us-now)/1000U;
                for(uint8_t i=0;i<remote_id_channel_count;++i)
                    if(remote_id_channels[i]==remote_id_request.follow_channel&&remaining){
                        remote_id_follow_candidate=true;
                        if(remaining<budget)budget=(uint32_t)remaining;
                        break;
                    }
            }
            if(!remote_id_follow_candidate)remote_id_follow_deadline_us=0U;
            remote_id_plan_ready=error==ESP_OK&&cerberus_remote_id_channel_plan_init(&remote_id_plan,
                remote_id_channels,remote_id_channel_count,remote_id_request.dwell_ms,
                budget,(uint32_t)(now/1000U));
            if(!remote_id_plan_ready&&error==ESP_OK)error=ESP_ERR_NOT_SUPPORTED;
            if(remote_id_plan_ready&&!cerberus_remote_id_channel_plan_configure_follow(&remote_id_plan,
                remote_id_follow_candidate?remote_id_request.follow_channel:0U))error=ESP_ERR_INVALID_STATE;
            if(error!=ESP_OK)remote_id_plan_ready=false;
            if(remote_id_plan_ready){
                (void)cerberus_remote_id_channel_plan_seek_background(&remote_id_plan,remote_id_next_background);
                portENTER_CRITICAL(&defense_capture_mux);
                remote_id_epoch=++watch_capture_epoch;
                cerberus_watch_capture_begin_with_epoch(&watch_capture,CERBERUS_WATCH_RADIO_WIFI,
                    remote_id_status.generation,(uint32_t)(now/1000U),remote_id_epoch);
                if(remote_id_request.flags)(void)cerberus_watch_capture_protect_wifi_source(&watch_capture,
                    remote_id_follow_transport(&remote_id_request),remote_id_request.transmitter);
                watch_capture_active=false;watch_capture_channel=0U;watch_capture_deadline_us=0U;
                portEXIT_CRITICAL(&defense_capture_mux);
                remote_id_status.state=CERBERUS_REMOTE_ID_STATE_RUNNING;
            }
        }
    }
    if(error==ESP_OK&&remote_id_status.state==CERBERUS_REMOTE_ID_STATE_RUNNING){
        if(remote_id_follow_candidate&&!remote_id_follow_bound&&now>=remote_id_follow_deadline_us&&
           !remote_id_search_plan_locked())error=ESP_ERR_INVALID_STATE;
        if(now<remote_id_window_started_us)error=ESP_ERR_INVALID_STATE;
        else remote_id_status.elapsed_ms=(uint32_t)((now-remote_id_window_started_us)/1000U);
        cerberus_remote_id_channel_slot_t slot;
        cerberus_remote_id_channel_plan_result_t action=error==ESP_OK?
            cerberus_remote_id_channel_plan_poll(&remote_id_plan,(uint32_t)(now/1000U),&slot):CERBERUS_REMOTE_ID_PLAN_INVALID;
        if(action==CERBERUS_REMOTE_ID_PLAN_DONE)remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_COMPLETE,ESP_OK);
        else if(action==CERBERUS_REMOTE_ID_PLAN_INVALID)error=ESP_ERR_INVALID_STATE;
        else if(action==CERBERUS_REMOTE_ID_PLAN_TUNE){
            bool restart_search=false;
            error=remote_id_pause_locked();
            if(error==ESP_OK)error=esp_wifi_set_band(slot.channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
            if(error==ESP_OK)error=esp_wifi_set_channel(slot.channel,WIFI_SECOND_CHAN_NONE);
            if(error==ESP_OK){
                int64_t tuned=esp_timer_get_time();
                if(tuned<0||(uint64_t)tuned<remote_id_lease_started_us||
                   (uint64_t)tuned-remote_id_lease_started_us>=(uint64_t)remote_id_request.lease_ms*1000U)
                    error=ESP_ERR_TIMEOUT;
                else if((uint64_t)tuned<remote_id_window_started_us)error=ESP_ERR_INVALID_STATE;
                else if((uint64_t)tuned-remote_id_window_started_us>=(uint64_t)remote_id_request.duration_ms*1000U)
                    remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_COMPLETE,ESP_OK);
                else if(remote_id_follow_deadline_us&&(uint64_t)tuned>=remote_id_follow_deadline_us){
                    if(remote_id_follow_bound)remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_COMPLETE,ESP_OK);
                    else {restart_search=true;if(!remote_id_search_plan_locked())error=ESP_ERR_INVALID_STATE;}
                }
            }
            if(error==ESP_OK&&!restart_search&&remote_id_status.state==CERBERUS_REMOTE_ID_STATE_RUNNING){
                remote_id_disable_pending=true;
                error=esp_wifi_set_promiscuous(true);
            }
            if(error==ESP_OK&&!restart_search&&remote_id_status.state==CERBERUS_REMOTE_ID_STATE_RUNNING){
                int64_t armed=esp_timer_get_time();
                if(armed<0||(uint64_t)armed<remote_id_lease_started_us||
                   (uint64_t)armed-remote_id_lease_started_us>=(uint64_t)remote_id_request.lease_ms*1000U)
                    error=ESP_ERR_TIMEOUT;
                else if((uint64_t)armed<remote_id_window_started_us)error=ESP_ERR_INVALID_STATE;
                else if((uint64_t)armed-remote_id_window_started_us>=(uint64_t)remote_id_request.duration_ms*1000U)
                    remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_COMPLETE,ESP_OK);
                else if(remote_id_follow_deadline_us&&(uint64_t)armed>=remote_id_follow_deadline_us){
                    if(remote_id_follow_bound)remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_COMPLETE,ESP_OK);
                    else {restart_search=true;if(!remote_id_search_plan_locked())error=ESP_ERR_INVALID_STATE;}
                }
                else {
                    if(remote_id_follow_candidate)remote_id_follow_bound=true;
                    portENTER_CRITICAL(&defense_capture_mux);
                    watch_capture_channel=slot.channel;watch_capture_armed_us=(uint64_t)armed;
                    watch_capture_deadline_us=remote_id_follow_bound?remote_id_follow_deadline_us:0U;
                    if(remote_id_follow_bound){
                        uint64_t window_end=remote_id_window_started_us+(uint64_t)remote_id_request.duration_ms*1000U;
                        if(window_end<watch_capture_deadline_us)watch_capture_deadline_us=window_end;
                    }
                    watch_capture_active=true;
                    portEXIT_CRITICAL(&defense_capture_mux);
                    remote_id_status.channel=slot.channel;
                }
            }
            if(restart_search&&error==ESP_OK)error=remote_id_pause_locked();
            if(error!=ESP_OK)remote_id_cache_valid=false;
        }
    }
    if(error!=ESP_OK)remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_ERROR,error);
    if(remote_id_status.state==CERBERUS_REMOTE_ID_STATE_STOPPING||remote_id_status.state==CERBERUS_REMOTE_ID_STATE_ERROR)
        remote_id_cleanup_locked();
    xSemaphoreGive(scanner_mutex);
}

void cerberus_wifi_remote_id_start(const cerberus_remote_id_start_request_t *request,
    cerberus_remote_id_status_payload_t *out)
{
    if(!out)return;
    uint64_t id=request?request->job_id:0U;
    if(!remote_id_valid_request(request)||!scanner_mutex){remote_id_rejected(id,ESP_ERR_INVALID_ARG,out);return;}
    /* Check the retained ID before foreign-radio guards: a transport retry
     * must return the terminal acknowledgement, never create another capture. */
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    if(id==remote_id_request.job_id){
        if(!memcmp(request,&remote_id_request,sizeof(*request)))remote_id_status_locked(out);
        else remote_id_rejected(id,ESP_ERR_INVALID_ARG,out);
        xSemaphoreGive(scanner_mutex);return;
    }
    bool stale=id<remote_id_request.job_id;
    xSemaphoreGive(scanner_mutex);
    if(stale||cerberus_c5_test_running()||another_radio_operation_is_active()||
       atomic_load_explicit(&channel_survey_radio_reserved,memory_order_acquire)){
        remote_id_rejected(id,ESP_ERR_INVALID_STATE,out);return;
    }
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    int64_t now=esp_timer_get_time();bool promiscuous=false;
    esp_err_t error=esp_wifi_get_promiscuous(&promiscuous);
    if(error!=ESP_OK||promiscuous||now<0||!scanner_ready||!scanner_task_handle||station_reserved||
       atomic_load_explicit(&mac_radio_reserved,memory_order_acquire) || atomic_load_explicit(&csi_radio_reserved,memory_order_acquire)||
       scan_status.state==CERBERUS_WIFI_SCAN_RUNNING||defense_status.state==CERBERUS_WIFI_DEFENSE_RUNNING||
       channel_survey_status.state==CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING||
       wifi_connection_is_active_locked()||watch_capture_epoch==UINT64_MAX||
       atomic_load_explicit(&remote_id_radio_reserved,memory_order_acquire)||id<=remote_id_request.job_id){
        remote_id_rejected(id,error!=ESP_OK?error:ESP_ERR_INVALID_STATE,out);
        xSemaphoreGive(scanner_mutex);return;
    }
    uint64_t follow_deadline=remote_id_follow_proof_locked(request,(uint64_t)now);
    remote_id_request=*request;memset(&remote_id_status,0,sizeof(remote_id_status));
    remote_id_follow_deadline_us=follow_deadline;
    remote_id_follow_candidate=remote_id_follow_bound=false;
    remote_id_status.job_id=id;remote_id_status.state=CERBERUS_REMOTE_ID_STATE_STARTING;
    portENTER_CRITICAL(&defense_capture_mux);
    remote_id_status.generation=(uint8_t)(watch_capture.status.generation+1U);
    portEXIT_CRITICAL(&defense_capture_mux);
    remote_id_saved=remote_id_disable_pending=remote_id_plan_ready=false;
    remote_id_epoch=0U;remote_id_window_started_us=0U;remote_id_lease_started_us=(uint64_t)now;
    remote_id_terminal_state=CERBERUS_REMOTE_ID_STATE_STOPPED;
    atomic_store_explicit(&remote_id_radio_reserved,true,memory_order_release);
    remote_id_status_locked(out);xSemaphoreGive(scanner_mutex);xTaskNotifyGive(scanner_task_handle);
}

void cerberus_wifi_remote_id_status(uint64_t id,cerberus_remote_id_status_payload_t *out)
{
    if(!out)return;
    if(!scanner_mutex){remote_id_rejected(id,ESP_ERR_INVALID_STATE,out);return;}
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    if(id&&id!=remote_id_request.job_id)remote_id_rejected(id,ESP_ERR_NOT_FOUND,out);
    else remote_id_status_locked(out);
    xSemaphoreGive(scanner_mutex);
}

void cerberus_wifi_remote_id_stop(uint64_t id,cerberus_remote_id_status_payload_t *out)
{
    if(!out)return;
    if(!scanner_mutex||!id){remote_id_rejected(id,ESP_ERR_INVALID_ARG,out);return;}
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    bool owned=atomic_load_explicit(&remote_id_radio_reserved,memory_order_acquire);
    if(id!=remote_id_request.job_id)remote_id_rejected(id,ESP_ERR_NOT_FOUND,out);
    else {
        if(owned&&(remote_id_status.state==CERBERUS_REMOTE_ID_STATE_STARTING||remote_id_status.state==CERBERUS_REMOTE_ID_STATE_RUNNING))
            remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_STOPPED,ESP_OK);
        remote_id_status_locked(out);
    }
    xSemaphoreGive(scanner_mutex);
    if(owned&&id==remote_id_request.job_id)xTaskNotifyGive(scanner_task_handle);
}

void cerberus_wifi_remote_id_renew(uint64_t id,cerberus_remote_id_status_payload_t *out)
{
    if(!out)return;
    if(!scanner_mutex||!id){remote_id_rejected(id,ESP_ERR_INVALID_ARG,out);return;}
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    int64_t now=esp_timer_get_time();
    if(id!=remote_id_request.job_id)remote_id_rejected(id,ESP_ERR_NOT_FOUND,out);
    else if(!atomic_load_explicit(&remote_id_radio_reserved,memory_order_acquire)||
        (remote_id_status.state!=CERBERUS_REMOTE_ID_STATE_STARTING&&remote_id_status.state!=CERBERUS_REMOTE_ID_STATE_RUNNING))
        remote_id_status_locked(out); /* Completion/cleanup won the race; never restart. */
    else {
        /* A late renewal cannot resurrect an expired lease. */
        if(now<0||(uint64_t)now<remote_id_lease_started_us||
            (uint64_t)now-remote_id_lease_started_us>=(uint64_t)remote_id_request.lease_ms*1000U)
            remote_id_end_locked(CERBERUS_REMOTE_ID_STATE_ERROR,ESP_ERR_TIMEOUT);
        else remote_id_lease_started_us=(uint64_t)now;
        remote_id_status_locked(out);
    }
    xSemaphoreGive(scanner_mutex);
}
