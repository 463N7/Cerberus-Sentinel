/* Included after the actual scanner and shared fake platform. Exercises both
 * real legacy and extended callback branches, not a replica of the adapter. */
static unsigned mac_checks;
#define MC(x) do {++mac_checks;if(!(x)){fprintf(stderr,"MAC line %u: %s\n",__LINE__,#x);abort();}}while(0)
static void mac_emit(uint8_t type,bool response,bool partial,int rssi,
    const uint8_t *data,size_t length,unsigned identity)
{
    struct ble_gap_event e={0};
#if FAKE_EXT_SCAN
    e.type=BLE_GAP_EVENT_EXT_DISC;e.ext_disc.addr.type=type;e.ext_disc.addr.val[0]=identity;
    e.ext_disc.addr.val[1]=identity>>8;e.ext_disc.rssi=rssi;e.ext_disc.sid=255;
    e.ext_disc.props=response?BLE_HCI_ADV_SCAN_RSP_MASK:BLE_HCI_ADV_CONN_MASK;
    e.ext_disc.data_status=partial?1:0;e.ext_disc.data=data;e.ext_disc.length_data=length;
#else
    (void)partial;e.type=BLE_GAP_EVENT_DISC;e.disc.addr.type=type;e.disc.addr.val[0]=identity;
    e.disc.addr.val[1]=identity>>8;e.disc.rssi=rssi;
    e.disc.event_type=response?BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    e.disc.data=data;e.disc.length_data=length;
#endif
    radio_callback(&e,radio_arg);
}
static void mac_complete(void)
{
    struct ble_gap_event complete={.type=BLE_GAP_EVENT_DISC_COMPLETE};
    radio_active=false;radio_callback(&complete,radio_arg);
}
static cerberus_mac_start_request_t mac_request(cerberus_mac_binding_t b)
{return (cerberus_mac_start_request_t){.boot_id=b.boot_id,.job_id=b.job_id,.version=1};}
static void mac_finish(cerberus_mac_monitor_ops_t *ops,cerberus_mac_binding_t *b)
{
    cerberus_mac_monitor_observation_t o;
    MC(!ops->stop(ops->context,1,b,&o).error);
    while(head<tail)pump();
    MC(!ops->poll(ops->context,1,b,&o).error);MC(!(o.status.flags&CERBERUS_MAC_OWNED));
    MC(!mac_monitor);head=tail=0;
}
static void check_mac_monitor(void)
{
    MC(!radio_active&&owner==CERBERUS_S3_BLE_OWNER_NONE&&!mac_monitor);
    head=tail=0;expected_active=false;tick=0;clock_base_us=0;
    watch_epoch=1; /* Prior independent exhaustion fixture deliberately leaves MAX. */
    cerberus_s3_ble_mac_monitor_t local={0},other={0};cerberus_mac_monitor_ops_t ops,other_ops;
    cerberus_mac_binding_t binding,foreign;cerberus_mac_monitor_observation_t o;
    cerberus_mac_snapshot_t bank,old;cerberus_mac_monitor_transfer_t transfer;
    cerberus_mac_status_t diagnostic;cerberus_mac_counters_t counters;
    unsigned allocated=allocations,starts=mac_radio_starts,cancels=mac_radio_cancels;
    cerberus_s3_ble_scanner_mac_ops(&local,&ops);cerberus_s3_ble_scanner_mac_ops(&other,&other_ops);
    cerberus_s3_ble_scanner_mac_diagnostics(&diagnostic,&counters);
    MC(diagnostic.state==0&&diagnostic.version==1&&!diagnostic.flags&&counters.version==1&&!counters.epoch);
    MC(!ops.reserve(ops.context,1,&binding));MC(binding.epoch==0&&binding.boot_id&&binding.job_id);
    MC(!radio_active&&mac_radio_starts==starts&&allocations==allocated);
    MC(other_ops.reserve(other_ops.context,1,&foreign)==ESP_ERR_INVALID_STATE&&all_zero(&foreign,sizeof(foreign)));
    cerberus_ble_scan_status_payload_t ordinary;
    MC(cerberus_s3_ble_scanner_start(&ordinary)==ESP_ERR_INVALID_STATE);
    MC(cerberus_s3_ble_scanner_start_active(&ordinary)==ESP_ERR_INVALID_STATE);
    MC(cerberus_s3_ble_scanner_track_begin((uint8_t[6]){1},0)==ESP_ERR_INVALID_STATE);
    MC(cerberus_s3_ble_scanner_analyzer_begin()==ESP_ERR_INVALID_STATE);
    MC(ops.read_step(ops.context,1,&binding,&bank,&transfer).error);
    MC(all_zero(&bank,sizeof(bank))&&all_zero(&transfer,sizeof(transfer)));
    cerberus_mac_start_request_t request=mac_request(binding),bad=request;bad.dwell_ms=250;
    MC(ops.start(ops.context,1,&bad,&o).error==ESP_ERR_INVALID_ARG&&all_zero(&o,sizeof(o)));
    MC(!ops.start(ops.context,1,&request,&o).error);MC(o.valid&&o.status.epoch&&o.status.flags==CERBERUS_MAC_OWNED);
    binding.epoch=o.status.epoch;MC(!o.profile.lease_ms&&!o.profile.dwell_ms&&!o.profile.channel_count);
    MC(!radio_active&&tail==1);pump();MC(radio_active&&mac_radio_starts==starts+1);
    MC(!ops.poll(ops.context,1,&binding,&o).error&&o.status.flags==3&&o.status.channel==0);
    MC(cerberus_s3_ble_scanner_stop(&ordinary)==ESP_ERR_INVALID_STATE&&mac_radio_cancels==cancels);
    /* Malformed AD still proves a report's typed address. Time zero is valid.
     * A response is a subset, and its RSSI replaces rather than peaks. */
    const uint8_t malformed[]={9,9,'x'};
    mac_emit(0,false,false,-10,malformed,sizeof(malformed),9);
    tick=1;mac_emit(0,true,true,127,NULL,0,9);
    for(uint8_t type=1;type<4;++type){++tick;mac_emit(type,false,false,-60,NULL,0,9);}
    MC(!ops.read_step(ops.context,1,&binding,&bank,&transfer).error);
    MC(transfer.complete&&transfer.timing_valid&&bank.header.count==4&&bank.header.observations==5);
    MC(bank.rows[0].first_us==0&&bank.rows[0].last_us==1000&&bank.rows[0].rssi==127&&!bank.rows[0].flags);
    MC(bank.rows[0].observations==2&&bank.rows[0].responses==1);
    MC(bank.rows[0].type_mask==(FAKE_EXT_SCAN?7:3));
    MC(bank.rows[1].address_type==1&&bank.rows[2].address_type==2&&bank.rows[3].address_type==3);
    old=bank;cerberus_mac_ack_request_t ack={.job_id=binding.job_id,.epoch=binding.epoch,.snapshot_id=bank.header.snapshot_id};
    MC(!ops.ack(ops.context,1,&ack).error&&!ops.ack(ops.context,1,&ack).error);
    MC(mac_radio_starts==starts+1&&allocations==allocated);
    /* Copy is one lock, anchors before that lock, and never refreshes source.
     * Callback timestamp is likewise sampled before the first lock. */
    tick=10;next_lock_delay_us=7000;mac_emit(0,false,false,-80,NULL,0,9);
    MC(local.capture.rows[0].last_us==10000&&local.capture.rows[0].rssi==-80);
    uint64_t requested=esp_timer_get_time();next_lock_delay_us=5000;unsigned locks=mutex_takes;
    MC(!ops.read_step(ops.context,1,&binding,&bank,&transfer).error);
    MC(mutex_takes==locks+1&&transfer.request_started_us==requested&&bank.header.source_now_us==requested+5000);
    MC(bank.rows[0].last_us==10000&&old.rows[0].last_us==1000);
    /* Header/type failures and negative/backward callback clocks do not count.
     * Neither an invalid RSSI nor malformed AD content is a header failure. */
    uint32_t observed=local.capture.observations,invalid=local.capture.invalid;
    mac_emit(4,false,false,-40,NULL,0,9);mac_emit(0,false,false,-40,NULL,1,9);
    uint64_t saved_clock=clock_base_us;clock_base_us=UINT64_MAX-100000U;mac_emit(0,false,false,-40,NULL,0,9);
    clock_base_us=0;tick=1;mac_emit(0,false,false,-40,NULL,0,99);
    MC(local.capture.observations==observed&&local.capture.invalid==invalid+4);
    clock_base_us=saved_clock;tick=20;
    struct ble_gap_event invalid_header={0};uint8_t oversized[32]={0};
#if FAKE_EXT_SCAN
    invalid_header.type=BLE_GAP_EVENT_EXT_DISC;
    invalid_header.ext_disc.props=BLE_HCI_ADV_LEGACY_MASK;
    invalid_header.ext_disc.legacy_event_type=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    invalid_header.ext_disc.data=oversized;invalid_header.ext_disc.length_data=32;
#else
    invalid_header.type=BLE_GAP_EVENT_DISC;
    invalid_header.disc.event_type=BLE_HCI_ADV_RPT_EVTYPE_ADV_IND;
    invalid_header.disc.data=oversized;invalid_header.disc.length_data=32;
#endif
    radio_callback(&invalid_header,radio_arg);
    MC(local.capture.observations==observed&&local.capture.invalid==invalid+5);
#if FAKE_EXT_SCAN
    invalid_header.ext_disc.props=0;invalid_header.ext_disc.length_data=0;
    invalid_header.ext_disc.data_status=3;radio_callback(&invalid_header,radio_arg);
    MC(local.capture.observations==observed&&local.capture.invalid==invalid+6);
#endif

    /* Finite procedure completion is not completion of the explicit monitor.
     * It keeps its epoch, counts and IDs; only an explicit poll rearms. */
    void *old_arg=radio_arg;uint32_t old_private=scan_epoch;mac_complete();
    MC(!radio_active&&local.owned&&!local.receiving&&local.capture.observations==observed);
    MC(!ops.read_step(ops.context,1,&binding,&bank,&transfer).error&&mac_radio_starts==starts+1);
    head=tail=0;MC(!ops.poll(ops.context,1,&binding,&o).error);MC(tail==1);pump();
    MC(radio_active&&mac_radio_starts==starts+2&&local.binding.epoch==binding.epoch&&scan_epoch!=old_private);
    struct ble_gap_event late={.type=BLE_GAP_EVENT_DISC_COMPLETE};radio_callback(&late,old_arg);
    finish_scan(old_private,false,123); /* Guard also holds at the final lock. */
    MC(radio_active&&snapshot->status.state==CERBERUS_BLE_SCAN_RUNNING&&owner==CERBERUS_S3_BLE_OWNER_SCANNER);
    clock_base_us=(UINT64_C(1)<<44);tick=0;mac_emit(0,false,false,-40,NULL,0,9);
    MC(local.capture.rows[0].last_us==(UINT64_C(1)<<44)&&local.capture.observations==observed+1);
    for(unsigned i=0;i<40;++i){++tick;mac_emit(0,false,false,-30,NULL,0,100+i);}
    MC(local.capture.count==32&&local.capture.replacements==12);
    MC(!ops.read_step(ops.context,1,&binding,&bank,&transfer).error&&cerberus_mac_snapshot_valid(&bank));
    cerberus_s3_ble_scanner_mac_diagnostics(&diagnostic,&counters);
    MC(diagnostic.flags==3&&counters.observations==local.capture.observations&&counters.invalid==local.capture.invalid&&counters.replacements==12);
    /* Each ownership component is checked. A foreign caller cannot stop or
     * read this table, including a public scan generation byte alias. */
    for(unsigned component=0;component<3;++component){
        foreign=binding;if(component==0)++foreign.boot_id;else if(component==1)++foreign.job_id;else ++foreign.epoch;
        MC(ops.poll(ops.context,1,&foreign,&o).no_ownership&&all_zero(&o,sizeof(o)));
        MC(ops.stop(ops.context,1,&foreign,&o).no_ownership);
        MC(ops.read_step(ops.context,1,&foreign,&bank,&transfer).no_ownership&&all_zero(&bank,sizeof(bank)));
    }
    MC(radio_active&&local.owned);
    /* A failed controller cancellation keeps the caller's PSRAM lease alive.
     * Retry remains explicit and no rearm occurs once Stop was requested. */
    mac_cancel_error=71;head=tail=0;
    MC(!ops.stop(ops.context,1,&binding,&o).error&&o.status.state==CERBERUS_MAC_STOPPING&&o.status.flags==1);
    MC(mac_monitor==&local&&local.owned);
    MC(ops.read_step(ops.context,1,&binding,&bank,&transfer).error&&all_zero(&bank,sizeof(bank)));
    pump();MC(radio_active&&local.owned);
    MC(!ops.poll(ops.context,1,&binding,&o).error&&o.status.flags==1&&o.status.error_code==71);
    mac_cancel_error=0;while(head<tail)pump();
    MC(!ops.poll(ops.context,1,&binding,&o).error&&!o.status.flags&&o.status.state==CERBERUS_MAC_ERROR);
    MC(!mac_monitor&&!local.owned&&!radio_active&&owner==0);head=tail=0;
    MC(ops.read_step(ops.context,1,&binding,&bank,&transfer).error&&all_zero(&bank,sizeof(bank)));
    starts=mac_radio_starts;MC(!ops.poll(ops.context,1,&binding,&o).error&&mac_radio_starts==starts);
    cerberus_s3_ble_scanner_mac_diagnostics(&diagnostic,&counters);
    MC(diagnostic.state==0&&!diagnostic.flags&&counters.observations==0&&counters.version==1);
    /* Pending START then STOP cannot dispatch RF, and storage is not detached
     * until queued START/STOP callbacks are drained. */
    cerberus_mac_binding_t previous=binding;
    MC(!ops.reserve(ops.context,1,&binding)&&binding.job_id>previous.job_id);
    request=mac_request(binding);MC(!ops.start(ops.context,1,&request,&o).error);binding.epoch=o.status.epoch;
    MC(!ops.stop(ops.context,1,&binding,&o).error&&local.owned&&tail==2);
    pump();MC(!radio_active&&local.owned);pump();
    MC(!ops.poll(ops.context,1,&binding,&o).error&&!local.owned&&!mac_monitor&&mac_radio_starts==starts);
    head=tail=0;
    /* Controller start failure releases after the same completion path. */
    MC(!ops.reserve(ops.context,1,&binding));request=mac_request(binding);
    MC(!ops.start(ops.context,1,&request,&o).error);binding.epoch=o.status.epoch;
    mac_start_error=73;pump();mac_start_error=0;
    MC(!ops.poll(ops.context,1,&binding,&o).error&&!local.owned&&o.status.error_code==73&&o.status.state==CERBERUS_MAC_ERROR);
    head=tail=0;
    /* Host reset relinquishes only our old token; no cancel can reach a new
     * central owner, and stale queued callbacks never borrow a new lease. */
    MC(!ops.reserve(ops.context,1,&binding));request=mac_request(binding);
    MC(!ops.start(ops.context,1,&request,&o).error);binding.epoch=o.status.epoch;pump();
    ++host_epoch;owner=CERBERUS_S3_BLE_OWNER_CENTRAL;radio_active=false;cancels=mac_radio_cancels;
    MC(!ops.poll(ops.context,1,&binding,&o).error&&!o.status.flags&&o.status.state==CERBERUS_MAC_ERROR);
    MC(!mac_monitor&&owner==CERBERUS_S3_BLE_OWNER_CENTRAL&&mac_radio_cancels==cancels);
    MC(ops.reserve(ops.context,1,&foreign)==ESP_ERR_INVALID_STATE);
    owner=CERBERUS_S3_BLE_OWNER_NONE;head=tail=0;
    /* Token exhaustion fails before touching caller storage or the radio. */
    uint32_t saved_job=mac_monitor_job;uint64_t saved_epoch=mac_monitor_epoch;
    mac_monitor_job=UINT32_MAX;MC(ops.reserve(ops.context,1,&foreign)==ESP_ERR_INVALID_STATE);
    mac_monitor_job=saved_job;mac_monitor_epoch=UINT64_MAX;MC(ops.reserve(ops.context,1,&foreign)==ESP_ERR_INVALID_STATE);
    mac_monitor_epoch=saved_epoch;
    MC(!ops.reserve(ops.context,1,&binding));mac_finish(&ops,&binding);
    MC(allocations==allocated&&!radio_active&&!mac_monitor);
    /* The SDK's private callback token never aliases after uint32 exhaustion. */
    clock_base_us=0;tick=100000;
    uint32_t saved_private=scan_epoch;scan_epoch=UINT32_MAX-1;
    MC(cerberus_s3_ble_scanner_start(&ordinary)==ESP_OK&&scan_epoch==UINT32_MAX);pump();
    uint8_t generation=snapshot->status.generation;starts=mac_radio_starts;
    ++host_epoch;radio_active=false;owner=CERBERUS_S3_BLE_OWNER_NONE;
    MC(cerberus_s3_ble_scanner_get_status(&ordinary)==ESP_OK&&scan_epoch==UINT32_MAX);
    MC(cerberus_s3_ble_scanner_start(&ordinary)==ESP_ERR_INVALID_STATE&&mac_radio_starts==starts);
    MC(snapshot->status.generation==generation&&owner==0);
    scan_epoch=saved_private;head=tail=0;

    printf("S3 MAC adapter: %u checks; native context %zu bytes; typed callback counts, active copies and owned passive lifecycle passed.\n",mac_checks,sizeof(local));
}
#undef MC
