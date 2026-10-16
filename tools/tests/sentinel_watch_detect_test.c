#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "sentinel_watch_detect.h"

static uint8_t packet[31];
static size_t used;
static void begin(void) { memset(packet, 0, sizeof(packet)); used = 0; }
static void field(uint8_t type, const void *data, size_t length)
{
    assert(used + length + 2U <= sizeof(packet));
    packet[used++] = (uint8_t)(length + 1U);
    packet[used++] = type;
    if (length) memcpy(packet + used, data, length);
    used += length;
}
static void name(const char *value) { field(9U, value, strlen(value)); }
static void service(uint16_t value)
{ uint8_t data[2] = {(uint8_t)value, (uint8_t)(value >> 8)}; field(3, data, 2); }
static void uuid(const char *value)
{
    uint8_t canonical[16], wire[16];
    size_t n = 0;
    for (const char *p = value; *p;) {
        if (*p == '-') { ++p; continue; }
        char pair[3] = {p[0], p[1], 0};
        assert(n < 16U);
        canonical[n++] = (uint8_t)strtoul(pair, NULL, 16);
        p += 2;
    }
    assert(n == 16U);
    for (size_t i = 0; i < 16U; ++i) wire[i] = canonical[15U - i];
    field(7, wire, 16U);
}
static sentinel_watch_match_t classify(const char *external_name)
{
    sentinel_watch_ble_observation_t observation = {
        .name = (const uint8_t *)external_name,
        .name_length = external_name ? strlen(external_name) : 0,
        .advertisement = packet, .advertisement_length = used,
    };
    sentinel_watch_match_t match;
    bool found = sentinel_watch_classify_ble(&observation, &match);
    assert(found == (match.category != SENTINEL_WATCH_NONE));
    if (found) {
        assert(match.confidence > SENTINEL_WATCH_CONFIDENCE_NONE);
        assert(match.label && strlen(match.label) <= 24U);
        assert(match.evidence && strlen(match.evidence) <= 48U);
        assert(match.rule_id && strlen(match.rule_id) < 48U);
        if (!strcmp(match.rule_id, "apple.offline-finding"))
            assert(!strcmp(match.label, "AirTag / Find My hint"));
        else assert(strstr(match.label, "AirTag") == NULL);
    } else assert(!match.rule_id && !match.label && !match.evidence && !match.confidence);
    return match;
}
static void expect(sentinel_watch_category_t category, sentinel_watch_confidence_t confidence, const char *id)
{
    sentinel_watch_match_t match = classify(NULL);
    if (match.category != category || match.confidence != confidence || !match.rule_id || strcmp(match.rule_id, id)) {
        fprintf(stderr,"Expected %s (%d/%d); got %s (%d/%d)\n", id, category, confidence,
            match.rule_id ? match.rule_id : "none", match.category, match.confidence);
        assert(false);
    }
}

static void test_finding_protocols(void)
{
    /* Synthetic full frame in the layout published by OpenHaystack Zephyr.
     * No real address, key or user observation is copied into the test. */
    uint8_t apple[29] = {0x4c,0,0x12,0x19};
    begin(); field(0xff,apple,sizeof(apple));
    expect(SENTINEL_WATCH_FINDMY,SENTINEL_WATCH_CONFIDENCE_HIGH,"apple.offline-finding");
    packet[4] = 0x02; /* iBeacon subtype is not Find My */
    assert(classify(NULL).category == SENTINEL_WATCH_NONE);
    packet[4] = 0x12; packet[5] = 0x02;
    assert(classify(NULL).category == SENTINEL_WATCH_NONE); /* nearby short form excluded */
    for (size_t length = 2; length < sizeof(apple); ++length) {
        begin(); field(0xff,apple,length);
        assert(classify(NULL).category == SENTINEL_WATCH_NONE);
    }
    uint8_t google[24] = {0xaa,0xfe,0x40};
    for (size_t length = 23; length <= 24; ++length) {
        for (uint8_t type = 0x40; type <= 0x41; ++type) {
            google[2] = type;
            begin(); field(1,"\x06",1); field(0x16,google,length);
            expect(SENTINEL_WATCH_TRACKER,SENTINEL_WATCH_CONFIDENCE_HIGH,"google.findhub-160");
        }
    }
    const uint8_t eddystone_types[] = {0,0x10,0x20,0x30};
    for (size_t i=0;i<sizeof(eddystone_types);++i) {
        google[2]=eddystone_types[i]; begin();field(0x16,google,sizeof(google));
        assert(classify(NULL).category == SENTINEL_WATCH_NONE);
    }
    begin();service(0xfe2c); /* Fast Pair is not a tracking-device claim. */
    assert(classify(NULL).category == SENTINEL_WATCH_NONE);
}

static void test_registry_service_families(void)
{
    const struct { uint16_t uuid; sentinel_watch_category_t category; const char *rule; } cases[] = {
        {0xfc81,SENTINEL_WATCH_AXON,"axon.fc81"},{0xfe6b,SENTINEL_WATCH_AXON,"axon.fe6b"},
        {0xfe6c,SENTINEL_WATCH_AXON,"axon.fe6c"},{0xfeed,SENTINEL_WATCH_TRACKER,"tile.feed"},
        {0xfeec,SENTINEL_WATCH_TRACKER,"tile.feec"},{0xfd84,SENTINEL_WATCH_TRACKER,"tile.fd84"},
        {0xfe65,SENTINEL_WATCH_TRACKER,"chipolo.fe65"},{0xfe33,SENTINEL_WATCH_TRACKER,"chipolo.fe33"},
        {0xfea6,SENTINEL_WATCH_CAMERA,"gopro.fea6"},
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        begin();service(cases[i].uuid);
        expect(cases[i].category,SENTINEL_WATCH_CONFIDENCE_MEDIUM,cases[i].rule);
        char expanded[40];
        snprintf(expanded,sizeof(expanded),"0000%04x-0000-1000-8000-00805f9b34fb",cases[i].uuid);
        begin();uuid(expanded);
        expect(cases[i].category,SENTINEL_WATCH_CONFIDENCE_MEDIUM,cases[i].rule);
        packet[2] ^= 1U; /* wrong Bluetooth base UUID */
        assert(classify(NULL).category == SENTINEL_WATCH_NONE);
    }
    begin(); service(0xfea6);
    uint8_t gopro[14] = {0xf2,0x02,2,1,65};
    field(0xff,gopro,sizeof(gopro));
    expect(SENTINEL_WATCH_CAMERA,SENTINEL_WATCH_CONFIDENCE_HIGH,"gopro.schema");
}

static void test_flock(void)
{
    uint8_t shared_chip[] = {0xc8,9,0x42};
    begin();field(0xff,shared_chip,sizeof(shared_chip));
    assert(classify(NULL).category == SENTINEL_WATCH_NONE);
    name("FS Ext Battery");
    expect(SENTINEL_WATCH_FLOCK,SENTINEL_WATCH_CONFIDENCE_HIGH,"flock.battery-corroborated");
    begin();name("Penguin-1234567890");field(0xff,shared_chip,sizeof(shared_chip));
    expect(SENTINEL_WATCH_FLOCK,SENTINEL_WATCH_CONFIDENCE_HIGH,"flock.battery-corroborated");
    begin();field(0xff,shared_chip,sizeof(shared_chip));
    sentinel_watch_match_t m=classify("FS Ext Battery");
    assert(m.confidence == SENTINEL_WATCH_CONFIDENCE_MEDIUM); /* cached name cannot corroborate */
    const char *bad[] = {"Penguin","Penguin-12345","Penguin-123456789X","My FS Ext Battery", "1234567890","Pigvision"};
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { begin();name(bad[i]);assert(classify(NULL).category == SENTINEL_WATCH_NONE); }
    begin();service(0x3100); assert(classify(NULL).category == SENTINEL_WATCH_NONE);
    service(0x3200);expect(SENTINEL_WATCH_FLOCK,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"flock.raven-service-pair");
    begin();uuid("e8ccbb38-9532-46a8-9fe5-1814df172e6f");
    expect(SENTINEL_WATCH_FLOCK,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"flock.accessory-service");

    sentinel_watch_wifi_observation_t wifi = {.ssid=(const uint8_t *)"Flock-A1B2C3",.ssid_length=12,.bssid={0x02,1,2,0xa1,0xb2,0xc3}};
    assert(sentinel_watch_classify_wifi(&wifi,&m) && m.confidence==SENTINEL_WATCH_CONFIDENCE_HIGH);
    ++wifi.bssid[5];
    assert(sentinel_watch_classify_wifi(&wifi,&m) && m.confidence==SENTINEL_WATCH_CONFIDENCE_MEDIUM);
    const char *wifi_bad[] = {"Flock","MyFlock-A1B2C3","Flock-A1B2C3-extra","Flock-12345X","flock of birds","Penguin"};
    for(size_t i=0;i<sizeof(wifi_bad)/sizeof(wifi_bad[0]);++i) {
        wifi.ssid=(const uint8_t *)wifi_bad[i];wifi.ssid_length=strlen(wifi_bad[i]);
        assert(!sentinel_watch_classify_wifi(&wifi,&m));
    }
}

static void test_wellness(void)
{
    begin();name("LVS-S001");service(0xfff0);
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"wellness.lovense-name-service");
    begin();service(0xfff0);
    assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    assert(classify("LVS-S001").confidence==SENTINEL_WATCH_CONFIDENCE_LOW);
    begin();uuid("53300001-0023-4bd4-bbd5-a6920e4c5653");
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"wellness.lovense-service");
    begin();uuid("5a300001-0024-4bd4-bbd5-a6920e4c5653");
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"wellness.lovense-service");
    begin();uuid("99990001-0023-4bd4-bbd5-a6920e4c5653");
    assert(classify(NULL).category==SENTINEL_WATCH_NONE); /* family wildcard forbidden */
    begin();uuid("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    assert(classify(NULL).category==SENTINEL_WATCH_NONE); /* generic Nordic UART */
    begin();name("Nova");uuid("f000bb03-0451-4000-b000-000000000000");
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_HIGH,"wellness.wevibe-name-service");
    begin();name("Nova"); assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    begin();name("6909 MV2");uuid("f0006900-110c-478b-b74b-6f403b364a9c");
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_HIGH,"wellness.mysteryvibe-service");
    begin();name("Onyx2");uuid("88f80580-0000-01e6-aace-0002a5d5c51b");
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_HIGH,"wellness.kiiroo2-name-service");
    begin();name("OhMiBod ESCA");service(0x1900);
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"wellness.kiiroo21-name-service");
    begin();name("KEON WIFI");service(0x1900);
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"wellness.keon-name-service");
    uint8_t satisfyer[]={0x5d,0,0,0,39};
    begin();name("SF 10005");field(0xff,satisfyer,sizeof(satisfyer));
    expect(SENTINEL_WATCH_WELLNESS,SENTINEL_WATCH_CONFIDENCE_MEDIUM,"wellness.satisfyer-pair");
    const char *ordinary[] = {"toy speaker","my vibe","Nova","Wand","Max","LOVE-Headphones","SF Airport","LVS-"};
    for(size_t i=0;i<sizeof(ordinary)/sizeof(ordinary[0]);++i) {
        begin();name(ordinary[i]);assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    }
}

static void test_reject_malformed_and_false_positives(void)
{
    const uint16_t generic[]={0x180a,0x1809,0x1819,0xfff0,0xfd5f,0xfd5a,0x1900};
    for(size_t i=0;i<sizeof(generic)/sizeof(generic[0]);++i) {begin();service(generic[i]);assert(classify(NULL).category==SENTINEL_WATCH_NONE);}
    const uint16_t manufacturers[]={0x004c,0x0075,0x09c8,0x058e,0x01ab,0x0d53,0x034d};
    for(size_t i=0;i<sizeof(manufacturers)/sizeof(manufacturers[0]);++i) {
        uint8_t bytes[]={(uint8_t)manufacturers[i],(uint8_t)(manufacturers[i]>>8),1,2,3};
        begin();field(0xff,bytes,sizeof(bytes));assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    }
    begin();service(0xfc81);packet[used++]=5;packet[used++]=0xff;
    assert(classify(NULL).category==SENTINEL_WATCH_NONE); /* no early positive before whole-packet validation */
    begin();field(3,"\x81\xfc\x00",3);
    assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    begin();field(7,packet,0);assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    begin();service(0xfc81);packet[used++]=0;packet[used++]=42;
    assert(classify(NULL).category==SENTINEL_WATCH_NONE); /* hidden TLV after terminator */
    begin();field(8,"FS Ext Battery",14);assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    begin();name("LVS-S001");name("LVS-X001");assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    begin();field(9,"FS Ext Battery\0X",16);assert(classify(NULL).category==SENTINEL_WATCH_NONE);
    sentinel_watch_match_t match;
    sentinel_watch_ble_observation_t o={.advertisement=packet,.advertisement_length=32};
    assert(!sentinel_watch_classify_ble(&o,&match));
    o.advertisement_length=1;o.advertisement=NULL;
    assert(!sentinel_watch_classify_ble(&o,&match));
    o=(sentinel_watch_ble_observation_t){.name=(const uint8_t *)"LVS-S001",.name_length=8,.advertisement_flags=1};
    assert(!sentinel_watch_classify_ble(&o,&match));
    assert(!sentinel_watch_classify_ble(NULL,&match));
    assert(!sentinel_watch_classify_ble(&o,NULL));
    assert(strcmp(sentinel_watch_category_name((sentinel_watch_category_t)-1),"Unknown")==0);
    assert(strcmp(sentinel_watch_confidence_name((sentinel_watch_confidence_t)99),"None")==0);
}

static void test_packet_boundary_fuzz(void)
{
    uint8_t buffer[64];
    uint8_t *end=buffer+sizeof(buffer);
#ifdef _WIN32
    SYSTEM_INFO info;GetSystemInfo(&info);
    uint8_t *allocation=VirtualAlloc(NULL,info.dwPageSize*2U,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(allocation);
    DWORD previous;
    assert(VirtualProtect(allocation+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&previous));
    end=allocation+info.dwPageSize;
#endif
    uint32_t random=0xCEB315U;
    for(size_t length=0;length<=31U;++length) {
        uint8_t *input=end-length;
        for(unsigned attempt=0;attempt<1000U;++attempt) {
            for(size_t i=0;i<length;++i) {random=random*1664525U+1013904223U;input[i]=(uint8_t)(random>>24);}
            sentinel_watch_match_t match;
            sentinel_watch_ble_observation_t o={.advertisement=input,.advertisement_length=length};
            (void)sentinel_watch_classify_ble(&o,&match);
            if(match.category==SENTINEL_WATCH_NONE) assert(!match.rule_id);
        }
    }
#ifdef _WIN32
    assert(VirtualFree(allocation,0,MEM_RELEASE));
#endif
}


static void test_new_broadcast_families(void)
{
    for (uint16_t color = 0; color <= 3U; ++color) {
        begin(); service((uint16_t)(0x3080U | color));
        assert(classify(NULL).category == SENTINEL_WATCH_NONE);
        assert(classify("Flipper Test").category == SENTINEL_WATCH_NONE);
        name("Flipper Test");
        expect(SENTINEL_WATCH_FLIPPER, SENTINEL_WATCH_CONFIDENCE_HIGH, "flipper.serial-name");
    }
    begin(); service(0x3084); name("Flipper Test"); assert(classify(NULL).category == 0);
    begin(); service(0x3081); name("My Flipper Test"); assert(classify(NULL).category == 0);
    begin(); name("Flipper Test"); assert(classify(NULL).category == 0);
    begin(); uuid("6ba1b218-15a8-461f-9fa8-5dcae273eafd");
    expect(SENTINEL_WATCH_MESHTASTIC, SENTINEL_WATCH_CONFIDENCE_MEDIUM, "meshtastic.service");
    uint8_t samsung[22] = {0x5a,0xfd,0x13,1,2,3,1,2,3,4,5,6,7,8,0xc3,0,0,0,1,2,3,4};
    for (uint8_t state = 1; state <= 6U; ++state) {
        samsung[2] = 0x10U | state;
        begin(); field(0x16,samsung,sizeof(samsung));
        expect(SENTINEL_WATCH_SAMSUNG, SENTINEL_WATCH_CONFIDENCE_HIGH, "samsung.smarttag-v1");
    }
    samsung[2] = 0x10; begin(); field(0x16,samsung,sizeof(samsung)); assert(classify(NULL).category == 0);
    samsung[2] = 0x23; begin(); field(0x16,samsung,sizeof(samsung)); assert(classify(NULL).category == 0);
    samsung[2] = 0x13; samsung[15] = 1; begin(); field(0x16,samsung,sizeof(samsung)); assert(classify(NULL).category == 0);
    samsung[15] = 0;
    for (size_t n = 2; n < sizeof(samsung); ++n) {
        begin(); field(0x16,samsung,n); assert(classify(NULL).category == 0);
    }
    uint8_t beacon[26] = {0xaa,0xfe};
    begin(); field(0x16,beacon,22); expect(SENTINEL_WATCH_BEACON,SENTINEL_WATCH_CONFIDENCE_HIGH,"beacon.eddystone");
    beacon[21] = 1; begin(); field(0x16,beacon,22); assert(classify(NULL).category == 0); beacon[21] = 0;
    beacon[2] = 0x10; beacon[4] = 2; beacon[5] = 'x';
    begin(); field(0x16,beacon,6); expect(SENTINEL_WATCH_BEACON,SENTINEL_WATCH_CONFIDENCE_HIGH,"beacon.eddystone");
    beacon[4] = 4; begin(); field(0x16,beacon,6); assert(classify(NULL).category == 0);
    beacon[2] = 0x20; beacon[3] = 0;
    begin(); field(0x16,beacon,16); expect(SENTINEL_WATCH_BEACON,SENTINEL_WATCH_CONFIDENCE_HIGH,"beacon.eddystone");
    beacon[3] = 1;
    begin(); field(0x16,beacon,20); expect(SENTINEL_WATCH_BEACON,SENTINEL_WATCH_CONFIDENCE_HIGH,"beacon.eddystone");
    begin(); field(0x16,beacon,16); assert(classify(NULL).category == 0);
    beacon[2] = 0x30;
    begin(); field(0x16,beacon,12); expect(SENTINEL_WATCH_BEACON,SENTINEL_WATCH_CONFIDENCE_HIGH,"beacon.eddystone");
    beacon[2] = 0x50; begin(); field(0x16,beacon,12); assert(classify(NULL).category == 0);
    uint8_t alt[26] = {1,2,0xbe,0xac}; alt[24] = 0xc5;
    begin(); field(0xff,alt,26); expect(SENTINEL_WATCH_BEACON,SENTINEL_WATCH_CONFIDENCE_HIGH,"beacon.altbeacon");
    alt[24] = 0x80; begin(); field(0xff,alt,26); assert(classify(NULL).category == 0);
    alt[24] = 0xc5; alt[2] = 0xac; begin(); field(0xff,alt,26); assert(classify(NULL).category == 0);
    for (int i = 0; i < SENTINEL_WATCH_CATEGORY_COUNT; ++i) assert(sentinel_watch_category_name((sentinel_watch_category_t)i));
}
void sentinel_test_protocol(void);

int main(void)
{
    test_finding_protocols();test_registry_service_families();test_flock();test_wellness();
    test_reject_malformed_and_false_positives();test_packet_boundary_fuzz();
    test_new_broadcast_families(); sentinel_test_protocol();
    puts("PASS: Watch protocol/name/UUID signatures, strict negatives, single-packet corroboration, malformed TLVs, and 32,000 guarded packet-boundary fuzz cases");
    return 0;
}
