#pragma once
#include "recon_db.h"
#include "esp_err.h"
/* Initialize before starting UI/field workers; lookup serializes its cache. */
esp_err_t recon_store_init(void);
/* The bounded cache retains only validated FOUND/UNKNOWN results. Each lookup
 * binds its database header and firmware SD mount epoch; transient failures
 * are never cached as a miss. Random/local identities require no SD lookup. */
recon_db_result_t recon_store_vendor(const uint8_t mac[6], char vendor[RECON_DB_VENDOR_SIZE]);
/* BLE scanner addresses use NimBLE byte order; random types have no OUI. */
recon_db_result_t recon_store_ble_vendor(const uint8_t ble_address[6],
    uint8_t address_type, char vendor[RECON_DB_VENDOR_SIZE]);
