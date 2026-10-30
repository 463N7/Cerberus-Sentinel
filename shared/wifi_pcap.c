#include "wifi_pcap.h"
#include <string.h>

/* PCAPNG block layouts follow draft-ietf-opsawg-pcapng-06, sections 4.1-4.4.
 * Radiotap field positions follow https://radiotap.org/fields/ . Upstream
 * ESP32Marauder 511bb3694d0c4dc064888e762bde44fcc9b7f48a uses PCAP
 * LINKTYPE_IEEE802_11 and boot-relative `micros()` in Buffer.cpp/PcapHeader.cpp.
 * This is a new serializer, not a copy of that implementation. */
static void le16(uint8_t *p,uint16_t value)
{p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void le32(uint8_t *p,uint32_t value)
{for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(i*8));}

size_t cerberus_wifi_pcapng_file_header(cerberus_wifi_pcap_radio_t radio,
    uint8_t *out,size_t capacity)
{
    if(!out||capacity<CERBERUS_WIFI_PCAP_FILE_HEADER_SIZE||
       radio<CERBERUS_WIFI_PCAP_RADIO_C5||radio>CERBERUS_WIFI_PCAP_RADIO_R6)return 0;
    memset(out,0,CERBERUS_WIFI_PCAP_FILE_HEADER_SIZE);
    /* Section Header Block: little endian, unknown section length. */
    le32(out,0x0a0d0d0aU);le32(out+4,28);le32(out+8,0x1a2b3c4dU);
    le16(out+12,1);le32(out+16,UINT32_MAX);le32(out+20,UINT32_MAX);
    le32(out+24,28);
    /* Interface Description Block: LINKTYPE_IEEE802_11_RADIOTAP = 127;
     * a two-character if_name identifies the radio that owned this file. */
    le32(out+28,1);le32(out+32,40);le16(out+36,127);
    le32(out+40,CERBERUS_WIFI_PCAP_SNAPLEN);
    le16(out+44,2);le16(out+46,2);
    if(radio==CERBERUS_WIFI_PCAP_RADIO_C5){out[48]='C';out[49]='5';}
    else if(radio==CERBERUS_WIFI_PCAP_RADIO_S3){out[48]='S';out[49]='3';}
    else {out[48]='R';out[49]=(uint8_t)('1'+radio-CERBERUS_WIFI_PCAP_RADIO_R1);}
    le16(out+52,9);le16(out+54,1);out[56]=6; /* if_tsresol = 10^-6 s */
    le32(out+64,40);
    return CERBERUS_WIFI_PCAP_FILE_HEADER_SIZE;
}

static bool channel_info(uint8_t channel,uint16_t *frequency,uint16_t *flags)
{
    if(channel>=1&&channel<=14){
        *frequency=channel==14?2484U:(uint16_t)(2407U+5U*channel);
        *flags=0x0080U; /* radiotap 2 GHz; modulation is not inferred. */
        return true;
    }
    if(channel>=32&&channel<=177){
        *frequency=(uint16_t)(5000U+5U*channel);*flags=0x0100U;
        return true;
    }
    return false;
}

size_t cerberus_wifi_pcapng_record(const cerberus_wifi_pcap_metadata_t *meta,
    const uint8_t *frame,size_t captured,size_t original,uint8_t *out,size_t capacity)
{
    uint16_t frequency,channel_flags;
    if(!meta||!frame||!out||!captured||captured>CERBERUS_WIFI_PCAP_FRAME_MAX||
       original<captured||original>UINT16_MAX||
       !channel_info(meta->channel,&frequency,&channel_flags)||
       (meta->frame_has_fcs&&captured<4U)||
       (meta->rssi_known&&meta->rssi_dbm>0))return 0;
    bool utc=meta->time_kind==CERBERUS_WIFI_PCAP_TIME_UTC;
    if(utc){
        /* Reject a boot-relative count mislabeled as Unix time. The owner of
         * the clock must still verify NTP/RTC/GPS provenance before calling. */
        if(meta->capture_time_us<UINT64_C(946684800000000)||
           meta->capture_time_us>UINT64_C(253402300799999999))return 0;
    }else if(meta->time_kind!=CERBERUS_WIFI_PCAP_TIME_MONOTONIC||
             (original>captured&&captured!=CERBERUS_WIFI_PCAP_FRAME_MAX)){
        /* SPB has no captured-length field; arbitrary truncation cannot be
         * represented without corrupting the record boundary. */
        return 0;
    }
    const size_t packet_captured=CERBERUS_WIFI_PCAP_RADIOTAP_SIZE+captured;
    const size_t packet_original=CERBERUS_WIFI_PCAP_RADIOTAP_SIZE+original;
    const size_t padded=(packet_captured+3U)&~(size_t)3U;
    const size_t fixed=utc?32U:16U,total=fixed+padded;
    if(capacity<total)return 0;
    const size_t packet_offset=utc?28U:12U;
    /* Move packet bytes first so an overlapping input cannot be overwritten by
     * the headers. The caller must still supply a valid `captured` span. */
    memmove(out+packet_offset+CERBERUS_WIFI_PCAP_RADIOTAP_SIZE,frame,captured);
    for(size_t i=packet_captured;i<padded;++i)out[packet_offset+i]=0;
    le32(out,utc?6U:3U);le32(out+4,(uint32_t)total);
    if(utc){
        le32(out+8,0); /* The first and only Interface Description Block. */
        le32(out+12,(uint32_t)(meta->capture_time_us>>32));
        le32(out+16,(uint32_t)meta->capture_time_us);
        le32(out+20,(uint32_t)packet_captured);
        le32(out+24,(uint32_t)packet_original);
    }else le32(out+8,(uint32_t)packet_original);
    uint8_t *rtap=out+packet_offset;
    memset(rtap,0,CERBERUS_WIFI_PCAP_RADIOTAP_SIZE);
    le16(rtap+2,CERBERUS_WIFI_PCAP_RADIOTAP_SIZE);
    le32(rtap+4,meta->rssi_known?0x2aU:0x0aU); /* flags, channel, optional dBm */
    rtap[8]=meta->frame_has_fcs?0x10U:0U;
    le16(rtap+10,frequency);le16(rtap+12,channel_flags);
    if(meta->rssi_known)rtap[14]=(uint8_t)meta->rssi_dbm;
    le32(out+total-4,(uint32_t)total);
    return total;
}
