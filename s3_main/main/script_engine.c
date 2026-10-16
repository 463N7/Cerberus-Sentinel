#include "script_engine.h"

#include "storage.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint8_t script_buffer[CERBERUS_SCRIPT_MAX_FILE_BYTES + 1U];

static void set_error(cerberus_script_error_t *error, uint16_t line, const char *message)
{
    if (error == NULL) {
        return;
    }
    error->line = line;
    snprintf(error->message, sizeof(error->message), "%s", message);
}

static char *trim(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

static bool text_is_safe(const char *text)
{
    if (text == NULL || *text == '\0') {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor != '\0'; ++cursor) {
        if (*cursor < 32U || *cursor > 126U) {
            return false;
        }
    }
    return true;
}

static bool parse_uint32(const char *text, uint32_t minimum, uint32_t maximum, uint32_t *value)
{
    if (text == NULL || *text == '\0' || value == NULL) {
        return false;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *trim(end) != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

bool cerberus_script_file_name_is_valid(const char *file_name)
{
    if (file_name == NULL) {
        return false;
    }
    size_t length = strlen(file_name);
    size_t extension_length = strlen(CERBERUS_SCRIPT_EXTENSION);
    return length > extension_length &&
        strcasecmp(file_name + length - extension_length, CERBERUS_SCRIPT_EXTENSION) == 0;
}

static esp_err_t append_action(
    cerberus_script_t *script,
    cerberus_script_error_t *error,
    uint16_t line,
    cerberus_script_action_type_t type,
    uint32_t value,
    const char *text)
{
    if (script->action_count >= CERBERUS_SCRIPT_MAX_ACTIONS) {
        set_error(error, line, "too many actions");
        return ESP_ERR_INVALID_SIZE;
    }
    cerberus_script_action_t *action = &script->actions[script->action_count++];
    action->type = type;
    action->value = value;
    if (text != NULL) {
        size_t length = strlen(text);
        if (length > CERBERUS_SCRIPT_TEXT_MAX || !text_is_safe(text)) {
            set_error(error, line, "text is empty, too long, or unsafe");
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(action->text, text, length + 1U);
    }
    return ESP_OK;
}

esp_err_t cerberus_script_load(
    const char *file_name,
    cerberus_script_t *script,
    cerberus_script_error_t *error)
{
    if (!cerberus_script_file_name_is_valid(file_name) || script == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(script, 0, sizeof(*script));
    if (error != NULL) {
        memset(error, 0, sizeof(*error));
    }

    size_t bytes_read = 0U;
    uint64_t file_size = 0U;
    esp_err_t err = cerberus_storage_read_file_chunk(
        CERBERUS_STORAGE_SCRIPTS_PATH,
        file_name,
        0U,
        script_buffer,
        CERBERUS_SCRIPT_MAX_FILE_BYTES,
        &bytes_read,
        &file_size);
    if (err != ESP_OK) {
        set_error(error, 0U, "cannot read script file");
        return err;
    }
    if (file_size == 0U || file_size > CERBERUS_SCRIPT_MAX_FILE_BYTES || bytes_read != file_size) {
        set_error(error, 0U, "script must be 1-2048 bytes");
        return ESP_ERR_INVALID_SIZE;
    }
    script_buffer[bytes_read] = '\0';
    snprintf(script->name, sizeof(script->name), "%.63s", file_name);

    bool header_seen = false;
    bool end_seen = false;
    uint16_t line_number = 0U;
    char *context = NULL;
    for (char *raw = strtok_r((char *)script_buffer, "\n", &context);
        raw != NULL;
        raw = strtok_r(NULL, "\n", &context)) {
        ++line_number;
        char *line = trim(raw);
        if (*line == '\0' || *line == '#') {
            continue;
        }
        if (!header_seen) {
            if (strcmp(line, "CERBERUS-SCRIPT 1") != 0) {
                set_error(error, line_number, "expected CERBERUS-SCRIPT 1 header");
                return ESP_ERR_NOT_SUPPORTED;
            }
            header_seen = true;
            continue;
        }
        if (end_seen) {
            set_error(error, line_number, "content after END");
            return ESP_ERR_INVALID_ARG;
        }
        if (strcmp(line, "END") == 0) {
            end_seen = true;
            continue;
        }

        char *argument = strchr(line, ' ');
        if (argument != NULL) {
            *argument++ = '\0';
            argument = trim(argument);
        }
        if (strcmp(line, "TEXT") == 0 || strcmp(line, "DIALOG") == 0 ||
            strcmp(line, "QR") == 0) {
            cerberus_script_action_type_t type = strcmp(line, "TEXT") == 0
                ? CERBERUS_SCRIPT_ACTION_TEXT
                : (strcmp(line, "DIALOG") == 0
                    ? CERBERUS_SCRIPT_ACTION_DIALOG
                    : CERBERUS_SCRIPT_ACTION_QR);
            err = append_action(script, error, line_number, type, 0U, argument);
        } else if (strcmp(line, "WAIT") == 0 || strcmp(line, "TIMER") == 0) {
            uint32_t value = 0U;
            uint32_t maximum = strcmp(line, "WAIT") == 0
                ? CERBERUS_SCRIPT_MAX_WAIT_MS
                : 86400U;
            if (!parse_uint32(argument, 1U, maximum, &value)) {
                set_error(error, line_number, strcmp(line, "WAIT") == 0
                    ? "WAIT requires 1-5000 milliseconds"
                    : "TIMER requires 1-86400 seconds");
                return ESP_ERR_INVALID_ARG;
            }
            if (strcmp(line, "WAIT") == 0) {
                if (script->total_wait_ms > CERBERUS_SCRIPT_MAX_TOTAL_WAIT_MS - value) {
                    set_error(error, line_number, "total WAIT limit is 30000 ms");
                    return ESP_ERR_INVALID_SIZE;
                }
                script->total_wait_ms += value;
            }
            err = append_action(script, error, line_number,
                strcmp(line, "WAIT") == 0 ? CERBERUS_SCRIPT_ACTION_WAIT
                                           : CERBERUS_SCRIPT_ACTION_TIMER,
                value, NULL);
        } else {
            set_error(error, line_number, "unknown or forbidden command");
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (err != ESP_OK) {
            return err;
        }
    }

    if (!header_seen || !end_seen || script->action_count == 0U) {
        set_error(error, line_number, "script needs header, actions, and END");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
