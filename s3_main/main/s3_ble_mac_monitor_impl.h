/* Included in the scanner translation unit: reuses its mutex and finite
 * passive GAP procedure. The UI owns context storage; no extra task/heap. */
static uint64_t mac_local_now(void *context)
{ (void)context; int64_t value=esp_timer_get_time(); return value<0?0U:(uint64_t)value; }

static bool mac_binding_matches(const cerberus_s3_ble_mac_monitor_t *m,
    const cerberus_mac_binding_t *b)
{
    return m && b && m->reserved && b->boot_id==m->binding.boot_id &&
        b->job_id==m->binding.job_id && (!b->epoch || b->epoch==m->binding.epoch);
}

static void mac_release_locked(cerberus_s3_ble_mac_monitor_t *m)
{
    if (!m || mac_monitor!=m || !m->owned || !m->stopping) return;
    bool own_scan=snapshot && snapshot->status.state==CERBERUS_BLE_SCAN_RUNNING &&
        scan_epoch==m->scan_epoch && scan_host_epoch==m->host_epoch;
    bool queued=stop_event_pending && stop_epoch==m->scan_epoch;
    bool own_host=m->host_epoch==cerberus_s3_ble_host_epoch() &&
        cerberus_s3_ble_host_owner()==CERBERUS_S3_BLE_OWNER_SCANNER;
    if (!own_scan && !queued && !own_host) {
        m->owned=m->receiving=false;
        m->state=m->error?CERBERUS_MAC_ERROR:CERBERUS_MAC_COMPLETE;
        mac_monitor=NULL; /* Detach before the caller can free its PSRAM store. */
    }
}

static void mac_status_locked(cerberus_s3_ble_mac_monitor_t *m,
    cerberus_mac_monitor_observation_t *out)
{
    memset(out,0,sizeof(*out));
    out->status=(cerberus_mac_status_t){.job_id=m->binding.job_id,.epoch=m->binding.epoch,
        .error_code=m->error,.state=m->state,.version=CERBERUS_MAC_VERSION,
        .flags=(uint8_t)((m->owned?CERBERUS_MAC_OWNED:0U)|(m->receiving?CERBERUS_MAC_RECEIVING:0U))};
    out->profile=(cerberus_mac_profile_t){.epoch=m->binding.epoch,.version=CERBERUS_MAC_VERSION};
    out->valid=true;
}

static int32_t mac_local_reserve(void *context,uint8_t radio,cerberus_mac_binding_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    cerberus_s3_ble_mac_monitor_t *m=context;
    if(!m || !out || radio!=CERBERUS_MAC_BLE)return ESP_ERR_INVALID_ARG;
    esp_err_t error=ensure_mutex();if(error)return error;
    xSemaphoreTake(scan_mutex,portMAX_DELAY);reconcile_host_locked();
    bool busy=mac_monitor || discovery_attached || snapshot->status.state==CERBERUS_BLE_SCAN_RUNNING ||
        stop_event_pending || start_event_pending || tracker.enabled || (analyzer && analyzer->active) ||
        cerberus_s3_ble_host_owner()!=CERBERUS_S3_BLE_OWNER_NONE ||
        cerberus_s3_wifi_owner()==CERBERUS_S3_WIFI_RECEIVER;
    int64_t source=esp_timer_get_time();
    if(busy || source<0 || mac_monitor_job==UINT32_MAX || mac_monitor_epoch==UINT64_MAX) {
        xSemaphoreGive(scan_mutex);return ESP_ERR_INVALID_STATE;
    }
    memset(m,0,sizeof(*m));
    m->host_epoch=cerberus_s3_ble_host_epoch();
    m->binding=(cerberus_mac_binding_t){.boot_id=(uint64_t)m->host_epoch+1U,
        .job_id=++mac_monitor_job,.epoch=++mac_monitor_epoch};
    (void)cerberus_mac_observation_init(&m->capture,CERBERUS_MAC_BLE,(uint64_t)source);
    m->reserved=m->owned=true;m->state=CERBERUS_MAC_STARTING;
    mac_monitor=m;*out=m->binding;out->epoch=0;
    xSemaphoreGive(scan_mutex);return ESP_OK;
}

/* Only poll/start advance a previously explicit job. Snapshot/ACK are RF-free. */
static void mac_local_progress(cerberus_s3_ble_mac_monitor_t *m)
{
    xSemaphoreTake(scan_mutex,portMAX_DELAY);reconcile_host_locked();
    bool stop=false,rearm=false;
    if(mac_monitor==m && m->owned) {
        bool own_scan=snapshot->status.state==CERBERUS_BLE_SCAN_RUNNING && scan_epoch==m->scan_epoch;
        if(own_scan && snapshot->status.error_code && m->stopping)m->error=snapshot->status.error_code;
        stop=m->stopping && own_scan;
        rearm=m->started && !m->stopping && !own_scan && !stop_event_pending &&
            cerberus_s3_ble_host_owner()==CERBERUS_S3_BLE_OWNER_NONE;
        mac_release_locked(m);
    }
    xSemaphoreGive(scan_mutex);
    cerberus_ble_scan_status_payload_t status={0};
    if(stop) {
        esp_err_t error=stop_scan(m,&status);
        if(error && error!=ESP_ERR_INVALID_STATE) {
            xSemaphoreTake(scan_mutex,portMAX_DELAY);m->error=error;xSemaphoreGive(scan_mutex);
        }
    } else if(rearm) {
        esp_err_t error=start_scan(false,m,&status);
        if(error) {
            xSemaphoreTake(scan_mutex,portMAX_DELAY);
            m->error=error;m->stopping=true;m->receiving=false;m->state=CERBERUS_MAC_ERROR;
            mac_release_locked(m);xSemaphoreGive(scan_mutex);
        }
    }
    xSemaphoreTake(scan_mutex,portMAX_DELAY);reconcile_host_locked();mac_release_locked(m);xSemaphoreGive(scan_mutex);
}

static cerberus_mac_monitor_operation_t mac_local_poll(void *context,uint8_t radio,
    const cerberus_mac_binding_t *binding,cerberus_mac_monitor_observation_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    cerberus_s3_ble_mac_monitor_t *m=context;
    if(!out || radio!=CERBERUS_MAC_BLE || !scan_mutex)return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG,false};
    xSemaphoreTake(scan_mutex,portMAX_DELAY);bool match=mac_binding_matches(m,binding);xSemaphoreGive(scan_mutex);
    if(!match)return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE,true};
    mac_local_progress(m);
    xSemaphoreTake(scan_mutex,portMAX_DELAY);mac_status_locked(m,out);xSemaphoreGive(scan_mutex);
    return (cerberus_mac_monitor_operation_t){ESP_OK,false};
}

static cerberus_mac_monitor_operation_t mac_local_start(void *context,uint8_t radio,
    const cerberus_mac_start_request_t *request,cerberus_mac_monitor_observation_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!request || !out || radio!=CERBERUS_MAC_BLE || !scan_mutex)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG,false};
    cerberus_s3_ble_mac_monitor_t *m=context;
    cerberus_mac_binding_t binding={.boot_id=request->boot_id,.job_id=request->job_id};
    xSemaphoreTake(scan_mutex,portMAX_DELAY);reconcile_host_locked();
    bool match=mac_binding_matches(m,&binding);
    bool valid=request->version==CERBERUS_MAC_VERSION && !request->lease_ms && !request->dwell_ms;
    for(size_t i=0;i<sizeof(request->reserved);++i)valid=valid && request->reserved[i]==0U;
    if(match && valid && m->owned && !m->stopping) {m->started=true;m->state=CERBERUS_MAC_RUNNING;}
    xSemaphoreGive(scan_mutex);
    if(!match)return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE,true};
    if(!valid)return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG,false};
    return mac_local_poll(context,radio,&binding,out);
}

static cerberus_mac_monitor_operation_t mac_local_stop(void *context,uint8_t radio,
    const cerberus_mac_binding_t *binding,cerberus_mac_monitor_observation_t *out)
{
    if(out)memset(out,0,sizeof(*out));
    if(!out || radio!=CERBERUS_MAC_BLE || !scan_mutex)return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG,false};
    cerberus_s3_ble_mac_monitor_t *m=context;
    xSemaphoreTake(scan_mutex,portMAX_DELAY);reconcile_host_locked();
    bool match=mac_binding_matches(m,binding);
    if(match && m->owned){m->stopping=true;m->receiving=false;m->state=CERBERUS_MAC_STOPPING;}
    xSemaphoreGive(scan_mutex);
    if(!match)return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_STATE,true};
    return mac_local_poll(context,radio,binding,out);
}

static cerberus_mac_monitor_operation_t mac_local_read(void *context,uint8_t radio,
    const cerberus_mac_binding_t *binding,cerberus_mac_snapshot_t *out,
    cerberus_mac_monitor_transfer_t *transfer)
{
    int64_t requested=esp_timer_get_time();
    if(out)memset(out,0,sizeof(*out));
    if(transfer)memset(transfer,0,sizeof(*transfer));
    if(!out || !transfer || radio!=CERBERUS_MAC_BLE || !scan_mutex)
        return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG,false};
    cerberus_s3_ble_mac_monitor_t *m=context;
    xSemaphoreTake(scan_mutex,portMAX_DELAY);reconcile_host_locked();
    bool match=mac_binding_matches(m,binding) && binding->epoch!=0U;
    int64_t sampled=esp_timer_get_time();
    bool valid=match && mac_monitor==m && m->owned && !m->stopping &&
        requested>=0 && sampled>=requested && m->started && !m->error &&
        cerberus_mac_observation_snapshot(&m->capture,(uint64_t)sampled,out);
    if(valid){transfer->request_started_us=(uint64_t)requested;transfer->complete=transfer->timing_valid=true;}
    xSemaphoreGive(scan_mutex);
    return (cerberus_mac_monitor_operation_t){valid?ESP_OK:ESP_ERR_INVALID_STATE,!match};
}

static cerberus_mac_monitor_operation_t mac_local_ack(void *context,uint8_t radio,
    const cerberus_mac_ack_request_t *request)
{
    if(!request || radio!=CERBERUS_MAC_BLE || !scan_mutex)return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG,false};
    cerberus_s3_ble_mac_monitor_t *m=context;
    xSemaphoreTake(scan_mutex,portMAX_DELAY);
    bool match=m && m->reserved && request->job_id==m->binding.job_id && request->epoch==m->binding.epoch;
    bool valid=match && request->snapshot_id && request->snapshot_id<=m->capture.next_snapshot_id;
    xSemaphoreGive(scan_mutex);
    return (cerberus_mac_monitor_operation_t){valid?ESP_OK:ESP_ERR_INVALID_STATE,!match};
}

static cerberus_mac_monitor_operation_t mac_local_renew(void *context,uint8_t radio,
    const cerberus_mac_renew_request_t *request,cerberus_mac_monitor_observation_t *out)
{
    if(!request || request->lease_ms){if(out)memset(out,0,sizeof(*out));return (cerberus_mac_monitor_operation_t){ESP_ERR_INVALID_ARG,false};}
    return mac_local_poll(context,radio,&request->binding,out);
}

void cerberus_s3_ble_scanner_mac_ops(cerberus_s3_ble_mac_monitor_t *context,
    cerberus_mac_monitor_ops_t *out)
{
    if(out)*out=(cerberus_mac_monitor_ops_t){.context=context,.now_us=mac_local_now,
        .reserve=mac_local_reserve,.start=mac_local_start,.poll=mac_local_poll,.renew=mac_local_renew,
        .read_step=mac_local_read,.ack=mac_local_ack,.stop=mac_local_stop};
}

void cerberus_s3_ble_scanner_mac_diagnostics(cerberus_mac_status_t *status,
    cerberus_mac_counters_t *counters)
{
    if(status)*status=(cerberus_mac_status_t){.version=CERBERUS_MAC_VERSION};
    if(counters)*counters=(cerberus_mac_counters_t){.version=CERBERUS_MAC_VERSION};
    if(!scan_mutex)return;
    xSemaphoreTake(scan_mutex,portMAX_DELAY);
    if(mac_monitor) {
        if(status) {
            cerberus_mac_monitor_observation_t value;
            mac_status_locked(mac_monitor,&value);*status=value.status;
        }
        if(counters)*counters=(cerberus_mac_counters_t){.epoch=mac_monitor->binding.epoch,
            .observations=mac_monitor->capture.observations,.invalid=mac_monitor->capture.invalid,
            .replacements=mac_monitor->capture.replacements,.version=CERBERUS_MAC_VERSION};
    }
    xSemaphoreGive(scan_mutex);
}
