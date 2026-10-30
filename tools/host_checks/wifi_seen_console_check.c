#ifdef NDEBUG
#undef NDEBUG
#endif
#include "c5_spi_link.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char output[4096];
static size_t used;
static unsigned peer_reads,scan_reads,row_reads,clock_reads,other_commands;
static unsigned fail_peer_call,fail_scan_call;
static int failed_row;
static int64_t clock_us;
static uint64_t row_delay_us;
static cerberus_c5_status_t peer,peer_after;
static cerberus_wifi_scan_status_payload_t scan,after;
static cerberus_s3_wifi_observation_t observations[32];
static void console_write(const char *text)
{
    size_t size=strlen(text);assert(size<sizeof(output)-used);
    memcpy(output+used,text,size+1U);used+=size;
}
static void console_printf(const char *format,...)
{
    char text[768];va_list args;va_start(args,format);
    int count=vsnprintf(text,sizeof(text),format,args);va_end(args);
    assert(count>=0&&(size_t)count<sizeof(text));console_write(text);
}
static const char *esp_err_to_name(esp_err_t error)
{
    switch(error){case ESP_OK:return "ESP_OK";case ESP_ERR_TIMEOUT:return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_STATE:return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_RESPONSE:return "ESP_ERR_INVALID_RESPONSE";default:return "ESP_FAIL";}
}
esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *out)
{
    ++peer_reads;assert(peer_reads<=2U);
    *out=peer_reads==1U?peer:peer_after;
    return peer_reads==fail_peer_call?ESP_ERR_TIMEOUT:ESP_OK;
}
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *out)
{
    assert(peer_reads>=1U);++scan_reads;assert(scan_reads<=2U);
    *out=scan_reads==1U?scan:after;
    return scan_reads==fail_scan_call?ESP_ERR_TIMEOUT:ESP_OK;
}
esp_err_t c5_spi_link_get_wifi_scan_observed_result(uint8_t generation,uint8_t index,
    cerberus_c5_wifi_result_t *result,cerberus_s3_wifi_observation_t *observation)
{
    assert(peer_reads==1U&&scan_reads==1U&&generation==scan.generation&&index==row_reads);
    assert(index<32U&&index<scan.stored_count);++row_reads;clock_us+=(int64_t)row_delay_us;
    memset(result,0,sizeof(*result));strcpy(result->ssid,"PRIVATE_TEST_SSID");
    memset(result->bssid,0xde,sizeof(result->bssid));*observation=observations[index];
    return index==failed_row?ESP_FAIL:ESP_OK;
}
static int64_t esp_timer_get_time(void)
{ assert(peer_reads==2U&&scan_reads==2U);++clock_reads;return clock_us; }
/* Other command bodies are irrelevant to dispatch, and must not be called. */
#define OTHER(name) static void name(void){++other_commands;}
OTHER(print_info) OTHER(print_health) OTHER(print_memory) OTHER(print_c5)
OTHER(print_storage) OTHER(print_resources) OTHER(print_capture) OTHER(print_usb)
OTHER(print_network) OTHER(print_stations) OTHER(print_reports) OTHER(print_radio) OTHER(print_battery)
OTHER(print_remote_id)
#include "wifi_seen_console_impl.h"

static void has(const char *text){assert(strstr(output,text));}
static void lacks(const char *text){assert(!strstr(output,text));}
static void no_counts(void)
{
    lacks("completed_snapshot");lacks("wifi_seen_stored=");lacks("wifi_seen_available=");
    lacks("wifi_seen_recent=");lacks("wifi_seen_total=");
    lacks("PRIVATE_TEST_SSID");lacks("de:de:");assert(other_commands==0U);
}
static void reset(uint32_t firmware,uint8_t count)
{
    used=0U;output[0]='\0';peer_reads=scan_reads=row_reads=clock_reads=other_commands=0U;
    fail_peer_call=fail_scan_call=0U;failed_row=-1;clock_us=1000000;row_delay_us=0U;
    peer=(cerberus_c5_status_t){.firmware_version=firmware,.boot_count=7U,.reset_reason=1U,.uptime_ms=10000U};
    peer_after=peer;peer_after.uptime_ms+=100U;
    scan=(cerberus_wifi_scan_status_payload_t){.state=CERBERUS_WIFI_SCAN_COMPLETE,
        .generation=99U,.stored_count=count,.total_count=count};after=scan;
    memset(observations,0,sizeof(observations));
}
static void success_and_dispatch(void)
{
    reset(51U,4U);scan.total_count=after.total_count=40U;
    observations[0]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=0U,.source_age_ms=0U};
    observations[1]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=1000000U,.source_age_ms=30000U};
    observations[2]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=1000000U,.source_age_ms=30001U};
    char command[]=" \tWiFi_SeEn\r\n";execute_command(command);
    has("wifi_seen=completed_snapshot\r\n");has("wifi_seen_firmware=51\r\n");
    has("wifi_seen_total=40\r\nwifi_seen_stored=4\r\n");
    has("wifi_seen_available=3\r\nwifi_seen_unavailable=1\r\nwifi_seen_recent=2\r\n");
    has("wifi_seen_not_recent=1\r\nwifi_seen_clock_invalid=0\r\n");
    has("wifi_seen_scope=retained_matching_frame_presence\r\n");
    assert(peer_reads==2U&&scan_reads==2U&&row_reads==4U&&clock_reads==1U&&!other_commands);
    lacks("PRIVATE_TEST_SSID");lacks("de:de:");
    reset(51U,32U);
    for(unsigned i=0;i<32U;++i)observations[i]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=1000000U};
    print_wifi_seen();has("wifi_seen_stored=32\r\nwifi_seen_available=32\r\n");
    has("wifi_seen_recent=32\r\n");assert(row_reads==32U);
    reset(51U,0U);print_wifi_seen();has("wifi_seen=completed_snapshot\r\n");
    has("wifi_seen_stored=0\r\nwifi_seen_available=0\r\n");
    assert(row_reads==0U&&peer_reads==2U&&scan_reads==2U);
    const uint32_t old[]={0U,50U};
    for(unsigned i=0;i<2U;++i){
        reset(old[i],1U);observations[0].valid=true;print_wifi_seen();
        has("wifi_seen_timing=unavailable_peer_firmware\r\n");
        has("wifi_seen_available=0\r\nwifi_seen_unavailable=1\r\nwifi_seen_recent=0\r\n");
    }
    reset(51U,1U);print_wifi_seen();has("wifi_seen_timing=supported\r\n");
    has("wifi_seen_available=0\r\nwifi_seen_unavailable=1\r\n");
    reset(51U,0U);char help[]="help";execute_command(help);has("wifi_seen Read completed Wi-Fi source-age coverage without scanning");
    assert(!peer_reads&&!scan_reads&&!row_reads);
    reset(51U,0U);char args[]="wifi_seen start";execute_command(args);has("ERR unknown command");
    assert(!peer_reads&&!scan_reads&&!row_reads&&!other_commands);
}
static void final_age(void)
{
    reset(51U,2U);row_delay_us=1000U;
    observations[0]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=1000000U,.source_age_ms=29999U};
    observations[1]=observations[0];observations[1].source_age_ms=29998U;
    print_wifi_seen();has("wifi_seen_recent=1\r\nwifi_seen_not_recent=1\r\n");
    /* First row would pass at its getter time; final common time must expire it. */
    reset(51U,4U);clock_us=(INT64_C(1)<<32)*1000+1000000;
    observations[0]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=1000000U};
    observations[1]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=(uint64_t)clock_us+1U};
    observations[2]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=(uint64_t)clock_us,.source_age_ms=UINT32_MAX};
    observations[3]=(cerberus_s3_wifi_observation_t){.valid=true,.request_started_us=(uint64_t)clock_us};
    print_wifi_seen();has("wifi_seen_available=4\r\n");
    has("wifi_seen_recent=1\r\nwifi_seen_not_recent=2\r\nwifi_seen_clock_invalid=1\r\n");
}
static void failure_paths(void)
{
    for(unsigned phase=1U;phase<=2U;++phase){
        reset(51U,2U);fail_peer_call=phase;print_wifi_seen();no_counts();
        has(phase==1U?"wifi_seen_stage=peer_status\r\n":"wifi_seen_stage=peer_recheck\r\n");
        assert(peer_reads==phase);if(phase==1U)assert(!scan_reads&&!row_reads);
        reset(51U,2U);fail_scan_call=phase;print_wifi_seen();no_counts();
        has(phase==1U?"wifi_seen_stage=scan_status\r\n":"wifi_seen_stage=scan_recheck\r\n");
        assert(scan_reads==phase);if(phase==1U)assert(!row_reads);
    }
    for(unsigned state=0U;state<=CERBERUS_WIFI_SCAN_ERROR;++state){
        if(state==CERBERUS_WIFI_SCAN_COMPLETE)continue;
        reset(51U,2U);scan.state=(uint8_t)state;print_wifi_seen();no_counts();has("wifi_seen=not_complete\r\n");
        assert(!row_reads&&scan_reads==1U&&peer_reads==1U);
    }
    reset(51U,2U);scan.error_code=ESP_FAIL;print_wifi_seen();no_counts();has("wifi_seen=not_complete\r\n");
    reset(51U,33U);print_wifi_seen();no_counts();has("wifi_seen_stage=scan_bounds\r\n");assert(!row_reads);
    reset(51U,2U);scan.total_count=1U;print_wifi_seen();no_counts();has("wifi_seen_stage=scan_bounds\r\n");
    for(unsigned index=0;index<32U;++index){
        reset(51U,32U);failed_row=(int)index;print_wifi_seen();no_counts();
        has("wifi_seen_stage=result\r\n");assert(row_reads==index+1U&&scan_reads==1U&&peer_reads==1U);
    }
    for(unsigned defect=0U;defect<5U;++defect){
        reset(51U,2U);
        if(defect==0U)++after.generation;
        if(defect==1U)--after.stored_count;
        if(defect==2U)after.state=CERBERUS_WIFI_SCAN_RUNNING;
        if(defect==3U)after.error_code=ESP_FAIL;
        if(defect==4U)++after.total_count;
        print_wifi_seen();no_counts();has("wifi_seen_stage=snapshot_changed\r\n");
        assert(row_reads==2U&&scan_reads==2U&&peer_reads==1U);
    }
    for(unsigned defect=0U;defect<4U;++defect){
        reset(51U,2U);
        if(defect==0U)peer_after.firmware_version=50U;
        if(defect==1U)++peer_after.boot_count;
        if(defect==2U)++peer_after.reset_reason;
        if(defect==3U)peer_after.uptime_ms=peer.uptime_ms-1U;
        print_wifi_seen();no_counts();has("wifi_seen_stage=peer_changed\r\n");
    }
    reset(51U,1U);clock_us=-1;print_wifi_seen();no_counts();has("wifi_seen_stage=clock\r\n");
}
int main(void)
{
    success_and_dispatch();final_age();failure_paths();
    puts("Production wifi_seen console: bounded read-only dispatch, fresh peer gates, coherent completed snapshots, common-time age counts, empty/legacy/expired/future cases, fault suppression and no identifiers passed.");
    return 0;
}
