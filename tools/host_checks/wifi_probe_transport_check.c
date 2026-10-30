#undef NDEBUG
#include "c5_spi_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); assert(x); } } while(0)
typedef unsigned TickType_t;
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static int mutex_value, *link_mutex=&mutex_value;
static uint8_t sequence;
static bool locked;
static unsigned takes,gives,calls,peer_calls,status_calls,chunk_calls,callbacks;
static unsigned fail_call,fail_take,cancel_at,change_at,peer_change_at,peer_change_kind;
static unsigned chunk_corrupt,corrupt_chunk_at,status_corrupt,status_corrupt_at;
static unsigned lock_delay_ms,transfer_delay_ms,transactions_in_lock,max_transactions_in_lock;
static bool backward_clock;
static int64_t clock_us;
static cerberus_c5_status_t peer;
static cerberus_wifi_probe_status_t bank;
static cerberus_wifi_probe_row_t bank_rows[CERBERUS_WIFI_PROBE_LIMIT];
static int context_token;
static int64_t esp_timer_get_time(void) {return backward_clock?-1:clock_us;}
static int take_link_mutex(TickType_t ticks)
{
    CHECK(!locked&&link_mutex&&ticks<=100U);++takes;
    clock_us+=(int64_t)lock_delay_ms*1000;
    if(takes==fail_take)return 0;
    locked=true;transactions_in_lock=0;return pdTRUE;
}
static void give_link_mutex(void)
{
    CHECK(locked);locked=false;++gives;
    if(transactions_in_lock>max_transactions_in_lock)max_transactions_in_lock=transactions_in_lock;
    CHECK(transactions_in_lock<=1U);
    /* Another client can acquire the bus at every release. Changing the bank
     * here models a complete same-generation scan between caller exchanges. */
    if(calls==change_at){bank.epoch+=UINT64_C(0x100000000);change_at=0;}
    if(calls==peer_change_at){
        if(peer_change_kind==0)++peer.boot_count;
        else if(peer_change_kind==1)++peer.reset_reason;
        else if(peer_change_kind==2)peer.capabilities^=1U;
        else if(peer_change_kind==3)++peer.protocol_version;
        else if(peer_change_kind==4)++peer.firmware_version;
        else if(peer_change_kind==5)peer.uptime_ms=0;
        else peer.firmware_version=56;
        peer_change_at=0;
    }
}
static esp_err_t call_begin(void)
{
    CHECK(locked);++calls;++transactions_in_lock;clock_us+=(int64_t)transfer_delay_ms*1000;
    return calls==fail_call?ESP_FAIL:ESP_OK;
}
static esp_err_t get_status_v2_locked(uint8_t seq,cerberus_c5_status_t *out)
{
    (void)seq;++peer_calls;esp_err_t err=call_begin();if(err!=ESP_OK)return err;
    *out=peer;return ESP_OK;
}
static void corrupt_status(cerberus_wifi_probe_status_t *s)
{
    switch(status_corrupt){
    case 1:s->generation++;break;case 2:s->state=CERBERUS_WIFI_PROBE_RUNNING;break;
    case 3:s->count=17;break;case 4:s->flags|=4;break;case 5:s->reserved[0]=1;break;
    case 6:s->reserved[1]=1;break;case 7:s->epoch=0;break;case 8:s->matched_frames=0;break;
    case 9:s->dropped_frames=UINT32_MAX;break;case 10:s->flags|=CERBERUS_WIFI_PROBE_OVERFLOW;break;
    case 11:s->flags=0;break;case 12:s->matched_frames++;break;default:break;
    }
}
static esp_err_t exchange_v2_locked(cerberus_spi_command_t command,const void *request,
    size_t request_size,void *response,size_t response_size)
{
    esp_err_t err=call_begin();if(err!=ESP_OK)return err;
    memset(response,0,response_size);
    if(command==CERBERUS_SPI_COMMAND_WIFI_PROBE_STATUS){
        CHECK(request_size==sizeof(cerberus_wifi_probe_status_query_t)&&response_size==sizeof(bank));
        const cerberus_wifi_probe_status_query_t *q=request;
        ++status_calls;if(q->generation==bank.generation)*(cerberus_wifi_probe_status_t *)response=bank;
        if(status_calls==status_corrupt_at)corrupt_status(response);
    }else{
        CHECK(command==CERBERUS_SPI_COMMAND_WIFI_PROBE_CHUNK);
        CHECK(request_size==sizeof(cerberus_wifi_probe_query_t)&&response_size==sizeof(cerberus_wifi_probe_chunk_t));
        const cerberus_wifi_probe_query_t *q=request;cerberus_wifi_probe_chunk_t *out=response;++chunk_calls;
        CHECK(q->index<CERBERUS_WIFI_PROBE_LIMIT&&q->chunk<CERBERUS_WIFI_PROBE_CHUNKS);
        if(q->epoch==bank.epoch&&q->generation==bank.generation&&q->index<bank.count){
            size_t offset=q->chunk*10U,length=sizeof(bank_rows[0])-offset;if(length>10U)length=10U;
            *out=(cerberus_wifi_probe_chunk_t){.epoch=bank.epoch,.generation=bank.generation,
                .index=q->index,.chunk=q->chunk,.length=(uint8_t)length};
            memcpy(out->data,(const uint8_t *)&bank_rows[q->index]+offset,length);
        }
        if(chunk_calls==corrupt_chunk_at)switch(chunk_corrupt){
        case 1:out->epoch++;break;case 2:out->generation++;break;case 3:out->index++;break;
        case 4:out->chunk++;break;case 5:out->length++;break;case 6:out->length--;break;
        case 7:out->epoch=0;break;case 8:out->data[9]=1;break;default:break;}
    }
    return ESP_OK;
}
static bool keep_reading(void *context)
{
    CHECK(!locked&&context==&context_token);++callbacks;return callbacks!=cancel_at;
}
#include "wifi_probe_transport_impl.h"
static bool zero(const void *p,size_t n)
{const uint8_t *a=p;for(size_t i=0;i<n;++i)if(a[i])return false;return true;}
static void reset(unsigned count)
{
    locked=false;link_mutex=&mutex_value;sequence=0;takes=gives=calls=peer_calls=status_calls=chunk_calls=callbacks=0;
    fail_call=fail_take=cancel_at=change_at=peer_change_at=peer_change_kind=0;
    chunk_corrupt=corrupt_chunk_at=status_corrupt=status_corrupt_at=0;
    lock_delay_ms=transfer_delay_ms=transactions_in_lock=max_transactions_in_lock=0;
    backward_clock=false;clock_us=1000000;
    peer=(cerberus_c5_status_t){.firmware_version=57,.uptime_ms=20000,.capabilities=0x123,
        .protocol_version=CERBERUS_PROTOCOL_VERSION,.reset_reason=1,.boot_count=2};
    bank=(cerberus_wifi_probe_status_t){.epoch=UINT64_C(0x10000002a),.generation=255,
        .count=(uint8_t)count,.state=CERBERUS_WIFI_PROBE_COMPLETE,.flags=CERBERUS_WIFI_PROBE_AVAILABLE};
    memset(bank_rows,0,sizeof(bank_rows));
    for(unsigned i=0;i<count;++i){
        cerberus_wifi_probe_row_t *r=&bank_rows[i];r->transmitter[0]=0x10;r->transmitter[5]=(uint8_t)(i+1);
        memcpy(r->ssid,"fLoCk",5);r->ssid_length=5;r->channel=i&1U?149:6;r->rssi=-45;
        r->name_bits=CERBERUS_WIFI_PROBE_NAME_FLOCK;r->callback_ms=i?UINT64_C(0x100000001):0;
        r->frames=i+1;bank.matched_frames+=r->frames;CHECK(cerberus_wifi_probe_row_valid(r));
    }
}
static esp_err_t copy(cerberus_c5_wifi_probe_bank_t *proof,cerberus_wifi_probe_row_t *rows)
{
    memset(proof,0xa5,sizeof(*proof));memset(rows,0xa5,sizeof(*rows)*16);
    return c5_spi_link_copy_wifi_probe_results(bank.generation,(uint32_t)(clock_us/1000)+5000,
        proof,rows,16,keep_reading,&context_token);
}
static void failed(esp_err_t expected)
{
    cerberus_c5_wifi_probe_bank_t proof;cerberus_wifi_probe_row_t rows[16];
    esp_err_t actual=copy(&proof,rows);
    if(actual!=expected)fprintf(stderr,"expected=%d actual=%d calls=%u fail=%u change=%u peerchange=%u kind=%u status=%u chunk=%u cancel=%u\n",expected,actual,calls,fail_call,change_at,peer_change_at,peer_change_kind,status_corrupt,chunk_corrupt,cancel_at);
    CHECK(actual==expected);CHECK(zero(&proof,sizeof(proof))&&zero(rows,sizeof(rows))&&!locked);
    CHECK(takes==gives+(fail_take&&takes>=fail_take?1U:0U));
}
static void healthy_and_fences(void)
{
    for(unsigned count=0;count<=16;++count){
        reset(count);cerberus_c5_wifi_probe_bank_t proof;cerberus_wifi_probe_row_t rows[16];
        CHECK(copy(&proof,rows)==ESP_OK);CHECK(!memcmp(&proof.status,&bank,sizeof(bank)));
        CHECK(!memcmp(rows,bank_rows,sizeof(rows))&&takes==gives&&max_transactions_in_lock==1);
        CHECK(chunk_calls==count*6&&status_calls==2&&peer_calls==4);
        CHECK(c5_spi_link_check_wifi_probe_bank(&proof,6000)==ESP_OK);
        bank.epoch+=UINT64_C(0x100000000);CHECK(c5_spi_link_check_wifi_probe_bank(&proof,6000)==ESP_ERR_INVALID_RESPONSE);
    }
    reset(16);bank_rows[0].frames=UINT32_MAX;bank.matched_frames=UINT32_MAX;
    bank.flags|=CERBERUS_WIFI_PROBE_OVERFLOW;bank.dropped_frames=20;
    cerberus_c5_wifi_probe_bank_t proof;cerberus_wifi_probe_row_t rows[16];CHECK(copy(&proof,rows)==ESP_OK);
    reset(0);bank.flags=0;bank.epoch=0;CHECK(copy(&proof,rows)==ESP_OK&&!proof.status.flags);
    CHECK(c5_spi_link_copy_wifi_probe_results(255,6000,&proof,NULL,0,NULL,NULL)==ESP_OK);
    reset(2);bank_rows[1]=bank_rows[0];bank_rows[1].channel=149;
    bank.matched_frames=2;CHECK(copy(&proof,rows)==ESP_OK); /* Same source/SSID, different RX channel. */
    reset(1);bank_rows[0].transmitter[0]=0x58;bank_rows[0].transmitter[1]=0x8e;bank_rows[0].transmitter[2]=0x81;
    bank_rows[0].oui_hint=1;bank_rows[0].ssid_length=0;bank_rows[0].name_bits=0;memset(bank_rows[0].ssid,0,32);
    CHECK(copy(&proof,rows)==ESP_OK); /* Empty SSID retained by exact OUI clue. */
    reset(1);peer.capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ;
    CHECK(copy(&proof,rows)==ESP_OK);
    reset(1);peer.capabilities=CERBERUS_C5_CAP_WIFI_5_GHZ;bank_rows[0].channel=177;
    CHECK(copy(&proof,rows)==ESP_OK);
}
static void errors_and_restart(void)
{
    reset(2);cerberus_c5_wifi_probe_bank_t p;cerberus_wifi_probe_row_t rows[16];CHECK(copy(&p,rows)==ESP_OK);
    unsigned total=calls;
    for(unsigned i=1;i<=total;++i){reset(2);fail_call=i;failed(ESP_FAIL);}
    for(unsigned i=1;i<=total;++i){reset(2);fail_take=i;failed(ESP_ERR_TIMEOUT);}
    for(unsigned i=2;i<total-1U;++i){reset(2);change_at=i;failed(ESP_ERR_INVALID_RESPONSE);}
    /* A bank completed before its first status read can be adopted coherently.
     * A change after the last status read is caught by the service fence. */
    reset(2);change_at=1;CHECK(copy(&p,rows)==ESP_OK&&p.status.epoch==bank.epoch);
    reset(2);change_at=total-1U;CHECK(copy(&p,rows)==ESP_OK);
    CHECK(c5_spi_link_check_wifi_probe_bank(&p,6000)==ESP_ERR_INVALID_RESPONSE);
    for(unsigned k=0;k<7;++k)for(unsigned i=1;i<total;++i){reset(2);peer_change_at=i;peer_change_kind=k;failed(ESP_ERR_INVALID_RESPONSE);}
    reset(1);peer.firmware_version=56;failed(ESP_ERR_NOT_SUPPORTED);CHECK(!status_calls&&!chunk_calls);
    reset(1);CHECK(c5_spi_link_get_wifi_probe_bank(255,6000,&p)==ESP_OK);
    peer.firmware_version=56;CHECK(c5_spi_link_check_wifi_probe_bank(&p,6000)==ESP_ERR_INVALID_RESPONSE);
    reset(1);bank.flags|=CERBERUS_WIFI_PROBE_OVERFLOW;bank.dropped_frames=1;bank.matched_frames=2;
    failed(ESP_ERR_INVALID_RESPONSE); /* Capacity drops require an actually full bank. */
    reset(0);bank.matched_frames=1;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);peer.protocol_version++;failed(ESP_ERR_INVALID_RESPONSE);CHECK(!status_calls&&!chunk_calls);
    reset(1);peer.capabilities&=~(CERBERUS_C5_CAP_WIFI_2_4_GHZ|CERBERUS_C5_CAP_WIFI_5_GHZ);
    failed(ESP_ERR_INVALID_RESPONSE);CHECK(!status_calls&&!chunk_calls);
    reset(1);peer.capabilities=CERBERUS_C5_CAP_WIFI_5_GHZ;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);peer.capabilities=CERBERUS_C5_CAP_WIFI_2_4_GHZ;bank_rows[0].channel=149;
    failed(ESP_ERR_INVALID_RESPONSE);
    for(unsigned k=1;k<=12;++k)for(unsigned stage=1;stage<=2;++stage){reset(2);status_corrupt=k;status_corrupt_at=stage;failed(ESP_ERR_INVALID_RESPONSE);}
    for(unsigned k=1;k<=7;++k)for(unsigned part=1;part<=12;++part){reset(2);chunk_corrupt=k;corrupt_chunk_at=part;failed(ESP_ERR_INVALID_RESPONSE);}
    reset(2);chunk_corrupt=8;corrupt_chunk_at=12;failed(ESP_ERR_INVALID_RESPONSE);
    reset(2);bank_rows[1]=bank_rows[0];bank.matched_frames=2;failed(ESP_ERR_INVALID_RESPONSE);
    reset(2);bank.matched_frames++;failed(ESP_ERR_INVALID_RESPONSE);
    for(unsigned generation=0;generation<256;++generation){
        reset(1);bank.generation=(uint8_t)generation;change_at=7;failed(ESP_ERR_INVALID_RESPONSE);
    }
}
static void malformed_rows(void)
{
    for(unsigned value=0;value<256;++value){
        reset(1);bank_rows[0].channel=(uint8_t)value;
        if(!cerberus_wifi_probe_channel_valid((uint8_t)value))failed(ESP_ERR_INVALID_RESPONSE);
        reset(1);bank_rows[0].name_bits=(uint8_t)value;
        if(value!=CERBERUS_WIFI_PROBE_NAME_FLOCK)failed(ESP_ERR_INVALID_RESPONSE);
        reset(1);bank_rows[0].oui_hint=(uint8_t)value;
        if(value)failed(ESP_ERR_INVALID_RESPONSE);
        reset(1);bank_rows[0].ssid_length=(uint8_t)value;
        if(value<5||value>32)failed(ESP_ERR_INVALID_RESPONSE);
    }
    reset(1);bank_rows[0].ssid[31]=1;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);bank_rows[0].rssi=-128;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);bank_rows[0].rssi=1;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);memset(bank_rows[0].transmitter,0,6);failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);bank_rows[0].transmitter[0]|=1;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);bank_rows[0].frames=0;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);bank_rows[0].callback_ms=UINT64_MAX/1000U+1U;failed(ESP_ERR_INVALID_RESPONSE);
    reset(1);bank_rows[0].callback_ms=UINT64_MAX/1000U;
    cerberus_c5_wifi_probe_bank_t proof;cerberus_wifi_probe_row_t rows[16];CHECK(copy(&proof,rows)==ESP_OK);
}
static void cancellation_deadlines_and_args(void)
{
    reset(2);cerberus_c5_wifi_probe_bank_t proof;cerberus_wifi_probe_row_t rows[16];CHECK(copy(&proof,rows)==ESP_OK);
    unsigned total=callbacks;
    for(unsigned i=1;i<=total;++i){reset(2);cancel_at=i;failed(ESP_ERR_NOT_FINISHED);}
    reset(2);backward_clock=true;failed(ESP_ERR_INVALID_STATE);
    reset(1);lock_delay_ms=5000;failed(ESP_ERR_TIMEOUT);CHECK(!calls);
    reset(1);transfer_delay_ms=5000;failed(ESP_ERR_TIMEOUT);CHECK(calls==1);
    reset(1);clock_us=(int64_t)(UINT32_MAX-10U)*1000;transfer_delay_ms=1;
    CHECK(c5_spi_link_copy_wifi_probe_results(255,100,&proof,rows,16,NULL,NULL)==ESP_OK);
    reset(1);CHECK(c5_spi_link_copy_wifi_probe_results(255,1000,&proof,rows,16,NULL,NULL)==ESP_ERR_TIMEOUT&&!calls);
    reset(1);CHECK(c5_spi_link_copy_wifi_probe_results(255,999,&proof,rows,16,NULL,NULL)==ESP_ERR_TIMEOUT&&!calls);
    reset(1);CHECK(c5_spi_link_copy_wifi_probe_results(255,6000,&proof,rows,0,NULL,NULL)==ESP_ERR_INVALID_SIZE&&zero(&proof,sizeof(proof)));
    reset(1);CHECK(c5_spi_link_copy_wifi_probe_results(255,6000,&proof,NULL,1,NULL,NULL)==ESP_ERR_INVALID_ARG&&!calls);
    CHECK(c5_spi_link_copy_wifi_probe_results(255,6000,NULL,rows,16,NULL,NULL)==ESP_ERR_INVALID_ARG&&zero(rows,sizeof(rows))&&!calls);
    CHECK(c5_spi_link_copy_wifi_probe_results(255,6000,&proof,rows,17,NULL,NULL)==ESP_ERR_INVALID_ARG&&zero(rows,sizeof(rows))&&!calls);
    CHECK(c5_spi_link_get_wifi_probe_bank(255,6000,NULL)==ESP_ERR_INVALID_ARG);
    CHECK(c5_spi_link_check_wifi_probe_bank(NULL,6000)==ESP_ERR_INVALID_ARG);
    link_mutex=NULL;CHECK(c5_spi_link_get_wifi_probe_bank(255,6000,&proof)==ESP_ERR_INVALID_STATE&&zero(&proof,sizeof(proof)));
    failed(ESP_ERR_INVALID_STATE);
    reset(1);CHECK(c5_spi_link_get_wifi_probe_bank(255,6000,&proof)==ESP_OK);
    cerberus_c5_wifi_probe_bank_t frozen=proof;bank.count=0;bank.matched_frames=0;
    CHECK(c5_spi_link_check_wifi_probe_bank(&proof,6000)==ESP_ERR_INVALID_RESPONSE&&!memcmp(&proof,&frozen,sizeof(proof)));
}
int main(void)
{
    healthy_and_fences();errors_and_restart();malformed_rows();cancellation_deadlines_and_args();
    printf("Production C5 passive probe transport: %u checks; per-exchange release, deadline/cancel and atomic bank fences passed.\n",checks);
    return 0;
}
