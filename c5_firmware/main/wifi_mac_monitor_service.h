/* Private to wifi_scanner.c. Commands own scanner_mutex; receive publication
 * owns mac_mux. Only the existing scanner worker calls the radio SDK. */
typedef struct {
    cerberus_mac_observation_t live;
    cerberus_mac_snapshot_t pinned;
} wifi_mac_store_t;
static portMUX_TYPE mac_mux = portMUX_INITIALIZER_UNLOCKED;
static wifi_mac_store_t *mac_store;
static cerberus_mac_start_request_t mac_request;
static cerberus_mac_status_t mac_status;
static cerberus_mac_counters_t mac_final_counters;
static cerberus_mac_peek_t mac_pinned;
static uint64_t mac_boot_id,mac_epoch_counter,mac_deadline_us,mac_armed_us,mac_clock_floor,mac_tuned_us;
static uint32_t mac_inflight,mac_last_ack;
static uint8_t mac_channels[64],mac_channel_count,mac_probe_index,mac_accepted_count,mac_next_index,mac_last_next_channel;
static bool mac_accepting,mac_saved,mac_disable_pending,mac_cache_valid,mac_exhausted;
static wifi_country_t mac_cached_country;
static wifi_band_mode_t mac_previous_mode;
static uint8_t mac_previous_channel;
static wifi_second_chan_t mac_previous_second;
static wifi_promiscuous_filter_t mac_previous_filter,mac_previous_control_filter;
static const uint8_t mac_5g_channels[]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,
    132,136,140,144,149,153,157,161,165,169,173,177};

/* Only clear transmitter headers are interpreted. Retries, encrypted payloads
 * and fragments remain observations; no association or payload claim is made.
 * ACK/CTS/wrapper and variable BAR/BA layouts are deliberately not admitted. */
static bool mac_wifi_header(const uint8_t *p,size_t n,wifi_promiscuous_pkt_type_t type,
    uint8_t address[6],uint8_t *kind)
{
    if(!p||n<2U||(p[0]&3U))return false;
    unsigned frame_type=(p[0]>>2U)&3U,subtype=p[0]>>4U;
    size_t minimum=0U;
    if(type==WIFI_PKT_MGMT&&frame_type==0U){
        if(p[1]&3U)return false;
        minimum=24U+((p[1]&0x80U)?4U:0U);*kind=CERBERUS_MAC_WIFI_MGMT;
    }else if(type==WIFI_PKT_DATA&&frame_type==2U){
        minimum=24U+((p[1]&3U)==3U?6U:0U);
        if(subtype&8U)minimum+=2U+((p[1]&0x80U)?4U:0U);
        *kind=CERBERUS_MAC_WIFI_DATA;
    }else if(type==WIFI_PKT_CTRL&&frame_type==1U&&(subtype==10U||subtype==11U||subtype==14U||subtype==15U)){
        if((p[1]&3U)||n!=16U)return false;
        minimum=16U;*kind=CERBERUS_MAC_WIFI_CONTROL;
    }else return false;
    if(n<minimum||(p[10]&1U))return false;
    uint8_t any=0U;for(unsigned i=0;i<6U;++i)any|=p[10U+i];
    if(!any)return false;
    memcpy(address,p+10U,6U);return true;
}

static bool mac_clock_valid_locked(int64_t now)
{return now>=0&&(uint64_t)now>=mac_clock_floor&&(uint64_t)now<mac_deadline_us;}

static void mac_receive(const wifi_promiscuous_pkt_t *packet,wifi_promiscuous_pkt_type_t type,
    int64_t entered,uint32_t token)
{
    if(!packet||type==WIFI_PKT_MISC||!token||
       !atomic_load_explicit(&mac_radio_reserved,memory_order_acquire))return;
    uint64_t epoch,source;
    portENTER_CRITICAL(&mac_mux);
    int64_t now=esp_timer_get_time();
    if(!mac_accepting||!mac_store||token!=atomic_load_explicit(&mac_admission_token,memory_order_acquire)||
       entered<0||(uint64_t)entered<mac_armed_us||now<entered||!mac_clock_valid_locked(now)){
        portEXIT_CRITICAL(&mac_mux);return;
    }
    epoch=mac_status.epoch;source=(uint64_t)now;mac_clock_floor=source;++mac_inflight;
    uint8_t expected_channel=mac_status.channel;
    portEXIT_CRITICAL(&mac_mux);
    uint8_t address[6],kind=0U;
    bool valid=packet->rx_ctrl.rx_state==0U&&packet->rx_ctrl.sig_len>=20U&&
        packet->rx_ctrl.channel==expected_channel&&
        mac_wifi_header(packet->payload,packet->rx_ctrl.sig_len-4U,type,address,&kind);
    portENTER_CRITICAL(&mac_mux);
    now=esp_timer_get_time();
    if(mac_accepting&&mac_store&&mac_status.epoch==epoch&&
       token==atomic_load_explicit(&mac_admission_token,memory_order_acquire)&&
       now>=0&&(uint64_t)now>=source&&mac_clock_valid_locked(now)){
        if(valid){
            bool accepted=cerberus_mac_observation_observe(&mac_store->live,address,0U,
                packet->rx_ctrl.rssi,packet->rx_ctrl.channel,kind,source);
            if(!accepted&&mac_store->live.next_incarnation==UINT32_MAX)mac_exhausted=true;
        }else cerberus_mac_observation_reject(&mac_store->live);
    }
    --mac_inflight;portEXIT_CRITICAL(&mac_mux);
}

static void mac_boot_locked(void)
{if(!mac_boot_id){esp_fill_random(&mac_boot_id,sizeof(mac_boot_id));if(!mac_boot_id)mac_boot_id=1U;}}
static bool mac_binding_valid(const cerberus_mac_binding_t *b,bool unbound_epoch)
{return b&&b->boot_id&&b->boot_id==mac_boot_id&&b->job_id&&b->job_id==mac_status.job_id&&
    mac_status.epoch&&(b->epoch==mac_status.epoch||(unbound_epoch&&!b->epoch));}
static void mac_status_locked(cerberus_mac_status_t *out)
{
    *out=mac_status;out->version=CERBERUS_MAC_VERSION;out->flags=0U;out->channel=0U;
    if(atomic_load_explicit(&mac_radio_reserved,memory_order_acquire))out->flags=CERBERUS_MAC_OWNED;
    portENTER_CRITICAL(&mac_mux);
    if(mac_accepting&&mac_clock_valid_locked(esp_timer_get_time())){
        out->flags|=CERBERUS_MAC_RECEIVING;out->channel=mac_status.channel;
    }
    portEXIT_CRITICAL(&mac_mux);
}
static void mac_end_locked(esp_err_t error)
{
    mac_status.state=error==ESP_OK?CERBERUS_MAC_STOPPING:CERBERUS_MAC_ERROR;
    if(error!=ESP_OK)mac_status.error_code=error;
    portENTER_CRITICAL(&mac_mux);mac_accepting=false;portEXIT_CRITICAL(&mac_mux);
}
static esp_err_t mac_pause_locked(void)
{
    portENTER_CRITICAL(&mac_mux);mac_accepting=false;mac_status.channel=0U;portEXIT_CRITICAL(&mac_mux);
    if(mac_disable_pending){
        esp_err_t error=esp_wifi_set_promiscuous(false);if(error!=ESP_OK)return error;
        mac_disable_pending=false;
    }
    int64_t started=esp_timer_get_time();
    for(;;){
        portENTER_CRITICAL(&mac_mux);uint32_t inflight=mac_inflight;portEXIT_CRITICAL(&mac_mux);
        if(!inflight)return ESP_OK;
        int64_t now=esp_timer_get_time();
        if(started<0||now<started||now-started>=50000)return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
static void mac_cleanup_locked(void)
{
    esp_err_t error=mac_pause_locked();
    if(error==ESP_OK&&mac_saved){
        error=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
        if(error==ESP_OK&&mac_previous_channel){
            error=esp_wifi_set_band(mac_previous_channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
            if(error==ESP_OK)error=esp_wifi_set_channel(mac_previous_channel,mac_previous_second);
        }
        if(error==ESP_OK)error=esp_wifi_set_band_mode(mac_previous_mode);
        if(error==ESP_OK)error=esp_wifi_set_promiscuous_filter(&mac_previous_filter);
        if(error==ESP_OK)error=esp_wifi_set_promiscuous_ctrl_filter(&mac_previous_control_filter);
        if(error==ESP_OK)mac_saved=false;
    }
    if(error!=ESP_OK){mac_end_locked(error);return;}
    portENTER_CRITICAL(&mac_mux);
    wifi_mac_store_t *store=mac_store;mac_store=NULL;
    if(store){mac_final_counters.observations=store->live.observations;mac_final_counters.invalid=store->live.invalid;
        mac_final_counters.replacements=store->live.replacements;}
    memset(&mac_pinned,0,sizeof(mac_pinned));
    portEXIT_CRITICAL(&mac_mux);
    free(store);mac_status.state=mac_status.error_code?CERBERUS_MAC_ERROR:CERBERUS_MAC_COMPLETE;
    atomic_store_explicit(&mac_radio_reserved,false,memory_order_release);
}
static bool mac_country_equal(const wifi_country_t *a,const wifi_country_t *b)
{return !memcmp(a->cc,b->cc,sizeof(a->cc))&&a->schan==b->schan&&a->nchan==b->nchan&&
    a->policy==b->policy&&a->wifi_5g_channel_mask==b->wifi_5g_channel_mask;}
static esp_err_t mac_prepare_locked(void)
{
    bool promiscuous=false;wifi_country_t country={0};wifi_csi_config_t csi={0};
    esp_err_t error=esp_wifi_get_promiscuous(&promiscuous);
    if(error!=ESP_OK||promiscuous)return error==ESP_OK?ESP_ERR_INVALID_STATE:error;
    error=esp_wifi_get_csi_config(&csi);
    if(error!=ESP_OK||csi.enable)return error==ESP_OK?ESP_ERR_INVALID_STATE:error;
    error=esp_wifi_get_band_mode(&mac_previous_mode);
    if(error==ESP_OK)error=esp_wifi_get_channel(&mac_previous_channel,&mac_previous_second);
    if(error==ESP_OK)error=esp_wifi_get_promiscuous_filter(&mac_previous_filter);
    if(error==ESP_OK)error=esp_wifi_get_promiscuous_ctrl_filter(&mac_previous_control_filter);
    if(error!=ESP_OK)return error;
    mac_saved=true;
    error=esp_wifi_get_country(&country);
    if(error==ESP_OK)error=esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    wifi_promiscuous_filter_t filter={.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_CTRL|WIFI_PROMIS_FILTER_MASK_DATA};
    wifi_promiscuous_filter_t control={.filter_mask=WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL|WIFI_PROMIS_CTRL_FILTER_MASK_RTS|
        WIFI_PROMIS_CTRL_FILTER_MASK_CFEND|WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK};
    if(error==ESP_OK)error=esp_wifi_set_promiscuous_filter(&filter);
    if(error==ESP_OK)error=esp_wifi_set_promiscuous_ctrl_filter(&control);
    if(error!=ESP_OK)return error;
    if(mac_cache_valid&&mac_country_equal(&country,&mac_cached_country)){
        mac_probe_index=mac_accepted_count=mac_channel_count;return ESP_OK;
    }
    mac_cache_valid=false;mac_cached_country=country;mac_channel_count=mac_probe_index=mac_accepted_count=0U;
    if(!country.schan||country.schan>14U||!country.nchan||(unsigned)country.schan+country.nchan>15U)return ESP_ERR_INVALID_STATE;
    for(unsigned ch=country.schan;ch<(unsigned)country.schan+country.nchan;++ch)mac_channels[mac_channel_count++]=(uint8_t)ch;
    for(unsigned i=0;i<sizeof(mac_5g_channels);++i){
        if(country.policy==WIFI_COUNTRY_POLICY_MANUAL&&country.wifi_5g_channel_mask&&
           !(country.wifi_5g_channel_mask&(UINT32_C(1)<<(i+1U))))continue;
        mac_channels[mac_channel_count++]=mac_5g_channels[i];
    }
    return ESP_OK;
}
static esp_err_t mac_tune_locked(void)
{
    uint8_t channel=mac_channels[mac_next_index];
    esp_err_t error=mac_pause_locked();
    if(error==ESP_OK)error=esp_wifi_set_band(channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
    if(error==ESP_OK)error=esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
    portENTER_CRITICAL(&mac_mux);
    if(error==ESP_OK&&!mac_clock_valid_locked(esp_timer_get_time()))error=ESP_ERR_TIMEOUT;
    portEXIT_CRITICAL(&mac_mux);
    if(error==ESP_OK){mac_disable_pending=true;error=esp_wifi_set_promiscuous(true);}
    if(error==ESP_OK){
        portENTER_CRITICAL(&mac_mux);
        int64_t now=esp_timer_get_time();uint32_t token=atomic_load_explicit(&mac_admission_token,memory_order_relaxed);
        if(!mac_clock_valid_locked(now))error=ESP_ERR_TIMEOUT;
        else if(token==UINT32_MAX)error=ESP_ERR_INVALID_STATE;
        else{
            mac_clock_floor=mac_armed_us=mac_tuned_us=(uint64_t)now;mac_status.channel=channel;
            atomic_store_explicit(&mac_admission_token,token+1U,memory_order_release);mac_accepting=true;
        }
        portEXIT_CRITICAL(&mac_mux);
    }
    if(error==ESP_OK){mac_next_index=(mac_next_index+1U)%mac_channel_count;mac_last_next_channel=mac_channels[mac_next_index];}
    else mac_cache_valid=false;
    return error;
}
static void mac_worker_tick(void)
{
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    if(!atomic_load_explicit(&mac_radio_reserved,memory_order_acquire)){xSemaphoreGive(scanner_mutex);return;}
    portENTER_CRITICAL(&mac_mux);
    int64_t now=esp_timer_get_time();bool expired=!mac_clock_valid_locked(now),exhausted=mac_exhausted;
    portEXIT_CRITICAL(&mac_mux);
    if(mac_status.state==CERBERUS_MAC_STARTING||mac_status.state==CERBERUS_MAC_RUNNING){
        if(expired||exhausted)mac_end_locked(expired?ESP_ERR_TIMEOUT:ESP_ERR_INVALID_STATE);
    }
    esp_err_t error=ESP_OK;
    if(mac_status.state==CERBERUS_MAC_STARTING){
        if(!mac_saved)error=mac_prepare_locked();
        if(error==ESP_OK&&mac_probe_index<mac_channel_count){
            uint8_t channel=mac_channels[mac_probe_index++];
            error=esp_wifi_set_band(channel<=14U?WIFI_BAND_2G:WIFI_BAND_5G);
            if(error==ESP_OK)error=esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
            if(error==ESP_OK)mac_channels[mac_accepted_count++]=channel;
            if(error==ESP_OK||error==ESP_ERR_INVALID_ARG||error==ESP_ERR_NOT_SUPPORTED){xSemaphoreGive(scanner_mutex);return;}
            mac_cache_valid=false;
        }
        if(error==ESP_OK){
            mac_channel_count=mac_accepted_count;mac_cache_valid=mac_channel_count!=0U;mac_next_index=0U;
            for(uint8_t i=0;i<mac_channel_count;++i)if(mac_channels[i]==mac_last_next_channel){mac_next_index=i;break;}
            if(!mac_channel_count)error=ESP_ERR_NOT_SUPPORTED;
            else error=mac_tune_locked();
            if(error==ESP_OK)mac_status.state=CERBERUS_MAC_RUNNING;
        }
    }else if(mac_status.state==CERBERUS_MAC_RUNNING&&(uint64_t)now-mac_tuned_us>=(uint64_t)mac_request.dwell_ms*1000U)
        error=mac_tune_locked();
    if(error!=ESP_OK)mac_end_locked(error);
    if(mac_status.state==CERBERUS_MAC_STOPPING||mac_status.state==CERBERUS_MAC_ERROR)mac_cleanup_locked();
    xSemaphoreGive(scanner_mutex);
}

esp_err_t cerberus_wifi_mac_start(const cerberus_mac_start_request_t *r,cerberus_mac_status_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!r||!out||!r->job_id||r->version!=CERBERUS_MAC_VERSION||r->lease_ms<CERBERUS_MAC_LEASE_MIN_MS||
       r->lease_ms>CERBERUS_MAC_LEASE_MAX_MS||r->dwell_ms<CERBERUS_MAC_DWELL_MIN_MS||r->dwell_ms>CERBERUS_MAC_DWELL_MAX_MS)
        return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<sizeof(r->reserved);++i)if(r->reserved[i])return ESP_ERR_INVALID_ARG;
    if(!scanner_mutex)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);mac_boot_locked();
    if(r->boot_id!=mac_boot_id){xSemaphoreGive(scanner_mutex);return ESP_ERR_INVALID_STATE;}
    if(r->job_id==mac_request.job_id){
        esp_err_t error=memcmp(r,&mac_request,sizeof(*r))?ESP_ERR_INVALID_ARG:ESP_OK;
        if(error==ESP_OK)mac_status_locked(out);
        xSemaphoreGive(scanner_mutex);
        return error;
    }
    int64_t now=esp_timer_get_time();
    if(!scanner_ready||!scanner_task_handle||r->job_id<mac_request.job_id||now<0||mac_epoch_counter==UINT64_MAX||
       atomic_load_explicit(&mac_admission_token,memory_order_relaxed)==UINT32_MAX||
       another_radio_operation_is_active()||cerberus_c5_test_running()||station_reserved||
       atomic_load_explicit(&channel_survey_radio_reserved,memory_order_acquire)||
       channel_survey_status.state==CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING||scan_status.state==CERBERUS_WIFI_SCAN_RUNNING||
       defense_status.state==CERBERUS_WIFI_DEFENSE_RUNNING||wifi_connection_is_active_locked()){
        xSemaphoreGive(scanner_mutex);return ESP_ERR_INVALID_STATE;
    }
    wifi_mac_store_t *store=heap_caps_calloc(1U,sizeof(*store),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!store){xSemaphoreGive(scanner_mutex);return ESP_ERR_NO_MEM;}
    if(!mac_epoch_counter){esp_fill_random(&mac_epoch_counter,sizeof(mac_epoch_counter));if(!mac_epoch_counter)mac_epoch_counter=1U;}
    if(mac_epoch_counter==UINT64_MAX||!cerberus_mac_observation_init(&store->live,CERBERUS_MAC_WIFI,(uint64_t)now)){
        free(store);xSemaphoreGive(scanner_mutex);return ESP_ERR_INVALID_STATE;
    }
    portENTER_CRITICAL(&mac_mux);
    mac_request=*r;memset(&mac_status,0,sizeof(mac_status));mac_status.job_id=r->job_id;
    mac_status.epoch=++mac_epoch_counter;mac_status.state=CERBERUS_MAC_STARTING;
    mac_store=store;mac_last_ack=0U;mac_accepting=mac_exhausted=false;
    memset(&mac_pinned,0,sizeof(mac_pinned));memset(&mac_final_counters,0,sizeof(mac_final_counters));
    mac_clock_floor=mac_armed_us=(uint64_t)now;mac_deadline_us=(uint64_t)now+(uint64_t)r->lease_ms*1000U;
    portEXIT_CRITICAL(&mac_mux);
    mac_saved=mac_disable_pending=false;
    atomic_store_explicit(&mac_radio_reserved,true,memory_order_release);
    mac_status_locked(out);xSemaphoreGive(scanner_mutex);xTaskNotifyGive(scanner_task_handle);return ESP_OK;
}
esp_err_t cerberus_wifi_mac_get_info(cerberus_mac_info_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!out||!scanner_mutex)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);mac_boot_locked();int64_t now=esp_timer_get_time();
    if(now>=0){out->boot_id=mac_boot_id;out->source_now_us=(uint64_t)now;out->capacity=CERBERUS_MAC_CAPACITY;
        out->row_bytes=sizeof(cerberus_mac_row_t);out->version=CERBERUS_MAC_VERSION;}
    xSemaphoreGive(scanner_mutex);return now>=0?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t cerberus_wifi_mac_get_status(const cerberus_mac_binding_t *b,cerberus_mac_status_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!b||!out||!scanner_mutex)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);bool valid=b->boot_id&&b->boot_id==mac_boot_id;
    if(valid)mac_status_locked(out);
    xSemaphoreGive(scanner_mutex);
    return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t cerberus_wifi_mac_get_profile(const cerberus_mac_binding_t *b,cerberus_mac_profile_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!out||!scanner_mutex)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);bool valid=mac_binding_valid(b,false);
    if(valid){out->epoch=mac_status.epoch;out->lease_ms=mac_request.lease_ms;out->dwell_ms=mac_request.dwell_ms;
        out->channel_count=mac_status.state==CERBERUS_MAC_STARTING?0U:mac_channel_count;out->version=CERBERUS_MAC_VERSION;}
    xSemaphoreGive(scanner_mutex);return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t cerberus_wifi_mac_get_counters(const cerberus_mac_binding_t *b,cerberus_mac_counters_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!out||!scanner_mutex)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);bool valid=mac_binding_valid(b,false);
    if(valid){portENTER_CRITICAL(&mac_mux);*out=mac_final_counters;
        if(mac_store){out->observations=mac_store->live.observations;out->invalid=mac_store->live.invalid;
            out->replacements=mac_store->live.replacements;}
        out->epoch=mac_status.epoch;out->version=CERBERUS_MAC_VERSION;portEXIT_CRITICAL(&mac_mux);}
    xSemaphoreGive(scanner_mutex);return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t cerberus_wifi_mac_stop(const cerberus_mac_binding_t *b,cerberus_mac_status_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!out||!scanner_mutex)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);bool valid=mac_binding_valid(b,true);
    if(valid){
        if(atomic_load_explicit(&mac_radio_reserved,memory_order_acquire))mac_end_locked(ESP_OK);
        mac_status_locked(out);
    }
    xSemaphoreGive(scanner_mutex);
    if(valid)xTaskNotifyGive(scanner_task_handle);
    return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t cerberus_wifi_mac_renew(const cerberus_mac_renew_request_t *r,cerberus_mac_status_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!r||!out||!scanner_mutex||r->lease_ms<CERBERUS_MAC_LEASE_MIN_MS||r->lease_ms>CERBERUS_MAC_LEASE_MAX_MS)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);bool valid=mac_binding_valid(&r->binding,false);
    if(valid){
        if(mac_status.state==CERBERUS_MAC_STARTING||mac_status.state==CERBERUS_MAC_RUNNING){
            portENTER_CRITICAL(&mac_mux);int64_t now=esp_timer_get_time();bool alive=mac_clock_valid_locked(now);
            if(alive)mac_deadline_us=(uint64_t)now+(uint64_t)r->lease_ms*1000U;
            portEXIT_CRITICAL(&mac_mux);
            if(!alive)mac_end_locked(ESP_ERR_TIMEOUT);
        }
        mac_status_locked(out);
    }
    xSemaphoreGive(scanner_mutex);return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t cerberus_wifi_mac_peek(const cerberus_mac_binding_t *b,cerberus_mac_peek_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!out||!scanner_mutex)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);esp_err_t error=mac_binding_valid(b,false)?ESP_OK:ESP_ERR_NOT_FOUND;
    if(error==ESP_OK&&(!mac_store||mac_status.state!=CERBERUS_MAC_RUNNING))error=ESP_ERR_INVALID_STATE;
    if(error==ESP_OK&&!mac_pinned.snapshot_id){
        portENTER_CRITICAL(&mac_mux);int64_t now=esp_timer_get_time();
        bool valid=now>=0&&(uint64_t)now>=mac_clock_floor&&
            cerberus_mac_observation_snapshot(&mac_store->live,(uint64_t)now,&mac_store->pinned);
        if(valid)mac_clock_floor=(uint64_t)now;
        else if(mac_store->live.next_snapshot_id==UINT32_MAX)mac_exhausted=true;
        portEXIT_CRITICAL(&mac_mux);
        if(!valid)error=ESP_ERR_INVALID_STATE;
        else{
            mac_pinned.epoch=mac_status.epoch;mac_pinned.snapshot_id=mac_store->pinned.header.snapshot_id;
            mac_pinned.count=mac_store->pinned.header.count;mac_pinned.version=CERBERUS_MAC_VERSION;
            mac_pinned.length=sizeof(cerberus_mac_snapshot_header_t)+mac_pinned.count*sizeof(cerberus_mac_row_t);
            mac_pinned.crc32=cerberus_mac_crc32(&mac_store->pinned,mac_pinned.length);
        }
    }
    if(error==ESP_OK)*out=mac_pinned;
    xSemaphoreGive(scanner_mutex);
    return error;
}
esp_err_t cerberus_wifi_mac_read(const cerberus_mac_read_request_t *r,cerberus_mac_chunk_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!r||!out||!scanner_mutex||!r->snapshot_id||r->offset%CERBERUS_MAC_CHUNK_BYTES)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    bool valid=r->job_id==mac_status.job_id&&r->epoch&&r->epoch==mac_status.epoch&&mac_store&&
        r->snapshot_id==mac_pinned.snapshot_id&&r->offset<mac_pinned.length;
    if(valid){out->offset=r->offset;size_t length=mac_pinned.length-r->offset;
        if(length>CERBERUS_MAC_CHUNK_BYTES)length=CERBERUS_MAC_CHUNK_BYTES;
        memcpy(out->data,(const uint8_t *)&mac_store->pinned+r->offset,length);}
    xSemaphoreGive(scanner_mutex);return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
esp_err_t cerberus_wifi_mac_ack(const cerberus_mac_ack_request_t *r,cerberus_mac_status_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!r||!out||!scanner_mutex||!r->snapshot_id)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex,portMAX_DELAY);
    bool valid=r->job_id==mac_status.job_id&&r->epoch&&r->epoch==mac_status.epoch&&
        (r->snapshot_id==mac_last_ack||(mac_store&&r->snapshot_id==mac_pinned.snapshot_id));
    if(valid){if(r->snapshot_id!=mac_last_ack){mac_last_ack=r->snapshot_id;memset(&mac_pinned,0,sizeof(mac_pinned));}
        mac_status_locked(out);}
    xSemaphoreGive(scanner_mutex);return valid?ESP_OK:ESP_ERR_NOT_FOUND;
}
