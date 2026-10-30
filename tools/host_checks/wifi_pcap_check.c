#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_pcap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

int main(void)
{
    uint8_t file[68+52+40]={0},short_header[68];
    memset(short_header,0xa5,sizeof(short_header));
    assert(!cerberus_wifi_pcapng_file_header(CERBERUS_WIFI_PCAP_RADIO_C5,NULL,sizeof(file)));
    assert(!cerberus_wifi_pcapng_file_header(CERBERUS_WIFI_PCAP_RADIO_C5,short_header,67));
    assert(!cerberus_wifi_pcapng_file_header((cerberus_wifi_pcap_radio_t)8,
        short_header,sizeof(short_header)));
    for(unsigned i=0;i<sizeof(short_header);++i)assert(short_header[i]==0xa5);
    assert(cerberus_wifi_pcapng_file_header(CERBERUS_WIFI_PCAP_RADIO_C5,file,
        sizeof(file))==68);
    assert(u32(file)==0x0a0d0d0aU&&u32(file+4)==28&&u32(file+8)==0x1a2b3c4dU);
    assert(u16(file+12)==1&&u16(file+14)==0&&u32(file+16)==UINT32_MAX&&
        u32(file+20)==UINT32_MAX&&u32(file+24)==28);
    assert(u32(file+28)==1&&u32(file+32)==40&&u16(file+36)==127&&
        u16(file+38)==0&&u32(file+40)==4112&&u16(file+44)==2&&
        u16(file+46)==2&&file[48]=='C'&&file[49]=='5'&&
        u16(file+52)==9&&u16(file+54)==1&&file[56]==6&&
        u32(file+60)==0&&u32(file+64)==40);
    static const uint8_t expected_header[68]={
        0x0a,0x0d,0x0d,0x0a, 0x1c,0,0,0, 0x4d,0x3c,0x2b,0x1a,
        1,0,0,0, 0xff,0xff,0xff,0xff, 0xff,0xff,0xff,0xff,
        0x1c,0,0,0, 1,0,0,0, 0x28,0,0,0, 0x7f,0,0,0,
        0x10,0x10,0,0, 2,0,2,0, 'C','5',0,0, 9,0,1,0,
        6,0,0,0, 0,0,0,0, 0x28,0,0,0,
    };
    assert(!memcmp(file,expected_header,sizeof(expected_header)));
    assert(cerberus_wifi_pcapng_file_header(CERBERUS_WIFI_PCAP_RADIO_S3,
        short_header,sizeof(short_header))==68);
    assert(short_header[48]=='S'&&short_header[49]=='3');
    assert(cerberus_wifi_pcapng_file_header(CERBERUS_WIFI_PCAP_RADIO_R6,
        short_header,sizeof(short_header))==68);
    assert(short_header[48]=='R'&&short_header[49]=='6');

    uint8_t probe[]={0x40,0x00};
    cerberus_wifi_pcap_metadata_t utc={.channel=6,.rssi_dbm=-42,.rssi_known=true,
        .time_kind=CERBERUS_WIFI_PCAP_TIME_UTC,.capture_time_us=UINT64_C(1735689600123456)};
    uint8_t *record=file+68;
    assert(cerberus_wifi_pcapng_record(&utc,probe,2,2,record,52)==52);
    assert(u32(record)==6&&u32(record+4)==52&&u32(record+8)==0);
    assert(u32(record+12)==(uint32_t)(utc.capture_time_us>>32)&&
        u32(record+16)==(uint32_t)utc.capture_time_us);
    assert(u32(record+20)==18&&u32(record+24)==18&&u32(record+48)==52);
    const uint8_t *rtap=record+28;
    assert(rtap[0]==0&&rtap[1]==0&&u16(rtap+2)==16&&u32(rtap+4)==0x2a);
    assert(rtap[8]==0&&rtap[9]==0&&u16(rtap+10)==2437&&u16(rtap+12)==0x80&&
        (int8_t)rtap[14]==-42&&rtap[15]==0);
    assert(!memcmp(record+44,probe,2)&&record[46]==0&&record[47]==0);
    static const uint8_t expected_utc[52]={
        6,0,0,0, 0x34,0,0,0, 0,0,0,0, 0x99,0x2a,0x06,0,
        0x40,0x42,0x0e,0xba, 0x12,0,0,0, 0x12,0,0,0,
        0,0,0x10,0, 0x2a,0,0,0, 0,0,0x85,0x09, 0x80,0,0xd6,0,
        0x40,0,0,0, 0x34,0,0,0,
    };
    assert(!memcmp(record,expected_utc,sizeof(expected_utc)));

    uint8_t with_fcs[]={0xd4,0x00,0x11,0x22,0x33,0x44};
    cerberus_wifi_pcap_metadata_t mono={.channel=36,.frame_has_fcs=true,
        .time_kind=CERBERUS_WIFI_PCAP_TIME_MONOTONIC,.capture_time_us=1234567};
    record=file+120;
    assert(cerberus_wifi_pcapng_record(&mono,with_fcs,6,6,record,40)==40);
    assert(u32(record)==3&&u32(record+4)==40&&u32(record+8)==22&&u32(record+36)==40);
    rtap=record+12;
    assert(u16(rtap+2)==16&&u32(rtap+4)==0x0a&&rtap[8]==0x10&&rtap[9]==0&&
        u16(rtap+10)==5180&&u16(rtap+12)==0x0100&&rtap[14]==0&&rtap[15]==0);
    assert(!memcmp(record+28,with_fcs,6)&&record[34]==0&&record[35]==0);
    static const uint8_t expected_mono[40]={
        3,0,0,0, 0x28,0,0,0, 0x16,0,0,0,
        0,0,0x10,0, 0x0a,0,0,0, 0x10,0,0x3c,0x14, 0,1,0,0,
        0xd4,0,0x11,0x22,0x33,0x44,0,0, 0x28,0,0,0,
    };
    assert(!memcmp(record,expected_mono,sizeof(expected_mono)));
    /* A parser can walk the mixed UTC/untimed stream without guessing sizes. */
    unsigned blocks=0;for(size_t at=0;at<sizeof(file);){
        uint32_t size=u32(file+at+4);assert(size>=16&&size%4==0&&at+size<=sizeof(file));
        assert(u32(file+at+size-4)==size);at+=size;++blocks;
    }assert(blocks==4);

    uint8_t guard[64];memset(guard,0xa5,sizeof(guard));
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,51));
    assert(!cerberus_wifi_pcapng_record(NULL,probe,2,2,guard,sizeof(guard)));
    assert(!cerberus_wifi_pcapng_record(&utc,NULL,2,2,guard,sizeof(guard)));
    assert(!cerberus_wifi_pcapng_record(&utc,probe,0,2,guard,sizeof(guard)));
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,1,guard,sizeof(guard)));
    assert(!cerberus_wifi_pcapng_record(&utc,probe,4097,4097,guard,sizeof(guard)));
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,65536,guard,sizeof(guard)));
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,NULL,sizeof(guard)));
    for(unsigned i=0;i<sizeof(guard);++i)assert(guard[i]==0xa5);
    utc.channel=0;assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard)));
    utc.channel=15;assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard)));
    utc.channel=178;assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard)));
    utc.channel=14;assert(cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard))==52);
    assert(u16(guard+28+10)==2484);
    memset(guard,0xa5,sizeof(guard));
    utc.channel=6;utc.rssi_dbm=1;
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard)));
    utc.rssi_dbm=-42;utc.capture_time_us=UINT64_C(1234567);
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard)));
    utc.capture_time_us=UINT64_C(253402300800000000);
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard)));
    utc.capture_time_us=UINT64_C(1735689600123456);utc.time_kind=0;
    assert(!cerberus_wifi_pcapng_record(&utc,probe,2,2,guard,sizeof(guard)));
    mono.frame_has_fcs=true;
    assert(!cerberus_wifi_pcapng_record(&mono,probe,2,2,guard,sizeof(guard)));
    mono.frame_has_fcs=false;
    assert(!cerberus_wifi_pcapng_record(&mono,probe,2,3,guard,sizeof(guard)));
    for(unsigned i=0;i<sizeof(guard);++i)assert(guard[i]==0xa5);
    uint8_t overlap[52]={0x40,0};
    utc.time_kind=CERBERUS_WIFI_PCAP_TIME_UTC;
    assert(cerberus_wifi_pcapng_record(&utc,overlap,2,2,overlap,sizeof(overlap))==52);
    assert(overlap[44]==0x40&&overlap[45]==0);
    /* Untimed SPB can encode truncation exactly at its advertised snaplen. */
    uint8_t long_frame[CERBERUS_WIFI_PCAP_FRAME_MAX];memset(long_frame,0x5a,sizeof(long_frame));
    uint8_t large[CERBERUS_WIFI_PCAP_RECORD_MAX_SIZE+1];memset(large,0xa5,sizeof(large));
    assert(cerberus_wifi_pcapng_record(&mono,long_frame,sizeof(long_frame),5000,
        large,CERBERUS_WIFI_PCAP_RECORD_MAX_SIZE)==4128);
    assert(u32(large)==3&&u32(large+4)==4128&&u32(large+8)==5016&&
        u32(large+4124)==4128&&large[4128]==0xa5);
    assert(!cerberus_wifi_pcapng_record(&mono,long_frame,sizeof(long_frame),5000,
        large,4127));
    puts("Wi-Fi PCAPNG codec passed: mixed UTC/untimed records, radiotap channel/RSSI/FCS, exact lengths, truncation and malformed metadata.");
    return 0;
}
