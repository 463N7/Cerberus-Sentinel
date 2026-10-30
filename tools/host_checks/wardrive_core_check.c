#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wardrive_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cerberus_wardrive_observation_t wifi_record(uint8_t suffix)
{
    cerberus_wardrive_observation_t record = {
        .type = CERBERUS_WARDRIVE_WIFI,
        .radio = CERBERUS_WARDRIVE_RADIO_C5,
        .address = {0x10, 0x20, 0x30, 0x40, 0x50, suffix},
        .channel = 6,
        .rssi_dbm = -60,
        .security = 3,
        .label = {'=', 'A', ',', 'B'},
        .label_length = 4,
    };
    return record;
}

static unsigned probe_checks;
#define PCHECK(x) do { ++probe_checks; assert(x); } while (0)

static cerberus_wifi_probe_row_t probe_row(void)
{
    return (cerberus_wifi_probe_row_t){.transmitter={2,1,2,3,4,5},
        .ssid={'F','l','o','c','k'},.ssid_length=5,.channel=6,.rssi=-70,
        .name_bits=CERBERUS_WIFI_PROBE_NAME_FLOCK,.frames=1,.callback_ms=0};
}

static cerberus_wardrive_observation_t probe_observation(cerberus_wifi_probe_row_t row)
{
    cerberus_wardrive_observation_t out;
    memset(&out,0xa5,sizeof(out));
    PCHECK(cerberus_wardrive_observation_from_probe(&row,&out));
    PCHECK(cerberus_wardrive_observation_valid(&out));
    PCHECK(out.type==CERBERUS_WARDRIVE_WIFI_PROBE&&out.radio==CERBERUS_WARDRIVE_RADIO_C5);
    PCHECK(!memcmp(out.address,row.transmitter,6)&&!memcmp(out.label,row.ssid,32));
    PCHECK(cerberus_wardrive_probe_observed_ms(&out.probe_evidence)==row.callback_ms&&
        out.probe_evidence.count==row.frames&&out.probe_evidence.name_bits==row.name_bits&&
        out.probe_evidence.oui_clue==row.oui_hint);
    return out;
}

static void rejected_probe(cerberus_wifi_probe_row_t row)
{
    cerberus_wardrive_observation_t out,before;
    memset(&out,0xa5,sizeof(out));before=out;
    PCHECK(!cerberus_wardrive_observation_from_probe(&row,&out));
    PCHECK(!memcmp(&out,&before,sizeof(out)));
}

static void csv_field(const char *row,unsigned column,const char *expected)
{
    const char *start=row;
    for(unsigned i=0;i<column;++i){start=strchr(start,',');PCHECK(start);++start;}
    const char *end=start;while(*end&&*end!=','&&*end!='\n')++end;
    PCHECK((size_t)(end-start)==strlen(expected)&&!memcmp(start,expected,strlen(expected)));
}

static void probe_evidence_and_csv(void)
{
    PCHECK(sizeof(cerberus_wardrive_observation_t)==80U);
    PCHECK(offsetof(cerberus_wardrive_observation_t,probe_evidence)==52U);
    PCHECK(sizeof(cerberus_wardrive_probe_dedupe_entry_t)==48U);
    PCHECK(sizeof(cerberus_wardrive_probe_dedupe_t)==776U);
    PCHECK(sizeof(CERBERUS_WARDRIVE_UNLOCATED_HEADER)-1U<=512U);
    const char *header=strchr(CERBERUS_WARDRIVE_UNLOCATED_HEADER,'\n');PCHECK(header);++header;
    csv_field(header,24,"requested_ssid_hex");csv_field(header,25,"probe_name_bits");
    csv_field(header,26,"probe_oui_clue");csv_field(header,27,"probe_observed_ms");csv_field(header,28,"probe_count");

    cerberus_wifi_probe_row_t row=probe_row();
    cerberus_wardrive_observation_t out=probe_observation(row),original=out;
    PCHECK(!cerberus_wardrive_observation_from_probe(NULL,&out)&&!memcmp(&out,&original,sizeof(out)));
    PCHECK(!cerberus_wardrive_observation_from_probe(&row,NULL));
    PCHECK(cerberus_wardrive_probe_observed_ms(NULL)==0U);
    for(unsigned bits=0;bits<256;++bits){
        row=probe_row();row.name_bits=(uint8_t)bits;
        if(bits==CERBERUS_WIFI_PROBE_NAME_FLOCK)(void)probe_observation(row);else rejected_probe(row);
        row=probe_row();row.oui_hint=(uint8_t)bits;
        if(bits==0)(void)probe_observation(row);else rejected_probe(row);
    }
    row=probe_row();row.frames=0;rejected_probe(row);
    row=probe_row();row.callback_ms=UINT64_MAX/1000U+1U;rejected_probe(row);
    row=probe_row();row.ssid_length=33;rejected_probe(row);
    row=probe_row();row.ssid[5]=1;rejected_probe(row);
    row=probe_row();row.channel=0;rejected_probe(row);
    row=probe_row();row.channel=178;rejected_probe(row);
    row=probe_row();row.rssi=1;rejected_probe(row);
    row=probe_row();row.rssi=-128;rejected_probe(row);
    row=probe_row();memset(row.transmitter,0,6);rejected_probe(row);
    row=probe_row();row.transmitter[0]|=1;rejected_probe(row);
    row=probe_row();row.ssid[0]='x';row.name_bits=0;rejected_probe(row); /* No clue. */
    for(unsigned i=0;i<14;++i){out=original;out.probe_evidence.reserved[i]=1;PCHECK(!cerberus_wardrive_observation_valid(&out));}
    out=original;out.security=1;PCHECK(!cerberus_wardrive_observation_valid(&out));
    out=original;out.manufacturer_id=1;PCHECK(!cerberus_wardrive_observation_valid(&out));
    out=original;out.address_type=1;PCHECK(!cerberus_wardrive_observation_valid(&out));
    for(unsigned radio=1;radio<=7;++radio){out=original;out.radio=(uint8_t)radio;PCHECK(!cerberus_wardrive_observation_valid(&out));}
    out=original;out.type=CERBERUS_WARDRIVE_WIFI;PCHECK(!cerberus_wardrive_observation_valid(&out));
    out=original;out.type=CERBERUS_WARDRIVE_BLE;PCHECK(!cerberus_wardrive_observation_valid(&out));

    char buffer[384],exact[384];
    size_t n=cerberus_wardrive_format_unlocated_row(&original,NULL,500,400,buffer,sizeof(buffer));
    PCHECK(n==strlen(buffer)&&n>0);
    csv_field(buffer,0,"500");csv_field(buffer,1,"400");csv_field(buffer,2,"wifi_probe");
    csv_field(buffer,3,"C5");csv_field(buffer,4,"02:01:02:03:04:05");csv_field(buffer,6,"6");csv_field(buffer,7,"-70");
    for(unsigned i=8;i<12;++i)csv_field(buffer,i,"");
    for(unsigned i=14;i<24;++i)csv_field(buffer,i,"");
    csv_field(buffer,24,"466C6F636B");csv_field(buffer,25,"1");csv_field(buffer,26,"0");
    csv_field(buffer,27,"0");csv_field(buffer,28,"1");
    unsigned columns=1;for(size_t i=0;i<n;++i)columns+=buffer[i]==',';PCHECK(columns==34);

    /* OUI-only wildcard and binary requested names remain raw hex. No AP
     * SSID/security or receiver coordinate is invented, even with a fix. */
    row=probe_row();const uint8_t oui[6]={0x58,0x8e,0x81,1,2,3};memcpy(row.transmitter,oui,6);
    memset(row.ssid,0,32);row.ssid_length=0;row.name_bits=0;row.oui_hint=1;
    out=probe_observation(row);n=cerberus_wardrive_format_unlocated_row(&out,NULL,0,0,buffer,sizeof(buffer));
    PCHECK(n);csv_field(buffer,24,"");csv_field(buffer,25,"0");csv_field(buffer,26,"1");
    memset(row.ssid,0xff,32);row.ssid[0]='=';row.ssid[1]=',';row.ssid[2]='\n';row.ssid[3]=0;
    row.ssid_length=32;row.callback_ms=UINT64_MAX/1000U;row.frames=UINT32_MAX;row.channel=177;row.rssi=-127;
    out=probe_observation(row);
    cerberus_wardrive_gps_snapshot_t gps={.state=CERBERUS_WARDRIVE_GPS_FRESH,
        .coordinates_valid=true,.utc_valid=true,.utc_from_rmc=true,.utc="2026-10-30T03:00:00.000Z",
        .latitude=31.25,.longitude=-80.75};
    n=cerberus_wardrive_format_unlocated_row(&out,&gps,UINT64_MAX,UINT64_MAX,buffer,sizeof(buffer));
    PCHECK(n&&n==strlen(buffer)&&n<sizeof(buffer));
    PCHECK(!strstr(buffer,"31.25")&&!strstr(buffer,"-80.75")&&!strstr(buffer,"=,"));
    PCHECK(strstr(buffer,"3D2C0A00FFFFFFFF")&&strchr(buffer,'\n')==buffer+n-1);
    csv_field(buffer,27,"18446744073709551");csv_field(buffer,28,"4294967295");
    csv_field(buffer,12,"fresh");csv_field(buffer,13,"2026-10-30T03:00:00.000Z");
    PCHECK(!cerberus_wardrive_format_unlocated_row(&out,&gps,UINT64_MAX,UINT64_MAX,exact,n));
    PCHECK(cerberus_wardrive_format_unlocated_row(&out,&gps,UINT64_MAX,UINT64_MAX,exact,n+1)==n);
    PCHECK(!strcmp(buffer,exact));
}

static void probe_exact_key_and_commit(void)
{
    cerberus_wardrive_probe_dedupe_t history={0},before;
    cerberus_wifi_probe_row_t source=probe_row();
    cerberus_wardrive_observation_t row=probe_observation(source);
    cerberus_wardrive_decision_t d=cerberus_wardrive_probe_decide(&history,&row,100);
    PCHECK(d.should_log);before=history;
    PCHECK(cerberus_wardrive_probe_decide(&history,&row,101).should_log&&!memcmp(&history,&before,sizeof(history)));
    cerberus_wardrive_probe_commit(&history,&row,d,100);
    PCHECK(history.used==1&&!history.evicted&&!cerberus_wardrive_probe_decide(&history,&row,101).should_log);
    source.callback_ms=UINT64_MAX/1000U;source.frames=UINT32_MAX;row=probe_observation(source);
    PCHECK(!cerberus_wardrive_probe_decide(&history,&row,102).should_log);
    row.rssi_dbm=-68;PCHECK(!cerberus_wardrive_probe_decide(&history,&row,103).should_log);
    row.rssi_dbm=-67;PCHECK(cerberus_wardrive_probe_decide(&history,&row,104).should_log);
    row.rssi_dbm=-70;row.channel=11;PCHECK(cerberus_wardrive_probe_decide(&history,&row,104).should_log);
    row.channel=6;PCHECK(!cerberus_wardrive_probe_decide(&history,&row,300099).should_log);
    PCHECK(cerberus_wardrive_probe_decide(&history,&row,300100).should_log);
    row.probe_evidence.name_bits|=CERBERUS_WIFI_PROBE_NAME_PENGUIN;
    PCHECK(!cerberus_wardrive_probe_decide(&history,&row,300100).should_log); /* forged metadata */
    row=probe_observation(source);row.label[5]=0;row.label_length=6;
    PCHECK(cerberus_wardrive_probe_decide(&history,&row,105).should_log); /* Flock vs Flock\0 */
    d=cerberus_wardrive_probe_decide(&history,&row,105);cerberus_wardrive_probe_commit(&history,&row,d,105);
    PCHECK(history.used==2);
    row.label[6]='A';row.label_length=7;d=cerberus_wardrive_probe_decide(&history,&row,106);
    PCHECK(d.should_log);cerberus_wardrive_probe_commit(&history,&row,d,106);PCHECK(history.used==3);
    row.address[5]++;PCHECK(cerberus_wardrive_probe_decide(&history,&row,107).should_log);

    cerberus_wardrive_dedupe_t ordinary={0},ordinary_before=ordinary;
    PCHECK(!cerberus_wardrive_decide(&ordinary,&row,108).should_log);
    PCHECK(!cerberus_wardrive_decide_evidence(&ordinary,&row,108).should_log);
    cerberus_wardrive_commit(&ordinary,&row,(cerberus_wardrive_decision_t){true,0},108);
    PCHECK(!memcmp(&ordinary,&ordinary_before,sizeof(ordinary)));
    before=history;cerberus_wardrive_observation_t ap=wifi_record(1);
    PCHECK(!cerberus_wardrive_probe_decide(&history,&ap,108).should_log);
    cerberus_wardrive_probe_commit(&history,&ap,(cerberus_wardrive_decision_t){true,0},108);
    PCHECK(!memcmp(&history,&before,sizeof(history)));
    cerberus_wardrive_probe_commit(&history,&row,(cerberus_wardrive_decision_t){false,0},108);
    cerberus_wardrive_probe_commit(&history,&row,(cerberus_wardrive_decision_t){true,16},108);
    PCHECK(!memcmp(&history,&before,sizeof(history)));

    cerberus_wardrive_probe_dedupe_reset(&history);source=probe_row();
    source.ssid_length=32;row=probe_observation(source);
    for(unsigned i=0;i<17;++i){
        row.label[31]=(uint8_t)i;d=cerberus_wardrive_probe_decide(&history,&row,1000+i);
        PCHECK(d.should_log);before=history;
        PCHECK(cerberus_wardrive_probe_decide(&history,&row,1001+i).should_log&&!memcmp(&before,&history,sizeof(history)));
        cerberus_wardrive_probe_commit(&history,&row,d,1000+i);
        PCHECK(history.used==(i<16?i+1:16)&&history.evicted==(i<16?0:1));
    }
    row.label[31]=0;PCHECK(cerberus_wardrive_probe_decide(&history,&row,1017).should_log);
    row.label[31]=1;PCHECK(!cerberus_wardrive_probe_decide(&history,&row,1017).should_log);
    history.evicted=UINT32_MAX;row.label[31]=18;d=cerberus_wardrive_probe_decide(&history,&row,1017);
    cerberus_wardrive_probe_commit(&history,&row,d,1017);PCHECK(history.evicted==UINT32_MAX);
    cerberus_wardrive_probe_dedupe_reset(&history);source=probe_row();row=probe_observation(source);
    uint32_t start=UINT32_MAX-10U;d=cerberus_wardrive_probe_decide(&history,&row,start);
    cerberus_wardrive_probe_commit(&history,&row,d,start);
    PCHECK(!cerberus_wardrive_probe_decide(&history,&row,start+CERBERUS_WARDRIVE_RELOG_MS-1U).should_log);
    PCHECK(cerberus_wardrive_probe_decide(&history,&row,start+CERBERUS_WARDRIVE_RELOG_MS).should_log);
    cerberus_wardrive_probe_dedupe_reset(&history);PCHECK(!history.used&&!history.evicted);
}

static void hint_export_and_dedupe(void)
{
    assert(sizeof(CERBERUS_WARDRIVE_UNLOCATED_HEADER)-1U<=512U);
    cerberus_wardrive_dedupe_t history = {0};
    cerberus_wardrive_observation_t row = {.type=CERBERUS_WARDRIVE_BLE,
        .radio=CERBERUS_WARDRIVE_RADIO_S3,.address={1,2,3,4,5,6},.rssi_dbm=-90,
        .manufacturer_id=2504,.label_length=32};
    memset(row.label, 0xff, sizeof(row.label));
    cerberus_wardrive_decision_t decision=cerberus_wardrive_decide_evidence(&history,&row,0);
    assert(decision.should_log);cerberus_wardrive_commit(&history,&row,decision,0);
    row.ble_evidence=(cerberus_s3_ble_wardrive_evidence_t){.flags=15,.sid=15,
        .advertisement_sequence=UINT32_MAX-1,.advertisement_observed_ms=UINT32_MAX-1,
        .scan_response_sequence=UINT32_MAX,.scan_response_observed_ms=1,
        .advertisement={.facts=CERBERUS_BLE_FLOCK_FS_BATTERY},
        .scan_response={.facts=CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD|CERBERUS_BLE_FLOCK_NO_LOCAL_NAME}};
    assert(cerberus_s3_ble_wardrive_evidence_level(&row.ble_evidence)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE);
    assert(!cerberus_wardrive_decide(&history,&row,1).should_log); /* WiGLE policy unchanged. */
    decision=cerberus_wardrive_decide_evidence(&history,&row,1);assert(decision.should_log);
    assert(cerberus_wardrive_decide_evidence(&history,&row,2).should_log); /* failed append never commits */
    cerberus_wardrive_commit(&history,&row,decision,1);
    assert(!cerberus_wardrive_decide_evidence(&history,&row,2).should_log);
    row.ble_evidence.advertisement_sequence=1;row.ble_evidence.scan_response_sequence=2;
    row.ble_evidence.advertisement_observed_ms=3;row.ble_evidence.scan_response_observed_ms=4;
    assert(!cerberus_wardrive_decide_evidence(&history,&row,3).should_log); /* own times alone do not relog */
    row.ble_evidence.advertisement.facts |= CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
    assert(cerberus_s3_ble_wardrive_evidence_level(&row.ble_evidence)==CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED);
    decision=cerberus_wardrive_decide_evidence(&history,&row,4);assert(decision.should_log);
    cerberus_wardrive_commit(&history,&row,decision,4);
    char buffer[384];
    cerberus_wardrive_gps_snapshot_t gps={.state=CERBERUS_WARDRIVE_GPS_FRESH,
        .coordinates_valid=true,.utc_valid=true,.utc_from_rmc=true,.utc="2026-10-30T03:00:00.000Z"};
    size_t length=cerberus_wardrive_format_unlocated_row(&row,&gps,UINT64_MAX,UINT64_MAX,buffer,sizeof(buffer));
    assert(length && length==strlen(buffer));
    assert(strstr(buffer,",1,3,31000000,1,3,1,38000000,2,4,15,,,,,,,,,,\n"));
    unsigned columns=1;for(size_t i=0;i<length;++i)if(buffer[i]==',')++columns;
    assert(columns==34 && strstr(CERBERUS_WARDRIVE_UNLOCATED_HEADER,"unlocated v4"));
    char exact[384];
    assert(cerberus_wardrive_format_unlocated_row(&row,&gps,UINT64_MAX,UINT64_MAX,exact,length)==0);
    assert(cerberus_wardrive_format_unlocated_row(&row,&gps,UINT64_MAX,UINT64_MAX,exact,length+1)==length);
    assert(!strcmp(exact,buffer));
    row.ble_evidence=(cerberus_s3_ble_wardrive_evidence_t){0};
    assert(cerberus_wardrive_decide_evidence(&history,&row,5).should_log); /* explicit loss is a changed summary */
    row.ble_evidence.reserved[0]=1;
    assert(!cerberus_wardrive_observation_valid(&row));
    assert(!cerberus_wardrive_format_unlocated_row(&row,&gps,0,0,buffer,sizeof(buffer)));
    row=wifi_record(1);row.ble_evidence.flags=CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE;
    assert(!cerberus_wardrive_observation_valid(&row));
}

#include "wardrive_relation_check.h"

int main(void)
{
    relation_core_checks();
    probe_evidence_and_csv();probe_exact_key_and_commit();
    hint_export_and_dedupe();
    cerberus_wardrive_dedupe_t state;
    cerberus_wardrive_dedupe_reset(&state);
    cerberus_wardrive_observation_t wifi = wifi_record(1);
    cerberus_wardrive_decision_t decision = cerberus_wardrive_decide(&state, &wifi, 1000);
    assert(decision.should_log);
    /* A failed storage append must not suppress this first sighting. */
    assert(cerberus_wardrive_decide(&state, &wifi, 1001).should_log);
    cerberus_wardrive_commit(&state, &wifi, decision, 1000);
    assert(state.used == 1 && state.evicted == 0);
    assert(!cerberus_wardrive_decide(&state, &wifi, 1001).should_log);
    wifi.radio = CERBERUS_WARDRIVE_RADIO_R1;
    assert(!cerberus_wardrive_decide(&state, &wifi, 1002).should_log);
    wifi.rssi_dbm = -58;
    assert(!cerberus_wardrive_decide(&state, &wifi, 1003).should_log);
    wifi.rssi_dbm = -57;
    decision = cerberus_wardrive_decide(&state, &wifi, 1004);
    assert(decision.should_log);
    cerberus_wardrive_commit(&state, &wifi, decision, 1004);
    wifi.rssi_dbm = -60;
    wifi.channel = 11;
    assert(cerberus_wardrive_decide(&state, &wifi, 1005).should_log);
    wifi.channel = 6;
    assert(cerberus_wardrive_decide(&state, &wifi, 301004).should_log);
    wifi.address[0] = 0;
    memset(wifi.address, 0, sizeof(wifi.address));
    assert(!cerberus_wardrive_decide(&state, &wifi, 301004).should_log);
    wifi = wifi_record(1);
    char row[384] = {0};
    cerberus_wardrive_gps_snapshot_t gps = {
        .state = CERBERUS_WARDRIVE_GPS_ABSENT,
    };
    size_t length = cerberus_wardrive_format_unlocated_row(&wifi,
        &gps, 4000, 3000, row, sizeof(row));
    assert(length == strlen(row));
    assert(strcmp(row, "4000,3000,wifi,C5,10:20:30:40:50:01,,6,-60,3,3D412C42,,,absent,,,,,,,,,,,,,,,,,,,,,\n") == 0);
    assert(strstr(row, "=A") == NULL); /* Spreadsheet formulas remain hex. */
    assert(cerberus_wardrive_format_unlocated_row(&wifi, &gps, 0, 0, row, 10) == 0);
    length = cerberus_wardrive_format_unlocated_row(&wifi,
        &gps, UINT64_C(4294967297), UINT64_C(4294967296), row, sizeof(row));
    assert(length == strlen(row));
    assert(strncmp(row, "4294967297,4294967296,", 22) == 0);
    wifi.label_length = CERBERUS_WARDRIVE_LABEL_CAPACITY;
    memset(wifi.label, 0, sizeof(wifi.label));
    wifi.label[0] = 0x00;
    wifi.label[1] = 0xFF;
    length = cerberus_wardrive_format_unlocated_row(&wifi, &gps, 1, 1, row, sizeof(row));
    assert(length != 0 && strstr(row, ",00FF0000") != NULL);
    /* Receiver status belongs to the logger at append time; UTC is only the
     * last validated RMC value, never an inferred observation timestamp. */
    gps.state = CERBERUS_WARDRIVE_GPS_NO_FIX;
    length = cerberus_wardrive_format_unlocated_row(&wifi, &gps, 2, 2, row, sizeof(row));
    assert(length != 0 && strstr(row, ",no_fix,,,,,,,,,,,,,,,,,,,,,\n") != NULL);
    gps.state = CERBERUS_WARDRIVE_GPS_STALE;
    length = cerberus_wardrive_format_unlocated_row(&wifi, &gps, 3, 3, row, sizeof(row));
    assert(length != 0 && strstr(row, ",stale,,,,,,,,,,,,,,,,,,,,,\n") != NULL);
    gps.state = CERBERUS_WARDRIVE_GPS_FRESH;
    gps.coordinates_valid = true;
    gps.utc_valid = true;
    gps.utc_from_rmc = true;
    memcpy(gps.utc, "2026-10-29T23:56:00.000Z", sizeof(gps.utc));
    length = cerberus_wardrive_format_unlocated_row(&wifi, &gps, 4, 4, row, sizeof(row));
    assert(length != 0 && strstr(row, ",fresh,2026-10-29T23:56:00.000Z,,,,,,,,,,,,,,,,,,,,\n") != NULL);
    gps.utc[4] = ',';
    length = cerberus_wardrive_format_unlocated_row(&wifi, &gps, 5, 5, row, sizeof(row));
    assert(length != 0 && strstr(row, ",fresh,,,,,,,,,,,,,,,,,,,,,\n") != NULL);
    gps.state = CERBERUS_WARDRIVE_GPS_ABSENT;

    cerberus_wardrive_observation_t ble = {
        .type = CERBERUS_WARDRIVE_BLE,
        .radio = CERBERUS_WARDRIVE_RADIO_S3,
        .address = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF},
        .address_type = 1,
        .rssi_dbm = -80,
        .manufacturer_id = 76,
        .label = {'B', 'L', 'E'},
        .label_length = 3,
    };
    assert(cerberus_wardrive_observation_valid(&ble));
    length = cerberus_wardrive_format_unlocated_row(&ble, &gps, 5000, 4000, row, sizeof(row));
    assert(length == strlen(row));
    assert(strcmp(row, "5000,4000,ble,S3,AA:BB:CC:DD:EE:FF,1,,-80,,,424C45,76,absent,,0,,,,,,,,,,,,,,,,,,,\n") == 0);
    decision = cerberus_wardrive_decide(&state, &ble, 5000);
    assert(decision.should_log);
    cerberus_wardrive_commit(&state, &ble, decision, 5000);
    ble.address_type = 0;
    assert(cerberus_wardrive_decide(&state, &ble, 5001).should_log);

    cerberus_wardrive_dedupe_reset(&state);
    for (unsigned i = 0; i < CERBERUS_WARDRIVE_DEDUPE_CAPACITY + 1U; ++i) {
        wifi = wifi_record((uint8_t)i);
        wifi.address[4] = (uint8_t)(i >> 8U);
        decision = cerberus_wardrive_decide(&state, &wifi, (uint32_t)i);
        assert(decision.should_log);
        cerberus_wardrive_commit(&state, &wifi, decision, (uint32_t)i);
    }
    assert(state.used == CERBERUS_WARDRIVE_DEDUPE_CAPACITY);
    assert(state.evicted == 1);
    printf("wardrive dedupe, unlocated v4 CSV, bounds: pass; %u probe/relation checks; header=%zu row limit=384 observation=%zu probe history=%zu bytes\n",
        probe_checks,sizeof(CERBERUS_WARDRIVE_UNLOCATED_HEADER)-1U,sizeof(cerberus_wardrive_observation_t),sizeof(cerberus_wardrive_probe_dedupe_t));
    return 0;
}
