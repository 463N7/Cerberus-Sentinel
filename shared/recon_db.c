#include "recon_db.h"
#include <string.h>
bool recon_db_ble_public_mac(const uint8_t ble_address[6], uint8_t address_type,
    uint8_t mac[6])
{
    if (!ble_address || !mac || (address_type != 0U && address_type != 2U)) return false;
    for (unsigned i = 0; i < 6U; ++i) mac[i] = ble_address[5U - i];
    return true;
}
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint32_t crc_bytes(uint32_t crc, const uint8_t *p, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}
recon_db_result_t recon_db_lookup(recon_db_read_t read, void *context, const uint8_t mac[6],
    char vendor[RECON_DB_VENDOR_SIZE], uint8_t scratch[RECON_DB_SCRATCH])
{
    if (!vendor) return RECON_DB_INVALID;
    vendor[0] = 0;
    if (!read || !mac || !scratch) return RECON_DB_INVALID;
    if (mac[0] & 3U) return RECON_DB_PRIVATE_ADDRESS;
    uint8_t header[40], again[40], index[16]; uint64_t size;
    if (!read(context, 0, header, sizeof(header), &size)) return RECON_DB_UNAVAILABLE;
    uint32_t count = u32(header + 12), blob = u32(header + 20), meta = u32(header + 24);
    if (memcmp(header, "CERBREC1", 8) || u32(header + 8) != 1 || u32(header + 16) != 40 ||
        count > 1000000 || blob != 40 + count * 16 || meta < blob ||
        u32(header + 28) != size || size > 32U * 1024 * 1024 || meta > size || u32(header + 36)) return RECON_DB_INVALID;
    uint32_t key = (uint32_t)mac[0]<<16 | (uint32_t)mac[1]<<8 | mac[2];
    uint32_t low = 0, high = count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        if (!read(context, 40 + middle * 16, index, 16, &size)) return RECON_DB_UNAVAILABLE;
        uint32_t found = u32(index);
        if (found < key) { low = middle + 1; continue; }
        if (found > key) { high = middle; continue; }
        uint32_t offset = u32(index + 4), length = u32(index + 8);
        if (length < 2 || length > RECON_DB_SCRATCH || offset < blob || offset > meta || length > meta - offset) return RECON_DB_INVALID;
        if (!read(context, offset, scratch, length, &size) || !read(context, 0, again, 40, &size)) return RECON_DB_UNAVAILABLE;
        if (memcmp(header, again, 40) || size != u32(header + 28)) return RECON_DB_INVALID;
        uint32_t crc = crc_bytes(0xFFFFFFFFU, index, 4);
        crc = crc_bytes(crc, scratch, length) ^ 0xFFFFFFFFU;
        if (crc != u32(index + 12) || !scratch[0] || scratch[0] + 1U >= length) return RECON_DB_INVALID;
        unsigned n = scratch[0] < RECON_DB_VENDOR_SIZE - 1 ? scratch[0] : RECON_DB_VENDOR_SIZE - 1;
        for (unsigned i = 0; i < n; ++i) {
            uint8_t c = scratch[i + 1]; vendor[i] = c >= 32 && c <= 126 ? (char)c : '?';
        }
        vendor[n] = 0;
        return RECON_DB_FOUND;
    }
    /* A miss is definitive only for the same database used to search the
     * index. USB handover or an in-place replacement can otherwise turn a
     * mixed-generation binary search into a cacheable false negative. */
    if (!read(context, 0, again, sizeof(again), &size)) return RECON_DB_UNAVAILABLE;
    if (memcmp(header, again, sizeof(header)) || size != u32(header + 28))
        return RECON_DB_INVALID;
    return RECON_DB_UNKNOWN;
}
