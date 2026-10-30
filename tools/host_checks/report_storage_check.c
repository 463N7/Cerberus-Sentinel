#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_check.h"
#include "storage_report_limits.h"
#define TAG "reports"
#define SD_DIAGNOSTIC_FILE_LIMIT 9999
#define CERBERUS_STORAGE_MOUNT_PATH test_root
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static const char *test_root;
static char sd_diagnostic_directory_path[1024],sd_diagnostic_temp_path[1024],sd_diagnostic_file_path[1024];
static bool sd_mounted=true,locked;
static bool cerberus_storage_is_mounted(void){return sd_mounted;}
static esp_err_t cerberus_storage_mount(void){return ESP_FAIL;}
static esp_err_t take_storage_mutex(void){assert(!locked);locked=true;return ESP_OK;}
static void release_storage_mutex(void){assert(locked);locked=false;}
static esp_err_t ensure_directory_exists(const char *path)
{struct stat s;return stat(path,&s)==0?ESP_OK:ESP_FAIL;}
#include "report_storage_impl.h"
static void verify(const char *expected,size_t length)
{
    assert(!locked);FILE *file=fopen(sd_diagnostic_file_path,"rb");assert(file);
    for(size_t i=0;i<length;++i)assert(fgetc(file)==(unsigned char)expected[i]);
    assert(fgetc(file)==EOF&&!fclose(file));struct stat s;assert(stat(sd_diagnostic_temp_path,&s)!=0);
}
int main(int argc,char **argv)
{
    assert(argc==2);test_root=argv[1];char name[32];
    char data[CERBERUS_STORAGE_WIFI_REPORT_MAX_BYTES+1];memset(data,'R',sizeof(data));
    assert(cerberus_storage_write_wifi_report(data,8192,name,sizeof(name))==ESP_OK&&!strcmp(name,"WIFI0001.CSV"));verify(data,8192);
    assert(cerberus_storage_write_wifi_report(data,CERBERUS_STORAGE_WIFI_REPORT_MAX_BYTES,name,sizeof(name))==ESP_OK&&!strcmp(name,"WIFI0002.CSV"));
    verify(data,CERBERUS_STORAGE_WIFI_REPORT_MAX_BYTES);
    assert(cerberus_storage_write_wifi_report(data,sizeof(data),name,sizeof(name))==ESP_ERR_INVALID_ARG&&!locked);
    assert(cerberus_storage_write_diagnostic_report(data,4097,name,sizeof(name))==ESP_ERR_INVALID_ARG&&!locked);
    assert(cerberus_storage_write_wifi_defense_report(data,4097,name,sizeof(name))==ESP_ERR_INVALID_ARG&&!locked);
    assert(cerberus_storage_write_diagnostic_report(data,4096,name,sizeof(name))==ESP_OK&&!strcmp(name,"154_0001.CSV"));verify(data,4096);
    assert(cerberus_storage_write_wifi_defense_report(data,4096,name,sizeof(name))==ESP_OK&&!strcmp(name,"WDEF0001.CSV"));verify(data,4096);
    assert(cerberus_storage_write_portal_report(data,4097,name,sizeof(name))==ESP_ERR_INVALID_ARG&&!locked);
    assert(cerberus_storage_write_portal_report(data,4096,name,sizeof(name))==ESP_OK&&!strcmp(name,"PORT0001.CSV"));verify(data,4096);
    assert(cerberus_storage_write_portal_report(data,7,name,sizeof(name))==ESP_OK&&!strcmp(name,"PORT0002.CSV"));verify(data,7);
    sd_mounted=false;assert(cerberus_storage_write_wifi_report(data,1,name,sizeof(name))==ESP_FAIL&&!locked);
    puts("Report storage passed: Wi-Fi and portal files, exact contents, unique names, temporary-file finalization, size limits and mount failure.");
}
