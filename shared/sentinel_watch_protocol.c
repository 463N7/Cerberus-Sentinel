#include "sentinel_watch_protocol.h"
#include <string.h>

static uint16_t le16(const uint8_t *p)
{ return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8); }
static int32_t sle32(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return u <= INT32_MAX ? (int32_t)u : -1 - (int32_t)(UINT32_MAX - u);
}
static bool zero(const uint8_t *p, size_t n)
{ for (size_t i = 0; i < n; ++i) if (p[i]) return false; return true; }
static bool text(const uint8_t *p, size_t n, char *out)
{
    size_t used = 0;
    while (used < n && p[used]) {
        if (p[used] < 32U || p[used] > 126U) return false;
        ++used;
    }
    if (!used || !zero(p + used, n - used)) return false;
    if (out) { memcpy(out, p, used); out[used] = 0; }
    return true;
}

/* Wire offsets are read explicitly; no packed casts, bitfield ABI, floats,
 * cryptographic verification or code from a third-party decoder is used. */
static bool drone_message(const uint8_t *p, sentinel_watch_protocol_record_t *r)
{
    if ((p[0] & 15U) > 2U) return false; /* documented versions 0, 1, 2 */
    switch (p[0] >> 4) {
    case 0: { /* Basic ID */
        uint8_t kind = p[1] >> 4;
        if (kind < 1U || kind > 4U || !zero(p + 22U, 3U)) return false;
        if (kind <= 2U) {
            if (!text(p + 2U, 20U, r->id)) return false;
            r->flags |= SENTINEL_WATCH_PROTOCOL_HAS_ID;
        } else if (zero(p + 2U, 20U)) return false;
        return true;
    }
    case 1: { /* Location, geodetic altitude is preferred over barometric. */
        int32_t lat = sle32(p + 5U), lon = sle32(p + 9U);
        uint16_t stamp = le16(p + 21U);
        if ((p[1] >> 4) > 4U || (p[1] & 8U) || p[2] > 181U ||
            lat < -900000000 || lat > 900000000 ||
            lon < -1800000000 || lon > 1800000000 ||
            (p[19] & 15U) > 12U || (p[19] >> 4) > 6U ||
            (p[20] & 15U) > 4U || (p[20] >> 4) > 6U ||
            (stamp > 36000U && stamp != UINT16_MAX) || (p[23] & 0xf0U) || p[24]) return false;
        r->motion.flags = SENTINEL_WATCH_MOTION_LOCATION_PRESENT;
        uint16_t speed = (p[1] & 1U) ? (uint16_t)(255U + 3U * p[3]) : p[3];
        if (speed <= 1017U) {
            r->motion.speed_qms = speed;
            r->motion.flags |= SENTINEL_WATCH_MOTION_SPEED;
        }
        uint16_t direction = (uint16_t)(p[2] + ((p[1] & 2U) ? 180U : 0U));
        if (direction <= 360U) {
            r->motion.direction_deg = direction;
            r->motion.flags |= SENTINEL_WATCH_MOTION_DIRECTION;
        }
        uint16_t height = le16(p + 17U);
        if (height) {
            r->motion.height_encoded = height;
            r->motion.flags |= SENTINEL_WATCH_MOTION_HEIGHT;
            if (p[1] & 4U) r->motion.flags |= SENTINEL_WATCH_MOTION_HEIGHT_AGL;
        }
        if (lat || lon) {
            r->latitude_e7 = lat; r->longitude_e7 = lon;
            r->flags |= SENTINEL_WATCH_PROTOCOL_HAS_POSITION;
        }
        uint16_t alt = le16(p + 15U);
        uint8_t reference = SENTINEL_WATCH_ALTITUDE_GEODETIC;
        if (!alt) { alt = le16(p + 13U); reference = SENTINEL_WATCH_ALTITUDE_PRESSURE; }
        if (alt) {
            r->altitude_dm = (int32_t)alt * 5 - 10000;
            r->altitude_kind = reference;
            r->flags |= SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE;
        }
        return true;
    }
    case 2: /* Authentication is recognizable, but is NEVER verified here. */
        return (p[1] >> 4) <= 5U && ((p[1] & 15U) != 0U ||
            (p[2] < 16U && p[3] != 0U && (unsigned)p[3] <= 17U + 23U * p[2]));
    case 3: return (p[1] <= 2U || p[1] >= 201U) && text(p + 2U, 23U, NULL);
    case 4: { /* System data: validate location, never expose pilot location. */
        int32_t lat = sle32(p + 2U), lon = sle32(p + 6U);
        return !(p[1] & 0xe0U) && (p[1] & 3U) <= 2U &&
            ((p[1] >> 2) & 7U) <= 1U && lat >= -900000000 && lat <= 900000000 &&
            lon >= -1800000000 && lon <= 1800000000 && p[24] == 0U;
    }
    case 5: return (p[1] == 0U || p[1] >= 201U) &&
        text(p + 2U, 20U, NULL) && zero(p + 22U, 3U);
    default: return false;
    }
}
static bool drone_payload(const uint8_t *p, size_t n, sentinel_watch_protocol_record_t *r)
{
    if (n == 25U && (p[0] >> 4) != 15U) return drone_message(p, r);
    if (n < 28U || (p[0] >> 4) != 15U || (p[0] & 15U) > 2U ||
        p[1] != 25U || p[2] < 1U || p[2] > 9U || n != 3U + 25U * p[2]) return false;
    bool location_seen = false;
    for (size_t i = 0; i < p[2]; ++i) {
        const uint8_t *message = p + 3U + i * 25U;
        if ((message[0] & 15U) != (p[0] & 15U)) return false;
        if ((message[0] >> 4) == 1U) {
            if (location_seen) return false;
            location_seen = true;
        }
        if (!drone_message(message, r)) return false;
    }
    return true;
}

/* The caller has matched the six-byte Remote ID service identifier. Instance
 * and requestor identifiers are local NAN fields, not aircraft identities.
 * Optional filters remain opaque and are bounded by their encoded lengths. */
static bool drone_nan_descriptor(const uint8_t *p, size_t n,
    sentinel_watch_protocol_record_t *r)
{
    if (n < 9U) return false;
    uint8_t control = p[8];
    /* Types 0/1/2 are Publish/Subscribe/Follow-up. Bit 5 is range-limited;
     * it adds no bytes. In particular, 0x10 is Publish with service info. */
    if (!(control & 0x10U) || (control & 3U) == 3U || (control & 0x80U)) return false;
    size_t pos = 9U;
    if (control & 0x40U) {
        if (n - pos < 2U) return false;
        pos += 2U;
    }
    for (unsigned flag = 0x04U; flag <= 0x08U; flag <<= 1U) {
        if (!(control & flag)) continue;
        if (pos == n) return false;
        size_t filter_size = p[pos++];
        if (filter_size > n - pos) return false;
        pos += filter_size;
    }
    if (pos == n) return false;
    size_t info_size = p[pos++];
    /* One counter byte followed by a strict MessagePack, with no trailing
     * descriptor bytes or bare single-message Wi-Fi payload accepted. */
    if (info_size != n - pos || info_size < 29U || (p[pos + 1U] >> 4) != 15U) return false;
    return drone_payload(p + pos + 1U, info_size - 1U, r);
}

bool sentinel_watch_protocol_decode_ble(const uint8_t *data, size_t length,
    sentinel_watch_protocol_record_t *record)
{
    if (!record) return false;
    memset(record, 0, sizeof(*record));
    if (!data || !length || length > SENTINEL_WATCH_PROTOCOL_BLE_MAX) return false;
    sentinel_watch_protocol_record_t pending = {0};
    size_t pos = 0;
    bool found = false;
    while (pos < length) {
        size_t n = data[pos++];
        if (!n) { if (!zero(data + pos, length - pos)) return false; break; }
        if (n > length - pos) return false;
        uint8_t type = data[pos++]; --n;
        const uint8_t *p = data + pos;
        if ((type == 1U && n != 1U) ||
            ((type == 2U || type == 3U) && (!n || n % 2U)) ||
            ((type == 4U || type == 5U) && (!n || n % 4U)) ||
            ((type == 6U || type == 7U) && (!n || n % 16U)) ||
            (type == 0x20U && n < 4U) || (type == 0x21U && n < 16U) ||
            (type == 0x16U && n < 2U) || (type == 0xffU && n < 2U)) return false;
        if (type == 0x16U && n >= 2U && le16(p) == 0xfffaU) {
            if (found || n < 5U || p[2] != 0x0dU || !drone_payload(p + 4U, n - 4U, &pending)) return false;
            found = true;
        }
        pos += n;
    }
    if (!found) return false;
    pending.kind = SENTINEL_WATCH_PROTOCOL_DRONE;
    pending.transport = SENTINEL_WATCH_TRANSPORT_BLE;
    *record = pending;
    return true;
}

/* PwnGrid JSON is spread over IE 222 chunks. This streaming reader consumes
 * only those bytes, avoiding a second 1 KiB callback-stack copy. */
typedef struct {
    const uint8_t *frame;
    size_t length, pos, end, count;
} json_stream_t;
static int peek(json_stream_t *s)
{
    while (s->pos == s->end) {
        if (s->pos >= s->length) return -1;
        uint8_t type = s->frame[s->pos++];
        size_t n = s->frame[s->pos++]; /* all IEs prevalidated */
        if (type == 222U) s->end = s->pos + n;
        else { s->pos += n; s->end = s->pos; }
    }
    return s->frame[s->pos];
}
static int take(json_stream_t *s)
{ int c = peek(s); if (c >= 0) { ++s->pos; ++s->count; } return c; }
static void space(json_stream_t *s)
{ int c; while ((c = peek(s)) == ' ' || c == '\n' || c == '\r' || c == '\t') take(s); }
static bool hex(int c)
{ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
/* Escapes are syntax-validated. Semantic keys/identity must be unescaped. */
static bool string(json_stream_t *s, char *out, size_t capacity, size_t *length)
{
    if (take(s) != '"') return false;
    size_t n = 0; bool escaped = false;
    for (;;) {
        int c = take(s);
        if (c < 32) return false;
        if (c == '"') break;
        if (c == '\\') {
            escaped = true; c = take(s);
            if (c == 'u') { for (unsigned i = 0; i < 4; ++i) if (!hex(take(s))) return false; }
            else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r' && c != 't') return false;
            c = '?';
        }
        if (out && n + 1U < capacity) out[n] = c >= 32 && c <= 126 ? (char)c : '?';
        ++n;
    }
    if (out && capacity) out[n < capacity ? n : capacity - 1U] = 0;
    if (length) *length = escaped ? SIZE_MAX : n;
    return true;
}
static bool value(json_stream_t *s, unsigned depth);
static bool container(json_stream_t *s, unsigned depth, bool object)
{
    int closing = object ? '}' : ']';
    take(s); space(s);
    if (peek(s) == closing) { take(s); return true; }
    for (unsigned count = 0; count < 128U; ++count) {
        if (object) {
            if (!string(s, NULL, 0, NULL)) return false;
            space(s); if (take(s) != ':') return false; space(s);
        }
        if (!value(s, depth + 1U)) return false;
        space(s); int c = take(s);
        if (c == closing) return true;
        if (c != ',') return false;
        space(s);
    }
    return false;
}
static bool value(json_stream_t *s, unsigned depth)
{
    if (depth > 6U) return false;
    int c = peek(s);
    if (c == '"') return string(s, NULL, 0, NULL);
    if (c == '{' || c == '[') return container(s, depth, c == '{');
    const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
    if (literal) { while (*literal) if (take(s) != *literal++) return false; return true; }
    if (c == '-') { take(s); c = peek(s); }
    if (c < '0' || c > '9') return false;
    take(s);
    if (c != '0') while ((c = peek(s)) >= '0' && c <= '9') take(s);
    if (peek(s) == '.') {
        take(s); c = peek(s); if (c < '0' || c > '9') return false;
        while ((c = peek(s)) >= '0' && c <= '9') take(s);
    }
    c = peek(s);
    if (c == 'e' || c == 'E') {
        take(s); c = peek(s); if (c == '+' || c == '-') take(s);
        c = peek(s); if (c < '0' || c > '9') return false;
        while ((c = peek(s)) >= '0' && c <= '9') take(s);
    }
    return true;
}
static bool pwn_json(const uint8_t *frame, size_t length, sentinel_watch_protocol_record_t *r)
{
    json_stream_t s = {frame, length, 36U, 36U, 0};
    unsigned seen = 0;
    space(&s); if (take(&s) != '{') return false; space(&s);
    for (unsigned fields = 0; fields < 128U; ++fields) {
        char key[16]; size_t n;
        if (!string(&s, key, sizeof(key), &n)) return false;
        space(&s); if (take(&s) != ':') return false; space(&s);
        unsigned bit = n == 4U && !strcmp(key, "name") ? 1U :
            n == 8U && !strcmp(key, "identity") ? 2U :
            n == 12U && !strcmp(key, "grid_version") ? 4U : 0U;
        if (bit) {
            char buffer[65]; size_t size;
            if (seen & bit || !string(&s, buffer, sizeof(buffer), &size) || !size || size == SIZE_MAX) return false;
            if (bit == 2U) {
                if (size != 64U) return false;
                for (size_t i = 0; i < size; ++i) if (!hex((unsigned char)buffer[i])) return false;
            } else if (bit == 1U) {
                if (size > 64U) return false;
                size_t copy = size < 20U ? size : 20U;
                memcpy(r->id, buffer, copy); r->id[copy] = 0;
            } else {
                if (size > 24U || buffer[0] < '0' || buffer[0] > '9') return false;
            }
            seen |= bit;
        } else if (!value(&s, 0U)) return false;
        space(&s); int c = take(&s);
        if (c == '}') { space(&s); return seen == 7U && peek(&s) < 0; }
        if (c != ',') return false;
        space(&s);
    }
    return false;
}

bool sentinel_watch_protocol_decode_wifi(const uint8_t *data, size_t length,
    sentinel_watch_protocol_record_t *record)
{
    if (!record) return false;
    memset(record, 0, sizeof(*record));
    if (!data || length < 24U || length > SENTINEL_WATCH_PROTOCOL_WIFI_MAX ||
        (data[0] & 15U) || (data[1] & 0xc7U) || (data[22] & 15U)) return false;
    sentinel_watch_protocol_record_t pending = {0};
    bool found = false;
    if (data[0] == 0x80U) {
        if (length < 36U) return false;
        static const uint8_t pwn_source[6] = {0xde,0xad,0xbe,0xef,0xde,0xad};
        bool pwn = memcmp(data + 10U, pwn_source, 6U) == 0;
        size_t json_size = 0;
        bool unsupported_pwn = false;
        for (size_t pos = 36U; pos < length;) {
            if (length - pos < 2U) return false;
            uint8_t type = data[pos++]; size_t n = data[pos++];
            if (n > length - pos) return false;
            const uint8_t *p = data + pos;
            if (type == 0U && n > 32U) return false;
            if (type == 221U && n >= 4U && p[3] == 0x0dU &&
                ((p[0] == 0xfaU && p[1] == 0x0bU && p[2] == 0xbcU) ||
                 (p[0] == 0x90U && p[1] == 0x3aU && p[2] == 0xe6U))) {
                if (found || n < 6U || (p[5] >> 4) != 15U || !drone_payload(p + 5U, n - 5U, &pending)) return false;
                found = true;
            }
            if (type == 222U) json_size += n;
            if (type == 223U || type == 226U) unsupported_pwn = true;
            pos += n;
        }
        if (found) { pending.kind = SENTINEL_WATCH_PROTOCOL_DRONE; }
        else if (pwn && !unsupported_pwn && json_size && json_size <= 1536U && pwn_json(data, length, &pending)) {
            pending.kind = SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;
            pending.flags = SENTINEL_WATCH_PROTOCOL_HAS_ID;
            found = true;
        }
        pending.transport = SENTINEL_WATCH_TRANSPORT_WIFI_BEACON;
    } else if (data[0] == 0xd0U) {
        static const uint8_t nan_destination[6] = {0x51,0x6f,0x9a,1,0,0};
        static const uint8_t nan_header[6] = {4,9,0x50,0x6f,0x9a,0x13};
        static const uint8_t service_id[6] = {0x88,0x69,0x19,0x9d,0x92,9};
        if (length < 30U || memcmp(data + 4U, nan_destination, 6U) ||
            memcmp(data + 24U, nan_header, 6U)) return false;
        for (size_t pos = 30U; pos < length;) {
            if (length - pos < 3U) return false;
            uint8_t type = data[pos++]; size_t n = le16(data + pos); pos += 2U;
            if (n > length - pos) return false;
            const uint8_t *p = data + pos;
            if (type == 3U && n >= 6U && !memcmp(p, service_id, 6U)) {
                if (found || !drone_nan_descriptor(p, n, &pending)) return false;
                found = true;
            }
            pos += n;
        }
        pending.kind = SENTINEL_WATCH_PROTOCOL_DRONE;
        pending.transport = SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    }
    if (!found) return false;
    *record = pending;
    return true;
}
