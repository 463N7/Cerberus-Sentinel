#include "portal_protocol.h"

#include <string.h>

static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

size_t cerberus_portal_dns_reply(const uint8_t *query, size_t query_size,
    const uint8_t address[4], uint8_t *reply, size_t reply_capacity)
{
    if (!query || !address || !reply || query_size < 17U || query_size > 512U ||
        reply_capacity < 17U || read_be16(query + 4U) != 1U ||
        (query[2] & 0xFEU) != 0U || query[3] != 0U) return 0U;
    size_t cursor = 12U;
    unsigned labels = 0U;
    while (cursor < query_size) {
        uint8_t part = query[cursor++];
        if (part == 0U) break;
        if (part > 63U || ++labels > 32U || cursor + part > query_size) return 0U;
        cursor += part;
    }
    if (cursor <= 13U || cursor + 4U > query_size) return 0U;
    uint16_t type = read_be16(query + cursor);
    uint16_t question_class = read_be16(query + cursor + 2U);
    if (question_class != 1U || (type != 1U && type != 28U)) return 0U;
    size_t question_end = cursor + 4U;
    bool answer = type == 1U;
    size_t output_size = question_end + (answer ? 16U : 0U);
    if (output_size > reply_capacity) return 0U;
    memcpy(reply, query, question_end);
    reply[2] = 0x80U | (query[2] & 0x01U); /* response, retain RD */
    reply[3] = 0x80U; /* recursion available locally for captive A answers */
    write_be16(reply + 4U, 1U);
    write_be16(reply + 6U, answer ? 1U : 0U);
    write_be16(reply + 8U, 0U);
    write_be16(reply + 10U, 0U);
    if (answer) {
        uint8_t *record = reply + question_end;
        record[0] = 0xC0U; record[1] = 0x0CU;
        write_be16(record + 2U, 1U);
        write_be16(record + 4U, 1U);
        memset(record + 6U, 0, 4U); /* TTL 0: do not cache the portal redirect */
        write_be16(record + 10U, 4U);
        memcpy(record + 12U, address, 4U);
    }
    return output_size;
}

static int hex_digit(char character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    return -1;
}

static bool decode_component(const char *source, size_t length, char *destination,
    size_t capacity, bool name)
{
    if (!source || !destination || capacity == 0U || length == 0U) return false;
    size_t written = 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char byte = (unsigned char)source[index];
        if (byte == '%') {
            if (index + 2U >= length) return false;
            int high = hex_digit(source[++index]);
            int low = hex_digit(source[++index]);
            if (high < 0 || low < 0) return false;
            byte = (unsigned char)((high << 4) | low);
        } else if (byte == '+') byte = ' ';
        if (byte < 0x20U || byte > 0x7EU || written + 1U >= capacity) return false;
        if (name && !((byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
            byte == '_' || byte == '-')) return false;
        destination[written++] = (char)byte;
    }
    destination[written] = '\0';
    return true;
}

bool cerberus_portal_form_decode(const char *body, size_t length,
    cerberus_portal_form_field_t fields[CERBERUS_PORTAL_FORM_FIELDS],
    uint8_t *field_count)
{
    if (!body || !fields || !field_count || length == 0U || length > 512U) return false;
    memset(fields, 0, sizeof(*fields) * CERBERUS_PORTAL_FORM_FIELDS);
    *field_count = 0U;
    size_t cursor = 0U;
    while (cursor < length) {
        if (*field_count >= CERBERUS_PORTAL_FORM_FIELDS) return false;
        size_t end = cursor;
        while (end < length && body[end] != '&') ++end;
        size_t equals = cursor;
        while (equals < end && body[equals] != '=') ++equals;
        if (equals == end || equals == cursor || equals + 1U == end ||
            !decode_component(body + cursor, equals - cursor,
                fields[*field_count].name, sizeof(fields[*field_count].name), true) ||
            !decode_component(body + equals + 1U, end - equals - 1U,
                fields[*field_count].value, sizeof(fields[*field_count].value), false)) return false;
        ++*field_count;
        cursor = end + 1U;
        if (end < length && cursor == length) return false;
    }
    return *field_count > 0U;
}

size_t cerberus_portal_html_escape(const uint8_t *source, size_t length,
    char *destination, size_t capacity)
{
    if ((!source && length) || !destination || capacity == 0U) return 0U;
    size_t written = 0U;
    for (size_t index = 0U; index < length; ++index) {
        unsigned char byte = source[index];
        const char *escaped = NULL;
        if (byte == '&') escaped = "&amp;";
        else if (byte == '<') escaped = "&lt;";
        else if (byte == '>') escaped = "&gt;";
        else if (byte == '"') escaped = "&quot;";
        else if (byte == '\'') escaped = "&#39;";
        size_t add = escaped ? strlen(escaped) : 1U;
        if (written + add >= capacity) return 0U;
        if (escaped) memcpy(destination + written, escaped, add);
        else destination[written] = byte >= 0x20U && byte <= 0x7EU ? (char)byte : '?';
        written += add;
    }
    destination[written] = '\0';
    return written;
}
