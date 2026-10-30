#define CERBERUS_DISCOVERY_NATIVE_TEST 1
#define main legacy_discovery_ui_suite
#include "s3_ble_discovery_ui_check.c"
#undef main

static unsigned live_checks;
#define CHECK(x) do { ++live_checks; if(!(x)){fprintf(stderr,"Live CHECK %u: %s\n",__LINE__,#x);exit(1);} } while(0)

static void clean_live(void)
{
    if(ble_discovery_live){
        CHECK(cerberus_ble_discovery_session_released(&ble_discovery_live->session));
        heap_caps_free(ble_discovery_live);
        ble_discovery_live=NULL;
    }
    if(ble_findmy_monitor){heap_caps_free(ble_findmy_monitor);ble_findmy_monitor=NULL;}
}

static void seed_native(cerberus_s3_ble_filter_t family)
{
    clean_live();
    seed_list();
    memset(&discovery_native,0,sizeof(discovery_native));
    memset(live_records,0,sizeof(live_records));
    ble_discovery_open_error=ESP_OK;
    ble_filter=family;ble_attack_selecting=false;ui_capability_blocked=false;
    current_page=UI_PAGE_BLE;ble_mode=UI_BLE_MODE_LIST;
    clock_ms=1000;owner=CERBERUS_S3_BLE_OWNER_NONE;c5_online=false;
    sentinel_scan_session_init(&session);sentinel_scan_session_init(&live_session);
    session.completed=live_session.completed=true;
    live_commands=live_observations=starts=stops=0;
    live_last_poll=0;fail_alloc=false;
    discovery_ui_allocations=0;
}

static void native_tick(uint64_t now,bool asleep)
{
    clock_ms=(int64_t)now;
    const unsigned before=discovery_native.start+discovery_native.copy+discovery_native.poll+
        discovery_native.stop+discovery_native.release;
    live_scan_tick((uint32_t)now,asleep);
    CHECK(discovery_native.start+discovery_native.copy+discovery_native.poll+
        discovery_native.stop+discovery_native.release-before<=1U);
}

static void row(unsigned index,unsigned key,uint8_t type,int8_t rssi)
{
    cerberus_s3_ble_discovery_bank_t *bank=&discovery_native.bank;
    CHECK(index<32&&bank->meta.attached);
    cerberus_s3_ble_discovery_record_t *record=&bank->records[index];
    memset(record,0,sizeof(*record));
    record->index=(uint8_t)index;record->generation=bank->meta.status.generation;
    record->peer.address[0]=(uint8_t)key;record->peer.address[5]=0x61;
    record->peer.address_type=type;record->peer.rssi=rssi;
    record->peer.flags=CERBERUS_BLE_RESULT_CONNECTABLE;
    snprintf(record->peer.name,sizeof(record->peer.name),"Device %u",key);
    memcpy(record->advertisement.address,record->peer.address,6);
    record->advertisement.address_type=type;
    record->advertisement.length=4;record->advertisement.data[0]=3;
    record->advertisement.data[1]=3;record->advertisement.data[2]=0x82;record->advertisement.data[3]=0x30;
    record->advertisement.reports_seen=1;
    record->observed_ms=(uint32_t)clock_ms;
    cerberus_s3_ble_discovery_evidence_t *proof=&record->focus.discovery;
    memcpy(proof->address,record->peer.address,6);proof->address_type=type;proof->generation=record->generation;
    proof->advertisement_sequence=(uint32_t)++bank->meta.revision;
    proof->advertisement_observed_ms=(uint32_t)clock_ms;
    proof->flags=CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE|CERBERUS_S3_BLE_EVIDENCE_ADV_VALID|CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
    proof->flipper_advertisement=(cerberus_ble_flipper_hints_t){CERBERUS_BLE_FLIPPER_HINT_WHITE,CERBERUS_BLE_FLIPPER_UUID16};
    record->focus.category_bits=CERBERUS_S3_BLE_FOCUS_FLIPPER;
    bank->meta.source_now_ms=(uint64_t)clock_ms;
    if(bank->meta.status.stored_count<=index)bank->meta.status.stored_count=(uint8_t)(index+1U);
    ++bank->meta.status.total_count;
}

static void stop_and_release(uint64_t now)
{
    cerberus_ble_discovery_session_stop(&ble_discovery_live->session);
    discovery_native.hold_stop=false;
    for(unsigned i=0;i<30&&!cerberus_ble_discovery_session_released(&ble_discovery_live->session);++i)
        native_tick(now+i*250U,false);
    CHECK(cerberus_ble_discovery_session_released(&ble_discovery_live->session));
    CHECK(!discovery_native.bank.meta.attached&&owner==CERBERUS_S3_BLE_OWNER_NONE);
}

static void first_results_and_frozen_tap(const char *prefix)
{
    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    CHECK(draw_live_scan()==ESP_OK);
    CHECK(ble_discovery_live&&discovery_native.start==0&&ble_discovery_keeps_awake());
    CHECK(discovery_ui_allocations==1);
    CHECK(ble_discovery_defers_c5_telemetry());
    native_tick(1000,false);
    CHECK(discovery_native.start==1&&discovery_native.bank.meta.owned);
    CHECK(live_commands==0&&live_observations==0); /* C5 offline does not delay S3. */
    clock_ms=1050;row(0,11,0,-45);
    native_tick(1050,false);
    CHECK(live_view.row_count==1&&ble_discovery_live->painted_count==1);
    CHECK(discovery_native.bank.meta.status.state==CERBERUS_BLE_SCAN_RUNNING);
    CHECK(!strcmp(live_names[0],"Device 11"));
    cerberus_s3_ble_discovery_entry_t selected=ble_discovery_live->painted[0];
    drawn[0]=0;CHECK(live_paint(true)==ESP_OK);save(prefix,"running-first-result");

    /* Newest bank has B, but the last physical screen still has A. Commit the
     * actual copy adapter without painting, then dispatch the actual row tap. */
    clock_ms=1100;row(0,11,1,-20); /* Same bytes, different BLE address type. */
    cerberus_ble_discovery_observation_t copied;
    CHECK(ble_discovery_copy(ble_discovery_live,ble_discovery_live->session.epoch,0,&copied).error==ESP_OK);
    CHECK(ble_discovery_live->inventory.entries[0].record.peer.address[0]==11);
    CHECK(ble_discovery_live->inventory.count==2); /* Missing A remains earlier. */
    CHECK(ble_discovery_live->inventory.entries[1].record.peer.address_type==1);
    CHECK(ble_discovery_live->painted[0].record.peer.address[0]==11);
    discovery_native.hold_stop=true;
    handle_ble_tap(20,135);
    CHECK(ble_discovery_live->selection_pending&&!ble_discovery_live->session.enabled);
    CHECK(!memcmp(&selected,&ble_discovery_live->selected,sizeof(selected)));
    native_tick(1101,false);
    CHECK(discovery_native.stop==1&&ble_mode==UI_BLE_MODE_LIST);
    CHECK(owner==CERBERUS_S3_BLE_OWNER_SCANNER&&!ble_selected_from_monitor);
    native_tick(1401,false);
    CHECK(ble_mode==UI_BLE_MODE_LIST&&discovery_native.release==0);
    discovery_native_finish(true);
    native_tick(1601,false);native_tick(1801,false);
    CHECK(discovery_native.release==1&&owner==CERBERUS_S3_BLE_OWNER_NONE);
    /* Existing frozen-detail bridge may use its RF-free queued monitor exit. */
    tick(1802);
    CHECK(ble_mode==UI_BLE_MODE_DETAIL);
    CHECK(ble_selected_result.address[0]==11&&ble_selected_result.address_type==0);
    CHECK(!memcmp(&ble_selected_result,&selected.record.peer,sizeof(ble_selected_result)));
    CHECK(ble_selected_original_time64_valid&&ble_selected_original_ms64==1050);
    CHECK(ble_selected_source_ceiling_ms64==selected.source_now_ms&&ble_selected_source_ceiling_ms64==1050);
    CHECK(discovery_native.start==1&&live_commands==0&&starts==0);
    CHECK(discovery_ui_allocations==1);
    actual_back(440);native_tick(2001,false);
    CHECK(ble_mode==UI_BLE_MODE_LIST&&discovery_native.start==1&&!ble_discovery_live->session.enabled);
    clean_live();
}

static void continuous_family_and_stop(const char *prefix)
{
    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    native_tick(1000,false);clock_ms=1050;row(0,31,1,-30);native_tick(1050,false);
    discovery_native_finish(false);
    native_tick(9000,false);
    CHECK(ble_discovery_live->session.final_copied&&discovery_native.start==1);
    native_tick(9050,false);
    CHECK(discovery_native.start==2&&discovery_native.release==0&&ble_discovery_live->inventory.count==1);
    CHECK(ble_discovery_live->session.active_family==CERBERUS_S3_BLE_FILTER_ALL);
    native_tick(9100,false);
    CHECK(ble_discovery_live->inventory.count==1&&!ble_discovery_live->inventory.current_mask);
    CHECK(strstr(live_details[0],"EARLIER"));

    handle_ble_tap(100,65); /* Actual family control requests iBeacon. */
    CHECK(ble_filter==CERBERUS_S3_BLE_FILTER_IBEACON);
    CHECK(ble_discovery_live->session.requested_family==1&&ble_discovery_live->session.active_family==0);
    discovery_native.hold_stop=true;
    native_tick(9150,false);
    CHECK(discovery_native.stop==1&&discovery_native.start==2);
    handle_ble_tap(100,65);handle_ble_tap(100,65); /* Latest request wins: Flipper. */
    CHECK(ble_filter==CERBERUS_S3_BLE_FILTER_FLIPPER);
    native_tick(9400,false);CHECK(discovery_native.start==2);
    discovery_native_finish(true);
    native_tick(9600,false);native_tick(9800,false);native_tick(10000,false);
    CHECK(discovery_native.start==3&&discovery_native.release==1);
    CHECK(ble_discovery_live->session.active_family==CERBERUS_S3_BLE_FILTER_FLIPPER);
    clock_ms=10050;row(0,33,2,-60);native_tick(10050,false);
    CHECK(ble_discovery_live->inventory.family==CERBERUS_S3_BLE_FILTER_FLIPPER);
    CHECK(ble_discovery_live->inventory.count==1&&live_view.row_count==1);
    CHECK(ble_discovery_live->painted[0].record.peer.address_type==2);
    drawn[0]=0;CHECK(live_paint(true)==ESP_OK);save(prefix,"focused-running");
    discovery_native.hold_stop=false;
    handle_live_scan_tap(100,380); /* Production pause control position. */
    CHECK(!ble_discovery_live->session.enabled);
    stop_and_release(10100);
    unsigned starts_before=discovery_native.start;
    handle_ble_tap(100,65);native_tick(12000,false);
    CHECK(discovery_native.start==starts_before&&!ble_discovery_keeps_awake());
    CHECK(discovery_ui_allocations==1); /* Same UI block across sweeps and family changes. */
    clean_live();
}

static void failures_and_navigation(void)
{
    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    fail_alloc=true;
    CHECK(draw_live_scan()==ESP_OK);
    native_tick(1000,false);
    CHECK(!ble_discovery_live&&ble_discovery_open_error==ESP_ERR_NO_MEM&&discovery_native.start==0);
    CHECK(discovery_ui_allocations==1);
    fail_alloc=false;
    native_tick(1500,false);
    CHECK(!ble_discovery_live&&discovery_native.start==0); /* Explicit retry only. */
    CHECK(discovery_ui_allocations==1);
    ble_discovery_toggle();CHECK(ble_discovery_live&&ble_discovery_live->session.enabled);
    native_tick(1550,false);
    CHECK(discovery_native.start==1);
    CHECK(discovery_ui_allocations==2);
    discovery_native.stop_errors=2;discovery_native.release_errors=1;
    current_page=UI_PAGE_OTHER;
    for(uint64_t now=1650;now<5000;now+=250)native_tick(now,false);
    CHECK(!ble_discovery_live->session.epoch&&discovery_native.stop==3&&discovery_native.release==2);
    CHECK(discovery_native.start==1&&!ble_discovery_live->session.enabled);
    current_page=UI_PAGE_BLE;ble_mode=UI_BLE_MODE_LIST;native_tick(5100,false);
    CHECK(discovery_native.start==1);
    ble_discovery_toggle();native_tick(5150,false);CHECK(discovery_native.start==2);
    native_tick(5200,true);native_tick(5450,true);
    CHECK(!ble_discovery_live->session.epoch&&!ble_discovery_live->session.enabled);
    native_tick(5700,false);CHECK(discovery_native.start==2); /* Waking never restarts. */
    clean_live();

    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    discovery_native.ambiguous_start=true;
    native_tick(1000,false);
    CHECK(ble_discovery_live->session.epoch&&ble_discovery_live->session.faulted);
    native_tick(1050,false);native_tick(1300,false);
    CHECK(!ble_discovery_live->session.epoch&&discovery_native.release==1&&discovery_native.start==1);
    clean_live();

    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    ble_attack_selecting=true;native_tick(1000,false);
    CHECK(!ble_discovery_live&&discovery_native.start==0); /* Legacy scheduler path retained. */
    ble_attack_selecting=false;current_page=UI_PAGE_WATCH;native_tick(1500,false);
    CHECK(!ble_discovery_live&&discovery_native.start==0);
    current_page=UI_PAGE_OTHER;
}

static void deferred_navigation_and_copy_failure(void)
{
    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    native_tick(1000,false);clock_ms=1050;row(0,50,0,-40);native_tick(1050,false);
    cerberus_s3_ble_discovery_inventory_t frozen=ble_discovery_live->inventory;
    clock_ms=1100;row(0,51,1,-30);discovery_native.copy_errors=1;
    native_tick(1250,false);
    CHECK(!memcmp(&frozen,&ble_discovery_live->inventory,sizeof(frozen)));
    CHECK(ble_discovery_live->session.enabled&&ble_discovery_live->session.error==ESP_ERR_TIMEOUT);
    native_tick(1500,false);native_tick(1700,false);
    CHECK(ble_discovery_live->inventory.count==2&&ble_discovery_live->session.error==0);
    discovery_native.hold_stop=true;
    handle_ble_tap(20,135);
    CHECK(ble_discovery_live->selection_pending);
    actual_back(440);
    CHECK(ble_mode==UI_BLE_MODE_MENU);
    CHECK(!ble_discovery_live->selection_pending&&!ble_discovery_live->session.enabled);
    CHECK(!ble_discovery_defers_c5_telemetry());
    native_tick(1750,false);
    CHECK(!ble_discovery_live->selection_pending&&!ble_discovery_live->session.enabled);
    CHECK(discovery_native.stop==1&&ble_mode==UI_BLE_MODE_MENU);
    discovery_native_finish(true);
    native_tick(2000,false);native_tick(2250,false);
    CHECK(!ble_discovery_live->session.epoch&&ble_mode==UI_BLE_MODE_MENU);
    CHECK(!ble_selected_from_monitor&&discovery_native.start==1);
    ble_mode=UI_BLE_MODE_LIST;native_tick(2500,false);
    CHECK(discovery_native.start==1&&!ble_discovery_live->session.enabled);
    clean_live();

    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    native_tick(1000,false);clock_ms=1050;row(0,55,0,-50);native_tick(1050,false);
    discovery_native.hold_stop=true;
    handle_live_scan_tap(270,380);
    CHECK(ble_discovery_live->destination==1&&!ble_discovery_live->session.enabled);
    native_tick(1100,false);CHECK(ble_mode==UI_BLE_MODE_LIST);
    discovery_native_finish(true);native_tick(1350,false);native_tick(1600,false);
    CHECK(ble_mode==UI_BLE_MODE_MENU&&discovery_native.release==1&&starts==0);
    clean_live();
}

static void source_switch_and_maximum(const char *prefix)
{
    seed_native(CERBERUS_S3_BLE_FILTER_ALL);
    native_tick(1000,false);clock_ms=1050;
    for(unsigned i=0;i<32;++i)row(i,i+1,(uint8_t)(i%4U),(int8_t)(-30-i));
    native_tick(1050,false);
    CHECK(live_ble_filtered_count==32&&live_view.total_count==32&&!live_ble_order_dirty);
    CHECK(live_ble_visible_mask==UINT32_MAX);
    ble_discovery_live->inventory.replacements=UINT32_MAX;
    ble_discovery_live->binding_overflows=UINT16_MAX;
    live_make_view();drawn[0]=0;CHECK(live_paint(true)==ESP_OK);save(prefix,"maximum");
    /* Same filter/sort/mask but a different backing bank must invalidate its
     * index cache. Legacy attack selection cannot reuse discovery indexes. */
    memset(&live_records[SENTINEL_SCAN_BLE],0,sizeof(live_records[SENTINEL_SCAN_BLE]));
    live_records[SENTINEL_SCAN_BLE].valid=true;
    live_records[SENTINEL_SCAN_BLE].captured=true;
    ble_attack_selecting=true;live_make_view();
    CHECK(!ble_discovery_defers_c5_telemetry());
    CHECK(live_ble_filtered_count==0&&live_view.row_count==0&&live_view.total_count==0);
    ble_attack_selecting=false;live_make_view();
    CHECK(ble_discovery_defers_c5_telemetry());
    CHECK(live_ble_filtered_count==32&&live_view.row_count==4);
    stop_and_release(1100);clean_live();
}

static void retained_badge_age(const char *prefix)
{
    seed_native(CERBERUS_S3_BLE_FILTER_IBEACON);
    native_tick(1000,false);clock_ms=1050;row(0,61,1,-55);
    cerberus_s3_ble_discovery_record_t *record=&discovery_native.bank.records[0];
    memset(record->advertisement.data,0,sizeof(record->advertisement.data));
    record->advertisement.length=27;
    record->advertisement.data[0]=26;record->advertisement.data[1]=0xff;
    record->advertisement.data[2]=0x4c;record->advertisement.data[4]=2;
    record->advertisement.data[5]=0x15;record->advertisement.data[26]=0xc5;
    record->focus.category_bits=CERBERUS_S3_BLE_FOCUS_IBEACON;
    record->focus.ibeacon_valid=true;
    record->focus.ibeacon_sequence=record->focus.discovery.advertisement_sequence;
    record->focus.ibeacon_observed_ms=record->observed_ms;
    CHECK(cerberus_ibeacon_parse_ad(record->advertisement.data,record->advertisement.length,
        &record->focus.ibeacon)==CERBERUS_IBEACON_FOUND);
    native_tick(1050,false);
    CHECK(strstr(live_badges[0]," / 0s"));
    stop_and_release(1100);
    cerberus_s3_ble_discovery_record_t frozen=ble_discovery_live->inventory.entries[0].record;
    clock_ms=INT64_C(4294967296)+1050;
    live_make_view();
    CHECK(strstr(live_badges[0]," / >49d")&&!strstr(live_badges[0]," / 0s"));
    drawn[0]=0;CHECK(live_paint(true)==ESP_OK);save(prefix,"retained-ibeacon-age");
    CHECK(!memcmp(&frozen,&ble_discovery_live->inventory.entries[0].record,sizeof(frozen)));
    ble_discovery_live->inventory.entries[0].source_now_ms=(uint64_t)clock_ms+1U;
    live_make_view();CHECK(strstr(live_badges[0]," / AGE ?"));
    CHECK(!memcmp(&frozen,&ble_discovery_live->inventory.entries[0].record,sizeof(frozen)));
    CHECK(discovery_native.start==1);
    clean_live();
}

int main(int argc,char **argv)
{
    CHECK(argc==2);
    first_results_and_frozen_tap(argv[1]);
    continuous_family_and_stop(argv[1]);
    failures_and_navigation();
    deferred_navigation_and_copy_failure();
    source_switch_and_maximum(argv[1]);
    retained_badge_age(argv[1]);
    printf("Ordinary BLE live UI: %u checks passed; active updates, frozen painted selection, scoped cleanup and C5 independence\n",live_checks);
    return 0;
}
