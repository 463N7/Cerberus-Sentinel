#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "sentinel_watch_protocol.h"

/* All payloads are synthetic; these are parser tests, never over-the-air claims. */
static uint8_t frame[2304], basic[25], location[25], pack[228];
static size_t used;
static sentinel_watch_protocol_record_t record;
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, int32_t value)
{ uint32_t v = (uint32_t)value; for (size_t i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8U * i)); }
static void messages(void)
{
    memset(basic,0,sizeof(basic)); memset(location,0,sizeof(location));
    basic[0] = 2; basic[1] = 0x12; memcpy(basic + 2,"TEST-UAS-0001",13);
    location[0] = 0x12; location[1] = 0x20;
    put32(location + 5, 412345678); put32(location + 9, -875432100);
    put16(location + 13, 2400); put16(location + 15, 2501);
    pack[0] = 0xf2; pack[1] = 25; pack[2] = 2;
    memcpy(pack + 3,basic,25); memcpy(pack + 28,location,25);
}
static void ble(const uint8_t *payload, size_t length)
{
    memset(frame,0,sizeof(frame));
    frame[0] = (uint8_t)(5U + length); frame[1] = 0x16;
    frame[2] = 0xfa; frame[3] = 0xff; frame[4] = 0x0d; frame[5] = 255;
    memcpy(frame + 6,payload,length); used = length + 6U;
}
static bool read_ble(void) { return sentinel_watch_protocol_decode_ble(frame,used,&record); }
static bool read_wifi(void) { return sentinel_watch_protocol_decode_wifi(frame,used,&record); }
static void rejected_at(bool (*reader)(void), int line)
{
    memset(&record,0xa5,sizeof(record)); if (reader()) { fprintf(stderr,"Unexpected positive at test line %d, frame length %zu\n",line,used); assert(0); }
    sentinel_watch_protocol_record_t clean = {0}; assert(!memcmp(&record,&clean,sizeof(record)));
}
#define rejected(reader) rejected_at(reader, __LINE__)
static void ie(uint8_t type, const uint8_t *p, size_t n)
{
    assert(n <= 255U && used + n + 2U <= sizeof(frame));
    frame[used++] = type; frame[used++] = (uint8_t)n;
    memcpy(frame + used,p,n); used += n;
}
static void beacon(void)
{
    memset(frame,0,sizeof(frame)); frame[0] = 0x80;
    memset(frame + 4,0xff,6); frame[10] = 2; frame[16] = 4; used = 36;
}
static void drone_beacon(void)
{
    uint8_t data[58] = {0xfa,0x0b,0xbc,0x0d,254};
    memcpy(data + 5,pack,53); beacon(); ie(0, (const uint8_t *)"test",4); ie(221,data,sizeof(data));
}
static void nan(void)
{
    memset(frame,0,sizeof(frame)); frame[0] = 0xd0;
    static const uint8_t header[] = {4,9,0x50,0x6f,0x9a,0x13};
    static const uint8_t desc[] = {0x88,0x69,0x19,0x9d,0x92,9,1,0,0x10,54,255};
    memcpy(frame + 24,header,sizeof(header)); used = 30;
    frame[used++] = 3; put16(frame + used,64); used += 2;
    memcpy(frame + used,desc,sizeof(desc)); used += sizeof(desc);
    memcpy(frame + used,pack,53); used += 53;
    frame[used++] = 14; put16(frame + used,4); used += 2;
    frame[used++] = 1; frame[used++] = 0; frame[used++] = 2; frame[used++] = 255;
}
static void test_drone(void)
{
    messages(); ble(basic,25); assert(used == 31 && read_ble());
    assert(record.kind == SENTINEL_WATCH_PROTOCOL_DRONE && record.flags == SENTINEL_WATCH_PROTOCOL_HAS_ID);
    assert(!strcmp(record.id,"TEST-UAS-0001"));
    frame[5] = 0; assert(read_ble()); /* counter rollover is not a packet failure */
    for (size_t n = 0; n < 31U; ++n) { used = n; rejected(read_ble); }
    ble(basic,25); frame[4] = 0x0e; rejected(read_ble);
    ble(basic,25); frame[used++] = 4; frame[used++] = 3; rejected(read_ble);
    ble(basic,25); frame[used++] = 0; frame[used++] = 1; rejected(read_ble);
    ble(basic,25); frame[used++] = 0; assert(read_ble());
    basic[24] = 1; ble(basic,25); rejected(read_ble); messages();
    basic[2] = 0x1b; ble(basic,25); rejected(read_ble); messages();
    basic[1] = 0x32; basic[2] = 0xab; ble(basic,25); assert(read_ble()); assert(!record.flags && !record.id[0]); messages();
    ble(location,25); assert(read_ble());
    assert(record.flags == 6 && record.latitude_e7 == 412345678 && record.longitude_e7 == -875432100);
    assert(record.altitude_kind == SENTINEL_WATCH_ALTITUDE_GEODETIC && record.altitude_dm == 2505);
    put16(location + 15,0); ble(location,25); assert(read_ble());
    assert(record.altitude_kind == SENTINEL_WATCH_ALTITUDE_PRESSURE && record.altitude_dm == 2000);
    put16(location + 13,0); put32(location + 5,0); put32(location + 9,0);
    ble(location,25); assert(read_ble()); assert(record.flags == 0); messages();
    put32(location + 5,900000001); ble(location,25); rejected(read_ble); messages();
    put32(location + 9,INT32_MIN); ble(location,25); rejected(read_ble); messages();
    put32(location + 5,-900000000); put32(location + 9,1800000000); put16(location + 15,UINT16_MAX);
    ble(location,25); assert(read_ble()); assert(record.altitude_dm == 317675); messages();
    put16(location + 21,36001); ble(location,25); rejected(read_ble);
    put16(location + 21,UINT16_MAX); ble(location,25); assert(read_ble()); messages();
    ble(pack,53); assert(read_ble()); assert(record.flags == 7 && record.transport == SENTINEL_WATCH_TRANSPORT_BLE);
    pack[2] = 3; ble(pack,53); rejected(read_ble); messages();
    pack[28] = 0x11; ble(pack,53); rejected(read_ble); messages();
    pack[28] = 0x62; ble(pack,53); rejected(read_ble); messages();
    memcpy(pack + 3,location,25); ble(pack,53); rejected(read_ble); messages();
    pack[2] = 9;
    for (size_t i = 2; i < 9; ++i) { memcpy(pack + 3U + 25U * i,basic,25); pack[3U + 25U * i + 2] = (uint8_t)('A' + i); }
    ble(pack,228); assert(read_ble()); assert(used == 234); messages();
    drone_beacon(); assert(read_wifi()); assert(record.flags == 7 && record.transport == SENTINEL_WATCH_TRANSPORT_WIFI_BEACON);
    size_t full = used;
    for (size_t n = 0; n < full; ++n) { used = n; rejected(read_wifi); }
    drone_beacon(); frame[used++] = 221; rejected(read_wifi);
    drone_beacon(); frame[1] = 0x40; rejected(read_wifi);
    drone_beacon(); frame[22] = 1; rejected(read_wifi);
    nan(); assert(read_wifi()); assert(record.flags == 7 && record.transport == SENTINEL_WATCH_TRANSPORT_WIFI_NAN);
    full = used;
    for (size_t n = 0; n < full; ++n) {
        used = n;
        /* Descriptor is enough; the extension is optional but must be complete if present. */
        if (n == full - 7U) assert(read_wifi()); else rejected(read_wifi);
    }
    nan(); frame[41] = 0x11; rejected(read_wifi);
    nan(); frame[42] = 53; rejected(read_wifi);
    nan(); frame[used++] = 1; rejected(read_wifi);
}

static const char good_json[] = "{\"name\":\"Test Friend\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.10.3\",\"face\":\"(test)\",\"stats\":{\"x\":[1,-2.5e+2,true,false,null]}}";
static void pwn(const char *json, size_t split)
{
    beacon(); static const uint8_t source[] = {0xde,0xad,0xbe,0xef,0xde,0xad};
    memcpy(frame + 10,source,6);
    size_t length = strlen(json), pos = 0;
    while (pos < length) {
        size_t count = length - pos; if (count > split) count = split;
        ie(222,(const uint8_t *)json + pos,count); pos += count;
    }
}
static void test_pwn(void)
{
    for (size_t chunk = 1; chunk <= 255; ++chunk) {
        pwn(good_json,chunk); assert(read_wifi());
        assert(record.kind == SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI && !strcmp(record.id,"Test Friend"));
        assert(record.flags == SENTINEL_WATCH_PROTOCOL_HAS_ID);
    }
    pwn(good_json,255); frame[10] = 2; rejected(read_wifi);
    pwn(good_json,255); uint8_t one = 1; ie(223,&one,1); rejected(read_wifi);
    pwn(good_json,255); ie(226,&one,1); rejected(read_wifi);
    pwn(good_json,255); frame[used++] = 1; rejected(read_wifi);
    const char *bad[] = {
        "{\"name\":\"Pwnagotchi\"}",
        "{\"name\":\"Test\",\"identity\":\"short\",\"grid_version\":\"1.0\"}",
        "{\"name\":\"Test\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.0\",}",
        "{\"name\":\"Test\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.0\",\"name\":\"Again\"}",
        "{\"name\":\"Test\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.0\",\"x\":[1,]}",
        "{\"name\":\"Test\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.0\",\"x\":01}",
        "{\"name\":\"Test\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.0\",\"x\":\"bad\\q\"}",
        "{\"name\":\"Test\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.0\"} trailing",
        "{\"name\":\"Test\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.0\",\"x\":[[[[[[[[0]]]]]]]]}",
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) { pwn(bad[i],17); rejected(read_wifi); }
}
static void guarded_random(void)
{
#ifdef _WIN32
    SYSTEM_INFO info; GetSystemInfo(&info); size_t page = info.dwPageSize;
    uint8_t *pages = VirtualAlloc(NULL,page * 2U,MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE);
    assert(pages); DWORD old; assert(VirtualProtect(pages + page,page,PAGE_NOACCESS,&old));
    uint32_t random = 0x463127ab;
    for (unsigned trial = 0; trial < 12000U; ++trial) {
        size_t length = trial % 2305U; uint8_t *bytes = pages + page - length;
        for (size_t i = 0; i < length; ++i) { random = random * 1664525U + 1013904223U; bytes[i] = (uint8_t)(random >> 24); }
        if (length && (trial % 3U) == 0U) bytes[0] = 0x80;
        if (length && (trial % 3U) == 1U) bytes[0] = 0xd0;
        sentinel_watch_protocol_decode_wifi(bytes,length,&record);
        sentinel_watch_protocol_decode_ble(bytes,length < 256U ? length : 255U,&record);
    }
    assert(VirtualFree(pages,0,MEM_RELEASE));
#endif
}
void sentinel_test_protocol(void)
{
    test_drone(); test_pwn(); guarded_random();
    assert(!sentinel_watch_protocol_decode_ble(NULL,1,&record));
    assert(!sentinel_watch_protocol_decode_wifi(NULL,1,&record));
    assert(!sentinel_watch_protocol_decode_wifi(frame,2305,&record));
    puts("PASS: synthetic Remote ID legacy/extended BLE, beacon/NAN, PwnGrid split JSON, malformed frames, 12,000 guarded protocol boundaries");
}
