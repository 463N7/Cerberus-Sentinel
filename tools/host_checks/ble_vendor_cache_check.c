#undef NDEBUG
#include "esp_err.h"
#include "recon_db.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
typedef struct { uint8_t address[6],address_type; } selected_result_t;
static selected_result_t ble_selected_result;
static struct { uint8_t bssid[6]; } wifi_selected_result;
static bool use_ble = true;
static uint32_t media_epoch;
static int64_t clock_us;
static unsigned epoch_calls,lookup_calls,epoch_fail_at;
static unsigned epoch_releases;
static esp_err_t epoch_error;
static recon_db_result_t lookup_result;
static const char *media_vendor;
static bool epoch_change_in_lookup,ownership_change_in_lookup;
static uint8_t lookup_address[6];
static char rendered_label[72];
static unsigned rendered_y,draw_calls;
static esp_err_t draw_error;
typedef int *SemaphoreHandle_t;
static int storage_mutex_object;
static SemaphoreHandle_t storage_mutex=&storage_mutex_object;
static bool sd_mounted,storage_locked;
enum { CERBERUS_STORAGE_OWNER_NONE,CERBERUS_STORAGE_OWNER_FIRMWARE,CERBERUS_STORAGE_OWNER_USB };
static unsigned storage_owner;
#define sd_read_epoch media_epoch
#define pdTRUE 1

static int64_t esp_timer_get_time(void) { return clock_us; }
static int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned wait_ticks)
{
    CHECK(mutex==&storage_mutex_object&&wait_ticks==0U);
    ++epoch_calls;
    if(storage_locked||epoch_error==ESP_ERR_TIMEOUT||epoch_calls==epoch_fail_at)return 0;
    storage_locked=true;
    storage_owner=epoch_error==ESP_ERR_INVALID_STATE?CERBERUS_STORAGE_OWNER_USB:CERBERUS_STORAGE_OWNER_FIRMWARE;
    return pdTRUE;
}
static void release_storage_mutex(void)
{CHECK(storage_locked);storage_locked=false;++epoch_releases;}
#include "storage_try_epoch_impl.h"
static recon_db_result_t recon_store_vendor(const uint8_t address[6],
    char name[RECON_DB_VENDOR_SIZE])
{
    ++lookup_calls;memcpy(lookup_address,address,6);
    CHECK(!(address[0]&3U));
    /* Return dirty text even for failure to prove the UI cannot retain it. */
    snprintf(name,RECON_DB_VENDOR_SIZE,"%s",media_vendor);
    if(epoch_change_in_lookup)++media_epoch;
    if(ownership_change_in_lookup)epoch_error=ESP_ERR_INVALID_STATE;
    return lookup_result;
}
#define UI_MUTED 0x84b5U
#define TAG "vendor-fixture"
#define ESP_RETURN_ON_ERROR(call,tag,message) do { esp_err_t e=(call);if(e!=ESP_OK)return e; } while(0)
static esp_err_t draw_centered(unsigned y,const char *line,unsigned color,unsigned scale)
{
    CHECK(color==UI_MUTED&&scale==1U);++draw_calls;rendered_y=y;
    snprintf(rendered_label,sizeof(rendered_label),"%s",line);
    return draw_error;
}
#include "ble_vendor_cache_impl.h"
static void refresh_vendor(void)
{
    CHECK((use_ble?render_ble_vendor():render_wifi_vendor())==ESP_OK);
    CHECK(rendered_y==(use_ble?174U:354U));
}

typedef struct {
    bool valid;uint8_t address[6],address_type;int64_t checked_at_ms;
    recon_db_result_t result;uint32_t storage_epoch;char name[RECON_DB_VENDOR_SIZE];
} previous_vendor_cache_t;

static void reset(void)
{
    memset(&vendor_cache,0,sizeof(vendor_cache));
    ble_selected_result=(selected_result_t){.address={0x21,0x43,0x65,0x87,0x09,0x00},.address_type=0};
    const uint8_t wifi[6]={0x00,0x09,0x87,0x65,0x43,0x21};memcpy(wifi_selected_result.bssid,wifi,6);
    media_epoch=5;clock_us=1000000;epoch_calls=lookup_calls=epoch_fail_at=0;
    epoch_releases=0;storage_mutex=&storage_mutex_object;sd_mounted=true;storage_locked=false;
    storage_owner=CERBERUS_STORAGE_OWNER_FIRMWARE;
    epoch_error=ESP_OK;lookup_result=RECON_DB_FOUND;media_vendor="Vendor A";
    epoch_change_in_lookup=ownership_change_in_lookup=false;
    rendered_label[0]='\0';rendered_y=draw_calls=0;draw_error=ESP_OK;
}
static void found(const char *name);
static void unavailable(void);
static void zero_wait_epoch_contract(void)
{
    reset();uint32_t epoch=123;
    CHECK(cerberus_storage_try_get_read_epoch(NULL)==ESP_ERR_INVALID_ARG&&!epoch_calls&&!epoch_releases);
    storage_mutex=NULL;
    CHECK(cerberus_storage_try_get_read_epoch(&epoch)==ESP_ERR_INVALID_STATE&&!epoch&&!epoch_calls);
    storage_mutex=&storage_mutex_object;sd_mounted=false;epoch=123;
    CHECK(cerberus_storage_try_get_read_epoch(&epoch)==ESP_ERR_INVALID_STATE&&!epoch&&epoch_calls==1&&epoch_releases==1);
    sd_mounted=true;storage_locked=true;epoch=123;
    CHECK(cerberus_storage_try_get_read_epoch(&epoch)==ESP_ERR_TIMEOUT&&!epoch&&storage_locked&&epoch_releases==1);
    storage_locked=false;epoch_error=ESP_ERR_INVALID_STATE;epoch=123;
    CHECK(cerberus_storage_try_get_read_epoch(&epoch)==ESP_ERR_INVALID_STATE&&!epoch&&!storage_locked&&epoch_releases==2);
    epoch_error=ESP_OK;
    CHECK(cerberus_storage_try_get_read_epoch(&epoch)==ESP_OK&&epoch==5&&!storage_locked&&epoch_releases==3);
    /* A held filesystem lock invalidates a warm UI hit immediately without
     * calling the slower vendor lookup or releasing another owner's lock. */
    refresh_vendor();found("Vendor A");unsigned reads=lookup_calls,releases=epoch_releases;
    storage_locked=true;refresh_vendor();unavailable();
    CHECK(lookup_calls==reads&&storage_locked&&epoch_releases==releases);
    storage_locked=false;refresh_vendor();found("Vendor A");CHECK(lookup_calls==reads+1);
}
static void found(const char *name)
{
    CHECK(vendor_cache.valid&&vendor_cache.result==RECON_DB_FOUND&&!strcmp(vendor_cache.name,name));
    CHECK(vendor_cache.storage_epoch==media_epoch);
    uint8_t expected[6];
    if(use_ble)CHECK(recon_db_ble_public_mac(ble_selected_result.address,ble_selected_result.address_type,expected));
    else memcpy(expected,wifi_selected_result.bssid,6);
    CHECK(!memcmp(lookup_address,expected,6));
    CHECK(!memcmp(vendor_cache.address,use_ble?ble_selected_result.address:wifi_selected_result.bssid,6));
    CHECK(vendor_cache.source==(use_ble?ble_selected_result.address_type:4U));
    char label[48];snprintf(label,sizeof(label),"VENDOR %.40s",name);CHECK(!strcmp(rendered_label,label));
}
static void unavailable(void)
{
    CHECK(!vendor_cache.valid&&vendor_cache.result==RECON_DB_UNAVAILABLE&&!vendor_cache.name[0]);
    CHECK(!strcmp(rendered_label,use_ble?"SD VENDOR DATA UNAVAILABLE":"SD VENDOR DATABASE NOT READY"));
}
static void healthy_cache_and_expiry(void)
{
    reset();refresh_vendor();found("Vendor A");CHECK(lookup_calls==1&&epoch_calls==2);
    const int64_t admitted=vendor_cache.checked_at_ms;
    for(unsigned i=0;i<1000;++i){clock_us+=1000;refresh_vendor();found("Vendor A");}
    CHECK(lookup_calls==1&&epoch_calls==1002&&vendor_cache.checked_at_ms==admitted);
    clock_us=30999999;refresh_vendor();CHECK(lookup_calls==1); /* 29,999 ms since lookup. */
    clock_us=31000000;media_vendor="Vendor B";refresh_vendor();found("Vendor B");CHECK(lookup_calls==2);
    /* Same epoch updates remain bounded by the original 30-second TTL. */
    media_vendor="Vendor C";clock_us++;refresh_vendor();found("Vendor B");CHECK(lookup_calls==2);
    clock_us=61000000;refresh_vendor();found("Vendor C");CHECK(lookup_calls==3);
    if(use_ble)ble_selected_result.address[0]++;else wifi_selected_result.bssid[5]++;
    refresh_vendor();found("Vendor C");CHECK(lookup_calls==4);
    if(use_ble){ble_selected_result.address_type=2;refresh_vendor();found("Vendor C");CHECK(lookup_calls==5);}
    reset();lookup_result=RECON_DB_UNKNOWN;refresh_vendor();
    CHECK(vendor_cache.valid&&vendor_cache.result==RECON_DB_UNKNOWN&&!vendor_cache.name[0]);
    CHECK(!strcmp(rendered_label,use_ble?"VENDOR UNKNOWN":"VENDOR NOT IN SD DATABASE"));
    for(unsigned i=0;i<100;++i)refresh_vendor();CHECK(lookup_calls==1&&epoch_calls==102);
}
static void remount_and_ownership(void)
{
    reset();refresh_vendor();found("Vendor A");
    ++media_epoch;media_vendor="Same address, new card";refresh_vendor();found(media_vendor);CHECK(lookup_calls==2);
    epoch_error=ESP_ERR_INVALID_STATE;refresh_vendor();unavailable();CHECK(lookup_calls==2);
    for(unsigned i=0;i<3;++i){refresh_vendor();unavailable();CHECK(lookup_calls==2);}
    epoch_error=ESP_OK;++media_epoch;media_vendor="Returned from USB";refresh_vendor();found(media_vendor);CHECK(lookup_calls==3);
    /* Even a transient unavailable check with unchanged epoch invalidates. */
    epoch_error=ESP_ERR_TIMEOUT;refresh_vendor();unavailable();epoch_error=ESP_OK;
    refresh_vendor();found(media_vendor);CHECK(lookup_calls==4);
    media_epoch=0;refresh_vendor();unavailable();CHECK(lookup_calls==4);
    media_epoch=9;refresh_vendor();found(media_vendor);CHECK(lookup_calls==5);
    reset();lookup_result=RECON_DB_UNKNOWN;refresh_vendor();CHECK(vendor_cache.valid);
    ++media_epoch;lookup_result=RECON_DB_FOUND;media_vendor="New assignment";
    refresh_vendor();found(media_vendor);CHECK(lookup_calls==2);
}
static void transient_lookup_and_midread_change(void)
{
    const recon_db_result_t failures[]={RECON_DB_UNAVAILABLE,RECON_DB_INVALID};
    for(unsigned i=0;i<sizeof(failures)/sizeof(failures[0]);++i){
        reset();lookup_result=failures[i];refresh_vendor();
        CHECK(!vendor_cache.valid&&!vendor_cache.name[0]&&lookup_calls==1&&epoch_calls==1);
        lookup_result=RECON_DB_FOUND;refresh_vendor();found("Vendor A");CHECK(lookup_calls==2&&epoch_calls==3);
        /* A failed refresh must not fall back to the previous healthy label. */
        clock_us+=30000000;lookup_result=failures[i];media_vendor="Dirty failure";refresh_vendor();
        CHECK(!vendor_cache.valid&&!vendor_cache.name[0]);lookup_result=RECON_DB_FOUND;
        media_vendor="Recovered";refresh_vendor();found("Recovered");CHECK(lookup_calls==4);
    }
    for(unsigned unknown=0;unknown<2;++unknown){
        reset();if(unknown)lookup_result=RECON_DB_UNKNOWN;
        epoch_change_in_lookup=true;refresh_vendor();unavailable();CHECK(epoch_calls==2&&lookup_calls==1);
        epoch_change_in_lookup=false;lookup_result=RECON_DB_FOUND;refresh_vendor();found("Vendor A");CHECK(lookup_calls==2);
        reset();if(unknown)lookup_result=RECON_DB_UNKNOWN;
        ownership_change_in_lookup=true;refresh_vendor();unavailable();
        ownership_change_in_lookup=false;epoch_error=ESP_OK;lookup_result=RECON_DB_FOUND;
        refresh_vendor();found("Vendor A");CHECK(lookup_calls==2);
        reset();if(unknown)lookup_result=RECON_DB_UNKNOWN;
        epoch_fail_at=2;refresh_vendor();unavailable();
        epoch_fail_at=0;lookup_result=RECON_DB_FOUND;refresh_vendor();found("Vendor A");CHECK(lookup_calls==2);
    }
}
static void address_rules_and_clock(void)
{
    use_ble=true;
    for(unsigned type=0;type<256;++type)for(unsigned prefix=0;prefix<4;++prefix){
        reset();ble_selected_result.address_type=(uint8_t)type;ble_selected_result.address[5]=(uint8_t)prefix;
        refresh_vendor();
        bool public_global=(type==0||type==2)&&prefix==0;
        if(public_global){found("Vendor A");CHECK(lookup_calls==1&&epoch_calls==2);}
        else CHECK(!lookup_calls&&!epoch_calls&&!vendor_cache.valid&&!vendor_cache.name[0]&&
            vendor_cache.result==(type<=3?RECON_DB_PRIVATE_ADDRESS:RECON_DB_INVALID));
    }
    reset();refresh_vendor();ble_selected_result.address_type=1;epoch_error=ESP_ERR_INVALID_STATE;
    unsigned before=epoch_calls;refresh_vendor();CHECK(epoch_calls==before&&lookup_calls==1&&
        vendor_cache.result==RECON_DB_PRIVATE_ADDRESS&&!vendor_cache.name[0]);
    /* Valid public-prefix lookup uses reversed NimBLE bytes, not address[0]. */
    reset();ble_selected_result.address[0]=0xff;refresh_vendor();found("Vendor A");
    reset();refresh_vendor();clock_us=999000;media_vendor="After rewind";refresh_vendor();found(media_vendor);CHECK(lookup_calls==2);
    reset();clock_us=-1000;refresh_vendor();CHECK(!vendor_cache.valid&&vendor_cache.result==RECON_DB_FOUND);
    clock_us=0;refresh_vendor();found("Vendor A");CHECK(lookup_calls==2&&vendor_cache.checked_at_ms==0);
    reset();clock_us=INT64_MAX;refresh_vendor();found("Vendor A");refresh_vendor();CHECK(lookup_calls==1);
    /* The transport key replaces the old type byte; there is one shared bank. */
    CHECK(sizeof(vendor_cache)==sizeof(previous_vendor_cache_t));
}
static void both_callers_share_one_typed_cache(void)
{
    /* The default Wi-Fi address and BLE wire address normalize to the same
     * MAC. Crossing the radio boundary still requires a new lookup. */
    use_ble=false;reset();refresh_vendor();found("Vendor A");
    use_ble=true;media_vendor="BLE public";refresh_vendor();found(media_vendor);CHECK(lookup_calls==2);
    ble_selected_result.address_type=2;media_vendor="BLE public identity";
    refresh_vendor();found(media_vendor);CHECK(lookup_calls==3);
    use_ble=false;media_vendor="Wi-Fi returned";refresh_vendor();found(media_vendor);CHECK(lookup_calls==4);
    const int64_t admitted=vendor_cache.checked_at_ms;
    refresh_vendor();found(media_vendor);CHECK(lookup_calls==4&&vendor_cache.checked_at_ms==admitted);

    /* Equal wire bytes are also distinct. Both endpoints have globally
     * assigned prefixes, so a private-address early return cannot mask it. */
    const uint8_t equal_wire[6]={0x10,0x21,0x32,0x43,0x54,0x20};
    reset();memcpy(wifi_selected_result.bssid,equal_wire,6);
    memcpy(ble_selected_result.address,equal_wire,6);
    refresh_vendor();found("Vendor A");use_ble=true;media_vendor="Reversed prefix";
    refresh_vendor();found(media_vendor);CHECK(lookup_calls==2);
    ble_selected_result.address_type=2;refresh_vendor();found(media_vendor);CHECK(lookup_calls==3);
    use_ble=false;refresh_vendor();found(media_vendor);CHECK(lookup_calls==4);

    /* BLE type4 must not alias the internal Wi-Fi key4. Invalid selection
     * clears the old label without touching the card or invoking lookup. */
    use_ble=true;ble_selected_result.address_type=4;
    unsigned epochs=epoch_calls;refresh_vendor();
    CHECK(vendor_cache.result==RECON_DB_INVALID&&!vendor_cache.valid&&!vendor_cache.name[0]);
    CHECK(lookup_calls==4&&epoch_calls==epochs&&!strcmp(rendered_label,"SD VENDOR DATA UNAVAILABLE"));
    use_ble=false;refresh_vendor();found(media_vendor);CHECK(lookup_calls==5);

    /* A failure on the newly selected transport cannot inherit the old label. */
    use_ble=true;ble_selected_result.address_type=0;lookup_result=RECON_DB_UNAVAILABLE;
    media_vendor="Dirty different device";refresh_vendor();unavailable();CHECK(lookup_calls==6);
    lookup_result=RECON_DB_FOUND;refresh_vendor();found(media_vendor);CHECK(lookup_calls==7);
    draw_error=ESP_ERR_INVALID_STATE;epochs=epoch_calls;
    CHECK(render_ble_vendor()==ESP_ERR_INVALID_STATE&&lookup_calls==7&&epoch_calls==epochs+1);
    draw_error=ESP_OK;refresh_vendor();found(media_vendor);CHECK(lookup_calls==7);

    /* The copied cache is bounded and owns the vendor text returned by lookup. */
    static const char long_vendor[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for(unsigned radio=0;radio<2;++radio){
        use_ble=radio!=0;reset();media_vendor=long_vendor;refresh_vendor();
        CHECK(strlen(vendor_cache.name)==RECON_DB_VENDOR_SIZE-1U&&strlen(rendered_label)==47U);
        media_vendor="Changed elsewhere";refresh_vendor();CHECK(lookup_calls==1&&strlen(rendered_label)==47U);
        draw_error=ESP_ERR_INVALID_STATE;
        CHECK((use_ble?render_ble_vendor():render_wifi_vendor())==ESP_ERR_INVALID_STATE&&lookup_calls==1);
    }
}
static void wifi_address_rules_and_clocks(void)
{
    use_ble=false;
    for(unsigned prefix=0;prefix<256;++prefix){
        reset();wifi_selected_result.bssid[0]=(uint8_t)prefix;refresh_vendor();
        if(!(prefix&3U)){found("Vendor A");CHECK(lookup_calls==1&&epoch_calls==2);}
        else CHECK(!lookup_calls&&!epoch_calls&&!vendor_cache.valid&&!vendor_cache.name[0]&&
            vendor_cache.result==RECON_DB_PRIVATE_ADDRESS&&
            !strcmp(rendered_label,"LOCAL ADDRESS / NO VENDOR ATTRIBUTION"));
    }
    reset();refresh_vendor();wifi_selected_result.bssid[0]|=2U;epoch_error=ESP_ERR_INVALID_STATE;
    unsigned before=epoch_calls;refresh_vendor();
    CHECK(epoch_calls==before&&lookup_calls==1&&vendor_cache.result==RECON_DB_PRIVATE_ADDRESS&&!vendor_cache.name[0]);
    reset();refresh_vendor();clock_us=999000;media_vendor="After rewind";refresh_vendor();found(media_vendor);CHECK(lookup_calls==2);
    reset();clock_us=-1000;refresh_vendor();CHECK(!vendor_cache.valid&&vendor_cache.result==RECON_DB_FOUND);
    clock_us=0;refresh_vendor();found("Vendor A");CHECK(lookup_calls==2&&vendor_cache.checked_at_ms==0);
    reset();clock_us=INT64_MAX;refresh_vendor();found("Vendor A");refresh_vendor();CHECK(lookup_calls==1);
    /* Null identity never reaches the actual epoch guard/lookup. */
    reset();refresh_selected_vendor(false,NULL,0,1000);
    CHECK(vendor_cache.result==RECON_DB_INVALID&&!vendor_cache.valid&&!epoch_calls&&!lookup_calls);
    refresh_selected_vendor(true,NULL,0,1000);
    CHECK(vendor_cache.result==RECON_DB_INVALID&&!vendor_cache.valid&&!epoch_calls&&!lookup_calls);
}
int main(void)
{
    for(unsigned radio=0;radio<2;++radio){
        use_ble=radio!=0;healthy_cache_and_expiry();remount_and_ownership();
        transient_lookup_and_midread_change();zero_wait_epoch_contract();
    }
    address_rules_and_clock();wifi_address_rules_and_clocks();both_callers_share_one_typed_cache();
    printf("Production Wi-Fi/BLE selected vendor UI cache: %u checks; shared cache=%zu previous BLE-only=%zu bytes; both actual callers' stable hits issue one zero-wait epoch check and zero vendor lookups.\n",
        checks,sizeof(vendor_cache),sizeof(previous_vendor_cache_t));
    return 0;
}
