#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "s3_ble_connection_ui.h"
#include "s3_ble_central.h"

#define UI_CAP_BLE_GATT 1U
#define UI_MUTED 2U
#define UI_BAD 3U
#define UI_GOOD 4U
static unsigned ui_capability_map;
static bool ble_connection_checked, ble_service_checked, ble_characteristic_checked, ble_read_checked;
static esp_err_t ble_connection_error;
static cerberus_ble_connection_status_payload_t ble_connection_status, actual;
static cerberus_c5_ble_result_t ble_selected_result;
static unsigned connects, closes, refreshes;
static esp_err_t refresh_error, command_error;
static char message[64];
static esp_err_t refresh_ble_connection_status(void)
{
    ++refreshes; ble_connection_checked=true; ble_connection_error=refresh_error;
    ble_connection_status=actual; return refresh_error;
}
static bool ble_connection_is_active(void)
{
    return ble_connection_status.state==CERBERUS_BLE_CONNECTION_CONNECTING ||
        ble_connection_status.state==CERBERUS_BLE_CONNECTION_CONNECTED ||
        ble_connection_status.state==CERBERUS_BLE_CONNECTION_DISCONNECTING;
}
static bool ble_connection_matches_selected(void)
{
    return ble_connection_status.address_type==ble_selected_result.address_type &&
        !memcmp(ble_connection_status.address,ble_selected_result.address,6);
}
static void set_ble_action_message(const char *value,unsigned color)
{ (void)color; snprintf(message,sizeof(message),"%s",value); }
esp_err_t cerberus_s3_ble_central_disconnect(cerberus_ble_connection_status_payload_t *out)
{
    ++closes;
    assert(ble_connection_matches_selected());
    if(command_error==ESP_OK){actual.state=CERBERUS_BLE_CONNECTION_DISCONNECTING;actual.error_code=0;}
    *out=actual;return command_error;
}
esp_err_t cerberus_s3_ble_central_connect(const cerberus_c5_ble_result_t *peer,
    cerberus_ble_connection_status_payload_t *out)
{
    ++connects;assert(peer==&ble_selected_result&&!ble_connection_is_active());
    if(command_error==ESP_OK)actual.state=CERBERUS_BLE_CONNECTION_CONNECTING;
    *out=actual;return command_error;
}
#include "ble_connection_control_impl.h"

static char output[1024];
static size_t output_length;
static unsigned diagnostic_reads;
static esp_err_t diagnostic_error;
static cerberus_s3_ble_central_diagnostics_t diagnostic;
esp_err_t cerberus_s3_ble_central_get_diagnostics(cerberus_s3_ble_central_diagnostics_t *out)
{ ++diagnostic_reads;*out=diagnostic;return diagnostic_error; }
static const char *esp_err_to_name(esp_err_t error){return error==ESP_OK?"ESP_OK":"FAILED";}
static void console_printf(const char *format,...)
{
    va_list args;va_start(args,format);
    int n=vsnprintf(output+output_length,sizeof(output)-output_length,format,args);
    va_end(args);assert(n>=0&&(size_t)n<sizeof(output)-output_length);output_length+=(size_t)n;
}
#include "ble_central_console_impl.h"
static cerberus_ble_scan_status_payload_t inventory_status;
static uint16_t inventory_replacements;
static unsigned inventory_reads;
static esp_err_t inventory_error;
esp_err_t cerberus_s3_ble_scanner_get_inventory_status(uint8_t generation,
    cerberus_ble_scan_status_payload_t *status,uint16_t *replacements)
{
    ++inventory_reads;assert(generation==inventory_status.generation);
    *status=inventory_status;*replacements=inventory_replacements;return inventory_error;
}
#include "ble_inventory_console_impl.h"
static void reset(uint8_t state,int error)
{
    ui_capability_map=UI_CAP_BLE_GATT;ble_connection_checked=true;
    ble_service_checked=ble_characteristic_checked=ble_read_checked=true;
    connects=closes=refreshes=0;refresh_error=command_error=ble_connection_error=ESP_OK;
    message[0]=0;
    ble_selected_result=(cerberus_c5_ble_result_t){.address={1,2,3,4,5,6},.address_type=1,
        .flags=CERBERUS_BLE_RESULT_CONNECTABLE};
    actual=(cerberus_ble_connection_status_payload_t){.state=state,.error_code=error,.address_type=1};
    memcpy(actual.address,ble_selected_result.address,6);ble_connection_status=actual;
}
int main(void)
{
    const uint8_t states[]={CERBERUS_BLE_CONNECTION_CONNECTING,CERBERUS_BLE_CONNECTION_CONNECTED,
        CERBERUS_BLE_CONNECTION_DISCONNECTING};
    const char *labels[]={"CANCEL","DISCONNECT","RETRY CLOSE"};
    for(unsigned i=0;i<3;++i){
        reset(states[i],i==2?13:0);ble_selected_result.flags=0;
        assert(!strcmp(ble_connection_control().label,labels[i]));
        ble_handle_connection_action();assert(closes==1&&!connects&&refreshes==1);
        assert(actual.state==CERBERUS_BLE_CONNECTION_DISCONNECTING);
        assert(!ble_service_checked&&!ble_characteristic_checked&&!ble_read_checked);
        assert(!strcmp(ble_connection_control().label,"CLOSING"));
        ble_handle_connection_action();assert(closes==1&&!connects);
    }
    for(unsigned i=0;i<3;++i)for(unsigned byte=0;byte<7;++byte){
        reset(states[i],13);
        if(byte<6)++actual.address[byte];else actual.address_type=0;
        /* The displayed status is stale; tap must re-read before acting. */
        ble_handle_connection_action();assert(!closes&&!connects&&refreshes==1);
        assert(!strcmp(ble_connection_control().label,"IN USE"));
    }
    reset(CERBERUS_BLE_CONNECTION_CONNECTING,0);actual.state=CERBERUS_BLE_CONNECTION_CONNECTED;
    ble_handle_connection_action();assert(closes==1&&!strcmp(message,"DISCONNECT REQUESTED"));
    reset(CERBERUS_BLE_CONNECTION_DISCONNECTING,13);command_error=ESP_FAIL;
    ble_handle_connection_action();assert(closes==1&&!strcmp(message,"CLOSE COMMAND FAILED"));
    assert(ble_connection_status.state==CERBERUS_BLE_CONNECTION_DISCONNECTING);
    reset(CERBERUS_BLE_CONNECTION_CONNECTED,0);refresh_error=ESP_FAIL;
    ble_handle_connection_action();assert(!closes&&!connects);
    reset(CERBERUS_BLE_CONNECTION_IDLE,0);ui_capability_map=0;
    ble_handle_connection_action();assert(!closes&&!connects&&!refreshes);
    reset(CERBERUS_BLE_CONNECTION_IDLE,0);ble_handle_connection_action();assert(connects==1&&!closes);
    reset(CERBERUS_BLE_CONNECTION_IDLE,0);ble_selected_result.flags=0;
    ble_handle_connection_action();assert(!connects&&!closes);
    reset(255,0);ble_handle_connection_action();assert(!connects&&!closes);
    assert(!strcmp(message,"CONNECTION STATUS UNAVAILABLE"));
    for(unsigned state=0;state<256;++state)for(unsigned type=0;type<5;++type){
        reset((uint8_t)state,13);ble_selected_result.address_type=(uint8_t)type;
        cerberus_ble_link_control_t control=ble_connection_control();
        assert(control.label&&strlen(control.label)<=11);
        if(type!=1&&state>=CERBERUS_BLE_CONNECTION_CONNECTING&&state<=CERBERUS_BLE_CONNECTION_DISCONNECTING)
            assert(control.action==CERBERUS_BLE_LINK_NONE);
    }
    diagnostic=(cerberus_s3_ble_central_diagnostics_t){.state=3,.generation=255,.host_owner=3,
        .host_epoch=UINT32_MAX,.handle_present=true,.error_code=-123,.service_state=1,
        .characteristic_state=2,.read_state=3};
    print_ble_central();assert(diagnostic_reads==1);
    assert(strstr(output,"host_epoch=4294967295\r\n")&&strstr(output,"error=-123\r\n"));
    assert(strstr(output,"handle_present=1\r\n")&&!strstr(output,"address="));
    output_length=0;output[0]=0;diagnostic_error=ESP_FAIL;print_ble_central();
    assert(diagnostic_reads==2&&strstr(output,"transport=FAILED")&&!strstr(output,"state="));
    output_length=0;output[0]=0;
    inventory_status=(cerberus_ble_scan_status_payload_t){.state=CERBERUS_BLE_SCAN_RUNNING};
    print_ble_inventory(&inventory_status);assert(!inventory_reads&&strstr(output,"inventory=not_complete"));
    output_length=0;output[0]=0;
    inventory_status=(cerberus_ble_scan_status_payload_t){.state=CERBERUS_BLE_SCAN_COMPLETE,
        .generation=255,.stored_count=32,.dropped_reports=65535};inventory_replacements=65535;
    print_ble_inventory(&inventory_status);assert(inventory_reads==1);
    assert(strstr(output,"inventory_replacements=65535\r\n")&&strstr(output,"inventory_generation=255\r\n"));
    assert(strstr(output,"inventory_rejected_reports=65535\r\n")&&!strstr(output,"address="));
    output_length=0;output[0]=0;inventory_error=ESP_FAIL;
    print_ble_inventory(&inventory_status);assert(inventory_reads==2&&strstr(output,"inventory_transport=FAILED"));
    assert(!strstr(output,"inventory_replacements="));
    puts("BLE connection controls: exact-peer Cancel/Retry, fresh tap state, asynchronous ownership and aggregate diagnostics pass");
    return 0;
}
