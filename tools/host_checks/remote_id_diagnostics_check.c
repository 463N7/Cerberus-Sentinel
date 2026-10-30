#undef NDEBUG
#include "remote_id_diagnostics.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static atomic_uint enters, exits;
static atomic_uintptr_t lock_address;
static _Thread_local bool held;
void remote_id_test_enter(portMUX_TYPE *lock)
{
    assert(lock && !held);
    uintptr_t expected = 0;
    if (!atomic_compare_exchange_strong(&lock_address, &expected, (uintptr_t)lock))
        assert(expected == (uintptr_t)lock);
    while (atomic_flag_test_and_set_explicit(&lock->flag, memory_order_acquire)) {}
    held = true;
    atomic_fetch_add(&enters, 1U);
}
void remote_id_test_exit(portMUX_TYPE *lock)
{
    assert(held && (uintptr_t)lock == atomic_load(&lock_address));
    atomic_fetch_add(&exits, 1U);
    held = false;
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

static cerberus_remote_id_diagnostics_t pattern(uint32_t n)
{
    cerberus_remote_id_diagnostics_t value = {0};
    value.radios[0] = (cerberus_remote_id_radio_diagnostics_t){
        .completed_sweeps = n, .error = -(int32_t)(n & INT32_MAX),
        .dropped_reports = (uint16_t)n, .retained_sources = (uint8_t)(n % 13U), .owned = (n & 1U) != 0U};
    value.radios[1] = (cerberus_remote_id_radio_diagnostics_t){
        .completed_sweeps = ~n, .error = (int32_t)(n & INT32_MAX),
        .dropped_reports = (uint16_t)~n, .retained_sources = (uint8_t)(12U - n % 13U), .owned = (n & 2U) != 0U};
    value.open = (n & 4U) != 0U; value.running = (n & 8U) != 0U;
    value.stopping = (n & 16U) != 0U; value.selected = (n & 32U) != 0U;
    value.present = (n & 64U) != 0U;
    value.wifi_job_id = ((uint64_t)n << 32) | ~n;
    value.wifi_window_elapsed_ms = n ^ 0x12345678U;
    value.wifi_lease_remaining_ms = (uint16_t)(n ^ 0x5678U);
    value.wifi_job_state = (uint8_t)(n % 8U);
    value.wifi_channel = (uint8_t)n;
    value.wifi_priority = (n & 128U) != 0U;
    value.wifi_profile_valid = (n & 256U) != 0U;
    value.wifi_follow_flags = (uint8_t)(n & 7U);
    return value;
}
static bool equal(const cerberus_remote_id_diagnostics_t *a, const cerberus_remote_id_diagnostics_t *b)
{
    if (a->open != b->open || a->running != b->running || a->stopping != b->stopping ||
        a->selected != b->selected || a->present != b->present) return false;
    if (a->wifi_job_id != b->wifi_job_id || a->wifi_window_elapsed_ms != b->wifi_window_elapsed_ms ||
        a->wifi_lease_remaining_ms != b->wifi_lease_remaining_ms || a->wifi_job_state != b->wifi_job_state ||
        a->wifi_channel != b->wifi_channel || a->wifi_priority != b->wifi_priority ||
        a->wifi_profile_valid != b->wifi_profile_valid || a->wifi_follow_flags != b->wifi_follow_flags) return false;
    for (unsigned i = 0U; i < 2U; ++i) {
        const cerberus_remote_id_radio_diagnostics_t *x = &a->radios[i], *y = &b->radios[i];
        if (x->completed_sweeps != y->completed_sweeps || x->error != y->error ||
            x->dropped_reports != y->dropped_reports || x->retained_sources != y->retained_sources ||
            x->owned != y->owned) return false;
    }
    return true;
}
static void value_store_cases(void)
{
    cerberus_remote_id_diagnostics_t value, zero = {0};
    cerberus_remote_id_diagnostics_get(&value); CHECK(equal(&value, &zero));
    unsigned before = atomic_load(&enters);
    cerberus_remote_id_diagnostics_get(NULL); CHECK(atomic_load(&enters) == before);
    cerberus_remote_id_diagnostics_t original = pattern(123456789U), expected = original;
    cerberus_remote_id_diagnostics_publish(&original);
    memset(&original, 0, sizeof(original));
    cerberus_remote_id_diagnostics_get(&value); CHECK(equal(&value, &expected));
    memset(&value, 0, sizeof(value));
    cerberus_remote_id_diagnostics_get(&value); CHECK(equal(&value, &expected));
    cerberus_remote_id_diagnostics_publish(NULL);
    cerberus_remote_id_diagnostics_get(&value); CHECK(equal(&value, &zero));
    CHECK(!held && atomic_load(&enters) == atomic_load(&exits));
}

/* Real concurrent readers/writer exercise the production snapshot copy. The
 * platform stub replaces only ESP's critical-section primitive with a C11 lock. */
#ifdef _WIN32
#define THREAD_RESULT DWORD WINAPI
#define THREAD_RETURN return 0
#else
#define THREAD_RESULT void *
#define THREAD_RETURN return NULL
#endif
static THREAD_RESULT writer(void *unused)
{
    (void)unused;
    for (uint32_t i = 1U; i <= 40000U; ++i) {
        cerberus_remote_id_diagnostics_t value = pattern(i);
        cerberus_remote_id_diagnostics_publish(&value);
        memset(&value, 0, sizeof(value));
    }
    THREAD_RETURN;
}
static THREAD_RESULT reader(void *unused)
{
    (void)unused;
    for (unsigned i = 0U; i < 50000U; ++i) {
        cerberus_remote_id_diagnostics_t value;
        cerberus_remote_id_diagnostics_get(&value);
        cerberus_remote_id_diagnostics_t expected = pattern(value.radios[0].completed_sweeps);
        assert(equal(&value, &expected));
    }
    THREAD_RETURN;
}
static void concurrency_cases(void)
{
    cerberus_remote_id_diagnostics_t first = pattern(0U);
    cerberus_remote_id_diagnostics_publish(&first);
    unsigned before = atomic_load(&enters);
#ifdef _WIN32
    HANDLE threads[3] = {CreateThread(NULL, 0, writer, NULL, 0, NULL),
        CreateThread(NULL, 0, reader, NULL, 0, NULL), CreateThread(NULL, 0, reader, NULL, 0, NULL)};
    for (unsigned i = 0U; i < 3U; ++i) CHECK(threads[i] != NULL);
    CHECK(WaitForMultipleObjects(3, threads, TRUE, 30000) == WAIT_OBJECT_0);
    for (unsigned i = 0U; i < 3U; ++i) CHECK(CloseHandle(threads[i]));
#else
    pthread_t threads[3];
    CHECK(pthread_create(&threads[0], NULL, writer, NULL) == 0);
    CHECK(pthread_create(&threads[1], NULL, reader, NULL) == 0);
    CHECK(pthread_create(&threads[2], NULL, reader, NULL) == 0);
    for (unsigned i = 0U; i < 3U; ++i) CHECK(pthread_join(threads[i], NULL) == 0);
#endif
    CHECK(atomic_load(&enters) - before == 140000U);
    CHECK(atomic_load(&enters) == atomic_load(&exits));
    cerberus_remote_id_diagnostics_t value, expected = pattern(40000U);
    cerberus_remote_id_diagnostics_get(&value); CHECK(equal(&value, &expected));
}

static char output[4096];
static size_t used;
static unsigned other_commands, print_calls;
static bool replace_after_first_print;
static cerberus_remote_id_diagnostics_t replacement;
static void console_write(const char *text)
{
    size_t length = strlen(text); assert(length < sizeof(output) - used);
    memcpy(output + used, text, length + 1U); used += length;
}
static void console_printf(const char *format, ...)
{
    char text[768]; va_list args; va_start(args, format);
    int length = vsnprintf(text, sizeof(text), format, args); va_end(args);
    assert(length >= 0 && (size_t)length < sizeof(text)); console_write(text);
    ++print_calls;
    if (replace_after_first_print && print_calls == 1U)
        cerberus_remote_id_diagnostics_publish(&replacement);
}
#define OTHER(name) static void name(void) { ++other_commands; }
OTHER(print_info) OTHER(print_health) OTHER(print_memory) OTHER(print_c5)
OTHER(print_storage) OTHER(print_resources) OTHER(print_capture) OTHER(print_usb)
OTHER(print_network) OTHER(print_wifi_seen) OTHER(print_stations) OTHER(print_reports)
OTHER(print_radio) OTHER(print_battery)
#include "remote_id_console_impl.h"

static void clear_output(void)
{used = print_calls = other_commands = 0U; output[0] = '\0'; replace_after_first_print = false;}
static void has(const char *text) { CHECK(strstr(output, text) != NULL); }
static void lacks(const char *text) { CHECK(strstr(output, text) == NULL); }
static void console_cases(void)
{
    cerberus_remote_id_diagnostics_publish(NULL); clear_output();
    unsigned before = atomic_load(&enters);
    char command[] = " \tReMoTe_Id\r\n"; execute_command(command);
    CHECK(atomic_load(&enters) - before == 1U && print_calls == 5U && !other_commands);
    has("remote_id_scope=published_session_aggregates\r\nremote_id_open=0\r\n");
    has("remote_id_wifi_sweeps=0\r\nremote_id_wifi_sources=0\r\n");
    has("remote_id_ble_error=0\r\nremote_id_ble_owned=0\r\n");
    has("remote_id_follow_enabled=0\r\nremote_id_wifi_follow_requested=0\r\nremote_id_wifi_follow_bound=0\r\n");
    cerberus_remote_id_diagnostics_t value = {.open = true, .running = true, .selected = true, .present = false,
        .wifi_job_id = UINT64_MAX, .wifi_window_elapsed_ms = UINT32_MAX,
        .wifi_lease_remaining_ms = UINT16_MAX, .wifi_job_state = 7, .wifi_channel = 177,
        .wifi_priority = true, .wifi_profile_valid = true, .wifi_follow_flags = 7U,
        .radios = {{.completed_sweeps = UINT32_MAX, .error = INT32_MIN, .dropped_reports = UINT16_MAX,
            .retained_sources = 12, .owned = true},
            {.completed_sweeps = 13, .error = INT32_MAX, .retained_sources = 1}}};
    cerberus_remote_id_diagnostics_publish(&value); clear_output(); print_remote_id();
    has("remote_id_open=1\r\nremote_id_running=1\r\nremote_id_stopping=0\r\n");
    has("remote_id_selected=1\r\nremote_id_present=0\r\n");
    has("remote_id_wifi_sweeps=4294967295\r\nremote_id_wifi_sources=12\r\n");
    has("remote_id_wifi_dropped_reports=65535\r\nremote_id_wifi_error=-2147483648\r\nremote_id_wifi_owned=1\r\n");
    has("remote_id_ble_sweeps=13\r\nremote_id_ble_sources=1\r\nremote_id_ble_dropped_reports=0\r\n");
    has("remote_id_ble_error=2147483647\r\nremote_id_ble_owned=0\r\n");
    has("remote_id_wifi_priority=1\r\nremote_id_wifi_profile_valid=1\r\n");
    has("remote_id_wifi_job_id=18446744073709551615\r\nremote_id_wifi_job_state=7\r\n");
    has("remote_id_wifi_channel=177\r\nremote_id_wifi_window_elapsed_ms=4294967295\r\n");
    has("remote_id_wifi_lease_remaining_ms=65535\r\n");
    has("remote_id_follow_enabled=1\r\nremote_id_wifi_follow_requested=1\r\nremote_id_wifi_follow_bound=1\r\n");
    CHECK(used < 1024U); lacks("address="); lacks("latitude"); lacks("longitude"); lacks("claimed");
    /* UI can publish during USB writes; both radio lines still use the one
     * private copied snapshot rather than mixing subsequent publications. */
    replacement = (cerberus_remote_id_diagnostics_t){0};
    clear_output(); replace_after_first_print = true; before = atomic_load(&enters);
    print_remote_id(); CHECK(atomic_load(&enters) - before == 2U);
    has("remote_id_open=1\r\n"); has("remote_id_wifi_sweeps=4294967295\r\n");
    has("remote_id_ble_sweeps=13\r\n");
    has("remote_id_wifi_job_id=18446744073709551615\r\n");
    has("remote_id_wifi_follow_bound=1\r\n");
    clear_output(); print_remote_id(); has("remote_id_open=0\r\n"); has("remote_id_wifi_sweeps=0\r\n");
    clear_output(); before = atomic_load(&enters); char help[] = "help"; execute_command(help);
    has("remote_id Read published Remote ID session counts without scanning");
    CHECK(atomic_load(&enters) == before && !other_commands);
    const char *invalid[] = {"remote_id start", "remote_id stop", "remote_id=1", "remote_id; radio"};
    for (unsigned i = 0U; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        char line[64]; strcpy(line, invalid[i]); clear_output(); execute_command(line);
        has("ERR unknown command"); CHECK(atomic_load(&enters) == before && !other_commands);
    }
    clear_output(); char empty[] = " \t\r\n"; execute_command(empty); CHECK(!used && !other_commands);
}
int main(void)
{
    value_store_cases(); concurrency_cases(); console_cases();
    printf("Remote ID diagnostics: %u checks plus 100,000 concurrent coherent reads; production value copy, clear, bounded locking, one-snapshot console/dispatch and no identifiers passed. Snapshot=%zu bytes on host.\n",
        checks, sizeof(cerberus_remote_id_diagnostics_t));
    return 0;
}
