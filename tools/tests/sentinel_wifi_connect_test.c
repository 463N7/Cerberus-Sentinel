/* Test the actual S3 multi-command connect function. Only the serialized SPI
 * exchange and FreeRTOS semaphore boundary are simulated. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c5_spi_link.h"

#define pdTRUE 1
#define pdMS_TO_TICKS(value) (value)
static int mutex_storage;
static void *link_mutex = &mutex_storage;
static bool mutex_held, lock_available;
static unsigned lock_takes, lock_gives;
static cerberus_wifi_connection_status_payload_t radio;
static esp_err_t begin_error, password_error, commit_error, status_error, disconnect_error;
static unsigned fail_password_chunk;
static int status_state_override, status_generation_override, status_index_override;
static int commit_delivery_state;
static unsigned begin_calls, password_calls, commit_calls, status_calls, disconnect_calls;
static char assembled_password[CERBERUS_WIFI_PASSWORD_MAX_LENGTH + 1U];
static size_t assembled_length;

static int xSemaphoreTake(void *mutex, unsigned timeout)
{
    assert(mutex == link_mutex && timeout == 500U && !mutex_held);
    ++lock_takes;
    if (!lock_available) { return 0; }
    mutex_held = true;
    return pdTRUE;
}

static void xSemaphoreGive(void *mutex)
{
    assert(mutex == link_mutex && mutex_held);
    mutex_held = false;
    ++lock_gives;
}

static esp_err_t exchange_v2_locked(cerberus_spi_command_t command, const void *request,
                                  size_t request_length, void *response, size_t response_length)
{
    /* Cleanup must remain in the same mutex reservation as BEGIN/password. */
    assert(mutex_held && response != NULL && response_length == sizeof(radio));
    esp_err_t err = ESP_OK;
    switch (command) {
    case CERBERUS_SPI_COMMAND_WIFI_CONNECT_BEGIN: {
        ++begin_calls;
        assert(request != NULL && request_length == sizeof(cerberus_wifi_connect_begin_request_t));
        const cerberus_wifi_connect_begin_request_t *begin = request;
        err = begin_error;
        if (err == ESP_OK) {
            radio.state = CERBERUS_WIFI_CONNECTION_STAGING;
            radio.generation = begin->generation;
            radio.index = begin->index;
            assembled_length = 0U;
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CONNECT_PASSWORD: {
        ++password_calls;
        assert(request != NULL && request_length == sizeof(cerberus_wifi_connect_password_request_t));
        const cerberus_wifi_connect_password_request_t *chunk = request;
        assert(chunk->offset == assembled_length);
        assert(chunk->byte_count > 0U && chunk->byte_count <= CERBERUS_WIFI_PASSWORD_CHUNK_SIZE);
        assert(assembled_length + chunk->byte_count < sizeof(assembled_password));
        err = password_calls == fail_password_chunk ? password_error : ESP_OK;
        if (err == ESP_OK) {
            memcpy(assembled_password + assembled_length, chunk->bytes, chunk->byte_count);
            assembled_length += chunk->byte_count;
            assembled_password[assembled_length] = '\0';
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CONNECT_COMMIT:
        ++commit_calls;
        assert(request == NULL && request_length == 0U);
        err = commit_error;
        if (err == ESP_OK) { radio.state = CERBERUS_WIFI_CONNECTION_CONNECTING; }
        else if (commit_delivery_state >= 0) { radio.state = (uint8_t)commit_delivery_state; }
        break;
    case CERBERUS_SPI_COMMAND_WIFI_CONNECTION_STATUS:
        ++status_calls;
        assert(request == NULL && request_length == 0U);
        err = status_error;
        if (status_state_override >= 0) { radio.state = (uint8_t)status_state_override; }
        if (status_generation_override >= 0) { radio.generation = (uint8_t)status_generation_override; }
        if (status_index_override >= 0) { radio.index = (uint8_t)status_index_override; }
        break;
    case CERBERUS_SPI_COMMAND_WIFI_DISCONNECT:
        ++disconnect_calls;
        assert(request == NULL && request_length == 0U);
        err = disconnect_error;
        if (err == ESP_OK) { radio.state = CERBERUS_WIFI_CONNECTION_IDLE; }
        break;
    default:
        assert(false);
    }
    memcpy(response, &radio, sizeof(radio));
    return err;
}

/* ACTUAL_CONNECT_FUNCTION */

static void reset(void)
{
    link_mutex = &mutex_storage;
    mutex_held = false;
    lock_available = true;
    lock_takes = lock_gives = 0U;
    memset(&radio, 0, sizeof(radio));
    memset(assembled_password, 0, sizeof(assembled_password));
    assembled_length = 0U;
    begin_error = password_error = commit_error = status_error = disconnect_error = ESP_OK;
    fail_password_chunk = 1U;
    status_state_override = status_generation_override = status_index_override = -1;
    commit_delivery_state = -1;
    begin_calls = password_calls = commit_calls = status_calls = disconnect_calls = 0U;
}

static esp_err_t connect(const char *password)
{
    cerberus_wifi_connection_status_payload_t status;
    const esp_err_t err = c5_spi_link_connect_wifi(17U, 3U, password, &status);
    assert(!mutex_held);
    assert(lock_takes == 1U && lock_gives == 1U);
    return err;
}

static void test_normal_transfer(void)
{
    reset();
    const char *password = "012345678901234567890123456789012345678901234567890123456789012";
    assert(strlen(password) == CERBERUS_WIFI_PASSWORD_MAX_LENGTH);
    assert(connect(password) == ESP_OK);
    assert(begin_calls == 1U && password_calls == 4U && commit_calls == 1U);
    assert(strcmp(assembled_password, password) == 0);
    assert(status_calls == 0U && disconnect_calls == 0U);
    assert(radio.state == CERBERUS_WIFI_CONNECTION_CONNECTING);
    reset();
    assert(connect("") == ESP_OK);
    assert(begin_calls == 1U && password_calls == 0U && commit_calls == 1U);
    assert(status_calls == 0U && disconnect_calls == 0U);
}

static void test_owned_stage_cleanup(void)
{
    const char *password = "012345678901234567890123456789012345678901234567890123456789012";
    for (unsigned chunk = 1U; chunk <= 4U; ++chunk) {
        reset();
        password_error = ESP_ERR_TIMEOUT;
        fail_password_chunk = chunk;
        assert(connect(password) == ESP_ERR_TIMEOUT);
        assert(begin_calls == 1U && password_calls == chunk && commit_calls == 0U);
        assert(status_calls == 1U && disconnect_calls == 1U);
        assert(radio.state == CERBERUS_WIFI_CONNECTION_IDLE);
    }
    reset();
    commit_error = ESP_ERR_TIMEOUT; /* COMMIT never reached the C5 */
    assert(connect("example-password") == ESP_ERR_TIMEOUT);
    assert(commit_calls == 1U && status_calls == 1U && disconnect_calls == 1U);
    assert(radio.state == CERBERUS_WIFI_CONNECTION_IDLE);
}

static void test_ambiguous_commit_preserves_connection(void)
{
    const int states[] = {CERBERUS_WIFI_CONNECTION_CONNECTING, CERBERUS_WIFI_CONNECTION_CONNECTED,
                          CERBERUS_WIFI_CONNECTION_DISCONNECTING, CERBERUS_WIFI_CONNECTION_IDLE,
                          CERBERUS_WIFI_CONNECTION_ERROR};
    for (size_t i = 0U; i < sizeof(states) / sizeof(states[0]); ++i) {
        reset();
        commit_error = ESP_ERR_TIMEOUT;
        commit_delivery_state = states[i];
        assert(connect("example-password") == ESP_ERR_TIMEOUT);
        assert(commit_calls == 1U && status_calls == 1U && disconnect_calls == 0U);
        assert(radio.state == states[i]);
    }
}

static void test_rejected_begin_cannot_cancel_other_session(void)
{
    for (unsigned same_identity = 0U; same_identity < 2U; ++same_identity) {
        reset();
        begin_error = ESP_ERR_INVALID_STATE;
        radio.state = CERBERUS_WIFI_CONNECTION_STAGING;
        radio.generation = same_identity ? 17U : 18U;
        radio.index = same_identity ? 3U : 4U;
        assert(connect("example-password") == ESP_ERR_INVALID_STATE);
        assert(begin_calls == 1U && password_calls == 0U && commit_calls == 0U);
        assert(status_calls == 0U && disconnect_calls == 0U);
        assert(radio.state == CERBERUS_WIFI_CONNECTION_STAGING);
    }
    reset();
    begin_error = ESP_ERR_TIMEOUT; /* ambiguous BEGIN is not an accepted BEGIN */
    radio.state = CERBERUS_WIFI_CONNECTION_CONNECTED;
    assert(connect("example-password") == ESP_ERR_TIMEOUT);
    assert(status_calls == 0U && disconnect_calls == 0U);
    assert(radio.state == CERBERUS_WIFI_CONNECTION_CONNECTED);
}

static void test_status_identity_and_failure(void)
{
    reset();
    password_error = ESP_ERR_TIMEOUT;
    status_generation_override = 18;
    assert(connect("example-password") == ESP_ERR_TIMEOUT);
    assert(status_calls == 1U && disconnect_calls == 0U);
    reset();
    password_error = ESP_ERR_TIMEOUT;
    status_index_override = 4;
    assert(connect("example-password") == ESP_ERR_TIMEOUT);
    assert(status_calls == 1U && disconnect_calls == 0U);
    reset();
    password_error = ESP_ERR_TIMEOUT;
    status_error = ESP_FAIL;
    assert(connect("example-password") == ESP_ERR_TIMEOUT);
    assert(status_calls == 1U && disconnect_calls == 0U);
    reset();
    password_error = ESP_ERR_TIMEOUT;
    disconnect_error = ESP_FAIL;
    assert(connect("example-password") == ESP_ERR_TIMEOUT); /* preserve original error */
    assert(status_calls == 1U && disconnect_calls == 1U);
    assert(radio.state == CERBERUS_WIFI_CONNECTION_STAGING);
}

static void test_invalid_arguments_and_lock_failure(void)
{
    reset();
    cerberus_wifi_connection_status_payload_t status;
    assert(c5_spi_link_connect_wifi(17U, 3U, NULL, &status) == ESP_ERR_INVALID_ARG);
    assert(c5_spi_link_connect_wifi(17U, 3U, "password", NULL) == ESP_ERR_INVALID_ARG);
    char too_long[CERBERUS_WIFI_PASSWORD_MAX_LENGTH + 2U];
    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = '\0';
    assert(c5_spi_link_connect_wifi(17U, 3U, too_long, &status) == ESP_ERR_INVALID_SIZE);
    assert(lock_takes == 0U && begin_calls == 0U);
    lock_available = false;
    assert(c5_spi_link_connect_wifi(17U, 3U, "password", &status) == ESP_ERR_TIMEOUT);
    assert(lock_takes == 1U && lock_gives == 0U && begin_calls == 0U);
}

int main(void)
{
    test_normal_transfer();
    test_owned_stage_cleanup();
    test_ambiguous_commit_preserves_connection();
    test_rejected_begin_cannot_cancel_other_session();
    test_status_identity_and_failure();
    test_invalid_arguments_and_lock_failure();
    puts("PASS: real S3 Wi-Fi connect chunking, owned-STAGING cleanup, mutex coverage, ambiguous COMMIT preservation, rejected BEGIN isolation, identity/status guards, and error preservation");
    return 0;
}
