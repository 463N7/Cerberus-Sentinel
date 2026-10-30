/* Reuse independent wire vectors and the checked SPI facade; its exchanges
 * now invoke the actual C5 Watch producer getters instead of synthetic rows. */
#define REMOTE_ID_C5_PRODUCER 1
#define main remote_id_foundation_suite_main
#include "remote_id_receive_check.c"
#undef main
#include "remote_id_inventory.h"
#include <stdatomic.h>

typedef unsigned wifi_promiscuous_pkt_type_t;
typedef struct { unsigned rx_state,sig_len,channel; int rssi,noise_floor; } wifi_pkt_rx_ctrl_t;
typedef struct { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[2400]; } wifi_promiscuous_pkt_t;
static atomic_uint mac_admission_token;
static void mac_receive(const wifi_promiscuous_pkt_t *p,wifi_promiscuous_pkt_type_t type,int64_t entered,uint32_t token)
{ (void)p;(void)type;(void)entered;(void)token; }
enum { WIFI_PKT_MGMT,WIFI_PKT_CTRL,WIFI_PKT_DATA,WIFI_PKT_MISC };
static wifi_promiscuous_pkt_t packet;
static cerberus_wifi_probe_table_t probe_capture;
static cerberus_wifi_relation_table_t relation_capture;
static atomic_bool relation_receiving;
static int defense_capture_mux;
static bool callback_locked,watch_capture_active,defense_capture_active;
static uint64_t watch_capture_armed_us,watch_capture_deadline_us;
static uint32_t watch_callback_inflight;
static uint8_t watch_capture_channel;
static unsigned callback_hook,critical_exits;
static unsigned defense_management_frames,defense_control_frames,defense_data_frames;
static unsigned defense_beacon_frames,defense_deauth_frames,defense_disassoc_frames,defense_noise_samples;
static int64_t defense_noise_sum;
static int8_t defense_strongest_rssi;
static cerberus_remote_id_inventory_t inventory;
static void enter_critical(int *mux)
{CHECK(mux==&defense_capture_mux&&!callback_locked);callback_locked=true;}
static void exit_critical(int *mux)
{
    CHECK(mux==&defense_capture_mux&&callback_locked);callback_locked=false;++critical_exits;
    if(callback_hook){
        unsigned action=callback_hook;callback_hook=0;
        /* Delay the old callback after its active/generation snapshot, then
         * close admission or replace the producer's generation before commit. */
        if(action==1)watch_capture_active=false;
        else cerberus_watch_capture_begin(&transport,CERBERUS_WATCH_RADIO_WIFI,
            (uint8_t)(transport.status.generation+1U),200U);
    }
}
#define portENTER_CRITICAL(m) enter_critical(m)
#define portEXIT_CRITICAL(m) exit_critical(m)
static int64_t esp_timer_get_time(void){return 1234567;}
static void scan_observation_receive(const wifi_promiscuous_pkt_t *p,unsigned type){(void)p;(void)type;}
static void channel_survey_receive(const wifi_promiscuous_pkt_t *p,unsigned type){(void)p;(void)type;}
static void packet_monitor_receive(const wifi_promiscuous_pkt_t *p,unsigned type){(void)p;(void)type;}
static bool cerberus_wifi_capture_service_active(void){return false;}
static void cerberus_wifi_capture_service_receive(const uint8_t *p,unsigned n,unsigned c,int r,uint64_t t)
{(void)p;(void)n;(void)c;(void)r;(void)t;CHECK(false);}
static void station_receive(const wifi_promiscuous_pkt_t *p){(void)p;}
static void cerberus_c5_test_receive(const uint8_t *p,unsigned n,unsigned c){(void)p;(void)n;(void)c;}
#define watch_capture transport
#include "remote_id_c5_producer_impl.h"
#undef watch_capture

static void reset_spi(void)
{
    CHECK(!locked&&!callback_locked);link_mutex=&mutex_token;lock_failure=false;
    takes=gives=status_reads=cap_reads=data_reads=fail_stage=fail_chunk=chunk_defect=before_defect=after_defect=0;
    peer=(cerberus_c5_status_t){.firmware_version=CERBERUS_WATCH_MIN_FIRMWARE,
        .capabilities=CERBERUS_C5_CAP_WATCH_OBSERVATIONS};
}
static void begin_capture(uint8_t generation)
{
    reset_spi();cerberus_watch_capture_begin(&transport,CERBERUS_WATCH_RADIO_WIFI,generation,100U);
    watch_capture_active=true;defense_capture_active=false;callback_hook=critical_exits=0;
}
static void frame(unsigned form,const char *id,bool coordinates)
{
    uint8_t payload[64];size_t size=make_pack(payload,id,coordinates);
    memset(&packet,0,sizeof(packet));
    size_t length=form==2?nan_variable(packet.payload,payload,size,0x7e,0xfe,0xab,17,31,NULL):
        beacon(packet.payload,payload,size);
    if(form==1){packet.payload[40]=0x90;packet.payload[41]=0x3a;packet.payload[42]=0xe6;}
    memset(packet.payload+length,0xb7,4); /* FCS must be excluded from decoder length. */
    packet.rx_ctrl=(wifi_pkt_rx_ctrl_t){.sig_len=(unsigned)length+4,.channel=149,.rssi=-67,.noise_floor=-96};
}
static void receive(void)
{wifi_promiscuous_callback(&packet,WIFI_PKT_MGMT);CHECK(!callback_locked);}
static void finish_capture(void)
{
    watch_capture_active=false;
    cerberus_watch_capture_finish(&transport,CERBERUS_WATCH_COMPLETE,1100U);
}
static cerberus_remote_id_publish_result_t transfer(uint64_t token,uint64_t receipt)
{
    cerberus_watch_scan_status_payload_t before,after;cerberus_watch_observation_t rows[12];
    uint8_t generation=transport.status.generation;
    reset_spi();CHECK(c5_spi_link_get_watch_status(CERBERUS_WATCH_RADIO_WIFI,generation,&before)==ESP_OK);
    for(uint8_t i=0;i<before.stored_count;++i){
        reset_spi();CHECK(c5_spi_link_get_watch_result(CERBERUS_WATCH_RADIO_WIFI,generation,i,&rows[i])==ESP_OK);
        CHECK(data_reads==3&&status_reads==2&&takes==1&&gives==1);
    }
    reset_spi();CHECK(c5_spi_link_get_watch_status(CERBERUS_WATCH_RADIO_WIFI,generation,&after)==ESP_OK);
    CHECK(!locked&&!callback_locked);
    return cerberus_remote_id_inventory_publish(&inventory,token,&before,&after,rows,before.stored_count,receipt);
}
static void decoded_formats_and_selected_source(void)
{
    for(unsigned form=0;form<3;++form){
        cerberus_remote_id_inventory_init(&inventory);begin_capture(255);frame(form,"TEST-A",true);receive();
        CHECK(transport.status.reports==1&&transport.status.stored_count==1);
        const cerberus_watch_observation_t *source=&transport.records[0];
        CHECK(source->transport==(form==2?SENTINEL_WATCH_TRANSPORT_WIFI_NAN:SENTINEL_WATCH_TRANSPORT_WIFI_BEACON));
        CHECK(source->address_type==0&&!memcmp(source->address,packet.payload+10,6));
        CHECK(source->channel==149&&source->rssi==-67&&source->flags==7);
        CHECK(source->latitude_e7==123456789&&source->longitude_e7==-987654321&&source->altitude_dm==150);
        finish_capture();CHECK(transfer(1,UINT64_C(5000000000))==CERBERUS_REMOTE_ID_PUBLISHED);
        cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&inventory,0,&page));
        CHECK(page.total==1&&page.rows[0].radio==CERBERUS_WATCH_RADIO_WIFI&&inventory.banks[0].status.generation==255);
        CHECK(page.rows[0].receipt_ms==UINT64_C(5000000000));
        CHECK(cerberus_remote_id_inventory_select(&inventory,&page.rows[0]));
        cerberus_remote_id_record_t frozen=inventory.selected.frozen;
        /* New whole sweep, same typed source, different claimed ID: no stale
         * coordinates; immutable painted evidence and its receipt stay intact. */
        begin_capture(0);frame(form,"TEST-B",false);packet.rx_ctrl.channel=6;packet.rx_ctrl.rssi=-81;receive();finish_capture();
        CHECK(transfer(2,UINT64_C(5000000900))==CERBERUS_REMOTE_ID_PUBLISHED);
        CHECK(inventory.selected.present&&inventory.selected.matching_sweeps==2);
        CHECK(!memcmp(&frozen,&inventory.selected.frozen,sizeof(frozen)));
        CHECK(!strcmp(inventory.selected.latest.observation.id,"TEST-B")&&inventory.selected.latest.observation.flags==1);
        CHECK(!inventory.selected.latest.observation.latitude_e7&&!inventory.selected.latest.observation.longitude_e7);
        CHECK(!inventory.selected.latest.observation.altitude_dm&&!inventory.selected.latest.observation.altitude_kind);
        CHECK(inventory.banks[0].status.generation==0&&inventory.selected.latest.sweep_token==2);
        CHECK(inventory.selected.latest.observation.channel==6&&inventory.selected.latest.observation.rssi==-81);
        CHECK(inventory.selected.latest.receipt_ms==UINT64_C(5000000900));
        /* Same claimed ID from another address must not become selected. */
        begin_capture(1);frame(form,"TEST-B",true);packet.payload[15]++;receive();finish_capture();
        CHECK(transfer(3,UINT64_C(5000001800))==CERBERUS_REMOTE_ID_PUBLISHED);
        CHECK(!inventory.selected.present&&inventory.selected.matching_sweeps==2);
        CHECK(!memcmp(&frozen,&inventory.selected.frozen,sizeof(frozen)));
    }
    /* Same MAC in beacon and NAN remains two distinct Wi-Fi sources. */
    cerberus_remote_id_inventory_init(&inventory);begin_capture(4);frame(0,"TEST-A",true);receive();
    frame(2,"TEST-A",true);receive();CHECK(transport.status.stored_count==2);finish_capture();
    CHECK(transfer(1,5000)==CERBERUS_REMOTE_ID_PUBLISHED);
    cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&inventory,0,&page)&&page.total==2);
    CHECK(!cerberus_remote_id_source_equal(&page.rows[0],&page.rows[1]));
}
static void admission_generation_and_malformed_frames(void)
{
    for(unsigned form=0;form<3;++form){
        for(unsigned reason=0;reason<7;++reason){
            begin_capture(255);frame(form,"TEST-A",true);
            if(reason==0)watch_capture_active=false;
            if(reason==1)packet.rx_ctrl.rx_state=1;
            if(reason==2)packet.rx_ctrl.sig_len=27;
            wifi_promiscuous_callback(reason==3?NULL:&packet,
                reason==4?WIFI_PKT_MISC:reason==5?WIFI_PKT_CTRL:reason==6?WIFI_PKT_DATA:WIFI_PKT_MGMT);
            CHECK(!transport.status.reports&&!transport.status.stored_count&&!callback_locked);
        }
        for(unsigned hook=1;hook<=2;++hook){
            begin_capture(255);frame(form,"TEST-A",true);callback_hook=hook;receive();
            CHECK(!transport.status.reports&&!transport.status.stored_count);
            CHECK(transport.status.generation==(hook==2?0:255));
        }
        begin_capture(3);frame(form,"TEST-A",true);
        unsigned full_length=packet.rx_ctrl.sig_len;
        for(unsigned length=28;length<full_length;++length){
            packet.rx_ctrl.sig_len=length;receive();CHECK(!transport.status.stored_count);
        }
        CHECK(transport.status.reports==full_length-28);
        packet.rx_ctrl.sig_len=full_length;receive();CHECK(transport.status.stored_count==1);
        uint32_t reports=transport.status.reports;cerberus_watch_observation_t saved=transport.records[0];
        /* Bad enclosing suffix after an otherwise qualifying frame is counted
         * as a delivered report but cannot overwrite the last good evidence. */
        unsigned end=full_length-4;packet.payload[end]=99;packet.rx_ctrl.sig_len=full_length+1;receive();
        CHECK(transport.status.reports==reports+1&&!memcmp(&saved,&transport.records[0],sizeof(saved)));
        if(form==2){packet.rx_ctrl.sig_len=full_length;packet.payload[4]^=1;receive();
            CHECK(transport.status.stored_count==1&&!memcmp(&saved,&transport.records[0],sizeof(saved)));}
    }
    begin_capture(8);frame(2,"TEST-A",true);receive();finish_capture();
    cerberus_watch_scan_status_payload_t status;memset(&status,0xa5,sizeof(status));
    CHECK(cerberus_wifi_watch_get_status(7,&status)==ESP_ERR_INVALID_STATE&&all_zero(&status,sizeof(status)));
    cerberus_watch_scan_data_request_t request={.radio=CERBERUS_WATCH_RADIO_WIFI,.generation=7,.index=0,.chunk=0};
    cerberus_watch_scan_data_payload_t chunk;memset(&chunk,0xa5,sizeof(chunk));
    CHECK(cerberus_wifi_watch_get_data(&request,&chunk)==ESP_ERR_INVALID_STATE&&all_zero(&chunk,sizeof(chunk)));
    request.generation=8;request.chunk=3;
    CHECK(cerberus_wifi_watch_get_data(&request,&chunk)==ESP_ERR_INVALID_STATE&&all_zero(&chunk,sizeof(chunk)));
    request.chunk=0;CHECK(cerberus_wifi_watch_get_data(&request,&chunk)==ESP_OK&&chunk.generation==8&&chunk.byte_count==17);
}
int main(void)
{
    decoded_formats_and_selected_source();admission_generation_and_malformed_frames();
    printf("Remote ID actual C5 callback/Watch producers -> S3 Watch getter -> inventory: %u checks; modern/legacy beacon, variable NAN, FCS exclusion, typed source, generation barrier/wrap, whole-payload replacement, frozen selection and invalid-frame retention passed.\n",checks);
    return 0;
}
