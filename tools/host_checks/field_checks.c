#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "expansion_protocol.h"
#include "recon_db.h"
#include "sentinel_watch.h"
#include "../../expansion/shared/cerberus_spi_protocol.h"
static unsigned reads;
typedef struct { uint8_t *data; size_t size; bool missing; } memory_file_t;
static bool read_memory(void *context, uint32_t offset, void *out, size_t size, uint64_t *total)
{
    memory_file_t *file = context; ++reads;
    *total = file->size;
    if (file->missing || offset > file->size || size > file->size - offset) return false;
    memcpy(out, file->data + offset, size); return true;
}
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t *p, uint32_t value)
{ for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(value>>(i*8)); }

static void protocol_tests(void)
{
    uint8_t packet[64];
    expansion_config(packet, 17, 5, true);
    cerberus_spi_config_request_t config; memcpy(&config, packet, sizeof(config));
    assert(config.config.node_id == 6 && config.config.scan_enabled == 1);
    assert(config.config.region == CERBERUS_REGION_US && config.config.channels_2g_mask == 0xFFE);
    assert(config.config_guard == CERBERUS_SPI_CONFIG_GUARD && config.footer.sequence == 17);
    assert(config.footer.request_guard == CERBERUS_SPI_REQUEST_GUARD_VALUE(EXP_CONFIG,17));
    memset(packet,0,64); put32(packet,CERBERUS_SPI_MAGIC);
    packet[4]=3; packet[6]=3; packet[7]=9; packet[9]=1; packet[10]=17;
    packet[12+10]=36; packet[12+11]= (uint8_t)-50; packet[12+12]=4; packet[12+13]=3;
    packet[26]='A';packet[27]=0;packet[28]='B';
    expansion_record_t output[3]; uint8_t count=0;
    assert(expansion_parse_records(packet,9,0,output,&count) && count==1);
    assert(output[0].ssid_length==3 && output[0].ssid[2]=='B' && output[0].radio==1);
    assert(!expansion_parse_records(packet,10,0,output,&count) && count==0);
    packet[9]=2; /* first record is valid, second truncated: publish neither */
    assert(!expansion_parse_records(packet,9,0,output,&count) && count==0 && output[0].channel==0);
    packet[9]=1; packet[25]=33;
    assert(!expansion_parse_records(packet,9,0,output,&count) && count==0);
    packet[25]=3;
    uint8_t shifted[64]={0}; memcpy(shifted+40,packet,24);
    assert(!expansion_parse_records(shifted,9,0,output,&count));
    expansion_status_t status;
    memset(packet,0,64); put32(packet,CERBERUS_SPI_MAGIC);
    packet[4]=1;packet[6]=3;packet[8]=9;packet[12]=1;
    assert(expansion_parse_status(packet,9,&status) && status.firmware==1);
    packet[12]=2; assert(!expansion_parse_status(packet,9,&status));
    /* Deterministic malformed-frame corpus exercises every length/offset. */
    uint32_t seed=0x12345678;
    for (unsigned n=0;n<100000;++n) {
        for(unsigned j=0;j<64;++j) {seed=1664525*seed+1013904223; packet[j]=seed>>24;}
        if (n%2==0) { unsigned offset=n%56; put32(packet+offset,CERBERUS_SPI_MAGIC); }
        expansion_parse_records(packet,9,n%6,output,&count);
        expansion_parse_status(packet,9,&status);
        assert(count<=3);
    }
}
static void database_tests(const char *path)
{
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    uint8_t *data=malloc(size);assert(data && fread(data,1,size,f)==(size_t)size);fclose(f);
    memory_file_t file={data,(size_t)size,false};uint8_t scratch[4096];char vendor[48];
    uint8_t mac[6]={0x00,0x25,0xDF,0,0,1};
    assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_FOUND && vendor[0]);
    uint8_t ble_public[6]={1,0,0,0xDF,0x25,0};
    uint8_t converted[6]={0};
    assert(recon_db_ble_public_mac(ble_public,0,converted));
    assert(memcmp(converted,mac,6)==0);
    assert(recon_db_lookup(read_memory,&file,converted,vendor,scratch)==RECON_DB_FOUND && vendor[0]);
    assert(recon_db_ble_public_mac(ble_public,2,converted));
    assert(!recon_db_ble_public_mac(ble_public,1,converted));
    assert(!recon_db_ble_public_mac(ble_public,3,converted));
    assert(!recon_db_ble_public_mac(NULL,0,converted));
    mac[0]=2;reads=0;
    assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_PRIVATE_ADDRESS && reads==0 && !vendor[0]);
    mac[0]=0;file.missing=true;
    assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_UNAVAILABLE);
    file.missing=false;
    uint32_t count=u32(data+12);unsigned verified=0;
    for(unsigned i=0;i<count;++i) {
        uint8_t *entry=data+40+i*16;uint32_t key=u32(entry);
        mac[0]=key>>16;mac[1]=key>>8;mac[2]=key;
        if(mac[0]&3)continue;
        reads=0;assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_FOUND);
        assert(reads<=21 && vendor[0]); ++verified;
        if(verified==1) {
            uint32_t offset=u32(entry+4);data[offset+1]^=1;
            assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_INVALID && !vendor[0]);
            data[offset+1]^=1;
            uint32_t saved=u32(entry+4);put32(entry+4,0xFFFFFFF0);
            assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_INVALID);
            put32(entry+4,saved);
        }
    }
    uint32_t saved=u32(data+12);put32(data+12,0xFFFFFFFF);
    assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_INVALID);put32(data+12,saved);
    file.size=20;assert(recon_db_lookup(read_memory,&file,mac,vendor,scratch)==RECON_DB_UNAVAILABLE);
    printf("PASS: %u globally administered OUI lookups; private addresses, missing/truncated/corrupt DB rejected.\n",verified);
    free(data);
}
static void recon_policy_tests(void)
{
    sentinel_watch_t watch; sentinel_watch_init(&watch);
    sentinel_watch_match_t match={SENTINEL_WATCH_CAMERA,SENTINEL_WATCH_CONFIDENCE_LOW,"fixture","Fixture","Synthetic test"};
    uint8_t mac[6]={0,1,2,3,4,5};
    assert(!sentinel_watch_observe(&watch,SENTINEL_WATCH_RADIO_BLE,mac,0,"unit",4,-60,&match,0));
    assert(watch.history_count==1 && watch.queue_count==0); /* lower confidence still logged */
    match.confidence=SENTINEL_WATCH_CONFIDENCE_HIGH;
    assert(sentinel_watch_observe(&watch,SENTINEL_WATCH_RADIO_BLE,mac,0,"unit",4,-55,&match,1000));
    sentinel_watch_entry_t selected=*sentinel_watch_history(&watch,0);
    assert(sentinel_watch_tick(&watch,400000) && watch.alert_active);
    assert(sentinel_watch_set_ignored(&watch,&selected,true,400001) && !watch.alert_active);
    assert(!sentinel_watch_observe(&watch,SENTINEL_WATCH_RADIO_BLE,mac,0,"unit",4,-50,&match,800000));
    assert(watch.history_count==1 && watch.history[0].rssi==-50);
    assert(sentinel_watch_set_ignored(&watch,&selected,false,800001));
    uint32_t before_wrap=UINT32_MAX-100000U;
    assert(sentinel_watch_snooze(&watch,&selected,600000,before_wrap));
    assert(!sentinel_watch_observe(&watch,SENTINEL_WATCH_RADIO_BLE,mac,0,"unit",4,-50,&match,before_wrap+400000U));
    assert(!sentinel_watch_observe(&watch,SENTINEL_WATCH_RADIO_BLE,mac,0,"unit",4,-50,&match,before_wrap+590000U));
    assert(sentinel_watch_observe(&watch,SENTINEL_WATCH_RADIO_BLE,mac,0,"unit",4,-49,&match,before_wrap+800000U));
    assert(watch.queue_count==1);
    sentinel_watch_set_alert_filter(&watch,SENTINEL_WATCH_CONFIDENCE_HIGH,true,before_wrap+800001U);
    assert(watch.queue_count==0 && watch.history_count==1);
    sentinel_watch_clear_device_policies(&watch);
    for(unsigned i=0;i<SENTINEL_WATCH_POLICY_CAPACITY;++i) {
        selected.address[5]=i;
        assert(sentinel_watch_set_ignored(&watch,&selected,true,100));
    }
    selected.address[5]=99;
    assert(!sentinel_watch_set_ignored(&watch,&selected,true,100)); /* reject overflow without evicting ignored device */
    selected.address[5]=0;selected.address_type=1;
    assert(!sentinel_watch_is_ignored(&watch,&selected)); /* random/public identity stays distinct */
    selected.address_type=0;assert(sentinel_watch_is_ignored(&watch,&selected));
    assert(sentinel_watch_set_ignored(&watch,&selected,false,100));
    selected.address[5]=99;assert(sentinel_watch_set_ignored(&watch,&selected,true,101)); /* reclaim unused slot */
    sentinel_watch_clear_history(&watch);
    assert(watch.history_count==0 && watch.muted && watch.policy_count==16);
    puts("PASS: Recon confidence/mute/ignore/snooze, queue pruning, clock wrap, identity separation and bounded policy capacity.");
}
int main(int argc,char **argv)
{ assert(argc==2);protocol_tests();database_tests(argv[1]);recon_policy_tests();puts("PASS: expansion framing, config, truncation and 100000 malformed frames."); }
