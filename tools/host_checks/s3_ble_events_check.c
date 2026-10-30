#ifdef NDEBUG
#undef NDEBUG
#endif
#include "fake_s3_ble_platform.h"
#include "s3_ble_host.h"
#include "s3_wifi_owner.h"
#include "s3_findmy_ui_route.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned tick, held, host_epoch;
static unsigned mutex_takes, allocations;
static bool fail_discovery_lock, fail_discovery_allocation;
static bool discovery_clock_rollback;
static unsigned clock_reads;
static uint64_t clock_base_us, next_lock_delay_us;
static cerberus_s3_ble_owner_t owner;
static bool radio_active;
static bool expected_active;
static int mac_start_error, mac_cancel_error;
static unsigned mac_radio_starts, mac_radio_cancels;
static int (*radio_callback)(struct ble_gap_event *, void *);
static void *radio_arg;
static struct ble_npl_event *queue[32];
static unsigned head, tail;
static struct ble_npl_eventq eventq;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout)
{ (void)mutex; assert(!held);
  if (fail_discovery_lock && timeout != portMAX_DELAY) { fail_discovery_lock=false; return 0; }
  held = 1; ++mutex_takes;
  clock_base_us += next_lock_delay_us; next_lock_delay_us=0U; return 1; }
int xSemaphoreGive(SemaphoreHandle_t mutex)
{ (void)mutex; assert(held); held = 0; return 1; }
int64_t esp_timer_get_time(void)
{
    int64_t value = (int64_t)(clock_base_us+(uint64_t)tick*1000U);
    ++clock_reads;
    if (discovery_clock_rollback) {
        discovery_clock_rollback = false;
        clock_base_us = 0U;
        tick = 0U;
    }
    return value;
}
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 128U * 1024U; }
void *heap_caps_calloc(size_t n, size_t size, unsigned caps)
{ (void)caps; ++allocations;
  if (fail_discovery_allocation) { fail_discovery_allocation=false; return NULL; }
  return calloc(n, size); }
void heap_caps_free(void *pointer) { free(pointer); }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock"; }
cerberus_s3_wifi_owner_t cerberus_s3_wifi_owner(void) { return CERBERUS_S3_WIFI_NONE; }
cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void) { return owner; }
bool cerberus_s3_ble_host_initialized(void) { return true; }
unsigned cerberus_s3_ble_host_epoch(void) { return host_epoch; }
esp_err_t cerberus_s3_ble_host_claim(cerberus_s3_ble_owner_t requested)
{ if (owner) return ESP_ERR_INVALID_STATE; owner = requested; return ESP_OK; }
esp_err_t cerberus_s3_ble_host_release(cerberus_s3_ble_owner_t requested)
{ if (owner != requested) return ESP_ERR_INVALID_STATE; owner = CERBERUS_S3_BLE_OWNER_NONE; return ESP_OK; }
int ble_hs_id_infer_auto(int privacy, uint8_t *type)
{ (void)privacy; *type = 0; return 0; }
int ble_gap_disc(uint8_t type, int32_t duration, const struct ble_gap_disc_params *params,
    int (*callback)(struct ble_gap_event *, void *), void *arg)
{ (void)type; assert(duration == 8000 && params->passive == !expected_active && !radio_active);
  assert(!expected_active || (params->itvl == 160 && params->window == 80));
  ++mac_radio_starts;if(mac_start_error)return mac_start_error;radio_active = true;
  radio_callback = callback; radio_arg = arg; return 0; }
int ble_gap_ext_disc(uint8_t type, uint16_t duration, uint16_t period,
    uint8_t duplicates, uint8_t filter_policy, uint8_t limited,
    const struct ble_gap_ext_disc_params *uncoded, const struct ble_gap_ext_disc_params *coded,
    int (*callback)(struct ble_gap_event *, void *), void *arg)
{ (void)type; (void)duplicates; (void)filter_policy; (void)limited;
  assert(duration == 800U && period == 0U &&
      uncoded->passive == !expected_active && coded->passive && !radio_active);
  ++mac_radio_starts;if(mac_start_error)return mac_start_error;
  radio_active = true; radio_callback = callback; radio_arg = arg; return 0; }
int ble_gap_disc_cancel(void)
{ ++mac_radio_cancels;if(mac_cancel_error)return mac_cancel_error;
  if (!radio_active) return BLE_HS_EALREADY; radio_active = false; return 0; }
int ble_gap_disc_active(void) { return radio_active; }
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) { return &eventq; }
void ble_npl_event_init(struct ble_npl_event *event, void (*fn)(struct ble_npl_event *), void *arg)
{ *event = (struct ble_npl_event){.fn = fn, .arg = arg}; }
void ble_npl_event_set_arg(struct ble_npl_event *event, void *arg) { event->arg = arg; }
void *ble_npl_event_get_arg(struct ble_npl_event *event) { return event->arg; }
void ble_npl_eventq_put(struct ble_npl_eventq *unused, struct ble_npl_event *event)
{ (void)unused; if (event->queued) return; assert(tail < 32); event->queued = true; queue[tail++] = event; }
void ble_npl_callout_init(struct ble_npl_callout *callout, struct ble_npl_eventq *unused,
    void (*fn)(struct ble_npl_event *), void *arg)
{ (void)unused; ble_npl_event_init(&callout->event, fn, arg); callout->active = false; }
int ble_npl_callout_reset(struct ble_npl_callout *callout, uint32_t ticks)
{ assert(ticks); callout->active = true; return BLE_NPL_OK; }
void ble_npl_callout_stop(struct ble_npl_callout *callout) { callout->active = false; }
void ble_npl_callout_set_arg(struct ble_npl_callout *callout, void *arg)
{ callout->event.arg = arg; }
uint32_t ble_npl_time_ms_to_ticks32(uint32_t ms) { return ms; }

#include "s3_ble_scanner.c"

static void pump(void)
{ assert(head < tail); struct ble_npl_event *event = queue[head++]; event->queued = false; event->fn(event); }

static void emit_hc_report(const uint8_t *data,size_t length,int8_t rssi,
    bool response,uint8_t identity,uint8_t sid)
{
    struct ble_gap_event event={0};
    ++tick;
#if FAKE_EXT_SCAN
    event.type=BLE_GAP_EVENT_EXT_DISC;
    memcpy(event.ext_disc.addr.val,(uint8_t[]){identity,2,3,4,5,6},6U);
    event.ext_disc.addr.type=1U;event.ext_disc.rssi=rssi;
    event.ext_disc.props=response?BLE_HCI_ADV_SCAN_RSP_MASK:BLE_HCI_ADV_CONN_MASK;
    event.ext_disc.sid=sid;event.ext_disc.data_status=BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE;
    event.ext_disc.data=data;event.ext_disc.length_data=length;
#else
    (void)sid;
    event.type=BLE_GAP_EVENT_DISC;
    memcpy(event.disc.addr.val,(uint8_t[]){identity,2,3,4,5,6},6U);
    event.disc.addr.type=1U;event.disc.rssi=rssi;
    event.disc.event_type=response?BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    event.disc.data=data;event.disc.length_data=length;
#endif
    radio_callback(&event,radio_arg);
}

static bool all_zero(const void *data,size_t size)
{
    const uint8_t *bytes=data;
    for(size_t i=0;i<size;++i)if(bytes[i])return false;
    return true;
}

static void emit_wardrive_report(const cerberus_s3_ble_report_t *r)
{
    struct ble_gap_event e={0};
#if FAKE_EXT_SCAN
    e.type=BLE_GAP_EVENT_EXT_DISC;
    memcpy(e.ext_disc.addr.val,r->address,6);e.ext_disc.addr.type=r->address_type;
    e.ext_disc.rssi=r->rssi;e.ext_disc.sid=r->sid;
    e.ext_disc.props=r->scan_response?BLE_HCI_ADV_SCAN_RSP_MASK:BLE_HCI_ADV_CONN_MASK;
    e.ext_disc.data_status=r->complete?BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE:1U;
    e.ext_disc.data=r->data;e.ext_disc.length_data=r->length;
#else
    e.type=BLE_GAP_EVENT_DISC;
    memcpy(e.disc.addr.val,r->address,6);e.disc.addr.type=r->address_type;
    e.disc.rssi=r->rssi;e.disc.event_type=r->scan_response?BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    e.disc.data=r->data;e.disc.length_data=r->length;
#endif
    radio_callback(&e,radio_arg);
}

static void check_recent_inventory_callbacks(void)
{
    const uint8_t ordinary[]={2,1,6},fmna[]={3,3,0x44,0xfd},malformed[]={6,9,'x'};
    cerberus_ble_scan_status_payload_t status,bank;
    uint16_t replacements=99;
    expected_active=true;tick=10000;clock_base_us=0;
    assert(cerberus_s3_ble_scanner_start_active(&status)==ESP_OK);pump();
    const unsigned allocated=allocations;
    memset(&bank,0xa5,sizeof(bank));unsigned before=mutex_takes;
    assert(cerberus_s3_ble_scanner_get_inventory_status(status.generation,&bank,&replacements)==ESP_ERR_INVALID_STATE);
    assert(mutex_takes==before+1 && all_zero(&bank,sizeof(bank)) && !replacements);
    for(unsigned i=0;i<32;++i)emit_hc_report(ordinary,sizeof(ordinary),-70,false,(uint8_t)i,3);
    /* Refresh original 0; the first newcomer must replace original 1, not 0.
     * Response evidence does not confer a new original's recency. */
    emit_hc_report(ordinary,sizeof(ordinary),-30,false,0,3);
    emit_hc_report(fmna,sizeof(fmna),-10,true,1,3);
    emit_hc_report(malformed,sizeof(malformed),-20,false,90,3);
    emit_hc_report(fmna,sizeof(fmna),-20,true,91,3);
    emit_hc_report(ordinary,sizeof(ordinary),-90,false,99,3);
    const uint32_t original_time=tick;
    emit_hc_report(fmna,sizeof(fmna),-20,true,99,3);
    cerberus_c5_ble_result_t row;
    assert(cerberus_s3_ble_scanner_get_result(status.generation,1,&row)==ESP_ERR_INVALID_STATE);
    struct ble_gap_event done={.type=BLE_GAP_EVENT_DISC_COMPLETE};
    radio_active=false;radio_callback(&done,radio_arg);
    before=mutex_takes;
    assert(cerberus_s3_ble_scanner_get_inventory_status(status.generation,&bank,&replacements)==ESP_OK);
    assert(mutex_takes==before+1 && bank.state==CERBERUS_BLE_SCAN_COMPLETE &&
        bank.generation==status.generation && bank.stored_count==32 && bank.dropped_reports==1 && replacements==1);
    assert(cerberus_s3_ble_scanner_get_result(status.generation,0,&row)==ESP_OK&&row.address[0]==0&&row.rssi==-30);
    assert(cerberus_s3_ble_scanner_get_result(status.generation,1,&row)==ESP_OK&&row.address[0]==99&&row.rssi==-90);
    const cerberus_c5_ble_result_t frozen_peer=row;
    cerberus_s3_ble_focus_t focus;
    cerberus_c5_ble_advertisement_t raw;
    uint32_t observed_ms;
    assert(cerberus_s3_ble_scanner_get_focus(status.generation,1,&focus)==ESP_OK &&
        focus.discovery.findmy_scan_response.bits && !focus.discovery.findmy_advertisement.bits);
    assert(cerberus_s3_ble_scanner_get_advertisement(status.generation,1,&raw)==ESP_OK&&raw.reports_seen==1);
    assert(cerberus_s3_ble_scanner_get_advertisement_time(status.generation,1,&observed_ms)==ESP_OK&&observed_ms==original_time);
    /* Unchanged active route sees only the new raw ordinary advertisement;
     * passive response hints never turn it into a Sound-capable profile. */
    assert(cerberus_findmy_ui_raw_profile(row.address,row.address_type,raw.address,raw.address_type,
        raw.event_type,raw.flags,raw.data,raw.length)==CERBERUS_FINDMY_PROFILE_NONE);
    cerberus_s3_ble_record_t frozen_record=snapshot->records[1];
    emit_hc_report(ordinary,sizeof(ordinary),-1,false,100,3);
    assert(!memcmp(&frozen_record,&snapshot->records[1],sizeof(frozen_record)));
    assert(allocations==allocated && owner==CERBERUS_S3_BLE_OWNER_NONE && !radio_active);
    memset(&bank,0xa5,sizeof(bank));replacements=99;
    assert(cerberus_s3_ble_scanner_get_inventory_status((uint8_t)(status.generation+1U),&bank,&replacements)==ESP_ERR_INVALID_STATE&&
        all_zero(&bank,sizeof(bank))&&!replacements);
    replacements=99;assert(cerberus_s3_ble_scanner_get_inventory_status(status.generation,NULL,&replacements)==ESP_ERR_INVALID_ARG&&!replacements);
    memset(&bank,0xa5,sizeof(bank));assert(cerberus_s3_ble_scanner_get_inventory_status(status.generation,&bank,NULL)==ESP_ERR_INVALID_ARG&&all_zero(&bank,sizeof(bank)));
    assert(cerberus_s3_ble_scanner_start_active(&status)==ESP_OK);pump();
    assert(!snapshot->inventory_replacements);
    for(unsigned i=0;i<35;++i)emit_hc_report(ordinary,sizeof(ordinary),-60,false,(uint8_t)i,3);
    radio_active=false;radio_callback(&done,radio_arg);
    assert(cerberus_s3_ble_scanner_get_inventory_status(status.generation,&bank,&replacements)==ESP_OK&&replacements==3);
    assert(frozen_peer.address[0]==99 && frozen_record.address[0]==99 && frozen_record.last_advertisement_ms==original_time);
    assert(allocations==allocated);
    puts("Real BLE callbacks: recent replacement, completed-only atomic counters, frozen typed selection, response/raw separation and no extra allocation passed.");
}

static void check_wardrive_atomic_copy(void)
{
    cerberus_c5_ble_result_t rows[CERBERUS_BLE_SCAN_MAX_RESULTS+1U];
    cerberus_s3_ble_wardrive_evidence_t evidence[CERBERUS_BLE_SCAN_MAX_RESULTS+1U];
    const uint8_t fs[]={15,9,'F','S',' ','E','x','t',' ','B','a','t','t','e','r','y'};
    const uint8_t mfg[]={4,0xff,0xc8,9,1},a[]={3,3,0,0x31},b[]={3,3,0,0x32};
    const uint8_t ordinary[]={2,1,6},malformed[]={8,9,'x'};
    cerberus_ble_scan_status_payload_t status;
    expected_active=false;clock_base_us=0;tick=1000;
    assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK);pump();
    cerberus_s3_ble_report_t r={.address={1,2,3,4,5,6},.address_type=1,.rssi=-50,
        .complete=true,.sid=3,.sid_valid=true,.data=fs,.length=sizeof(fs)};
    emit_wardrive_report(&r);tick=1100;r.scan_response=true;r.data=mfg;r.length=sizeof(mfg);emit_wardrive_report(&r);
    /* Same-report corroboration cannot be assembled from ADV name + RSP company. */
    r.address[0]=2;r.scan_response=false;r.data=a;r.length=sizeof(a);tick=1200;emit_wardrive_report(&r);
    r.scan_response=true;r.data=b;r.length=sizeof(b);tick=1300;emit_wardrive_report(&r);
    /* Current ordinary and malformed originals replace earlier packet facts. */
    r.address[0]=3;r.scan_response=false;r.data=mfg;r.length=sizeof(mfg);tick=1400;emit_wardrive_report(&r);
    r.data=ordinary;r.length=sizeof(ordinary);tick=1500;emit_wardrive_report(&r);
    r.address[0]=4;r.data=fs;r.length=sizeof(fs);tick=1600;emit_wardrive_report(&r);
    r.data=malformed;r.length=sizeof(malformed);tick=1700;emit_wardrive_report(&r);
    /* Address-only context survives current malformed packet replacement. */
    memcpy(r.address,(uint8_t[]){1,2,3,0x81,0x8e,0x58},6);r.address_type=0;
    r.data=ordinary;r.length=sizeof(ordinary);tick=1800;emit_wardrive_report(&r);
    r.data=malformed;r.length=sizeof(malformed);tick=1900;emit_wardrive_report(&r);
    memcpy(r.address,(uint8_t[]){6,2,3,4,5,6},6);r.address_type=1;r.data=fs;r.length=sizeof(fs);
    tick=0;emit_wardrive_report(&r); /* Observation time zero is valid. */
    r.scan_response=true;r.data=mfg;r.length=sizeof(mfg);tick=2001;emit_wardrive_report(&r); /* Too old. */
    r.address[0]=7;r.scan_response=false;r.data=fs;r.length=sizeof(fs);tick=UINT32_MAX-10U;emit_wardrive_report(&r);
    r.scan_response=true;r.data=mfg;r.length=sizeof(mfg);tick=5;emit_wardrive_report(&r);
    memset(rows,0xa5,sizeof(rows));memset(evidence,0xa5,sizeof(evidence));
    unsigned locks=mutex_takes,allocated=allocations;
    assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,7,rows,evidence,32)==ESP_ERR_INVALID_STATE);
    assert(mutex_takes==locks+1U&&allocations==allocated&&all_zero(rows,32*sizeof(*rows))&&all_zero(evidence,32*sizeof(*evidence)));
    struct ble_gap_event complete={.type=BLE_GAP_EVENT_DISC_COMPLETE};
    radio_active=false;radio_callback(&complete,radio_arg);
    cerberus_s3_ble_snapshot_t frozen=*snapshot;
    locks=mutex_takes;allocated=allocations;unsigned old_head=head,old_tail=tail;
    assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,7,rows,evidence,32)==ESP_OK);
    assert(mutex_takes==locks+1U&&allocations==allocated&&head==old_head&&tail==old_tail&&!radio_active&&!owner);
    assert(!memcmp(&frozen,snapshot,sizeof(frozen)));
    assert(evidence[0].advertisement_observed_ms==1000&&evidence[0].scan_response_observed_ms==1100);
    assert(cerberus_s3_ble_wardrive_evidence_level(&evidence[0])==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE);
    assert(evidence[0].advertisement.facts==CERBERUS_BLE_FLOCK_FS_BATTERY);
    assert(evidence[0].scan_response.facts==(CERBERUS_BLE_FLOCK_NO_LOCAL_NAME|CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD));
    assert(cerberus_s3_ble_wardrive_evidence_level(&evidence[1])==CERBERUS_BLE_FLOCK_LEVEL_NONE);
    assert(evidence[1].advertisement.raven_3100_contexts&&evidence[1].scan_response.raven_3200_contexts);
    assert(evidence[2].advertisement_observed_ms==1500&&!evidence[2].scan_response_sequence);
    assert(cerberus_s3_ble_wardrive_evidence_level(&evidence[2])==CERBERUS_BLE_FLOCK_LEVEL_NONE);
    assert(all_zero(&evidence[3],sizeof(evidence[3])));
    assert(evidence[4].flags==CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE&&!evidence[4].advertisement_sequence);
    assert(cerberus_s3_ble_wardrive_evidence_level(&evidence[4])==CERBERUS_BLE_FLOCK_LEVEL_CLUE);
    assert(evidence[5].advertisement_sequence&&evidence[5].advertisement_observed_ms==0&&!evidence[5].scan_response_sequence);
    assert(evidence[6].advertisement_observed_ms==UINT32_MAX-10U&&evidence[6].scan_response_observed_ms==5U);
    for(unsigned i=0;i<7;++i){
        cerberus_c5_ble_result_t old;
        assert(cerberus_s3_ble_scanner_get_result(status.generation,i,&old)==ESP_OK&&!memcmp(&rows[i],&old,sizeof(old)));
        assert(cerberus_s3_ble_wardrive_evidence_valid(&evidence[i]));
    }
    assert(all_zero(rows+7,25*sizeof(*rows))&&all_zero(evidence+7,25*sizeof(*evidence)));
    /* Defensive provenance checks cannot donate foreign-generation/address/SID facts. */
    cerberus_s3_ble_discovery_evidence_t source=snapshot->records[0].focus.discovery;
    for(unsigned change=0;change<9;++change){
        snapshot->records[0].focus.discovery=source;
        cerberus_s3_ble_discovery_evidence_t *d=&snapshot->records[0].focus.discovery;
        if(change==0)++d->generation;
        if(change==1)++d->address[0];
        if(change==2)++d->address_type;
        if(change==3)d->flags|=CERBERUS_S3_BLE_EVIDENCE_SID_INVALID;
        if(change==4){d->flags|=CERBERUS_S3_BLE_EVIDENCE_SID_VALID;d->sid=16;}
        if(change==5)d->flags&=(uint8_t)~CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE;
        if(change==6)d->advertisement_sequence=0;
        if(change==7)d->flock_advertisement.facts=0x80;
        if(change==8)d->flags|=0x80;
        assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,7,rows,evidence,32)==ESP_OK);
        assert(all_zero(&evidence[0],sizeof(evidence[0])));
    }
    snapshot->records[0].focus.discovery=source;
    for(unsigned change=0;change<3;++change){
        snapshot->records[0].focus.discovery=source;
        cerberus_s3_ble_discovery_evidence_t *d=&snapshot->records[0].focus.discovery;
        if(change==0)d->scan_response_sequence=d->advertisement_sequence;
        if(change==1)d->scan_response_observed_ms=d->advertisement_observed_ms+2001;
        if(change==2)d->flock_scan_response.accessory_contexts=1;
        assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,7,rows,evidence,32)==ESP_OK);
        assert(evidence[0].advertisement_sequence==source.advertisement_sequence&&!evidence[0].scan_response_sequence);
    }
    *snapshot=frozen;
    for(unsigned failure=0;failure<7;++failure){
        memset(rows,0xa5,sizeof(rows));memset(evidence,0xa5,sizeof(evidence));
        if(failure==4)snapshot->records[6].name_length=CERBERUS_BLE_NAME_MAX_LENGTH+1;
        if(failure==5)snapshot->records[6].uuid_length=CERBERUS_BLE_UUID_MAX_LENGTH+1;
        esp_err_t error=cerberus_s3_ble_scanner_copy_wardrive(
            failure==0?(uint8_t)(status.generation+1):status.generation,failure==1?6:7,
            failure==2?NULL:rows,failure==3?NULL:evidence,failure==6?33:32);
        assert(error!=ESP_OK);
        if(failure!=2)assert(all_zero(rows,32*sizeof(*rows)));
        if(failure!=3)assert(all_zero(evidence,32*sizeof(*evidence)));
        assert(((uint8_t *)&rows[32])[0]==0xa5&&((uint8_t *)&evidence[32])[0]==0xa5);
        *snapshot=frozen;
    }
    assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,7,rows,evidence,6)==ESP_ERR_INVALID_ARG);
    /* Host epoch reconciliation and completed empty-bank behavior stay atomic. */
    tick=4000;assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK);pump();
    ++host_epoch;owner=CERBERUS_S3_BLE_OWNER_NONE;radio_active=false;
    locks=mutex_takes;assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,0,NULL,NULL,0)==ESP_ERR_INVALID_STATE);
    assert(mutex_takes==locks+1&&snapshot->status.state==CERBERUS_BLE_SCAN_ERROR);
    assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK);pump();radio_active=false;radio_callback(&complete,radio_arg);
    assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,0,NULL,NULL,0)==ESP_OK);
    /* A full bank remains bounded; full-source extended hints do not require
     * the separate31-byte raw-copy-complete flag. */
    assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK);pump();
    uint8_t long_fs[40]={0};memcpy(long_fs,fs,sizeof(fs));
    r.scan_response=false;r.address_type=1;r.sid=3;r.complete=true;
    for(unsigned i=0;i<CERBERUS_BLE_SCAN_MAX_RESULTS+1U;++i){
        memcpy(r.address,(uint8_t[]){(uint8_t)(20+i),2,3,4,5,6},6);
        r.data=i==CERBERUS_BLE_SCAN_MAX_RESULTS?long_fs:ordinary;
#if FAKE_EXT_SCAN
        r.length=i==CERBERUS_BLE_SCAN_MAX_RESULTS?sizeof(long_fs):sizeof(ordinary);
#else
        r.length=i==CERBERUS_BLE_SCAN_MAX_RESULTS?sizeof(fs):sizeof(ordinary);
#endif
        ++tick;emit_wardrive_report(&r);
    }
#if FAKE_EXT_SCAN
    memcpy(r.address,(uint8_t[]){52,2,3,4,5,6},6);r.scan_response=true;r.sid=4;r.data=mfg;r.length=sizeof(mfg);
    ++tick;emit_wardrive_report(&r); /* A differentSID cannot supply RSP facts. */
    assert(!(snapshot->records[0].focus.discovery.flags&CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE));
#endif
    radio_active=false;radio_callback(&complete,radio_arg);
    locks=mutex_takes;allocated=allocations;
    assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,32,rows,evidence,32)==ESP_OK);
    assert(mutex_takes==locks+1&&allocations==allocated);
    assert(evidence[0].advertisement.facts==CERBERUS_BLE_FLOCK_FS_BATTERY&&!evidence[0].scan_response_sequence);
    assert(rows[0].address[0]==52&&rows[31].address[0]==51&&snapshot->status.stored_count==32&&snapshot->inventory_replacements==1);
    cerberus_s3_ble_snapshot_t *saved_snapshot=snapshot;snapshot=NULL;
    memset(rows,0xa5,sizeof(rows));memset(evidence,0xa5,sizeof(evidence));
    assert(cerberus_s3_ble_scanner_copy_wardrive(status.generation,32,rows,evidence,32)==ESP_ERR_INVALID_STATE);
    assert(all_zero(rows,32*sizeof(*rows))&&all_zero(evidence,32*sizeof(*evidence)));
    snapshot=saved_snapshot;
    puts("Wardrive completed-bank copy: 28-byte evidence; one lock, no RF/allocation, paired source validation and atomic failure checks passed.");
}

static void check_hc_peak_callbacks(void)
{
    cerberus_ble_scan_status_payload_t status;
    cerberus_ble_analyzer_view_t view;
    const uint8_t hc03[]={6,9,'H','C','-','0','3'},hc05[]={6,9,'H','C','-','0','5'},
        hc06[]={6,9,'H','C','-','0','6'},ordinary[]={2,1,6};
    assert(cerberus_s3_ble_scanner_analyzer_begin()==ESP_OK);
    expected_active=true;
    assert(cerberus_s3_ble_scanner_start_active(&status)==ESP_OK);pump();
    emit_hc_report(hc03,sizeof(hc03),127,false,1U,3U);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view)==ESP_OK && view.last_hc_name==CERBERUS_BLE_HC_03);
    emit_hc_report(hc05,sizeof(hc05),-50,false,1U,3U);
    emit_hc_report(hc06,sizeof(hc06),127,false,1U,3U);
    emit_hc_report(hc05,sizeof(hc05),-40,false,1U,3U);
    const uint32_t adv_ms=tick;
    emit_hc_report(hc06,sizeof(hc06),20,true,1U,3U);
    const uint32_t rsp_ms=tick;
    assert(cerberus_s3_ble_scanner_analyzer_status(&view)==ESP_OK &&
        view.last_hc_name==CERBERUS_BLE_HC_06 && view.last_scan_response && view.hc_name_hint_events==5U);
    /* All-invalid original row cannot gain a peak from a response. */
    emit_hc_report(ordinary,sizeof(ordinary),-128,false,2U,3U);
    emit_hc_report(hc03,sizeof(hc03),20,true,2U,3U);
    emit_hc_report(hc03,sizeof(hc03),-60,false,3U,3U);
#if FAKE_EXT_SCAN
    emit_hc_report(hc05,sizeof(hc05),-55,false,3U,0xffU);
    emit_hc_report(hc06,sizeof(hc06),20,true,3U,3U);
#else
    emit_hc_report(ordinary,sizeof(ordinary),-55,false,3U,3U);
#endif
    emit_hc_report(ordinary,sizeof(ordinary),127,false,4U,3U);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view)==ESP_OK && !view.last_hc_name &&
        !view.last_scan_response && view.last_report_valid);
    struct ble_gap_event complete={.type=BLE_GAP_EVENT_DISC_COMPLETE};
    radio_active=false;radio_callback(&complete,radio_arg);
    cerberus_c5_ble_result_t result;
    cerberus_c5_ble_advertisement_t raw;
    cerberus_s3_ble_focus_t focus;
    uint32_t observed_ms;
    assert(cerberus_s3_ble_scanner_get_result(status.generation,0U,&result)==ESP_OK && result.rssi==-40);
    assert(cerberus_s3_ble_scanner_get_focus(status.generation,0U,&focus)==ESP_OK &&
        focus.discovery.hc_advertisement==CERBERUS_BLE_HC_05 &&
        focus.discovery.hc_scan_response==CERBERUS_BLE_HC_06 &&
        focus.discovery.advertisement_observed_ms==adv_ms && focus.discovery.scan_response_observed_ms==rsp_ms);
    assert(cerberus_s3_ble_scanner_get_advertisement(status.generation,0U,&raw)==ESP_OK &&
        raw.length==sizeof(hc05) && !raw.flags && raw.reports_seen==4U &&
        !memcmp(raw.data,hc05,sizeof(hc05)));
    assert(cerberus_s3_ble_scanner_get_advertisement_time(status.generation,0U,&observed_ms)==ESP_OK && observed_ms==adv_ms);
    assert(cerberus_findmy_ui_raw_profile(result.address,result.address_type,raw.address,
        raw.address_type,raw.event_type,raw.flags,raw.data,raw.length)==CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_s3_ble_scanner_get_result(status.generation,1U,&result)==ESP_OK && result.rssi==127);
    assert(cerberus_s3_ble_scanner_get_focus(status.generation,1U,&focus)==ESP_OK &&
        !focus.discovery.hc_advertisement && focus.discovery.hc_scan_response==CERBERUS_BLE_HC_03);
    assert(cerberus_s3_ble_scanner_get_focus(status.generation,2U,&focus)==ESP_OK &&
        !focus.discovery.hc_advertisement && !focus.discovery.hc_scan_response &&
        !(focus.category_bits&CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT));
#if FAKE_EXT_SCAN
    assert(focus.discovery.flags&CERBERUS_S3_BLE_EVIDENCE_SID_INVALID);
#endif
    cerberus_s3_ble_scanner_analyzer_end();
}

static void check_public_generation_wrap(void)
{
    cerberus_ble_scan_status_payload_t status;
    assert(cerberus_s3_ble_scanner_get_status(&status)==ESP_OK&&status.state==CERBERUS_BLE_SCAN_COMPLETE);
    const uint8_t published_generation=status.generation;
    const uint64_t published_watch_epoch=watch_epoch;
    void *old_epoch=radio_arg;
    cerberus_c5_ble_result_t frozen;
    assert(cerberus_s3_ble_scanner_get_result(published_generation,0,&frozen)==ESP_OK);
    cerberus_c5_ble_result_t original=frozen;
    struct ble_gap_event complete={.type=BLE_GAP_EVENT_DISC_COMPLETE};
    expected_active=false;
    /* These are real scanner starts and queued NimBLE callbacks, not direct
     * generation assignments. Reuse the fake queue only after it is drained. */
    for(unsigned i=1;i<=255;++i){
        assert(head==tail);head=tail=0;
        assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK&&status.generation==(uint8_t)(published_generation+i));pump();
        assert(radio_active);
        if(i==255){assert(cerberus_s3_ble_scanner_stop(&status)==ESP_OK);pump();}
        else {tick+=8000;radio_active=false;radio_callback(&complete,radio_arg);}
        assert(cerberus_s3_ble_scanner_get_status(&status)==ESP_OK&&
            status.state==(i==255?CERBERUS_BLE_SCAN_IDLE:CERBERUS_BLE_SCAN_COMPLETE));
        assert(owner==CERBERUS_S3_BLE_OWNER_NONE&&!radio_active);
    }
    assert(status.generation==(uint8_t)(published_generation-1U));
    assert(head==tail);head=tail=0;
    assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK&&status.generation==published_generation);pump();
    radio_callback(&complete,old_epoch); /* Public byte aliases, private epoch does not. */
    assert(cerberus_s3_ble_scanner_get_status(&status)==ESP_OK&&status.state==CERBERUS_BLE_SCAN_RUNNING);
    const uint8_t ordinary[]={2,1,6};emit_hc_report(ordinary,sizeof(ordinary),-42,false,99,3);
    radio_active=false;radio_callback(&complete,radio_arg);
    assert(cerberus_s3_ble_scanner_get_status(&status)==ESP_OK&&status.state==CERBERUS_BLE_SCAN_COMPLETE&&status.generation==published_generation);
    cerberus_c5_ble_result_t result;assert(cerberus_s3_ble_scanner_get_result(published_generation,0,&result)==ESP_OK&&result.address[0]==99);
    assert(!memcmp(&frozen,&original,sizeof(frozen))&&memcmp(&result,&frozen,sizeof(result)));
    assert(watch_epoch==published_watch_epoch+256U && snapshot->watch.capture_epoch==watch_epoch);
}

static bool bytes_zero(const void *data,size_t length)
{
    const uint8_t *bytes=data;
    for(size_t i=0;i<length;++i)if(bytes[i])return false;
    return true;
}

static void check_watch_motion_callbacks(void)
{
    for(unsigned scenario=0;scenario<4;++scenario){
        assert(head==tail);head=tail=0;tick=0;expected_active=false;
        clock_base_us=scenario?((UINT64_C(1)<<32)*1000U+17U):0U;
        cerberus_ble_scan_status_payload_t status;
        assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK);pump();
        uint8_t basic[31]={30,0x16,0xfa,0xff,0x0d,0,0x02,0x10,'M','O','T','I','O','N'};
        uint8_t location[31]={30,0x16,0xfa,0xff,0x0d,0,0x12,7,180,1};
        location[11]=1;location[15]=2; /* Latitude/longitude little-endian. */
        location[21]=0xd1;location[22]=7; /* Geodetic altitude encoded2001. */
        location[23]=0xd2;location[24]=7; /* Height encoded2002. */
        emit_hc_report(basic,sizeof(basic),-40,false,1,3);
        const uint64_t location_us=clock_base_us+(uint64_t)(tick+1U)*1000U;
        next_lock_delay_us=1001U;
        emit_hc_report(location,sizeof(location),-41,false,1,3);
        assert(snapshot->watch.motion_us[0]==location_us&&snapshot->watch.motion_valid==1);
        assert(snapshot->watch.motion[0].flags==31&&snapshot->watch.motion[0].speed_qms==258&&
            snapshot->watch.motion[0].direction_deg==360&&snapshot->watch.motion[0].height_encoded==2002);
        if(scenario==1)basic[8]='N'; /* Same address claims a new Basic ID. */
        emit_hc_report(basic,sizeof(basic),-42,false,1,3);
        if(scenario==2){
            memset(location+6,0,25);location[6]=0x12;location[7]=3;location[8]=181;location[9]=255;
            emit_hc_report(location,sizeof(location),-43,false,1,3);
        }
        if(scenario==3){
            /* A new ordinary sweep never carries the prior Location group. */
            struct ble_gap_event end={.type=BLE_GAP_EVENT_DISC_COMPLETE};
            radio_active=false;radio_callback(&end,radio_arg);
            assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK);pump();
            emit_hc_report(basic,sizeof(basic),-44,false,1,3);
        }
        const uint64_t source_us=snapshot->watch.qualifying_us[0];
        const uint64_t retained_location_us=snapshot->watch.motion_us[0];
        const sentinel_watch_motion_t retained_motion=snapshot->watch.motion[0];
        uint8_t malformed[31];memcpy(malformed,location,sizeof(malformed));malformed[30]=1;
        emit_hc_report(malformed,sizeof(malformed),-45,false,1,3);
        emit_hc_report(location,sizeof(location),-46,true,1,3);
        assert(snapshot->watch.qualifying_us[0]==source_us&&snapshot->watch.motion_us[0]==retained_location_us&&
            !memcmp(&retained_motion,&snapshot->watch.motion[0],sizeof(retained_motion)));
        cerberus_watch_observation_t row;cerberus_watch_receive_timing_t timing,motion_timing;
        sentinel_watch_motion_t motion;
        memset(&row,0xa5,sizeof(row));memset(&timing,0xa5,sizeof(timing));
        memset(&motion,0xa5,sizeof(motion));memset(&motion_timing,0xa5,sizeof(motion_timing));
        assert(cerberus_s3_ble_scanner_get_watch_detailed_result(status.generation,0,&row,&timing,&motion,&motion_timing)==ESP_ERR_INVALID_STATE);
        assert(bytes_zero(&row,sizeof(row))&&bytes_zero(&timing,sizeof(timing))&&
            bytes_zero(&motion,sizeof(motion))&&bytes_zero(&motion_timing,sizeof(motion_timing)));
        struct ble_gap_event end={.type=BLE_GAP_EVENT_DISC_COMPLETE};
        radio_active=false;radio_callback(&end,radio_arg);
        uint64_t requested_us=(uint64_t)esp_timer_get_time();next_lock_delay_us=1001U;
        assert(cerberus_s3_ble_scanner_get_watch_detailed_result(status.generation,0,&row,&timing,&motion,&motion_timing)==ESP_OK);
        uint64_t read_us=(uint64_t)esp_timer_get_time();
        assert(timing.valid&&timing.request_started_us==requested_us&&timing.source_age_ms==(read_us-source_us+999U)/1000U);
        assert(!memcmp(&motion,&retained_motion,sizeof(motion)));
        if(scenario==0||scenario==2){
            assert(motion_timing.valid&&motion_timing.request_started_us==requested_us&&
                motion_timing.source_age_ms==(read_us-retained_location_us+999U)/1000U);
            if(scenario==0)assert(motion_timing.source_age_ms>timing.source_age_ms&&row.latitude_e7==1&&row.longitude_e7==2&&row.altitude_dm==5);
            else assert(motion.flags==1&&!row.latitude_e7&&!row.longitude_e7&&!row.altitude_dm&&
                row.flags==SENTINEL_WATCH_PROTOCOL_HAS_ID);
        }else assert(bytes_zero(&motion,sizeof(motion))&&bytes_zero(&motion_timing,sizeof(motion_timing)));
        cerberus_watch_observation_t raw;cerberus_watch_receive_timing_t old_timing;
        assert(cerberus_s3_ble_scanner_get_watch_result(status.generation,0,&raw)==ESP_OK&&!memcmp(&row,&raw,sizeof(row)));
        assert(cerberus_s3_ble_scanner_get_watch_observed_result(status.generation,0,&raw,&old_timing)==ESP_OK&&!memcmp(&row,&raw,sizeof(row)));
        assert(cerberus_s3_ble_scanner_get_watch_detailed_result((uint8_t)(status.generation-1),0,&row,&timing,&motion,&motion_timing)==ESP_ERR_INVALID_STATE);
        assert(bytes_zero(&row,sizeof(row))&&bytes_zero(&motion,sizeof(motion))&&!timing.valid&&!motion_timing.valid);
        assert(cerberus_s3_ble_scanner_get_watch_detailed_result(status.generation,12,&row,&timing,&motion,&motion_timing)==ESP_ERR_INVALID_STATE);
        for(unsigned missing=0;missing<4;++missing){
            memset(&row,0xa5,sizeof(row));memset(&timing,0xa5,sizeof(timing));
            memset(&motion,0xa5,sizeof(motion));memset(&motion_timing,0xa5,sizeof(motion_timing));
            assert(cerberus_s3_ble_scanner_get_watch_detailed_result(status.generation,0,missing==0?NULL:&row,
                missing==1?NULL:&timing,missing==2?NULL:&motion,missing==3?NULL:&motion_timing)==ESP_ERR_INVALID_ARG);
            assert((missing==0||bytes_zero(&row,sizeof(row)))&&(missing==1||bytes_zero(&timing,sizeof(timing)))&&
                (missing==2||bytes_zero(&motion,sizeof(motion)))&&(missing==3||bytes_zero(&motion_timing,sizeof(motion_timing))));
        }
        if(scenario==0){
            clock_base_us=0;tick=0;
            assert(cerberus_s3_ble_scanner_get_watch_detailed_result(status.generation,0,&row,&timing,&motion,&motion_timing)==ESP_OK&&
                !timing.valid&&!motion_timing.valid&&!memcmp(&motion,&retained_motion,sizeof(motion)));
            snapshot->watch.motion[0].reserved=1;
            assert(cerberus_s3_ble_scanner_get_watch_detailed_result(status.generation,0,&row,&timing,&motion,&motion_timing)==ESP_ERR_INVALID_STATE&&
                bytes_zero(&row,sizeof(row))&&bytes_zero(&motion,sizeof(motion))&&!timing.valid&&!motion_timing.valid);
            snapshot->watch.motion[0]=retained_motion;snapshot->watch.capture_epoch=0;
            assert(cerberus_s3_ble_scanner_get_watch_detailed_result(status.generation,0,&row,&timing,&motion,&motion_timing)==ESP_ERR_INVALID_STATE);
        }
    }
    clock_base_us=0;tick=0;
}

static void check_watch_timing_callbacks(void)
{
    for(unsigned pass=0;pass<2;++pass){
        assert(head==tail);head=tail=0;tick=0U;
        clock_base_us=pass?(UINT64_C(1)<<32)*1000U+17U:0U;
        const uint64_t prior_epoch=watch_epoch, observed_us=clock_base_us;
        cerberus_ble_scan_status_payload_t status;
        expected_active=false;
        assert(cerberus_s3_ble_scanner_start(&status)==ESP_OK);pump();
        assert(watch_epoch==prior_epoch+1U);
        assert(cerberus_s3_ble_scanner_start(&status)==ESP_ERR_INVALID_STATE && watch_epoch==prior_epoch+1U);
        uint8_t rid[31]={30U,0x16U,0xfaU,0xffU,0x0dU,0U,0x02U,0x10U,'T','I','M','E'};
        struct ble_gap_event event={0};
#if FAKE_EXT_SCAN
        event.type=BLE_GAP_EVENT_EXT_DISC;event.ext_disc.addr.type=1U;
        memcpy(event.ext_disc.addr.val,(uint8_t[]){1,2,3,4,5,6},6U);
        event.ext_disc.rssi=-40;event.ext_disc.props=BLE_HCI_ADV_CONN_MASK;
        event.ext_disc.sid=3U;event.ext_disc.data_status=BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE;
        event.ext_disc.data=rid;event.ext_disc.length_data=sizeof(rid);
#else
        event.type=BLE_GAP_EVENT_DISC;event.disc.addr.type=1U;
        memcpy(event.disc.addr.val,(uint8_t[]){1,2,3,4,5,6},6U);
        event.disc.rssi=-40;event.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
        event.disc.data=rid;event.disc.length_data=sizeof(rid);
#endif
        next_lock_delay_us=1001U;radio_callback(&event,radio_arg);
        assert(snapshot->watch.qualifying_us[0]==observed_us && snapshot->watch.qualifying_valid==1U);
        assert(snapshot->records[0].last_advertisement_ms==(uint32_t)((observed_us+1001U)/1000U));
        rid[8]='R';clock_base_us=observed_us+2000U;
#if FAKE_EXT_SCAN
        event.ext_disc.props=BLE_HCI_ADV_SCAN_RSP_MASK;
#else
        event.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
#endif
        radio_callback(&event,radio_arg);
        assert(snapshot->watch.qualifying_us[0]==observed_us && !strcmp(snapshot->watch.records[0].id,"TIME"));
        cerberus_watch_observation_t row;
        cerberus_watch_receive_timing_t timing;
        memset(&row,0xa5,sizeof(row));memset(&timing,0xa5,sizeof(timing));
        assert(cerberus_s3_ble_scanner_get_watch_observed_result(status.generation,0U,&row,&timing)==ESP_ERR_INVALID_STATE);
        assert(!row.kind && !timing.valid && !timing.source_age_ms && !timing.request_started_us);
        struct ble_gap_event complete={.type=BLE_GAP_EVENT_DISC_COMPLETE};
        radio_active=false;radio_callback(&complete,radio_arg);
        clock_base_us=observed_us+3000U;next_lock_delay_us=1001U;
        assert(cerberus_s3_ble_scanner_get_watch_observed_result(status.generation,0U,&row,&timing)==ESP_OK);
        assert(!strcmp(row.id,"TIME") && timing.valid && timing.request_started_us==observed_us+3000U &&
            timing.source_age_ms==5U); /* ceil((4001us since entry)/1000) */
        cerberus_watch_observation_t old_api;
        assert(cerberus_s3_ble_scanner_get_watch_result(status.generation,0U,&old_api)==ESP_OK && !memcmp(&row,&old_api,sizeof(row)));
        assert(cerberus_s3_ble_scanner_get_watch_observed_result((uint8_t)(status.generation-1U),0U,&row,&timing)==ESP_ERR_INVALID_STATE);
        assert(cerberus_s3_ble_scanner_get_watch_observed_result(status.generation,1U,&row,&timing)==ESP_ERR_INVALID_STATE);
        assert(cerberus_s3_ble_scanner_get_watch_observed_result(status.generation,0U,NULL,&timing)==ESP_ERR_INVALID_ARG);
        if(pass){
            clock_base_us=observed_us-1U;
            assert(cerberus_s3_ble_scanner_get_watch_observed_result(status.generation,0U,&row,&timing)==ESP_OK && !timing.valid);
        }
        const uint64_t retained_epoch=watch_epoch;
        ++host_epoch; /* Reinitializing the host must not reset timing identity. */
        assert(cerberus_s3_ble_scanner_get_status(&status)==ESP_OK && watch_epoch==retained_epoch);
    }
    /* Exhaustion fails before claiming the host or destroying the prior bank. */
    cerberus_s3_ble_snapshot_t saved=*snapshot;
    cerberus_ble_scan_status_payload_t status;
    unsigned queued=tail;watch_epoch=UINT64_MAX;
    assert(cerberus_s3_ble_scanner_start(&status)==ESP_ERR_INVALID_STATE &&
        owner==CERBERUS_S3_BLE_OWNER_NONE && tail==queued && !memcmp(snapshot,&saved,sizeof(saved)));
    clock_base_us=0U;
}


#include "s3_ble_mac_monitor_check.h"
#include "s3_ble_discovery_native_check.h"

int main(void)
{
    check_discovery_initial_diagnostics();
    cerberus_ble_scan_status_payload_t status = {0};
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_OK && status.state == CERBERUS_BLE_SCAN_RUNNING);
    assert(owner == CERBERUS_S3_BLE_OWNER_SCANNER);
    pump(); /* START reaches NimBLE. */
    assert(radio_active);
    int (*old_callback)(struct ble_gap_event *, void *) = radio_callback;
    void *old_arg = radio_arg;

    assert(cerberus_s3_ble_scanner_stop(&status) == ESP_OK); /* STOP queued, not handled. */
    unsigned queued_after_stop = tail;
    assert(cerberus_s3_ble_scanner_stop(&status) == ESP_OK && tail == queued_after_stop);
    struct ble_gap_event complete = {.type = BLE_GAP_EVENT_DISC_COMPLETE};
    radio_active = false;
    old_callback(&complete, old_arg); /* Natural completion wins the race. */
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE && !radio_active);
    /* The old STOP is still queued and may not reach a new sweep. */
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_ERR_INVALID_STATE);
    pump(); /* Old STOP drains. */
    assert(!stop_event_pending);
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_OK && status.generation == 2U);
    pump();
    assert(radio_active && owner == CERBERUS_S3_BLE_OWNER_SCANNER);
    old_callback(&complete, old_arg); /* Stale GAP completion is epoch rejected. */
    assert(cerberus_s3_ble_scanner_get_status(&status) == ESP_OK &&
        status.state == CERBERUS_BLE_SCAN_RUNNING && status.generation == 2U);
    assert(cerberus_s3_ble_scanner_stop(&status) == ESP_OK);
    pump(); /* STOP of current sweep. */
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE && stop_event_pending == false);
    assert(!radio_active);
    /* Host reset has no GAP completion. A queued STOP and old GAP callback
     * cannot finish or cancel the next scan generation. */
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_OK);
    pump();
    int (*reset_callback)(struct ble_gap_event *, void *) = radio_callback;
    void *reset_arg = radio_arg;
    assert(cerberus_s3_ble_scanner_stop(&status) == ESP_OK);
    ++host_epoch;
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    radio_active = false;
    assert(cerberus_s3_ble_scanner_get_status(&status) == ESP_OK &&
        status.state == CERBERUS_BLE_SCAN_ERROR &&
        status.error_code == ESP_ERR_INVALID_STATE && !stop_event_pending);
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_OK);
    pump(); /* Old STOP is stale. */
    pump(); /* New START reaches NimBLE. */
    assert(status.state == CERBERUS_BLE_SCAN_RUNNING && radio_active);
    reset_callback(&complete, reset_arg);
    assert(cerberus_s3_ble_scanner_get_status(&status) == ESP_OK &&
        status.state == CERBERUS_BLE_SCAN_RUNNING);
    assert(cerberus_s3_ble_scanner_stop(&status) == ESP_OK);
    pump();
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE && !radio_active);
    /* Analyzer consumes every live GAP report, survives bounded rearm, and
     * retires its evidence on a host reset without changing Fox Hunt state. */
    assert(cerberus_s3_ble_scanner_analyzer_begin() == ESP_OK);
    cerberus_ble_analyzer_view_t view = {0};
    assert(cerberus_s3_ble_scanner_analyzer_status(&view) == ESP_OK &&
        view.active && view.total_events == 0U);
    uint8_t target[6] = {1, 2, 3, 4, 5, 6};
    assert(cerberus_s3_ble_scanner_track_begin(target, 1U) == ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_OK);
    pump();
    assert(cerberus_s3_ble_scanner_analyzer_begin() == ESP_ERR_INVALID_STATE);
    uint8_t meta_ad[] = {3U, 0xFFU, 0x5FU, 0xFDU};
    struct ble_gap_event adv = {0};
#if FAKE_EXT_SCAN
    adv.type = BLE_GAP_EVENT_EXT_DISC;
    memcpy(adv.ext_disc.addr.val, target, sizeof(target));
    adv.ext_disc.addr.type = 1U;
    adv.ext_disc.rssi = -58;
    adv.ext_disc.data_status = BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE;
    adv.ext_disc.data = meta_ad;
    adv.ext_disc.length_data = sizeof(meta_ad);
#else
    adv.type = BLE_GAP_EVENT_DISC;
    memcpy(adv.disc.addr.val, target, sizeof(target));
    adv.disc.addr.type = 1U;
    adv.disc.rssi = -58;
    adv.disc.data = meta_ad;
    adv.disc.length_data = sizeof(meta_ad);
#endif
    radio_callback(&adv, radio_arg);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view) == ESP_OK &&
        view.total_events == 1U && view.current_events == 1U &&
        view.meta_hint_events == 1U && view.last_rssi_dbm == -58);
    assert(view.last_report_valid && !view.last_scan_response &&
        cerberus_ble_meta_evidence_has(&view.last_meta,0U,CERBERUS_BLE_META_MFG16));
    tick += CERBERUS_BLE_ANALYZER_BUCKET_MS;
    assert(cerberus_s3_ble_scanner_analyzer_status(&view) == ESP_OK &&
        view.history_count == 1U && view.history[0] == 1U);
    assert(cerberus_s3_ble_scanner_stop(&status) == ESP_OK);
    pump();
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE);
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_OK);
    pump();
#if FAKE_EXT_SCAN
    adv.ext_disc.props = BLE_HCI_ADV_SCAN_RSP_MASK;
#else
    adv.disc.event_type = BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
#endif
    radio_callback(&adv, radio_arg);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view) == ESP_OK &&
        view.total_events == 2U);
    assert(view.last_report_valid && view.last_scan_response &&
        cerberus_ble_meta_evidence_has(&view.last_meta,0U,CERBERUS_BLE_META_MFG16));
#if FAKE_EXT_SCAN
    uint8_t extended_meta[40]={35U,0x30U};
    memcpy(extended_meta+36U,meta_ad,sizeof(meta_ad));
    adv.ext_disc.props=0U;
    adv.ext_disc.data=extended_meta;
    adv.ext_disc.length_data=sizeof(extended_meta);
    radio_callback(&adv,radio_arg);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view)==ESP_OK &&
        view.total_events==3U && view.meta_hint_events==3U && view.last_report_valid && !view.last_scan_response);
    assert(cerberus_ble_meta_evidence_has(&view.last_meta,0U,CERBERUS_BLE_META_MFG16));
    adv.ext_disc.data_status=1U;
    radio_callback(&adv,radio_arg);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view)==ESP_OK &&
        view.total_events==4U && !view.last_report_valid && !cerberus_ble_meta_evidence_count(&view.last_meta));
#endif
    ++host_epoch;
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    radio_active = false;
    assert(cerberus_s3_ble_scanner_get_status(&status) == ESP_OK &&
        status.state == CERBERUS_BLE_SCAN_ERROR);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view) == ESP_ERR_INVALID_STATE);
    cerberus_s3_ble_scanner_analyzer_end();
    /* Active device-list scanning requests 1M scan responses while the coded
     * PHY stays passive. A response enriches the same observed identity only. */
    expected_active = true;
    assert(cerberus_s3_ble_scanner_start_active(&status) == ESP_OK);
    pump();
    assert(radio_active);
    const uint8_t short_ad[] = {
        3U, 0x08U, 'T', 'e', 3U, 0xFFU, 0x34U, 0x12U,
        3U, 0x03U, 0x44U, 0xFDU,
    };
    const uint8_t response_ad[] = {5U, 0x09U, 'T', 'e', 's', 't',
        3U, 0x03U, 0x81U, 0x30U,3U,0x16U,0x5FU,0xFDU,5U,3U,0U,0x31U,0U,0x32U};
    struct ble_gap_event scan_ad = {0};
    tick += 1000U;
    const uint32_t raw_ad_at_ms = tick;
#if FAKE_EXT_SCAN
    scan_ad.type = BLE_GAP_EVENT_EXT_DISC;
    memcpy(scan_ad.ext_disc.addr.val, target, sizeof(target));
    scan_ad.ext_disc.addr.type = 1U;
    scan_ad.ext_disc.props = BLE_HCI_ADV_LEGACY_MASK | BLE_HCI_ADV_CONN_MASK;
    scan_ad.ext_disc.legacy_event_type = BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    scan_ad.ext_disc.data_status = BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE;
    scan_ad.ext_disc.data = short_ad;
    scan_ad.ext_disc.length_data = sizeof(short_ad);
    radio_callback(&scan_ad, radio_arg);
    tick += 100U;
    scan_ad.ext_disc.props = BLE_HCI_ADV_LEGACY_MASK | BLE_HCI_ADV_SCAN_RSP_MASK;
    scan_ad.ext_disc.legacy_event_type = BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
    scan_ad.ext_disc.data = response_ad;
    scan_ad.ext_disc.length_data = sizeof(response_ad);
#else
    scan_ad.type = BLE_GAP_EVENT_DISC;
    memcpy(scan_ad.disc.addr.val, target, sizeof(target));
    scan_ad.disc.addr.type = 1U;
    scan_ad.disc.event_type = BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    scan_ad.disc.data = short_ad;
    scan_ad.disc.length_data = sizeof(short_ad);
    radio_callback(&scan_ad, radio_arg);
    tick += 100U;
    scan_ad.disc.event_type = BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
    scan_ad.disc.data = response_ad;
    scan_ad.disc.length_data = sizeof(response_ad);
#endif
    radio_callback(&scan_ad, radio_arg);
    radio_active = false;
    radio_callback(&complete, radio_arg);
    cerberus_c5_ble_result_t result = {0};
    cerberus_c5_ble_advertisement_t raw = {0};
    assert(cerberus_s3_ble_scanner_get_result(status.generation, 0U, &result) == ESP_OK);
    assert(memcmp(result.name, "Test", 4U) == 0);
    assert(result.manufacturer_id == 0x1234U);
    assert(cerberus_s3_ble_scanner_get_advertisement(status.generation, 0U, &raw) == ESP_OK);
    assert(raw.length == sizeof(short_ad) && raw.reports_seen == 1U &&
        memcmp(raw.data, short_ad, sizeof(short_ad)) == 0);
    uint32_t observed_ms = 0U;
    assert(cerberus_s3_ble_scanner_get_advertisement_time(status.generation,
        0U, &observed_ms) == ESP_OK && observed_ms == raw_ad_at_ms);
    assert(cerberus_findmy_ui_raw_profile(result.address, result.address_type,
        raw.address, raw.address_type, raw.event_type, raw.flags,
        raw.data, raw.length) == CERBERUS_FINDMY_PROFILE_FMNA);
    cerberus_s3_ble_focus_t focus = {0};
    assert(cerberus_s3_ble_scanner_get_focus(status.generation, 0U, &focus) == ESP_OK);
    assert(focus.discovery.findmy_advertisement.bits==CERBERUS_BLE_FINDMY_HINT_FMNA);
    assert(!focus.discovery.flipper_advertisement.bits);
    assert(focus.discovery.flipper_scan_response.bits==CERBERUS_BLE_FLIPPER_HINT_BLACK);
    assert(cerberus_ble_meta_evidence_has(&focus.discovery.meta_scan_response,0U,CERBERUS_BLE_META_DATA16));
    assert(cerberus_ble_flock_hints_level(&focus.discovery.flock_scan_response)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE &&
        focus.discovery.flock_scan_response.raven_3100_contexts==CERBERUS_BLE_FLOCK_LIST16 &&
        focus.discovery.flock_scan_response.raven_3200_contexts==CERBERUS_BLE_FLOCK_LIST16);
    assert(!cerberus_ble_flock_hints_level(&focus.discovery.flock_advertisement) && !focus.flock_oui_clue);
    assert(!cerberus_ble_meta_evidence_count(&focus.discovery.meta_advertisement) && !focus.meta_suppressed);
    assert(focus.discovery.advertisement_observed_ms==raw_ad_at_ms &&
        focus.discovery.scan_response_observed_ms==raw_ad_at_ms+100U);
    tick = raw_ad_at_ms + CERBERUS_FINDMY_SELECTED_MAX_AGE_MS + 1U;
    /* Opening an old completed row cannot replace its RF observation time. */
    assert(cerberus_s3_ble_scanner_get_advertisement_time(status.generation,
        0U, &observed_ms) == ESP_OK && observed_ms == raw_ad_at_ms);
    assert(!cerberus_findmy_ui_fresh(observed_ms, tick));
    expected_active = false;
    assert(cerberus_s3_ble_scanner_start(&status) == ESP_OK);
    assert(cerberus_s3_ble_scanner_get_advertisement_time(
        (uint8_t)(status.generation - 1U), 0U, &observed_ms) ==
        ESP_ERR_INVALID_STATE);
    pump();
    assert(cerberus_s3_ble_scanner_stop(&status) == ESP_OK);
    pump();
#if FAKE_EXT_SCAN
    /* An unavailable extended SID is not the legacy no-SID identity. Publish
     * it explicitly so it cannot retain old hints or bind a later response. */
    expected_active = true;
    assert(cerberus_s3_ble_scanner_start_active(&status) == ESP_OK);
    pump();
    scan_ad.ext_disc.props = BLE_HCI_ADV_CONN_MASK;
    scan_ad.ext_disc.sid = 3U;
    scan_ad.ext_disc.data = short_ad;
    scan_ad.ext_disc.length_data = sizeof(short_ad);
    tick += 10U;
    radio_callback(&scan_ad, radio_arg);
    scan_ad.ext_disc.sid = 0xFFU;
    tick += 10U;
    radio_callback(&scan_ad, radio_arg);
    const uint32_t invalid_sid_at_ms = tick;
    scan_ad.ext_disc.props = BLE_HCI_ADV_SCAN_RSP_MASK;
    scan_ad.ext_disc.sid = 3U;
    tick += 10U;
    radio_callback(&scan_ad, radio_arg);
    radio_active = false;
    radio_callback(&complete, radio_arg);
    assert(cerberus_s3_ble_scanner_get_focus(status.generation, 0U, &focus) == ESP_OK);
    assert(focus.discovery.flags & CERBERUS_S3_BLE_EVIDENCE_SID_INVALID);
    assert(!(focus.discovery.flags & (CERBERUS_S3_BLE_EVIDENCE_ADV_VALID |
        CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID)));
    assert(focus.discovery.advertisement_observed_ms == invalid_sid_at_ms);
    assert(!focus.discovery.findmy_advertisement.bits && !focus.discovery.findmy_scan_response.bits);
    assert(!focus.discovery.flipper_advertisement.bits && !focus.discovery.flipper_scan_response.bits);
    assert(!cerberus_ble_meta_evidence_count(&focus.discovery.meta_advertisement) &&
        !cerberus_ble_meta_evidence_count(&focus.discovery.meta_scan_response));
    assert(!(focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY));
#endif
    /* Real callback/getter publication refreshes iBeacon payloads and binds
     * response metadata without changing the separately captured original. */
    assert(cerberus_s3_ble_scanner_analyzer_begin()==ESP_OK);
    expected_active=true;
    assert(cerberus_s3_ble_scanner_start_active(&status)==ESP_OK);
    pump();
    uint8_t beacon_a[27]={26U,0xffU,0x4cU,0U,2U,0x15U};
    beacon_a[23]=0x11U;beacon_a[26]=0x80U;
    uint8_t beacon_b[54];memcpy(beacon_b,beacon_a,sizeof(beacon_a));
    beacon_b[23]=0x22U;memcpy(beacon_b+27U,beacon_b,27U);
    tick+=100U;
#if FAKE_EXT_SCAN
    scan_ad.ext_disc.props=BLE_HCI_ADV_LEGACY_MASK|BLE_HCI_ADV_CONN_MASK;
    scan_ad.ext_disc.legacy_event_type=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    scan_ad.ext_disc.sid=0U;
    scan_ad.ext_disc.data_status=BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE;
    scan_ad.ext_disc.data=beacon_a;scan_ad.ext_disc.length_data=sizeof(beacon_a);
#else
    scan_ad.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    scan_ad.disc.data=beacon_a;scan_ad.disc.length_data=sizeof(beacon_a);
#endif
    radio_callback(&scan_ad,radio_arg);
    tick+=10U;const uint32_t original_beacon_ms=tick;
#if FAKE_EXT_SCAN
    scan_ad.ext_disc.props=BLE_HCI_ADV_CONN_MASK;scan_ad.ext_disc.sid=3U;
    scan_ad.ext_disc.data=beacon_b;scan_ad.ext_disc.length_data=sizeof(beacon_b);
#else
    scan_ad.disc.data=beacon_b;scan_ad.disc.length_data=27U;
#endif
    radio_callback(&scan_ad,radio_arg);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view)==ESP_OK && view.last_ibeacon_valid &&
        view.last_ibeacon.major==0x22U && !view.last_scan_response);
    tick+=10U;const uint32_t response_beacon_ms=tick;
#if FAKE_EXT_SCAN
    scan_ad.ext_disc.props=BLE_HCI_ADV_SCAN_RSP_MASK;
    scan_ad.ext_disc.data=beacon_a;scan_ad.ext_disc.length_data=sizeof(beacon_a);
#else
    scan_ad.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
    scan_ad.disc.data=beacon_a;scan_ad.disc.length_data=sizeof(beacon_a);
#endif
    radio_callback(&scan_ad,radio_arg);
    assert(cerberus_s3_ble_scanner_analyzer_status(&view)==ESP_OK && view.last_ibeacon_valid &&
        view.last_ibeacon.major==0x11U && view.last_ibeacon.measured_power_dbm==-128 && view.last_scan_response);
    radio_active=false;radio_callback(&complete,radio_arg);
    assert(cerberus_s3_ble_scanner_get_focus(status.generation,0U,&focus)==ESP_OK);
    assert(focus.ibeacon_valid && focus.ibeacon_scan_response && focus.ibeacon.major==0x11U &&
        focus.ibeacon_observed_ms==response_beacon_ms && cerberus_s3_ble_ibeacon_is_current(&focus));
    assert(cerberus_s3_ble_scanner_get_advertisement_time(status.generation,0U,&observed_ms)==ESP_OK &&
        observed_ms==original_beacon_ms);
    assert(cerberus_s3_ble_scanner_get_advertisement(status.generation,0U,&raw)==ESP_OK && raw.data[23]==0x22U);
#if FAKE_EXT_SCAN
    assert(raw.flags==CERBERUS_BLE_ADVERTISEMENT_TRUNCATED && raw.length==31U);
#else
    assert(!raw.flags && raw.length==27U);
#endif
    cerberus_s3_ble_scanner_analyzer_end();
    check_hc_peak_callbacks();
    check_recent_inventory_callbacks();
    check_public_generation_wrap();
    check_watch_motion_callbacks();
    check_wardrive_atomic_copy();
    check_watch_timing_callbacks();
    check_mac_monitor();
    check_discovery_native();
    puts("S3 BLE event checks passed: finite duration, stale callbacks, reset reconciliation, 256-start byte wrap with fresh records, and safe rearm.");
}
