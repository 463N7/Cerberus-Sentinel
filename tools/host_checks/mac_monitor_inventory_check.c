#include "mac_monitor_inventory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line%u: %s\n",__LINE__,#c);abort();}}while(0)
static cerberus_mac_monitor_inventory_t inventory,before;
static cerberus_mac_observation_t producer[2];
static cerberus_mac_snapshot_t snapshot,copy;
static uint64_t clock_us;
static void setup(void)
{cerberus_mac_monitor_inventory_init(&inventory);clock_us=0;for(unsigned r=0;r<2;++r)CHECK(cerberus_mac_observation_init(&producer[r],(uint8_t)r,0));}
static void observe(unsigned radio,unsigned id,unsigned type,int rssi,bool response)
{uint8_t address[6]={2,3,4,5,6,(uint8_t)id};CHECK(cerberus_mac_observation_observe(&producer[radio],address,(uint8_t)type,rssi,radio?0:6,response?2:1,++clock_us));}
static void publish(unsigned radio,uint64_t epoch)
{CHECK(cerberus_mac_observation_snapshot(&producer[radio],++clock_us,&snapshot));CHECK(cerberus_mac_monitor_inventory_publish(&inventory,(uint8_t)radio,epoch,&snapshot,100,200,true)==CERBERUS_MAC_PUBLISHED);}
static cerberus_mac_monitor_record_t *record(unsigned radio,unsigned id,unsigned type)
{for(unsigned n=0;n<32;++n){cerberus_mac_monitor_record_t *r=&inventory.banks[radio].records[n];if(r->valid && r->row.address[5]==id && r->row.address_type==type)return r;}return NULL;}
static void identities_counts_selection(void)
{
    setup();observe(0,7,0,-60,false);observe(1,7,0,-40,false);observe(1,7,2,-20,true);publish(0,11);publish(1,22);
    CHECK(record(0,7,0)->observations==1 && record(1,7,0)->observations==1 && record(1,7,2)->responses==1);
    cerberus_mac_monitor_page_t page;CHECK(cerberus_mac_monitor_inventory_page(&inventory,CERBERUS_MAC_SORT_SIGNAL,0,200,&page));
    CHECK(page.total==3 && page.rows[0].row.address_type==2 && page.rows[0].row.radio==1);
    CHECK(cerberus_mac_monitor_inventory_select(&inventory,&page.rows[0]));cerberus_mac_monitor_record_t frozen=inventory.selected.frozen;
    before=inventory;CHECK(cerberus_mac_monitor_inventory_publish(&inventory,1,22,&snapshot,900,1000,true)==CERBERUS_MAC_DUPLICATE);
    CHECK(!memcmp(&before,&inventory,sizeof(inventory)));
    for(unsigned n=0;n<100;++n){observe(1,7,2,n==99?127:-25,n%2!=0);publish(1,22);CHECK(record(1,7,2)->observations==n+2);}
    CHECK(record(1,7,2)->responses==51 && record(1,7,2)->row.rssi==127 && record(1,7,2)->row.flags==0);
    CHECK(!memcmp(&frozen,&inventory.selected.frozen,sizeof(frozen)) && inventory.selected.latest.observations==101);
    CHECK(cerberus_mac_monitor_inventory_page(&inventory,CERBERUS_MAC_SORT_SIGNAL,0,200,&page));
    CHECK(page.rows[page.copied-1].row.rssi==127);
    cerberus_mac_monitor_inventory_unavailable(&inventory,1,true);CHECK(inventory.selected.unavailable && !inventory.selected.present);
    before=inventory;copy=snapshot;copy.rows[0].incarnation=0;
    CHECK(cerberus_mac_monitor_inventory_publish(&inventory,1,22,&copy,100,200,true)==CERBERUS_MAC_INVALID);
    CHECK(!memcmp(&before,&inventory,sizeof(before)));
    publish(1,22);CHECK(!inventory.selected.unavailable && inventory.selected.present);
}
static void eviction_and_delta(void)
{
    setup();observe(0,1,0,-40,false);publish(0,1);
    CHECK(cerberus_mac_monitor_inventory_select(&inventory,record(0,1,0)));
    for(unsigned id=2;id<=33;++id)observe(0,id,0,-50,false);
    publish(0,1);CHECK(!inventory.selected.present && inventory.selected.latest.row.address[5]==1);
    CHECK(inventory.banks[0].header.replacements==1 && !record(0,1,0));
    observe(0,1,0,-30,false);publish(0,1);
    CHECK(inventory.selected.present && inventory.selected.latest.row.incarnation==34 && inventory.selected.frozen.row.incarnation==1);
    CHECK(inventory.selected.latest.observations==1); /* Prior evicted history is not invented. */
    for(unsigned n=0;n<120;++n){
        observe(0,1,0,-30,false);publish(0,1);CHECK(record(0,1,0)->observations==n+2);
        before=inventory;copy=snapshot;copy.header.snapshot_id--;CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,1,&copy,100,200,true)==CERBERUS_MAC_STALE);
        CHECK(!memcmp(&before,&inventory,sizeof(before)));
    }
    CHECK(cerberus_mac_observation_init(&producer[0],0,0));observe(0,1,0,-10,false);publish(0,2);
    CHECK(record(0,1,0)->observations==122 && inventory.selected.latest.epoch==2);
    cerberus_mac_monitor_page_t page;CHECK(cerberus_mac_monitor_inventory_page(&inventory,0,999,200,&page));
    CHECK(page.offset==28 && page.copied==4); /* Bounded earlier rows remain labeled. */
}
static void atomic_and_time(void)
{
    setup();observe(0,5,0,-70,false);publish(0,UINT64_MAX);
    cerberus_mac_monitor_record_t r=*record(0,5,0);uint64_t age;
    CHECK(cerberus_mac_monitor_age(&r,100,&age) && age==1);
    CHECK(!cerberus_mac_monitor_age(&r,99,&age) && !age);
    r.source_now_us=UINT64_MAX;r.row.last_us=0;CHECK(!cerberus_mac_monitor_age(&r,101,&age));
    CHECK(cerberus_mac_monitor_age(&r,100,&age) && age==UINT64_MAX/1000U+1);
    r.timing_valid=false;CHECK(!cerberus_mac_monitor_age(&r,100,&age));
    for(unsigned flaw=0;flaw<12;++flaw){
        before=inventory;copy=snapshot;copy.header.snapshot_id++;
        switch(flaw){case 0:copy.header.version++;break;case 1:copy.header.count=33;break;
        case 2:copy.rows[1].flags=1;break;case 3:copy.rows[0].address[0]|=1;break;
        case 4:copy.rows[0].last_us=copy.header.source_now_us+1;break;case 5:copy.rows[0].observations=0;break;
        case 6:copy.rows[0].responses=1;break;case 7:copy.rows[0].rssi=127;break;
        case 8:copy.rows[0].flags=0;break;case 9:copy.rows[0].incarnation++;break;
        case 10:copy.rows[0].address[5]++;break;case 11:copy.header.source_now_us=0;break;}
        /* A larger incarnation for the same key is a legitimate replacement. */
        if(flaw==9)continue;
        CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,UINT64_MAX,&copy,100,200,true)==CERBERUS_MAC_INVALID);
        CHECK(!memcmp(&before,&inventory,sizeof(before)));
    }
    copy=snapshot;copy.header.snapshot_id++;CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,UINT64_MAX,&copy,201,200,true)==CERBERUS_MAC_INVALID);
    CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,UINT64_MAX,&copy,0,200,false)==CERBERUS_MAC_PUBLISHED);
    CHECK(!record(0,5,0)->timing_valid);
    cerberus_mac_monitor_record_t forged=*record(0,5,0);forged.observations++;
    CHECK(!cerberus_mac_monitor_inventory_select(&inventory,&forged));
    CHECK(cerberus_mac_monitor_inventory_select(&inventory,record(0,5,0)));
    CHECK(cerberus_mac_monitor_inventory_select(&inventory,&inventory.selected.frozen));
}
static void saturation_and_sort(void)
{
    setup();for(unsigned radio=0;radio<2;++radio){for(unsigned n=0;n<32;++n)observe(radio,n,radio?n%4:0,(int)n-70,false);publish(radio,radio+1);}
    cerberus_mac_monitor_page_t page;
    for(unsigned sort=0;sort<3;++sort)for(unsigned offset=0;offset<80;++offset){
        CHECK(cerberus_mac_monitor_inventory_page(&inventory,(uint8_t)sort,offset,200,&page));
        CHECK(page.total==64 && page.copied<=4 && page.offset<64);
        for(unsigned n=0;n<page.copied;++n)CHECK(page.rows[n].valid);
    }
    setup();observe(0,1,0,-40,false);publish(0,1);
    producer[0].observations=UINT32_MAX;producer[0].rows[0].observations=UINT32_MAX;publish(0,1);
    CHECK(record(0,1,0)->observations==UINT32_MAX);
    observe(0,1,0,127,false);publish(0,1);CHECK(record(0,1,0)->observations==UINT32_MAX && !record(0,1,0)->row.flags);
}
static void impossible_shrink(void)
{
    setup();observe(0,1,0,-50,false);observe(0,2,0,-60,false);publish(0,8);
    CHECK(cerberus_mac_monitor_inventory_select(&inventory,record(0,2,0)));before=inventory;
    copy=snapshot;copy.header.snapshot_id++;copy.header.source_now_us++;
    copy.header.count=1;copy.header.observations=3;copy.rows[0].observations=3;
    copy.rows[0].last_us=copy.header.source_now_us;memset(&copy.rows[1],0,sizeof(copy.rows[1]));
    CHECK(cerberus_mac_snapshot_valid(&copy));
    CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,8,&copy,100,200,true)==CERBERUS_MAC_INVALID);
    CHECK(!memcmp(&before,&inventory,sizeof(before)));
    /* Empty is valid on a new epoch, but never deletes a live same-epoch bank. */
    memset(&copy,0,sizeof(copy));copy.header=(cerberus_mac_snapshot_header_t){.snapshot_id=3,.source_now_us=10,.version=1,.radio=0};
    CHECK(cerberus_mac_snapshot_valid(&copy));
    CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,8,&copy,100,200,true)==CERBERUS_MAC_INVALID);
    CHECK(!memcmp(&before,&inventory,sizeof(before)));
    CHECK(cerberus_mac_monitor_inventory_publish(&inventory,0,9,&copy,100,200,true)==CERBERUS_MAC_PUBLISHED);
    CHECK(!inventory.selected.present && inventory.selected.latest.row.address[5]==2);
}
int main(void){identities_counts_selection();eviction_and_delta();atomic_and_time();saturation_and_sort();impossible_shrink();
printf("MAC inventory: %u checks passed; inventory %zu record %zu bytes\n",checks,sizeof(inventory),sizeof(cerberus_mac_monitor_record_t));return 0;}
