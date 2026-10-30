#include "s3_wifi_relation.h"
#include "cerberus_protocol.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"Relation model line %u: %s\n",__LINE__,#x); exit(1); } } while (0)

static cerberus_wifi_relation_row_t row(unsigned station, uint8_t channel)
{
    cerberus_wifi_relation_row_t r = {.bssid={2,1,2,3,4,5},.station={4,1,2,3,4,0},
        .channel=channel,.directions=3,.latest_direction=2,.rssi_valid=3,
        .to_ap_frames=2,.from_ap_frames=3,.to_ap_callback_us=1000000,
        .from_ap_callback_us=2000000,.to_ap_rssi=-45,.from_ap_rssi=-60};
    r.station[5]=(uint8_t)station;
    return r;
}

static cerberus_s3_wifi_relation_meta_t meta(size_t count)
{
    cerberus_s3_wifi_relation_meta_t m = {.status={.epoch=123,.generation=7,
        .count=(uint8_t)count,.state=CERBERUS_WIFI_RELATION_COMPLETE,
        .flags=CERBERUS_WIFI_RELATION_AVAILABLE,.matched_frames=(uint32_t)count*5U},
        .request_started_us=5000000,.source_now_us=3000000,
        .firmware_version=60,.capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ|CERBERUS_C5_CAP_WIFI_5_GHZ,
        .peer_uptime_ms=3000,.protocol_version=CERBERUS_PROTOCOL_VERSION,
        .boot_count=1,.clock_valid=true};
    return m;
}

static void retained_selection(void)
{
    cerberus_s3_wifi_relation_bank_t bank={0};
    cerberus_wifi_relation_row_t rows[4]={row(1,6),row(2,6),row(1,149),row(3,6)};
    rows[3].bssid[5]=6;
    cerberus_s3_wifi_relation_meta_t m=meta(4);
    CHECK(cerberus_s3_wifi_relation_apply(&bank,1,&m,rows,4,5100000)==CERBERUS_S3_WIFI_RELATION_APPLIED);
    cerberus_s3_wifi_relation_selection_t selected;
    CHECK(cerberus_s3_wifi_relation_select(&bank,1,rows[0].bssid,6,&selected));
    CHECK(selected.valid&&selected.count==2&&selected.bank.meta.status.count==4);
    CHECK(!memcmp(&selected.bank.rows[0],&rows[0],sizeof(rows[0])));
    CHECK(!memcmp(&selected.bank.rows[1],&rows[1],sizeof(rows[1])));
    uint64_t age=99;
    CHECK(cerberus_s3_wifi_relation_age_ms(&selected,0,1,5500001,&age)&&age==2501);
    CHECK(cerberus_s3_wifi_relation_age_ms(&selected,0,2,5500001,&age)&&age==1501);
    CHECK(!cerberus_s3_wifi_relation_age_ms(&selected,2,2,5500001,&age)&&age==0);
    CHECK(!cerberus_s3_wifi_relation_age_ms(&selected,0,3,5500001,&age)&&age==0);
    CHECK(!cerberus_s3_wifi_relation_age_ms(&selected,0,1,4999999,&age)&&age==0);
    cerberus_s3_wifi_relation_selection_t frozen=selected;
    cerberus_s3_wifi_relation_bank_t prior=bank;
    m.source_now_us+=1000000;m.request_started_us+=1000000;m.peer_uptime_ms+=1000;
    CHECK(cerberus_s3_wifi_relation_apply(&bank,1,&m,rows,4,6100000)==CERBERUS_S3_WIFI_RELATION_DUPLICATE);
    CHECK(!memcmp(&bank,&prior,sizeof(bank)));
    rows[0].to_ap_frames++;
    m.status.matched_frames++;
    CHECK(cerberus_s3_wifi_relation_apply(&bank,1,&m,rows,4,6100000)==CERBERUS_S3_WIFI_RELATION_REJECTED);
    CHECK(!memcmp(&bank,&prior,sizeof(bank)));
    m.status.epoch++;m.status.generation=7; /* Same public generation, new publication. */
    CHECK(cerberus_s3_wifi_relation_apply(&bank,257,&m,rows,4,6100000)==CERBERUS_S3_WIFI_RELATION_APPLIED);
    CHECK(!memcmp(&selected,&frozen,sizeof(selected)));
    CHECK(cerberus_s3_wifi_relation_select(&bank,1,rows[0].bssid,6,&selected));
    CHECK(selected.valid&&!selected.count&&selected.bank.outcome==CERBERUS_S3_WIFI_RELATION_UNKNOWN);
    CHECK(cerberus_s3_wifi_relation_select(&bank,257,rows[0].bssid,149,&selected)&&selected.count==1);
    CHECK(cerberus_s3_wifi_relation_select(&bank,257,rows[3].bssid,6,&selected)&&selected.count==1);
    CHECK(cerberus_s3_wifi_relation_set_outcome(&bank,258,8,CERBERUS_S3_WIFI_RELATION_LOADING,0));
    CHECK(cerberus_s3_wifi_relation_select(&bank,258,rows[0].bssid,6,&selected));
    CHECK(!selected.count&&selected.bank.outcome==CERBERUS_S3_WIFI_RELATION_LOADING);
    CHECK(!memcmp(&frozen.bank.rows[0],&prior.rows[0],sizeof(rows[0])));
    for (int outcome=0;outcome<=CERBERUS_S3_WIFI_RELATION_CANCELLED;++outcome) {
        if (outcome==CERBERUS_S3_WIFI_RELATION_READY) continue;
        CHECK(cerberus_s3_wifi_relation_set_outcome(&bank,259+(uint64_t)outcome,9,
            (cerberus_s3_wifi_relation_outcome_t)outcome,outcome==CERBERUS_S3_WIFI_RELATION_FAILED?-7:0));
        CHECK(cerberus_s3_wifi_relation_select(&bank,259+(uint64_t)outcome,rows[0].bssid,6,&selected));
        CHECK(!selected.count&&selected.bank.outcome==(cerberus_s3_wifi_relation_outcome_t)outcome);
    }
    CHECK(!cerberus_s3_wifi_relation_set_outcome(&bank,1,1,CERBERUS_S3_WIFI_RELATION_LOADING,0));
}

static void unavailable_zero_and_clock(void)
{
    cerberus_s3_wifi_relation_bank_t bank={0};
    cerberus_s3_wifi_relation_meta_t m=meta(0);
    cerberus_s3_wifi_relation_selection_t s;
    const uint8_t bssid[6]={2,1,2,3,4,5};
    CHECK(cerberus_s3_wifi_relation_apply(&bank,1,&m,NULL,0,5000000)==1);
    CHECK(cerberus_s3_wifi_relation_select(&bank,1,bssid,6,&s)&&s.count==0&&s.bank.outcome==CERBERUS_S3_WIFI_RELATION_READY);
    m.status.flags=0;m.status.epoch=0;m.clock_valid=false;m.request_started_us=m.source_now_us=0;
    CHECK(cerberus_s3_wifi_relation_apply(&bank,2,&m,NULL,0,5000000)==1);
    CHECK(cerberus_s3_wifi_relation_select(&bank,2,bssid,6,&s)&&s.bank.outcome==CERBERUS_S3_WIFI_RELATION_UNAVAILABLE);
    uint64_t age=9;CHECK(!cerberus_s3_wifi_relation_age_ms(&s,0,1,6000000,&age)&&age==0);
    cerberus_wifi_relation_row_t r=row(9,6);
    r.to_ap_callback_us=r.from_ap_callback_us=0;
    m=meta(1);m.source_now_us=m.request_started_us=0;
    CHECK(cerberus_s3_wifi_relation_apply(&bank,3,&m,&r,1,0)==1);
    CHECK(cerberus_s3_wifi_relation_select(&bank,3,bssid,6,&s));
    CHECK(cerberus_s3_wifi_relation_age_ms(&s,0,1,1,&age)&&age==1);
    const uint64_t revolution=UINT64_C(4294967296)*1000U;
    CHECK(cerberus_s3_wifi_relation_age_ms(&s,0,2,revolution+1000U,&age)&&age==UINT64_C(4294967297));
    s.bank.meta.source_now_us=INT64_MAX;s.bank.meta.request_started_us=0;
    CHECK(!cerberus_s3_wifi_relation_age_ms(&s,0,1,UINT64_MAX,&age)&&age==0);
    CHECK(!cerberus_s3_wifi_relation_select(&bank,0,bssid,6,&s));
    CHECK(!s.valid&&!s.count);
}

static void bad_banks_and_saturation(void)
{
    cerberus_wifi_relation_row_t rows[32];
    for (unsigned i=0;i<32;++i) rows[i]=row(i+1,6);
    cerberus_s3_wifi_relation_bank_t bank={0};
    cerberus_s3_wifi_relation_meta_t good=meta(32);
    good.status.flags|=CERBERUS_WIFI_RELATION_OVERFLOW;
    good.status.dropped_frames=20;good.status.matched_frames+=20;
    CHECK(cerberus_s3_wifi_relation_apply(&bank,1,&good,rows,32,5000000)==1);
    cerberus_s3_wifi_relation_bank_t unchanged=bank;
    for (unsigned field=0;field<13;++field) {
        cerberus_s3_wifi_relation_meta_t bad=good;
        switch(field) {
            case 0:bad.status.count=33;break;case 1:bad.status.state=1;break;
            case 2:bad.status.reserved[1]=1;break;case 3:bad.status.epoch=0;break;
            case 4:bad.status.matched_frames++;break;case 5:bad.clock_valid=false;break;
            case 6:bad.source_now_us=1999999;break;case 7:bad.source_now_us=UINT64_MAX;break;
            case 8:bad.request_started_us=5000001;break;case 9:bad.capabilities=0;break;
            case 10:bad.protocol_version=1;break;case 11:bad.firmware_version=59;break;
            default:bad.status.flags|=4;break;
        }
        CHECK(cerberus_s3_wifi_relation_apply(&bank,2,&bad,rows,32,5000000)==-1);
        CHECK(!memcmp(&bank,&unchanged,sizeof(bank)));
    }
    for(unsigned i=0;i<32;++i) {
        cerberus_wifi_relation_row_t old=rows[i];rows[i].reserved[1]=1;
        CHECK(cerberus_s3_wifi_relation_apply(&bank,2,&good,rows,32,5000000)==-1);
        CHECK(!memcmp(&bank,&unchanged,sizeof(bank)));rows[i]=old;
    }
    rows[31]=rows[0];CHECK(cerberus_s3_wifi_relation_apply(&bank,2,&good,rows,32,5000000)==-1);
    rows[31]=row(32,149);good.capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ;
    CHECK(cerberus_s3_wifi_relation_apply(&bank,2,&good,rows,32,5000000)==-1);
    good.capabilities|=CERBERUS_C5_CAP_WIFI_5_GHZ;
    for(unsigned i=0;i<32;++i) rows[i].to_ap_frames=rows[i].from_ap_frames=UINT32_MAX;
    good.status.matched_frames=good.status.dropped_frames=UINT32_MAX;
    CHECK(cerberus_s3_wifi_relation_apply(&bank,2,&good,rows,32,5000000)==1);
    cerberus_s3_wifi_relation_selection_t s;
    CHECK(cerberus_s3_wifi_relation_select(&bank,2,rows[0].bssid,6,&s)&&s.count==31);
    CHECK(s.bank.meta.status.dropped_frames==UINT32_MAX&&s.bank.rows[0].to_ap_frames==UINT32_MAX);
}

int main(void)
{
    retained_selection();unavailable_zero_and_clock();bad_banks_and_saturation();
    printf("Selected AP relationship model: %u checks; meta=%zu bank=%zu selection=%zu bytes; no allocation\n",
        checks,sizeof(cerberus_s3_wifi_relation_meta_t),sizeof(cerberus_s3_wifi_relation_bank_t),sizeof(cerberus_s3_wifi_relation_selection_t));
    return 0;
}
