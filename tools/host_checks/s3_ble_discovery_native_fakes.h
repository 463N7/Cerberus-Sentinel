/* Native scanner adapter only: the live header, controller, history, view,
 * hit-test and selected-detail handoff are compiled from production source. */
static struct {
    cerberus_s3_ble_discovery_bank_t bank;
    uint64_t counter;
    unsigned start,copy,poll,stop,release;
    unsigned stop_errors,copy_errors,release_errors;
    bool reject_start,ambiguous_start,hold_stop;
} discovery_native;

static void discovery_native_finish(bool cancelled)
{
    discovery_native.bank.meta.status.state = cancelled ? CERBERUS_BLE_SCAN_IDLE : CERBERUS_BLE_SCAN_COMPLETE;
    discovery_native.bank.meta.owned = discovery_native.bank.meta.stopping = false;
    ++discovery_native.bank.meta.revision;
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    scan = discovery_native.bank.meta.status;
}

esp_err_t cerberus_s3_ble_scanner_start_discovery(cerberus_s3_ble_filter_t family,
    uint64_t previous,cerberus_s3_ble_discovery_meta_t *out)
{
    ++discovery_native.start;
    memset(out,0,sizeof(*out));
    const cerberus_s3_ble_discovery_meta_t *old=&discovery_native.bank.meta;
    if(discovery_native.reject_start)return ESP_ERR_INVALID_STATE;
    if(previous){
        assert(old->attached&&old->scan_epoch==previous&&old->family==family);
        assert(!old->owned&&!old->stopping&&old->status.state==CERBERUS_BLE_SCAN_COMPLETE);
    }else assert(!old->attached);
    assert(owner==CERBERUS_S3_BLE_OWNER_NONE);
    memset(&discovery_native.bank,0,sizeof(discovery_native.bank));
    discovery_native.bank.meta=(cerberus_s3_ble_discovery_meta_t){
        .scan_epoch=++discovery_native.counter,.revision=1,.started_ms=(uint64_t)clock_ms,
        .source_now_ms=(uint64_t)clock_ms,.family=family,.attached=true,.owned=true,
        .status={.state=CERBERUS_BLE_SCAN_RUNNING,.generation=(uint8_t)discovery_native.counter}};
    owner=CERBERUS_S3_BLE_OWNER_SCANNER;
    scan=discovery_native.bank.meta.status;
    *out=discovery_native.bank.meta;
    return discovery_native.ambiguous_start?ESP_ERR_TIMEOUT:ESP_OK;
}

static esp_err_t discovery_native_status(uint64_t epoch,cerberus_s3_ble_discovery_meta_t *out)
{
    memset(out,0,sizeof(*out));
    if(!discovery_native.bank.meta.attached||epoch!=discovery_native.bank.meta.scan_epoch)return ESP_ERR_NOT_FOUND;
    discovery_native.bank.meta.source_now_ms=(uint64_t)clock_ms;
    *out=discovery_native.bank.meta;
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_discovery_status(uint64_t epoch,cerberus_s3_ble_discovery_meta_t *out)
{++discovery_native.poll;return discovery_native_status(epoch,out);}

esp_err_t cerberus_s3_ble_scanner_copy_discovery(uint64_t epoch,cerberus_s3_ble_discovery_bank_t *out)
{
    ++discovery_native.copy;
    memset(out,0,sizeof(*out));
    if(discovery_native.copy_errors){--discovery_native.copy_errors;return ESP_ERR_TIMEOUT;}
    cerberus_s3_ble_discovery_meta_t meta;
    esp_err_t error=discovery_native_status(epoch,&meta);
    if(error!=ESP_OK)return error;
    if(meta.status.state!=CERBERUS_BLE_SCAN_RUNNING&&meta.status.state!=CERBERUS_BLE_SCAN_COMPLETE)return ESP_ERR_INVALID_STATE;
    *out=discovery_native.bank;
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_stop_discovery(uint64_t epoch,cerberus_s3_ble_discovery_meta_t *out)
{
    ++discovery_native.stop;
    memset(out,0,sizeof(*out));
    if(discovery_native.stop_errors){--discovery_native.stop_errors;return ESP_ERR_TIMEOUT;}
    if(!discovery_native.bank.meta.attached||epoch!=discovery_native.bank.meta.scan_epoch)return ESP_ERR_NOT_FOUND;
    if(discovery_native.bank.meta.owned||discovery_native.bank.meta.stopping){
        if(discovery_native.hold_stop)discovery_native.bank.meta.stopping=true;
        else discovery_native_finish(true);
    }
    return discovery_native_status(epoch,out);
}

esp_err_t cerberus_s3_ble_scanner_release_discovery(uint64_t epoch)
{
    ++discovery_native.release;
    if(!discovery_native.bank.meta.attached||epoch!=discovery_native.bank.meta.scan_epoch)return ESP_ERR_NOT_FOUND;
    assert(!discovery_native.bank.meta.owned&&!discovery_native.bank.meta.stopping);
    if(discovery_native.release_errors){--discovery_native.release_errors;return ESP_ERR_TIMEOUT;}
    discovery_native.bank.meta.attached=false;
    return ESP_OK;
}
