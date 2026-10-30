#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_check.h"
#include "wardrive_core.h"

static bool partial_next_write, fail_next_sync;
static unsigned write_calls;
static size_t injected_fwrite(const void *data, size_t size, size_t count, FILE *file)
{
    ++write_calls;
    if (partial_next_write) {
        partial_next_write = false;
        assert(size == 1U && count > 2U);
        /* Really write a prefix, without setting FILE's error flag. This
         * reproduces a short-write path that final fflush cannot diagnose. */
        return fwrite(data, size, 2U, file);
    }
    return fwrite(data, size, count, file);
}
static int injected_fsync(int fd)
{
    (void)fd;
    if (fail_next_sync) { fail_next_sync = false; return -1; }
    return 0;
}

#define TAG "wardrive-storage"
#define ESP_LOGI(...) ((void)0)
#define CERBERUS_STORAGE_MOUNT_PATH test_root
#define CERBERUS_STORAGE_WARDRIVE_PATH "/CERBERUS/WARD"
#define CERBERUS_STORAGE_PATH_MAX 512U
#define CERBERUS_STORAGE_NAME_MAX 256U
#define CERBERUS_STORAGE_WARDRIVE_MAX_BYTES (64U * 1024U * 1024U)
#define SD_WARDRIVE_FILE_LIMIT 999U
#define fileno(file) (0)
#define fsync(fd) injected_fsync(fd)
#define fwrite(data, size, count, file) injected_fwrite(data, size, count, file)

typedef enum {
    CERBERUS_STORAGE_OWNER_NONE,
    CERBERUS_STORAGE_OWNER_FIRMWARE,
    CERBERUS_STORAGE_OWNER_USB,
} cerberus_storage_owner_t;
static const char *test_root;
static bool sd_mounted = true, locked;
static cerberus_storage_owner_t storage_owner = CERBERUS_STORAGE_OWNER_FIRMWARE;
static FILE *sd_wifi_capture_file;
static int64_t test_now_us;
static bool cerberus_storage_is_mounted(void) { return sd_mounted; }
static esp_err_t cerberus_storage_mount(void)
{
    if (storage_owner == CERBERUS_STORAGE_OWNER_USB) return ESP_ERR_INVALID_STATE;
    sd_mounted = true;
    return ESP_OK;
}
static esp_err_t take_storage_mutex(void)
{ assert(!locked); locked = true; return ESP_OK; }
static void release_storage_mutex(void)
{ assert(locked); locked = false; }
static esp_err_t require_firmware_owner_locked(void)
{ return sd_mounted && storage_owner == CERBERUS_STORAGE_OWNER_FIRMWARE ?
    ESP_OK : ESP_ERR_INVALID_STATE; }
static esp_err_t ensure_directory_exists(const char *path)
{ struct stat item; return stat(path, &item) == 0 ? ESP_OK : ESP_FAIL; }
static esp_err_t unmount_locked(void) { sd_mounted = false; return ESP_OK; }
static int64_t esp_timer_get_time(void) { return test_now_us; }

#include "wardrive_storage_impl.h"
_Static_assert(sizeof(sd_wardrive_stream) + sizeof(sd_wigle_stream) <
    2U * (CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX) +
    CERBERUS_STORAGE_NAME_MAX, "two compact streams cost less than old path buffers");

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_root = argv[1];
    const char header[] = "header\n";
    const char row[] = "row1\n";
    char name[24];
    uint64_t bytes = 0;
    assert(cerberus_storage_wardrive_begin(header, sizeof(header) - 1U) == ESP_OK);
    assert(!locked && sd_wardrive_stream.file != NULL);
    assert(cerberus_storage_reserve_for_usb() == ESP_ERR_INVALID_STATE);
    assert(storage_owner == CERBERUS_STORAGE_OWNER_FIRMWARE && sd_mounted);
    assert(cerberus_storage_wardrive_append(row, sizeof(row) - 1U) == ESP_OK);
    test_now_us = 3000000;
    assert(cerberus_storage_wardrive_sync() == ESP_OK);
    assert(cerberus_storage_wardrive_finish(true, name, sizeof(name), &bytes) == ESP_OK);
    assert(!strcmp(name, "UNLOC001.CSV") && bytes == sizeof(header) + sizeof(row) - 2U);
    FILE *saved = fopen(sd_wardrive_stream.final_path, "rb");
    assert(saved != NULL);
    char content[32] = {0};
    assert(fread(content, 1U, bytes, saved) == bytes);
    assert(!fclose(saved));
    assert(!strcmp(content, "header\nrow1\n"));
    struct stat item;
    assert(stat(sd_wardrive_stream.temp_path, &item) != 0);
    assert(cerberus_storage_reserve_for_usb() == ESP_OK);
    assert(storage_owner == CERBERUS_STORAGE_OWNER_USB && !sd_mounted);
    assert(cerberus_storage_wardrive_begin(header, sizeof(header) - 1U) == ESP_ERR_INVALID_STATE);
    storage_owner = CERBERUS_STORAGE_OWNER_FIRMWARE;
    sd_mounted = true;
    assert(cerberus_storage_wardrive_begin(header, sizeof(header) - 1U) == ESP_OK);
    assert(!strcmp(sd_wardrive_stream.name, "UNLOC002.CSV"));
    assert(cerberus_storage_wardrive_finish(false, name, sizeof(name), NULL) == ESP_OK);
    assert(stat(sd_wardrive_stream.final_path, &item) != 0);
    assert(stat(sd_wardrive_stream.temp_path, &item) != 0);
    assert(cerberus_storage_wardrive_session_begin() == ESP_OK);
    assert(cerberus_storage_reserve_for_usb() == ESP_ERR_INVALID_STATE);
    assert(cerberus_storage_wardrive_session_begin() == ESP_ERR_INVALID_STATE);
    assert(cerberus_storage_wardrive_wigle_begin(header, sizeof(header)-1U) == ESP_OK);
    assert(cerberus_storage_wardrive_begin(header, sizeof(header)-1U) == ESP_OK);
    assert(cerberus_storage_wardrive_session_finish() == ESP_ERR_INVALID_STATE);
    assert(cerberus_storage_wardrive_wigle_append(row, sizeof(row)-1U) == ESP_OK);
    assert(cerberus_storage_wardrive_wigle_sync() == ESP_OK);
    assert(cerberus_storage_wardrive_wigle_finish(true, name, sizeof(name), &bytes) == ESP_OK);
    assert(!strcmp(name, "WIGLE001.CSV"));
    assert(cerberus_storage_reserve_for_usb() == ESP_ERR_INVALID_STATE);
    assert(cerberus_storage_wardrive_finish(false, name, sizeof(name), NULL) == ESP_OK);
    assert(cerberus_storage_reserve_for_usb() == ESP_ERR_INVALID_STATE);
    assert(cerberus_storage_wardrive_session_finish() == ESP_OK);
    assert(cerberus_storage_reserve_for_usb() == ESP_OK);
    storage_owner = CERBERUS_STORAGE_OWNER_FIRMWARE;
    sd_mounted = true;
    assert(cerberus_storage_wardrive_session_begin() == ESP_OK);
    assert(cerberus_storage_wardrive_begin(header, sizeof(header)-1U) == ESP_OK);
    assert(cerberus_storage_wardrive_append(row, sizeof(row)-1U) == ESP_OK);
    partial_next_write = true;
    assert(cerberus_storage_wardrive_append(row, sizeof(row)-1U) == ESP_FAIL);
    assert(sd_wardrive_stream.write_failed && !ferror(sd_wardrive_stream.file));
    unsigned writes_after_failure = write_calls;
    assert(cerberus_storage_wardrive_append(row, sizeof(row)-1U) == ESP_FAIL);
    assert(write_calls == writes_after_failure);
    assert(cerberus_storage_wardrive_sync() == ESP_FAIL);
    assert(cerberus_storage_wardrive_finish(true, name, sizeof(name), &bytes) == ESP_FAIL);
    assert(name[0] == '\0' && sd_wardrive_stream.file == NULL);
    assert(stat(sd_wardrive_stream.final_path, &item) != 0);
    assert(stat(sd_wardrive_stream.temp_path, &item) == 0);
    assert((uint64_t)item.st_size == sizeof(header) + sizeof(row)); /* complete row + 2-byte tail */
    char tainted_name[24];
    memcpy(tainted_name, sd_wardrive_stream.name, sizeof(tainted_name));
    assert(cerberus_storage_wardrive_session_finish() == ESP_OK);
    assert(cerberus_storage_wardrive_begin(header, sizeof(header)-1U) == ESP_OK);
    assert(strcmp(sd_wardrive_stream.name, tainted_name) != 0 && !sd_wardrive_stream.write_failed);
    assert(cerberus_storage_wardrive_append(row, sizeof(row)-1U) == ESP_OK);
    assert(cerberus_storage_wardrive_finish(true, name, sizeof(name), &bytes) == ESP_OK);

    /* Even a failed first row must remain recoverable when the service has
     * no committed rows and requests keep=false. The other stream is clean. */
    assert(cerberus_storage_wardrive_wigle_begin(header, sizeof(header)-1U) == ESP_OK);
    partial_next_write = true;
    assert(cerberus_storage_wardrive_wigle_append(row, sizeof(row)-1U) == ESP_FAIL);
    assert(cerberus_storage_wardrive_wigle_finish(false, NULL, 0U, NULL) == ESP_FAIL);
    assert(sd_wigle_stream.file == NULL && stat(sd_wigle_stream.final_path, &item) != 0);
    assert(stat(sd_wigle_stream.temp_path, &item) == 0 && (size_t)item.st_size == sizeof(header)+1U);
    assert(cerberus_storage_wardrive_wigle_begin(header, sizeof(header)-1U) == ESP_OK);
    assert(cerberus_storage_wardrive_wigle_append(row, sizeof(row)-1U) == ESP_OK);
    fail_next_sync = true;
    assert(cerberus_storage_wardrive_wigle_sync() == ESP_FAIL);
    assert(cerberus_storage_wardrive_wigle_finish(true, name, sizeof(name), &bytes) == ESP_FAIL);
    assert(stat(sd_wigle_stream.final_path, &item) != 0 && stat(sd_wigle_stream.temp_path, &item) == 0);

    /* Periodic sync inside append must latch failure too. */
    assert(cerberus_storage_wardrive_begin(header, sizeof(header)-1U) == ESP_OK);
    test_now_us += 3000000;
    fail_next_sync = true;
    assert(cerberus_storage_wardrive_append(row, sizeof(row)-1U) == ESP_FAIL);
    assert(cerberus_storage_wardrive_finish(false, NULL, 0U, NULL) == ESP_FAIL);
    assert(stat(sd_wardrive_stream.final_path, &item) != 0 && stat(sd_wardrive_stream.temp_path, &item) == 0);
    /* Actual v4 header + actual formatted BLE and probe evidence through the production
     * stream, including its 512-byte header gate and saved-file readback. */
    const char v4_header[]=CERBERUS_WARDRIVE_UNLOCATED_HEADER;
    cerberus_wardrive_observation_t ble={.type=CERBERUS_WARDRIVE_BLE,.radio=CERBERUS_WARDRIVE_RADIO_S3,
        .address={1,2,3,4,5,6},.rssi_dbm=-70,.ble_evidence={.flags=1,.advertisement_sequence=1,
            .advertisement_observed_ms=123,.advertisement={.facts=CERBERUS_BLE_FLOCK_FS_BATTERY}}};
    char v4_row[384],v4_content[1536]={0};
    size_t v4_length=cerberus_wardrive_format_unlocated_row(&ble,NULL,500,400,v4_row,sizeof(v4_row));
    assert(v4_length&&sizeof(v4_header)-1U<=512U);
    assert(cerberus_storage_wardrive_begin(v4_header,sizeof(v4_header)-1U)==ESP_OK);
    assert(cerberus_storage_wardrive_append(v4_row,v4_length)==ESP_OK);
    cerberus_wifi_probe_row_t source={.transmitter={2,1,2,3,4,5},
        .ssid={'F','l','o','c','k',0,',','\n'},.ssid_length=8,.channel=6,.rssi=-50,
        .name_bits=CERBERUS_WIFI_PROBE_NAME_FLOCK,.frames=9,.callback_ms=UINT64_C(4294967297)};
    cerberus_wardrive_observation_t probe;
    assert(cerberus_wardrive_observation_from_probe(&source,&probe));
    char probe_row[384];
    size_t probe_length=cerberus_wardrive_format_unlocated_row(&probe,NULL,502,402,probe_row,sizeof(probe_row));
    assert(probe_length&&strstr(probe_row,",466C6F636B002C0A,1,0,4294967297,9,,,,,\n"));
    assert(cerberus_storage_wardrive_append(probe_row,probe_length)==ESP_OK);
    cerberus_wifi_relation_row_t relation_source={.bssid={0x10,1,2,3,4,5},.station={2,6,7,8,9,10},
        .channel=149,.directions=3,.latest_direction=2,.rssi_valid=1,
        .to_ap_frames=2,.from_ap_frames=3,.to_ap_callback_us=1,
        .from_ap_callback_us=UINT64_C(4294967297001),.to_ap_rssi=-80,.from_ap_rssi=127};
    cerberus_wardrive_observation_t relation;
    assert(cerberus_wardrive_observation_from_relation(&relation_source,&relation));
    char relation_row[384];
    size_t relation_length=cerberus_wardrive_format_unlocated_row(&relation,NULL,503,403,relation_row,sizeof(relation_row));
    assert(relation_length&&strstr(relation_row,",wifi_relation,C5,02:06:07:08:09:0A,,149,,")&&
        strstr(relation_row,",10:01:02:03:04:05,FROM_AP,4294967297001,2,3\n"));
    assert(cerberus_storage_wardrive_append(relation_row,relation_length)==ESP_OK);
    assert(cerberus_storage_wardrive_finish(true,name,sizeof(name),&bytes)==ESP_OK);
    assert(bytes==sizeof(v4_header)-1U+v4_length+probe_length+relation_length&&bytes<sizeof(v4_content));
    FILE *v4_saved=fopen(sd_wardrive_stream.final_path,"rb");assert(v4_saved);
    assert(fread(v4_content,1U,bytes,v4_saved)==bytes&&!fclose(v4_saved));
    assert(!memcmp(v4_content,v4_header,sizeof(v4_header)-1U));
    assert(!memcmp(v4_content+sizeof(v4_header)-1U,v4_row,v4_length));
    assert(!memcmp(v4_content+sizeof(v4_header)-1U+v4_length,probe_row,probe_length));
    assert(!strcmp(v4_content+sizeof(v4_header)-1U+v4_length+probe_length,relation_row));
    assert(cerberus_storage_reserve_for_usb() == ESP_OK);
    puts("wardrive SD dual streams, USB lease, partial-write/sync failure retention: pass");
    return 0;
}
