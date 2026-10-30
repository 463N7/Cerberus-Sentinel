/* Included by the production-core fixture; no replacement implementation. */
static cerberus_wifi_relation_row_t relation_source(void)
{
    return (cerberus_wifi_relation_row_t){
        .bssid={0x10,1,2,3,4,5}, .station={2,6,7,8,9,10}, .channel=6,
        .directions=CERBERUS_WIFI_RELATION_TO_AP, .latest_direction=CERBERUS_WIFI_RELATION_TO_AP,
        .rssi_valid=CERBERUS_WIFI_RELATION_TO_AP, .to_ap_frames=1, .to_ap_rssi=-80,
        .from_ap_rssi=CERBERUS_WIFI_RELATION_RSSI_UNAVAILABLE,
    };
}

static cerberus_wardrive_observation_t relation_observation(cerberus_wifi_relation_row_t source)
{
    cerberus_wardrive_observation_t row;
    memset(&row, 0xa5, sizeof(row));
    PCHECK(cerberus_wardrive_observation_from_relation(&source, &row));
    PCHECK(cerberus_wardrive_observation_valid(&row));
    PCHECK(row.type == CERBERUS_WARDRIVE_WIFI_RELATION && row.radio == CERBERUS_WARDRIVE_RADIO_C5);
    PCHECK(!memcmp(row.address, source.station, 6) &&
        !memcmp(row.relation_evidence.bssid, source.bssid, 6));
    PCHECK(row.relation_evidence.latest_direction == source.latest_direction &&
        row.relation_evidence.to_ap_frames == source.to_ap_frames &&
        row.relation_evidence.from_ap_frames == source.from_ap_frames);
    PCHECK(cerberus_wardrive_relation_callback_us(&row.relation_evidence) ==
        cerberus_wifi_relation_latest_us(&source));
    return row;
}

static void relation_core_checks(void)
{
    PCHECK(sizeof(cerberus_wardrive_relation_evidence_t) == 28U);
    PCHECK(sizeof(cerberus_wardrive_relation_dedupe_entry_t) == 24U);
    PCHECK(sizeof(cerberus_wardrive_relation_dedupe_t) == 776U);
    PCHECK(sizeof(cerberus_wardrive_observation_t) == 80U);
    PCHECK(sizeof(CERBERUS_WARDRIVE_UNLOCATED_HEADER) - 1U == 509U);
    PCHECK(cerberus_wardrive_relation_callback_us(NULL) == 0U);
    const char *header = strchr(CERBERUS_WARDRIVE_UNLOCATED_HEADER, '\n') + 1;
    csv_field(header,29,"bssid"); csv_field(header,30,"relation_ds");
    csv_field(header,31,"relation_rx_us"); csv_field(header,32,"uplink_count");
    csv_field(header,33,"downlink_count");

    cerberus_wifi_relation_row_t source = relation_source();
    cerberus_wardrive_observation_t row = relation_observation(source), before = row;
    PCHECK(!cerberus_wardrive_observation_from_relation(NULL, &row) && !memcmp(&row,&before,sizeof(row)));
    PCHECK(!cerberus_wardrive_observation_from_relation(&source, NULL));
    source.to_ap_callback_us = UINT64_C(4294967297001);
    source.from_ap_callback_us = source.to_ap_callback_us + 1U;
    source.directions = 3U; source.latest_direction = 2U; source.rssi_valid = 3U;
    source.from_ap_frames = 7U; source.from_ap_rssi = -30;
    row = relation_observation(source);
    PCHECK(row.rssi_dbm == -30 && row.relation_evidence.rssi_valid == 1U);
    source.latest_direction = 1U; source.to_ap_callback_us = source.from_ap_callback_us;
    row = relation_observation(source); /* Equal clocks retain the explicit latest direction. */
    PCHECK(row.rssi_dbm == -80);
    for (int rssi = -128; rssi <= 127; ++rssi) {
        source = relation_source(); source.to_ap_rssi = (int8_t)rssi;
        row = before;
        bool valid = rssi >= -127 && rssi <= 0;
        PCHECK(cerberus_wardrive_observation_from_relation(&source, &row) == valid);
        if (valid) PCHECK(row.rssi_dbm == rssi && row.relation_evidence.rssi_valid == 1U);
        else PCHECK(!memcmp(&row, &before, sizeof(row)));
    }
    source = relation_source(); source.to_ap_rssi = 127; source.rssi_valid = 0;
    row = relation_observation(source); PCHECK(row.rssi_dbm == 127 && !row.relation_evidence.rssi_valid);
    for (unsigned i=0; i<4; ++i) {
        cerberus_wardrive_observation_t bad = before; bad.relation_evidence.reserved[i] = 1;
        PCHECK(!cerberus_wardrive_observation_valid(&bad));
    }
    for (unsigned i=0; i<32; ++i) {
        cerberus_wardrive_observation_t bad = before; bad.label[i] = 1;
        PCHECK(!cerberus_wardrive_observation_valid(&bad));
    }
    for (unsigned field=0; field<12; ++field) {
        cerberus_wardrive_observation_t bad = before;
        switch(field) {
        case 0: bad.security=1; break;
        case 1: bad.manufacturer_id=1; break;
        case 2: bad.address_type=1; break;
        case 3: bad.label_length=1; break;
        case 4: bad.radio=CERBERUS_WARDRIVE_RADIO_R1; break;
        case 5: bad.channel=0; break;
        case 6: bad.relation_evidence.latest_direction=3; break;
        case 7: bad.relation_evidence.to_ap_frames=0; break;
        case 8: bad.relation_evidence.rssi_valid=2; break;
        case 9: bad.relation_evidence.callback_us_high=UINT32_MAX; break;
        case 10: memcpy(bad.address,bad.relation_evidence.bssid,6); break;
        case 11: bad.relation_evidence.bssid[0]|=1; break;
        }
        PCHECK(!cerberus_wardrive_observation_valid(&bad));
    }
    source=relation_source(); source.to_ap_callback_us=UINT64_MAX;
    row=before; PCHECK(!cerberus_wardrive_observation_from_relation(&source,&row));
    PCHECK(!memcmp(&row,&before,sizeof(row)));
    source=relation_source(); source.to_ap_callback_us=INT64_MAX;
    source.to_ap_frames=UINT32_MAX; source.from_ap_frames=UINT32_MAX;
    source.directions=3; source.channel=177; source.to_ap_rssi=-127;
    row=relation_observation(source);
    char csv[384], exact[384];
    size_t n=cerberus_wardrive_format_unlocated_row(&row,NULL,UINT64_MAX,UINT64_MAX,csv,sizeof(csv));
    PCHECK(n && n==strlen(csv) && n<sizeof(csv));
    csv_field(csv,2,"wifi_relation"); csv_field(csv,4,"02:06:07:08:09:0A");
    csv_field(csv,6,"177"); csv_field(csv,7,"-127");
    for (unsigned i=8; i<12; ++i) csv_field(csv,i,"");
    for (unsigned i=14; i<29; ++i) csv_field(csv,i,"");
    csv_field(csv,29,"10:01:02:03:04:05"); csv_field(csv,30,"TO_AP");
    csv_field(csv,31,"9223372036854775807");
    csv_field(csv,32,"4294967295"); csv_field(csv,33,"4294967295");
    unsigned columns=1; for(size_t i=0;i<n;++i) columns+=csv[i]==',';
    PCHECK(columns==34 && strchr(csv,'\n')==csv+n-1U);
    PCHECK(!cerberus_wardrive_format_unlocated_row(&row,NULL,UINT64_MAX,UINT64_MAX,exact,n));
    PCHECK(cerberus_wardrive_format_unlocated_row(&row,NULL,UINT64_MAX,UINT64_MAX,exact,n+1U)==n);
    PCHECK(!strcmp(csv,exact));
    source=relation_source(); source.directions=source.latest_direction=2; source.rssi_valid=0;
    source.to_ap_frames=0; source.to_ap_rssi=127; source.from_ap_frames=1;
    row=relation_observation(source);
    PCHECK(cerberus_wardrive_format_unlocated_row(&row,NULL,5,4,csv,sizeof(csv)));
    csv_field(csv,7,""); csv_field(csv,30,"FROM_AP"); csv_field(csv,31,"0");
    csv_field(csv,32,"0"); csv_field(csv,33,"1");

    cerberus_wardrive_relation_dedupe_t history={0}, prior;
    source=relation_source(); row=relation_observation(source);
    cerberus_wardrive_decision_t d=cerberus_wardrive_relation_decide(&history,&row,100);
    PCHECK(d.should_log); prior=history;
    PCHECK(cerberus_wardrive_relation_decide(&history,&row,101).should_log);
    PCHECK(!memcmp(&history,&prior,sizeof(history)));
    cerberus_wardrive_relation_commit(&history,&row,d,100);
    PCHECK(history.used==1 && !history.evicted && !cerberus_wardrive_relation_decide(&history,&row,101).should_log);
    row.relation_evidence.to_ap_frames=UINT32_MAX; row.relation_evidence.callback_us_high=1;
    PCHECK(!cerberus_wardrive_relation_decide(&history,&row,102).should_log);
    source.directions=3; source.latest_direction=2; source.rssi_valid=3;
    source.from_ap_frames=1; source.from_ap_rssi=-30;
    row=relation_observation(source); d=cerberus_wardrive_relation_decide(&history,&row,103);
    PCHECK(d.should_log); cerberus_wardrive_relation_commit(&history,&row,d,103);
    source.latest_direction=1; source.to_ap_rssi=-78; row=relation_observation(source);
    PCHECK(!cerberus_wardrive_relation_decide(&history,&row,104).should_log);
    source.to_ap_rssi=-77; row=relation_observation(source);
    d=cerberus_wardrive_relation_decide(&history,&row,104); PCHECK(d.should_log);
    cerberus_wardrive_relation_commit(&history,&row,d,104);
    source.latest_direction=2; source.from_ap_rssi=-28; row=relation_observation(source);
    PCHECK(!cerberus_wardrive_relation_decide(&history,&row,105).should_log);
    source.rssi_valid=1; source.from_ap_rssi=127; row=relation_observation(source);
    PCHECK(!cerberus_wardrive_relation_decide(&history,&row,106).should_log);
    row.channel=11; PCHECK(cerberus_wardrive_relation_decide(&history,&row,107).should_log);
    row.channel=6; row.relation_evidence.bssid[5]++;
    PCHECK(cerberus_wardrive_relation_decide(&history,&row,107).should_log);
    row=relation_observation(source); row.address[5]++;
    PCHECK(cerberus_wardrive_relation_decide(&history,&row,107).should_log);
    row=relation_observation(source);
    PCHECK(!cerberus_wardrive_relation_decide(&history,&row,300103).should_log);
    PCHECK(cerberus_wardrive_relation_decide(&history,&row,300104).should_log);
    prior=history;
    cerberus_wardrive_relation_commit(&history,&row,(cerberus_wardrive_decision_t){false,0},107);
    cerberus_wardrive_relation_commit(&history,&row,(cerberus_wardrive_decision_t){true,32},107);
    PCHECK(!memcmp(&history,&prior,sizeof(history)));
    cerberus_wardrive_dedupe_t normal={0}, saved=normal;
    PCHECK(!cerberus_wardrive_decide(&normal,&row,108).should_log);
    PCHECK(!cerberus_wardrive_decide_evidence(&normal,&row,108).should_log);
    cerberus_wardrive_commit(&normal,&row,(cerberus_wardrive_decision_t){true,0},108);
    PCHECK(!memcmp(&normal,&saved,sizeof(normal)));
    cerberus_wardrive_relation_dedupe_reset(&history);
    source=relation_source(); source.to_ap_rssi=127; source.rssi_valid=0;
    row=relation_observation(source); d=cerberus_wardrive_relation_decide(&history,&row,0);
    cerberus_wardrive_relation_commit(&history,&row,d,0);
    source.to_ap_rssi=-80; source.rssi_valid=1; row=relation_observation(source);
    PCHECK(cerberus_wardrive_relation_decide(&history,&row,1).should_log);
    cerberus_wardrive_relation_dedupe_reset(&history);
    for (unsigned i=0;i<33;++i) {
        source=relation_source(); source.station[5]=(uint8_t)i; row=relation_observation(source);
        d=cerberus_wardrive_relation_decide(&history,&row,1000+i); PCHECK(d.should_log);
        cerberus_wardrive_relation_commit(&history,&row,d,1000+i);
        PCHECK(history.used==(i<32?i+1:32) && history.evicted==(i<32?0:1));
    }
    history.evicted=UINT32_MAX; row.address[5]=40;
    d=cerberus_wardrive_relation_decide(&history,&row,2000);
    cerberus_wardrive_relation_commit(&history,&row,d,2000); PCHECK(history.evicted==UINT32_MAX);
    cerberus_wardrive_relation_dedupe_reset(&history);
    uint32_t start=UINT32_MAX-10U; d=cerberus_wardrive_relation_decide(&history,&row,start);
    cerberus_wardrive_relation_commit(&history,&row,d,start);
    PCHECK(!cerberus_wardrive_relation_decide(&history,&row,start+CERBERUS_WARDRIVE_RELOG_MS-1U).should_log);
    PCHECK(cerberus_wardrive_relation_decide(&history,&row,start+CERBERUS_WARDRIVE_RELOG_MS).should_log);
}
