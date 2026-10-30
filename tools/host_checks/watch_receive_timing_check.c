#undef NDEBUG
#include "watch_receive_timing.h"
#include <assert.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static uint64_t ceil_ms(uint64_t us){return us/1000U+(us%1000U!=0U);}
static bool empty(const void *memory,size_t size)
{const unsigned char *p=memory;while(size--)if(*p++)return false;return true;}
static cerberus_watch_scan_timing_payload_t payload(void)
{
    cerberus_watch_scan_timing_payload_t p={0};
    p.radio=CERBERUS_WATCH_RADIO_WIFI;p.generation=255;p.index=11;
    p.flags=CERBERUS_WATCH_TIMING_VALID;p.capture_epoch=UINT64_C(0x100000000);
    return p;
}
static void decode_and_validation(void)
{
    cerberus_watch_scan_timing_payload_t p=payload();cerberus_watch_receive_timing_t timing;
    CHECK(sizeof(p)==22);
    CHECK(cerberus_watch_receive_timing_decode(&p,2,255,11,0,&timing));
    CHECK(timing.valid&&!timing.source_age_ms&&!timing.request_started_us);
    CHECK(cerberus_watch_receive_timing_valid(&timing,0));
    uint64_t age=123;CHECK(cerberus_watch_receive_age_ms(&timing,0,&age)&&age==0);
    for(unsigned defect=0;defect<9;++defect){
        p=payload();
        if(defect==0)p.radio=1;
        if(defect==1)p.generation=0;
        if(defect==2)p.index=10;
        if(defect==3)p.capture_epoch=0;
        if(defect==4)p.flags=2;
        if(defect==5)p.reserved[0]=1;
        if(defect==6)p.reserved[1]=1;
        if(defect==7){p.flags=0;p.source_age_ms=1;}
        if(defect==8)p.source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS+1U;
        memset(&timing,0xa5,sizeof(timing));
        CHECK(!cerberus_watch_receive_timing_decode(&p,2,255,11,100,&timing));
        CHECK(empty(&timing,sizeof(timing)));
    }
    p=payload();p.flags=0;
    CHECK(cerberus_watch_receive_timing_decode(&p,2,255,11,100,&timing));
    CHECK(empty(&timing,sizeof(timing))&&cerberus_watch_receive_timing_valid(&timing,0));
    CHECK(!cerberus_watch_receive_age_ms(&timing,100,&age)&&age==0);
    timing.request_started_us=1;CHECK(!cerberus_watch_receive_timing_valid(&timing,100));
    timing=(cerberus_watch_receive_timing_t){1999,0,true};
    CHECK(cerberus_watch_receive_timing_valid(&timing,1));
    CHECK(!cerberus_watch_receive_timing_valid(&timing,0));
    CHECK(cerberus_watch_receive_age_at_ms(&timing,1,&age)&&age==0);
    CHECK(!cerberus_watch_receive_age_ms(&timing,1998,&age)&&age==0);
    CHECK(cerberus_watch_receive_age_ms(&timing,1999,&age)&&age==0);
    CHECK(cerberus_watch_receive_age_ms(&timing,2000,&age)&&age==1);
    CHECK(cerberus_watch_receive_age_ms(&timing,2998,&age)&&age==1);
    CHECK(cerberus_watch_receive_age_ms(&timing,2999,&age)&&age==1);
    CHECK(cerberus_watch_receive_age_ms(&timing,3000,&age)&&age==2);
    CHECK(!cerberus_watch_receive_age_at_ms(&timing,UINT64_MAX,&age)&&age==0);
    CHECK(!cerberus_watch_receive_timing_valid(NULL,0));
    CHECK(!cerberus_watch_receive_age_ms(NULL,0,&age));
    CHECK(!cerberus_watch_receive_age_ms(&timing,0,NULL));
    CHECK(!cerberus_watch_receive_timing_decode(NULL,2,255,11,0,&timing));
    CHECK(!cerberus_watch_receive_timing_decode(&p,2,255,11,0,NULL));
    CHECK(!cerberus_watch_receive_timing_decode(&p,0,255,11,0,&timing));
    CHECK(!cerberus_watch_receive_timing_decode(&p,2,255,12,0,&timing));
    p=payload();p.source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS;
    CHECK(cerberus_watch_receive_timing_decode(&p,2,255,11,0,&timing));
    CHECK(cerberus_watch_receive_age_ms(&timing,UINT64_MAX,&age));
    CHECK(age==2U*CERBERUS_WATCH_MAX_SOURCE_AGE_MS);
    cerberus_watch_receive_timing_t other=timing;
    CHECK(cerberus_watch_receive_timing_equal(&timing,&other));
    ++other.request_started_us;CHECK(!cerberus_watch_receive_timing_equal(&timing,&other));
    other=timing;++other.source_age_ms;CHECK(!cerberus_watch_receive_timing_equal(&timing,&other));
    other=timing;other.valid=false;CHECK(!cerberus_watch_receive_timing_equal(&timing,&other));
}
static void independent_clock_bound_properties(void)
{
    /* Source and S3 have unrelated uptime origins. Ground truth below uses
     * a separate physical timeline, never the helper's own subtraction. */
    const uint64_t origins[]={0,UINT64_C(4294967296)*1000U,UINT64_C(10000000000000)};
    const uint64_t spans[]={0,1,999,1000,1001,UINT64_C(4294967296)*1000U+37U};
    const unsigned delays[]={0,1,999,1000,1001,1999,17001};
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b)
    for(unsigned s=0;s<6;++s)for(unsigned q=0;q<7;++q)
    for(unsigned r=0;r<7;++r)for(unsigned d=0;d<7;++d){
        uint64_t rx=5000,request=rx+spans[s],sample=request+delays[q];
        uint64_t receive=sample+delays[r],display=receive+delays[d];
        uint64_t source_rx=origins[a]+rx,source_sample=origins[a]+sample;
        cerberus_watch_scan_timing_payload_t p=payload();
        p.source_age_ms=ceil_ms(source_sample-source_rx);
        cerberus_watch_receive_timing_t timing;
        uint64_t anchor=origins[b]+request,now=origins[b]+display;
        CHECK(cerberus_watch_receive_timing_decode(&p,2,255,11,anchor,&timing));
        CHECK(cerberus_watch_receive_timing_valid(&timing,now/1000U));
        uint64_t actual=ceil_ms(display-rx),upper;
        CHECK(cerberus_watch_receive_age_ms(&timing,now,&upper));
        CHECK(upper>=actual&&upper<=actual+ceil_ms(delays[q])+1U);
        uint64_t quantized;
        CHECK(cerberus_watch_receive_age_at_ms(&timing,now/1000U,&quantized));
        CHECK(quantized>=upper&&quantized<=upper+1U);
        CHECK(timing.request_started_us==anchor&&timing.source_age_ms==p.source_age_ms);
    }
}
int main(void)
{
    decode_and_validation();independent_clock_bound_properties();
    printf("Watch receive timing: %u checks; independent clock origins, transfer/quantization upper bounds, full-width epochs, zero, rewind and malformed metadata passed.\n",checks);
    return 0;
}
