#include "sentinel_watch_detect.h"

#include <string.h>

/* Protocol facts and selected permissively licensed catalog entries are
 * attributed in docs/WATCH_SIGNATURES.md. This parser and matcher are original.
 * UUID constants use canonical (display) byte order; BLE sends them reversed. */
typedef struct { const uint8_t *bytes; size_t length; } span_t;
typedef struct {
    uint32_t services;
    span_t complete_name;
    bool has_name, findmy, findhub, xuntong, gopro, satisfyer;
    bool samsung, eddystone, altbeacon;
} ad_facts_t;

enum {
    S_AXON = 1U << 0, S_TASER_B = 1U << 1, S_TASER_C = 1U << 2,
    S_RAVEN_GPS = 1U << 3, S_RAVEN_POWER = 1U << 4,
    S_FLOCK = 1U << 5, S_LOVENSE = 1U << 6, S_FFF0 = 1U << 7,
    S_WEVIBE = 1U << 8, S_MYSTERY = 1U << 9, S_KIIROO2 = 1U << 10,
    S_KIIROO21 = 1U << 11, S_1900 = 1U << 12, S_SATISFYER = 1U << 13,
    S_TILE_FEED = 1U << 14, S_TILE_FEEC = 1U << 15, S_TILE_FD84 = 1U << 16,
    S_CHIPOLO_FE65 = 1U << 17, S_CHIPOLO_FE33 = 1U << 18,
    S_GOPRO = 1U << 19, S_FLIPPER = 1U << 20, S_MESH = 1U << 21,
};

static uint16_t read_le16(const uint8_t *bytes)
{ return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8); }

static bool same(span_t value, const char *literal)
{
    size_t length = strlen(literal); /* literals only; never untrusted bytes */
    return value.length == length && memcmp(value.bytes, literal, length) == 0;
}

static bool text_valid(span_t value)
{
    if (value.length == 0 || value.bytes == NULL) return false;
    for (size_t i = 0; i < value.length; ++i)
        if (value.bytes[i] < 32U || value.bytes[i] > 126U) return false;
    return true;
}

static bool named(span_t name, const char *const *names, size_t count)
{
    for (size_t i = 0; i < count; ++i) if (same(name, names[i])) return true;
    return false;
}

static bool penguin_serial(span_t name)
{
    if (name.length != 18U || memcmp(name.bytes, "Penguin-", 8U) != 0) return false;
    for (size_t i = 8U; i < 18U; ++i)
        if (name.bytes[i] < '0' || name.bytes[i] > '9') return false;
    return true;
}

static bool lovense_name(span_t name)
{
    if (name.length <= 4U || memcmp(name.bytes, "LVS-", 4U) != 0) return false;
    for (size_t i = 4U; i < name.length; ++i) {
        uint8_t c = name.bytes[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

static bool uuid_equal(const uint8_t *wire, const uint8_t canonical[16])
{
    for (size_t i = 0; i < 16U; ++i) if (wire[i] != canonical[15U - i]) return false;
    return true;
}

static void service16(ad_facts_t *facts, uint16_t uuid)
{
    switch (uuid) {
    case 0x3080: case 0x3081: case 0x3082: case 0x3083: facts->services |= S_FLIPPER; break;
    case 0xfc81: facts->services |= S_AXON; break;
    case 0xfe6b: facts->services |= S_TASER_B; break;
    case 0xfe6c: facts->services |= S_TASER_C; break;
    case 0x3100: facts->services |= S_RAVEN_GPS; break;
    case 0x3200: facts->services |= S_RAVEN_POWER; break;
    case 0xfff0: facts->services |= S_FFF0; break;
    case 0x1900: facts->services |= S_1900; break;
    case 0xfeed: facts->services |= S_TILE_FEED; break;
    case 0xfeec: facts->services |= S_TILE_FEEC; break;
    case 0xfd84: facts->services |= S_TILE_FD84; break;
    case 0xfe65: facts->services |= S_CHIPOLO_FE65; break;
    case 0xfe33: facts->services |= S_CHIPOLO_FE33; break;
    case 0xfea6: facts->services |= S_GOPRO; break;
    default: break;
    }
}

typedef struct { uint8_t uuid[16]; uint32_t flag; } uuid_rule_t;
static const uuid_rule_t uuid_rules[] = {
    {{0x6b,0xa1,0xb2,0x18,0x15,0xa8,0x46,0x1f,0x9f,0xa8,0x5d,0xca,0xe2,0x73,0xea,0xfd}, S_MESH},
    {{0xe8,0xcc,0xbb,0x38,0x95,0x32,0x46,0xa8,0x9f,0xe5,0x18,0x14,0xdf,0x17,0x2e,0x6f}, S_FLOCK}, /* e8ccbb38-9532-46a8-9fe5-1814df172e6f */
    {{0xf0,0x00,0xbb,0x03,0x04,0x51,0x40,0x00,0xb0,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, S_WEVIBE}, /* f000bb03-0451-4000-b000-000000000000 */
    {{0xf0,0x00,0x69,0x00,0x11,0x0c,0x47,0x8b,0xb7,0x4b,0x6f,0x40,0x3b,0x36,0x4a,0x9c}, S_MYSTERY}, /* f0006900-110c-478b-b74b-6f403b364a9c */
    {{0x88,0xf8,0x05,0x80,0x00,0x00,0x01,0xe6,0xaa,0xce,0x00,0x02,0xa5,0xd5,0xc5,0x1b}, S_KIIROO2}, /* 88f80580-0000-01e6-aace-0002a5d5c51b */
    {{0xf6,0x04,0x02,0xa6,0x02,0x93,0x4b,0xdb,0x9f,0x20,0x67,0x58,0x13,0x3f,0x70,0x90}, S_KIIROO2}, /* f60402a6-0293-4bdb-9f20-6758133f7090 */
    {{0xa0,0xd7,0x00,0x01,0x4c,0x16,0x4b,0xa7,0x97,0x7a,0xd3,0x94,0x92,0x0e,0x13,0xa3}, S_KIIROO21}, /* a0d70001-4c16-4ba7-977a-d394920e13a3 */
    {{0x51,0x36,0x15,0x00,0xc5,0xe7,0x47,0xc7,0x8a,0x6e,0x47,0xeb,0xc9,0x9d,0x80,0xe8}, S_SATISFYER}, /* 51361500-c5e7-47c7-8a6e-47ebc99d80e8 */
    {{0x57,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 57300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x50,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 50300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x53,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 53300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x5a,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 5a300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x4f,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 4f300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x42,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 42300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x43,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 43300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x4c,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 4c300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x4c,0x41,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 4c410001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x56,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 56300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x58,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 58300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x52,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 52300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x46,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 46300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x50,0x30,0x00,0x11,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 50300011-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x4a,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 4a300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x45,0x44,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 45440001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x45,0x42,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 45420001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x54,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 54300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x45,0x49,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 45490001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x4e,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 4e300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x45,0x41,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 45410001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x51,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 51300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x45,0x46,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 45460001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x45,0x4c,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 454c0001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x55,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 55300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x53,0x44,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 53440001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x48,0x30,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 48300001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x46,0x53,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 46530001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x42,0x41,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 42410001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x43,0x41,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 43410001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x4f,0x43,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 4f430001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x45,0x5a,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 455a0001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x57,0x44,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 57440001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x41,0x4e,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 414e0001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x51,0x42,0x00,0x01,0x00,0x23,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 51420001-0023-4bd4-bbd5-a6920e4c5653 */
    {{0x50,0x30,0x00,0x01,0x00,0x24,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 50300001-0024-4bd4-bbd5-a6920e4c5653 */
    {{0x5a,0x30,0x00,0x01,0x00,0x24,0x4b,0xd4,0xbb,0xd5,0xa6,0x92,0x0e,0x4c,0x56,0x53}, S_LOVENSE}, /* 5a300001-0024-4bd4-bbd5-a6920e4c5653 */
};

static void service128(ad_facts_t *facts, const uint8_t *wire)
{
    static const uint8_t base[16] = {0,0,0,0,0,0,0x10,0,0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb};
    bool bluetooth_base = wire[15] == 0U && wire[14] == 0U;
    for (size_t i = 0; i < 12U; ++i) bluetooth_base &= wire[i] == base[15U - i];
    if (bluetooth_base) service16(facts, read_le16(wire + 12));
    for (size_t i = 0; i < sizeof(uuid_rules) / sizeof(uuid_rules[0]); ++i)
        if (uuid_equal(wire, uuid_rules[i].uuid)) facts->services |= uuid_rules[i].flag;
}

/* Registered SmartTag v1 service payload, as measured in the cited research.
 * Bit numbering in its diagram is MSB-first. Never match FD5A alone. */
static bool samsung_tag(const uint8_t *p, size_t n)
{
    if (n != 20U || (p[0] >> 4) != 1U || (p[0] & 7U) < 1U ||
        (p[0] & 7U) > 6U || p[13] || p[14] || p[15]) return false;
    uint8_t nonzero = 0;
    for (size_t i = 4U; i < 12U; ++i) nonzero |= p[i];
    return nonzero != 0;
}
static bool eddystone_frame(const uint8_t *p, size_t n)
{
    if (n < 2U) return false;
    switch (p[0]) {
    case 0x00: return n == 20U && p[18] == 0U && p[19] == 0U; /* UID */
    case 0x10: /* URL: scheme + at least one encoded byte; no URL is opened. */
        if (n < 4U || n > 20U || p[2] > 3U) return false;
        for (size_t i = 3U; i < n; ++i)
            if (p[i] > 13U && (p[i] < 33U || p[i] > 126U)) return false;
        return true;
    case 0x20: return (p[1] == 0U && n == 14U) || (p[1] == 1U && n == 18U);
    case 0x30: return n == 10U; /* rotating EID, not a physical identity */
    default: return false;
    }
}

static bool parse_ad(const uint8_t *data, size_t length, ad_facts_t *facts)
{
    size_t cursor = 0;
    while (cursor < length) {
        size_t field_length = data[cursor++];
        if (field_length == 0U) {
            /* Zero terminates AD. Only zero padding may follow. */
            while (cursor < length) if (data[cursor++] != 0U) return false;
            break;
        }
        if (field_length > length - cursor) return false;
        uint8_t type = data[cursor];
        const uint8_t *value = data + cursor + 1U;
        size_t size = field_length - 1U;
        if (type == 0x01 && size != 1U) return false;
        if (type == 0x02 || type == 0x03) {
            if (size == 0U || size % 2U != 0U) return false;
            for (size_t i = 0; i < size; i += 2U) service16(facts, read_le16(value + i));
        } else if (type == 0x04 || type == 0x05) {
            if (size == 0U || size % 4U != 0U) return false;
            for (size_t i = 0; i < size; i += 4U)
                if (value[i + 2U] == 0 && value[i + 3U] == 0) service16(facts, read_le16(value + i));
        } else if (type == 0x06 || type == 0x07) {
            if (size == 0U || size % 16U != 0U) return false;
            for (size_t i = 0; i < size; i += 16U) service128(facts, value + i);
        } else if (type == 0x08 || type == 0x09) {
            if (size > SENTINEL_WATCH_MAX_NAME_LENGTH || facts->has_name) return false;
            facts->has_name = true;
            /* A shortened name cannot satisfy an exact name rule. */
            if (type == 0x09 && text_valid((span_t){value, size}))
                facts->complete_name = (span_t){value, size};
        } else if (type == 0x16) {
            if (size < 2U) return false;
            uint16_t uuid = read_le16(value);
            service16(facts, uuid);
            /* Google Find Hub 160-bit frame. FEAA also carries Eddystone;
             * distinguish the documented 40/41 type and exact length. */
            if (uuid == 0xfd5a && samsung_tag(value + 2U, size - 2U)) facts->samsung = true;
            if (uuid == 0xfeaa && eddystone_frame(value + 2U, size - 2U)) facts->eddystone = true;
            if (uuid == 0xfeaa && (size == 23U || size == 24U) &&
                (value[2] == 0x40 || value[2] == 0x41)) facts->findhub = true;
        } else if (type == 0x20) {
            if (size < 4U) return false;
            if (value[2] == 0U && value[3] == 0U) service16(facts, read_le16(value));
        } else if (type == 0x21) {
            if (size < 16U) return false;
            service128(facts, value);
        } else if (type == 0xff) {
            if (size < 2U) return false;
            uint16_t company = read_le16(value);
            if (size == 26U && value[2] == 0xbe && value[3] == 0xac &&
                (value[24] == 0U || value[24] > 0x80U)) facts->altbeacon = true;
            if (company == 0x004c && size == 29U && value[2] == 0x12 && value[3] == 0x19)
                facts->findmy = true;
            if (company == 0x09c8 && size > 2U) facts->xuntong = true;
            if (company == 0x02f2 && size == 14U && (value[2] == 2U || value[2] == 3U))
                facts->gopro = true;
            if (company == 0x005d && size >= 5U && value[2] == 0U && value[3] == 0U &&
                (value[4] == 39U || value[4] == 40U)) facts->satisfyer = true;
        }
        cursor += field_length;
    }
    return true;
}

static void candidate(sentinel_watch_match_t *match, sentinel_watch_category_t category,
    sentinel_watch_confidence_t confidence, const char *id, const char *label, const char *evidence)
{
    if (confidence > match->confidence)
        *match = (sentinel_watch_match_t){category, confidence, id, label, evidence};
}

bool sentinel_watch_classify_ble(const sentinel_watch_ble_observation_t *observation,
    sentinel_watch_match_t *match)
{
    if (match == NULL) return false;
    memset(match, 0, sizeof(*match));
    if (observation == NULL || observation->name_length > SENTINEL_WATCH_MAX_NAME_LENGTH ||
        observation->advertisement_length > SENTINEL_WATCH_MAX_AD_LENGTH || observation->advertisement_flags != 0U ||
        (observation->name_length != 0U && observation->name == NULL) ||
        (observation->advertisement_length != 0U && observation->advertisement == NULL)) return false;
    ad_facts_t facts = {0};
    if (!parse_ad(observation->advertisement, observation->advertisement_length, &facts)) return false;
    /* Corroboration uses only a complete name IN THIS packet. An optional name
     * cached by the radio can support a name-only hint, never a binary upgrade. */
    span_t name = facts.complete_name;
    span_t fallback = facts.has_name ? name : (span_t){observation->name, observation->name_length};
    if (!text_valid(fallback)) fallback = (span_t){0};
    uint32_t s = facts.services;
    bool flipper_name = same(name, "Flipper") ||
        (name.length > 8U && memcmp(name.bytes, "Flipper ", 8U) == 0);
    if ((s & S_FLIPPER) && flipper_name)
        candidate(match, SENTINEL_WATCH_FLIPPER, SENTINEL_WATCH_CONFIDENCE_HIGH,
            "flipper.serial-name", "Flipper broadcast hint", "3080-3083 + Flipper name in the same packet");
    if ((s & S_MESH) != 0)
        candidate(match, SENTINEL_WATCH_MESHTASTIC, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
            "meshtastic.service", "Meshtastic BLE hint", "Documented Meshtastic 128-bit BLE service");
    if (facts.samsung)
        candidate(match, SENTINEL_WATCH_SAMSUNG, SENTINEL_WATCH_CONFIDENCE_HIGH,
            "samsung.smarttag-v1", "SmartTag protocol hint", "FD5A + complete registered-tag v1 structure");
    if (facts.eddystone)
        candidate(match, SENTINEL_WATCH_BEACON, SENTINEL_WATCH_CONFIDENCE_HIGH,
            "beacon.eddystone", "Eddystone beacon", "FEAA + supported Eddystone frame structure");
    if (facts.altbeacon)
        candidate(match, SENTINEL_WATCH_BEACON, SENTINEL_WATCH_CONFIDENCE_HIGH,
            "beacon.altbeacon", "AltBeacon broadcast", "BEAC marker + complete 26-byte manufacturer data");
    if (facts.findmy) candidate(match, SENTINEL_WATCH_FINDMY, SENTINEL_WATCH_CONFIDENCE_HIGH,
        "apple.offline-finding", "AirTag / Find My hint", "Apple 004C / type 12 / full 25-byte frame");
    if (facts.findhub) candidate(match, SENTINEL_WATCH_TRACKER, SENTINEL_WATCH_CONFIDENCE_HIGH,
        "google.findhub-160", "Find Hub broadcast", "FEAA / Find Hub type 40 or 41 / valid length");
    if ((s & S_FLOCK) != 0) candidate(match, SENTINEL_WATCH_FLOCK, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "flock.accessory-service", "Flock accessory hint", "Advertised Flock accessory 128-bit service");
    if ((s & (S_RAVEN_GPS | S_RAVEN_POWER)) == (S_RAVEN_GPS | S_RAVEN_POWER))
        candidate(match, SENTINEL_WATCH_FLOCK, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "flock.raven-service-pair", "Raven service hint", "Both custom services 3100 and 3200 observed");
    if (facts.xuntong && (same(name, "FS Ext Battery") || penguin_serial(name)))
        candidate(match, SENTINEL_WATCH_FLOCK, SENTINEL_WATCH_CONFIDENCE_HIGH,
        "flock.battery-corroborated", "Flock battery hint", "Battery name + 09C8 in the same packet");
    if (same(fallback, "FS Ext Battery") || penguin_serial(fallback))
        candidate(match, SENTINEL_WATCH_FLOCK, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "flock.battery-name", "Flock battery hint", "Exact battery name pattern / name can be copied");
    if ((s & S_AXON) != 0) candidate(match, SENTINEL_WATCH_AXON, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "axon.fc81", "Axon equipment hint", "FC81 service assigned to Axon / model unknown");
    if ((s & S_TASER_B) != 0) candidate(match, SENTINEL_WATCH_AXON, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "axon.fe6b", "Axon / TASER hint", "FE6B assigned to TASER / model unknown");
    if ((s & S_TASER_C) != 0) candidate(match, SENTINEL_WATCH_AXON, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "axon.fe6c", "Axon / TASER hint", "FE6C assigned to TASER / model unknown");
    if ((s & S_GOPRO) != 0) candidate(match, SENTINEL_WATCH_CAMERA,
        facts.gopro ? SENTINEL_WATCH_CONFIDENCE_HIGH : SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        facts.gopro ? "gopro.schema" : "gopro.fea6", "GoPro camera hint",
        facts.gopro ? "FEA6 + GoPro manufacturer schema 2 or 3" : "FEA6 advertised service / recording unknown");
    if ((s & S_TILE_FEED) != 0) candidate(match, SENTINEL_WATCH_TRACKER, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "tile.feed", "Tile tracker hint", "FEED service assigned to Tile / model unknown");
    if ((s & S_TILE_FEEC) != 0) candidate(match, SENTINEL_WATCH_TRACKER, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "tile.feec", "Tile tracker hint", "FEEC service assigned to Tile / model unknown");
    if ((s & S_TILE_FD84) != 0) candidate(match, SENTINEL_WATCH_TRACKER, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "tile.fd84", "Tile tracker hint", "FD84 service assigned to Tile / model unknown");
    if ((s & S_CHIPOLO_FE65) != 0) candidate(match, SENTINEL_WATCH_TRACKER, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "chipolo.fe65", "Chipolo tracker hint", "FE65 assigned to Chipolo / model unknown");
    if ((s & S_CHIPOLO_FE33) != 0) candidate(match, SENTINEL_WATCH_TRACKER, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "chipolo.fe33", "Chipolo tracker hint", "FE33 assigned to Chipolo / model unknown");
    if ((s & S_LOVENSE) != 0) candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "wellness.lovense-service", "Lovense protocol hint", "Documented Lovense proprietary service UUID");
    if ((s & S_FFF0) != 0 && lovense_name(name))
        candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "wellness.lovense-name-service", "Lovense protocol hint", "LVS- name + FFF0 service in the same packet");
    if (lovense_name(fallback)) candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_LOW,
        "wellness.lovense-name", "Lovense name hint", "LVS- local name only / identity unverified");
    static const char *const wevibe_names[] = {"Cougar","4 Plus","4_Plus","4plus","Bloom","classic","Classic",
        "Ditto","Gala","Jive","Nova","Pivot","Rave","Sync","Verge","Wish","Melt","Melt 2","Moxie",
        "Vector","Wand","Wand 2","Bond","Nelson","Nova2","Nova_2","Nova 2","Jive 2","Jive Lite",
        "Chorus","skeena","Sync 2","Sync O","Sync Lite"};
    if ((s & S_WEVIBE) != 0 && named(name, wevibe_names, sizeof(wevibe_names)/sizeof(wevibe_names[0])))
        candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_HIGH,
        "wellness.wevibe-name-service", "WeVibe protocol hint", "Known model name + proprietary WeVibe service");
    static const char *const mystery_names[] = {"MV Crescendo","MV Tenuto   ","MV Poco     ",
        "6907 MV1","6908 MV1","6909 MV1","6909 MV2","6914 MV1","6915 MV1"};
    if ((s & S_MYSTERY) != 0) candidate(match, SENTINEL_WATCH_WELLNESS,
        named(name, mystery_names, sizeof(mystery_names)/sizeof(mystery_names[0])) ?
        SENTINEL_WATCH_CONFIDENCE_HIGH : SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "wellness.mysteryvibe-service", "MysteryVibe service hint", "Proprietary MysteryVibe 128-bit service");
    static const char *const kiiroo2_names[] = {"Launch","Onyx2"};
    if ((s & S_KIIROO2) != 0 && named(name, kiiroo2_names, 2U))
        candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_HIGH,
        "wellness.kiiroo2-name-service", "Kiiroo protocol hint", "Known model name + Kiiroo V2 service");
    static const char *const kiiroo21_names[] = {"Titan1.1","Cliona","Pearl2.1","Pearl2+","Pearl 2+",
        "Pearl3","Pearl 3","OhMiBod 4.0","OhMiBod LUMEN","OhMiBod NEX2","OhMiBod NEX3","OhMiBod ESCA",
        "OhMiBod Foxy","OhMiBod Chill Panty Vibe","OhMiBod Sphinx","Pulse Interactive","Fuse1.1"};
    if ((s & (S_KIIROO21 | S_1900)) != 0 && named(name, kiiroo21_names, sizeof(kiiroo21_names)/sizeof(kiiroo21_names[0])))
        candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "wellness.kiiroo21-name-service", "Kiiroo / OhMiBod hint", "Known model name + documented Kiiroo service");
    if ((s & S_1900) != 0 && (same(name, "KEON WIFI") || same(name, "Keon Wifi") || same(name, "KEON2")))
        candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "wellness.keon-name-service", "Kiiroo Keon hint", "Exact KEON name + 1900 service");
    bool sf_name = name.length > 3U && memcmp(name.bytes, "SF ", 3U) == 0;
    if (sf_name && ((s & S_SATISFYER) != 0 || facts.satisfyer))
        candidate(match, SENTINEL_WATCH_WELLNESS, SENTINEL_WATCH_CONFIDENCE_MEDIUM,
        "wellness.satisfyer-pair", "Satisfyer protocol hint", "SF name + Satisfyer protocol evidence");
    return match->category != SENTINEL_WATCH_NONE;
}

static int hex_value(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool sentinel_watch_classify_wifi(const sentinel_watch_wifi_observation_t *observation,
    sentinel_watch_match_t *match)
{
    if (match == NULL) return false;
    memset(match, 0, sizeof(*match));
    if (observation == NULL || observation->ssid_length > SENTINEL_WATCH_MAX_SSID_LENGTH ||
        (observation->ssid_length != 0U && observation->ssid == NULL)) return false;
    span_t name = {observation->ssid, observation->ssid_length};
    if (!text_valid(name) || name.length != 12U) return false;
    /* Exact Flock-XXXXXX layout, not "flock" anywhere in an SSID. */
    static const char prefix[] = "flock-";
    for (size_t i = 0; i < 6U; ++i) {
        uint8_t c = name.bytes[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (uint8_t)prefix[i]) return false;
    }
    bool suffix_matches = true;
    for (size_t i = 0; i < 3U; ++i) {
        int hi = hex_value(name.bytes[6U + i * 2U]);
        int lo = hex_value(name.bytes[7U + i * 2U]);
        if (hi < 0 || lo < 0) return false;
        suffix_matches &= (uint8_t)((hi << 4) | lo) == observation->bssid[i + 3U];
    }
    candidate(match, SENTINEL_WATCH_FLOCK, suffix_matches ? SENTINEL_WATCH_CONFIDENCE_HIGH :
        SENTINEL_WATCH_CONFIDENCE_MEDIUM, suffix_matches ? "flock.ssid-mac-suffix" : "flock.ssid-format",
        "Flock Wi-Fi hint", suffix_matches ? "Flock- + six hex digits match BSSID suffix" :
        "Flock- + six hex digits / name can be copied");
    return true;
}

const char *sentinel_watch_category_name(sentinel_watch_category_t category)
{
    static const char *const names[] = {"Unknown", "Axon equipment", "Flock ecosystem", "Adult wellness",
        "AirTag / Find My", "Tile / Chipolo / Find Hub", "GoPro camera", "Smart glasses demo",
        "Flipper", "Meshtastic", "Samsung tracker", "Bluetooth beacon", "Pwnagotchi",
        "Drone Remote ID", "Bluetooth activity"};
    return category >= SENTINEL_WATCH_NONE && category < SENTINEL_WATCH_CATEGORY_COUNT ? names[category] : "Unknown";
}

const char *sentinel_watch_confidence_name(sentinel_watch_confidence_t confidence)
{
    static const char *const names[] = {"None", "Low", "Medium", "High"};
    return confidence >= SENTINEL_WATCH_CONFIDENCE_NONE && confidence <= SENTINEL_WATCH_CONFIDENCE_HIGH ? names[confidence] : "None";
}
