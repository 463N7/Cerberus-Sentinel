#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_NET_SCAN_MAX_RESULTS 64U
#define CERBERUS_NET_SCAN_BANNER_BYTES 10U
enum { CERBERUS_NET_SCAN_COMMON, CERBERUS_NET_SCAN_RANGE,
       CERBERUS_NET_SCAN_SSH_LAN, CERBERUS_NET_SCAN_TELNET_LAN,
       CERBERUS_NET_SCAN_SMTP_LAN, CERBERUS_NET_SCAN_DNS_LAN,
       CERBERUS_NET_SCAN_HTTP_LAN, CERBERUS_NET_SCAN_HTTPS_LAN,
       CERBERUS_NET_SCAN_RDP_LAN, CERBERUS_NET_SCAN_PING_LAN,
       CERBERUS_NET_SCAN_ARP_LAN, CERBERUS_NET_SCAN_MODE_COUNT };
enum { CERBERUS_NET_SCAN_IDLE, CERBERUS_NET_SCAN_RUNNING, CERBERUS_NET_SCAN_STOPPING,
       CERBERUS_NET_SCAN_COMPLETE, CERBERUS_NET_SCAN_CANCELLED, CERBERUS_NET_SCAN_ERROR };
enum { CERBERUS_NET_SCAN_SSH_BANNER = 1U, CERBERUS_NET_SCAN_BANNER_TRUNCATED = 2U,
       CERBERUS_NET_SCAN_ICMP_REPLY = 4U, CERBERUS_NET_SCAN_ARP_RESOLVED = 8U,
       CERBERUS_NET_SCAN_ARP_CACHED = 16U };

typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t connection_generation, connection_index;
    uint8_t target[4];
    uint16_t first_port, last_port, timeout_ms;
    uint8_t mode;
} cerberus_net_scan_request_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t state, result_count;
    uint16_t tested, total, open, closed, filtered;
    int32_t error_code;
    uint32_t elapsed_ms;
} cerberus_net_scan_status_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t index, flags;
    uint8_t ipv4[4];
    uint16_t port, connect_ms;
    /* ARP results use the first six bytes for the resolved MAC; remaining bytes
     * are zero. TCP results carry a sanitized banner. Flags distinguish them. */
    char banner[CERBERUS_NET_SCAN_BANNER_BYTES];
} cerberus_net_scan_result_t;
typedef struct __attribute__((packed)) { uint16_t job_id; uint8_t index; } cerberus_net_scan_query_t;

typedef struct {
    cerberus_net_scan_request_t request;
    uint32_t first_host, own_host;
    uint16_t total;
} cerberus_net_scan_plan_t;
bool cerberus_net_parse_ipv4(const char *text, uint8_t out[4]);
bool cerberus_net_parse_ports(const char *text, uint16_t *first, uint16_t *last);
/* Service discovery tests TCP reachability, including DNS-over-TCP. */
uint16_t cerberus_net_scan_service_port(uint8_t mode);
bool cerberus_net_scan_plan(cerberus_net_scan_plan_t *plan,
    const cerberus_net_scan_request_t *request, const uint8_t own[4], const uint8_t mask[4]);
bool cerberus_net_scan_probe(const cerberus_net_scan_plan_t *plan, uint16_t index,
    uint8_t target[4], uint16_t *port);
/* Validate a decoded SPI result, including protocol-specific port/payload rules. */
bool cerberus_net_scan_result_valid(const cerberus_net_scan_result_t *result,
    uint16_t job_id, uint8_t index);

/* A small ICMP echo payload with a per-job nonce. Helpers accept byte arrays,
 * including unaligned/truncated packets, without casting wire data to structs. */
#define CERBERUS_NET_ICMP_BYTES 16U
void cerberus_net_icmp_request(uint8_t out[CERBERUS_NET_ICMP_BYTES],
    uint16_t id, uint16_t sequence, uint32_t nonce);
bool cerberus_net_icmp_reply(const uint8_t *ipv4_packet, size_t length,
    const uint8_t source[4], const uint8_t destination[4],
    uint16_t id, uint16_t sequence, uint32_t nonce);
