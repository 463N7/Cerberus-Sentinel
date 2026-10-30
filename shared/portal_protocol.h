#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_PORTAL_FORM_FIELDS 4U
#define CERBERUS_PORTAL_FIELD_NAME 24U
#define CERBERUS_PORTAL_FIELD_VALUE 96U

typedef struct {
    char name[CERBERUS_PORTAL_FIELD_NAME];
    char value[CERBERUS_PORTAL_FIELD_VALUE];
} cerberus_portal_form_field_t;

/* No heap or ESP-IDF dependencies. Zero means malformed, truncated, or
 * unsupported. A valid non-A DNS question receives an empty answer. */
size_t cerberus_portal_dns_reply(const uint8_t *query, size_t query_size,
    const uint8_t address[4], uint8_t *reply, size_t reply_capacity);

/* Decode one bounded application/x-www-form-urlencoded submission. */
bool cerberus_portal_form_decode(const char *body, size_t length,
    cerberus_portal_form_field_t fields[CERBERUS_PORTAL_FORM_FIELDS],
    uint8_t *field_count);

/* Escape printable text for a limited HTML title, replacing control bytes. */
size_t cerberus_portal_html_escape(const uint8_t *source, size_t length,
    char *destination, size_t capacity);
