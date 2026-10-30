#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RECON_DB_SCRATCH 4096
#define RECON_DB_VENDOR_SIZE 48
typedef bool (*recon_db_read_t)(void *context, uint32_t offset, void *data, size_t size, uint64_t *file_size);
typedef enum { RECON_DB_FOUND, RECON_DB_UNKNOWN, RECON_DB_PRIVATE_ADDRESS, RECON_DB_UNAVAILABLE, RECON_DB_INVALID } recon_db_result_t;
/* NimBLE stores BLE addresses least-significant byte first. Only public
 * addresses (types 0 and 2) have a registry OUI; random addresses must not
 * be attributed, even if their first printed bytes match a vendor prefix. */
bool recon_db_ble_public_mac(const uint8_t ble_address[6], uint8_t address_type,
    uint8_t mac[6]);
/* Caller serializes access and supplies scratch; no heap, open files or global
 * state. No BLE random-address attribution: callers must also check address type. */
recon_db_result_t recon_db_lookup(recon_db_read_t read, void *context, const uint8_t mac[6],
    char vendor[RECON_DB_VENDOR_SIZE], uint8_t scratch[RECON_DB_SCRATCH]);
