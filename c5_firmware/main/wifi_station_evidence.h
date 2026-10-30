/* Private to the station service. Allocate only when capturing an AP, never
 * inside RX. Each AP retains exactly the bounded table exported to S3. */
enum { STATION_EVIDENCE_LIFETIME_MS=3600000 };
typedef struct {
    uint8_t mac[6];
    uint64_t seen_ms;
} station_evidence_row_t;
typedef struct {
    uint8_t bssid[6],channel,count;
    uint64_t captured_ms;
    station_evidence_row_t rows[CERBERUS_WIFI_STATION_LIMIT];
} station_evidence_ap_t;
static station_evidence_ap_t *station_evidence[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static station_evidence_ap_t *station_active_evidence;
static uint64_t station_evidence_now(void){return (uint64_t)(esp_timer_get_time()/1000);}

/* scanner_mutex held; capture is idle. All allocation happens before ownership
 * is reserved. Reuse expired/oldest AP storage only at the 32-AP limit. */
static esp_err_t station_evidence_prepare(const cerberus_wifi_station_request_t *r)
{
    unsigned slot=0;uint64_t oldest=UINT64_MAX;bool found=false;
    for(unsigned i=0;i<CERBERUS_WIFI_SCAN_MAX_RESULTS;++i){
        station_evidence_ap_t *ap=station_evidence[i];
        if(ap&&ap->channel==r->channel&&!memcmp(ap->bssid,r->bssid,6)){slot=i;found=true;break;}
    }
    if(!found)for(unsigned i=0;i<CERBERUS_WIFI_SCAN_MAX_RESULTS;++i){
        station_evidence_ap_t *ap=station_evidence[i];
        if(!ap){slot=i;break;}
        if(ap->captured_ms<oldest){oldest=ap->captured_ms;slot=i;}
    }
    station_evidence_ap_t *ap=station_evidence[slot];
    if(!ap){
        if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24576U+sizeof(*ap))return ESP_ERR_NO_MEM;
        ap=calloc(1,sizeof(*ap));if(!ap)return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&defense_capture_mux);
    memset(ap,0,sizeof(*ap));memcpy(ap->bssid,r->bssid,6);ap->channel=r->channel;
    ap->captured_ms=station_evidence_now();station_evidence[slot]=ap;station_active_evidence=ap;
    portEXIT_CRITICAL(&defense_capture_mux);return ESP_OK;
}
/* Capture mux held. Overflow traffic cannot evict rows visible in the UI. */
static void station_remember(const uint8_t mac[6])
{
    if(!station_active_evidence)return;
    for(unsigned i=0;i<station_table.status.count;++i)if(!memcmp(station_table.rows[i].mac,mac,6)){
        station_evidence_row_t *row=&station_active_evidence->rows[i];
        memcpy(row->mac,mac,6);row->seen_ms=station_evidence_now();
        station_active_evidence->count=station_table.status.count;return;
    }
}
/* Capture mux held. A 64-bit monotonic timestamp cannot resurrect old evidence
 * at the 32-bit RTOS elapsed-time wrap. A recapture replaces only that AP. */
static bool station_evidence_valid(const cerberus_wifi_ap_job_t *r)
{
    uint64_t now=station_evidence_now();
    for(unsigned i=0;i<CERBERUS_WIFI_SCAN_MAX_RESULTS;++i){
        const station_evidence_ap_t *ap=station_evidence[i];
        if(!ap||ap->channel!=r->channel||memcmp(ap->bssid,r->bssid,6))continue;
        for(unsigned j=0;j<ap->count;++j){
            const station_evidence_row_t *row=&ap->rows[j];
            if(now>=row->seen_ms&&now-row->seen_ms<STATION_EVIDENCE_LIFETIME_MS&&!memcmp(row->mac,r->station,6))return true;
        }
        return false;
    }
    return false;
}
