#include "network_scan.h"
#include <string.h>

static uint32_t address(const uint8_t ip[4])
{
    return (uint32_t)ip[0] << 24 | (uint32_t)ip[1] << 16 | (uint32_t)ip[2] << 8 | ip[3];
}
static bool number(const char **text, uint32_t maximum, uint32_t *out)
{
    const char *p = *text;
    uint32_t n = 0;
    if (*p < '0' || *p > '9') return false;
    do {
        n = n * 10U + (unsigned)(*p++ - '0');
        if (n > maximum) return false;
    } while (*p >= '0' && *p <= '9');
    *text = p; *out = n;
    return true;
}
bool cerberus_net_parse_ipv4(const char *text, uint8_t out[4])
{
    if (!text || !out) return false;
    uint8_t parsed[4];
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t n;
        if (!number(&text, 255, &n)) return false;
        parsed[i] = (uint8_t)n;
        if (i < 3 && *text++ != '.') return false;
    }
    if (*text) return false;
    memcpy(out, parsed, 4);
    return true;
}
bool cerberus_net_parse_ports(const char *text, uint16_t *first, uint16_t *last)
{
    if (!text || !first || !last) return false;
    uint32_t a, b;
    if (!number(&text, 65535, &a) || !a) return false;
    b = a;
    if (*text == '-') {
        ++text;
        if (!number(&text, 65535, &b) || b < a) return false;
    }
    if (*text) return false;
    *first = (uint16_t)a; *last = (uint16_t)b;
    return true;
}
static const uint16_t common_ports[] = {
    21, 22, 23, 25, 53, 80, 110, 135, 139, 143, 443, 445, 554, 3389, 8080, 8443
};
uint16_t cerberus_net_scan_service_port(uint8_t mode)
{
    static const uint16_t ports[] = {22,23,25,53,80,443,3389};
    return mode >= CERBERUS_NET_SCAN_SSH_LAN && mode <= CERBERUS_NET_SCAN_RDP_LAN ?
        ports[mode - CERBERUS_NET_SCAN_SSH_LAN] : 0;
}
bool cerberus_net_scan_plan(cerberus_net_scan_plan_t *plan,
    const cerberus_net_scan_request_t *r, const uint8_t own[4], const uint8_t mask[4])
{
    if (!plan || !r || !own || !mask || !r->job_id || r->timeout_ms < 100 ||
        r->timeout_ms > 2000 || r->mode >= CERBERUS_NET_SCAN_MODE_COUNT) return false;
    uint32_t ip = address(own), m = address(mask), host_mask = ~m;
    if (!m || (host_mask & (host_mask + 1U)) || !own[0] || own[0] == 127 || own[0] >= 224) return false;
    uint32_t network = ip & m, broadcast = network | host_mask;
    cerberus_net_scan_plan_t next = {.request = *r, .own_host = ip};
    if (r->mode >= CERBERUS_NET_SCAN_SSH_LAN) {
        /* Limit a LAN sweep to an explicitly supported /24.. /31 network;
         * never silently truncate a larger network or cross a subnet. */
        if (host_mask < 1 || host_mask > 255) return false;
        next.first_host = network + (host_mask > 1 ? 1U : 0U);
        next.total = (uint16_t)(host_mask > 1 ? host_mask - 2U : 1U);
        if (ip < next.first_host || ip > broadcast - (host_mask > 1 ? 1U : 0U)) return false;
    } else {
        uint32_t target = address(r->target);
        if (!r->target[0] || r->target[0] == 127 || r->target[0] >= 224 ||
            (target & m) != network || target == ip ||
            (host_mask > 1 && (target == network || target == broadcast))) return false;
        if (r->mode == CERBERUS_NET_SCAN_RANGE) {
            if (!r->first_port || r->last_port < r->first_port) return false;
            next.total = (uint16_t)((uint32_t)r->last_port - r->first_port + 1U);
        } else next.total = sizeof(common_ports) / sizeof(common_ports[0]);
    }
    *plan = next;
    return true;
}
bool cerberus_net_scan_probe(const cerberus_net_scan_plan_t *p, uint16_t index,
    uint8_t target[4], uint16_t *port)
{
    if (!p || !target || !port || index >= p->total) return false;
    if (p->request.mode >= CERBERUS_NET_SCAN_SSH_LAN) {
        uint32_t ip = p->first_host + index;
        if (ip >= p->own_host) ++ip;
        target[0] = (uint8_t)(ip >> 24); target[1] = (uint8_t)(ip >> 16);
        target[2] = (uint8_t)(ip >> 8); target[3] = (uint8_t)ip;
        *port = cerberus_net_scan_service_port(p->request.mode);
        if (!*port && p->request.mode != CERBERUS_NET_SCAN_PING_LAN &&
            p->request.mode != CERBERUS_NET_SCAN_ARP_LAN) return false;
    } else {
        memcpy(target, p->request.target, 4);
        *port = p->request.mode == CERBERUS_NET_SCAN_COMMON ? common_ports[index] :
            (uint16_t)(p->request.first_port + index);
    }
    return true;
}

static uint16_t read_be16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
bool cerberus_net_scan_result_valid(const cerberus_net_scan_result_t *r, uint16_t job_id, uint8_t index)
{
    if (!r || !job_id || r->job_id != job_id || r->index != index || index >= CERBERUS_NET_SCAN_MAX_RESULTS ||
        !r->ipv4[0] || r->ipv4[0] == 127 || r->ipv4[0] >= 224) return false;
    if (!(r->flags & ~(CERBERUS_NET_SCAN_SSH_BANNER | CERBERUS_NET_SCAN_BANNER_TRUNCATED))) return r->port != 0;
    if (r->port) return false;
    const uint8_t empty[10] = {0};
    if (r->flags == CERBERUS_NET_SCAN_ICMP_REPLY) return !memcmp(r->banner, empty, 10);
    if (r->flags != CERBERUS_NET_SCAN_ARP_RESOLVED &&
        r->flags != (CERBERUS_NET_SCAN_ARP_RESOLVED | CERBERUS_NET_SCAN_ARP_CACHED)) return false;
    return !((uint8_t)r->banner[0] & 1U) && memcmp(r->banner, empty, 6) && !memcmp(r->banner + 6, empty, 4);
}
static void write_be16(uint8_t *p, uint16_t n) { p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n; }
static uint16_t checksum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    while (n > 1) { sum += read_be16(p); p += 2; n -= 2; }
    if (n) sum += (uint16_t)*p << 8;
    while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
    return (uint16_t)~sum;
}
void cerberus_net_icmp_request(uint8_t out[CERBERUS_NET_ICMP_BYTES],
    uint16_t id, uint16_t sequence, uint32_t nonce)
{
    memset(out, 0, CERBERUS_NET_ICMP_BYTES);
    out[0] = 8;
    write_be16(out + 4, id); write_be16(out + 6, sequence);
    memcpy(out + 8, "CRBR", 4);
    out[12] = (uint8_t)(nonce >> 24); out[13] = (uint8_t)(nonce >> 16);
    out[14] = (uint8_t)(nonce >> 8); out[15] = (uint8_t)nonce;
    write_be16(out + 2, checksum(out, CERBERUS_NET_ICMP_BYTES));
}
bool cerberus_net_icmp_reply(const uint8_t *p, size_t length,
    const uint8_t source[4], const uint8_t destination[4],
    uint16_t id, uint16_t sequence, uint32_t nonce)
{
    if (!p || !source || !destination || length < 20 || (p[0] >> 4) != 4) return false;
    size_t header = (p[0] & 15U) * 4U;
    if (header < 20 || header > length || read_be16(p + 2) != header + CERBERUS_NET_ICMP_BYTES ||
        length < header + CERBERUS_NET_ICMP_BYTES || p[9] != 1 ||
        (read_be16(p + 6) & 0xbfffU) || checksum(p, header) ||
        memcmp(p + 12, source, 4) || memcmp(p + 16, destination, 4)) return false;
    const uint8_t *echo = p + header;
    if (echo[0] || echo[1] || checksum(echo, CERBERUS_NET_ICMP_BYTES)) return false;
    uint8_t expected[CERBERUS_NET_ICMP_BYTES];
    cerberus_net_icmp_request(expected, id, sequence, nonce);
    return !memcmp(echo + 4, expected + 4, CERBERUS_NET_ICMP_BYTES - 4);
}
