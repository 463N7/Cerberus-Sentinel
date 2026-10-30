#include "s3_wifi_owner.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <stdatomic.h>
#include <stdbool.h>
static atomic_int owner;
static bool netif_ready,event_ready;
cerberus_s3_wifi_owner_t cerberus_s3_wifi_owner(void){return atomic_load(&owner);}
esp_err_t cerberus_s3_wifi_claim(cerberus_s3_wifi_owner_t value)
{
    if(value!=CERBERUS_S3_WIFI_WEB&&value!=CERBERUS_S3_WIFI_RECEIVER&&value!=CERBERUS_S3_WIFI_PORTAL)return ESP_ERR_INVALID_ARG;
    int expected=CERBERUS_S3_WIFI_NONE;
    return atomic_compare_exchange_strong(&owner,&expected,value)?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t cerberus_s3_wifi_release(cerberus_s3_wifi_owner_t value)
{
    if(value!=CERBERUS_S3_WIFI_WEB&&value!=CERBERUS_S3_WIFI_RECEIVER&&value!=CERBERUS_S3_WIFI_PORTAL)return ESP_ERR_INVALID_ARG;
    int expected=value;
    return atomic_compare_exchange_strong(&owner,&expected,CERBERUS_S3_WIFI_NONE)?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t cerberus_s3_wifi_prepare(cerberus_s3_wifi_owner_t value)
{
    if(value==CERBERUS_S3_WIFI_NONE||cerberus_s3_wifi_owner()!=value)return ESP_ERR_INVALID_STATE;
    /* One claimed owner initializes these shared platform facilities at a time.
     * They survive driver deinit and are never torn down beneath another user. */
    if(!netif_ready){esp_err_t error=esp_netif_init();if(error!=ESP_OK)return error;netif_ready=true;}
    if(!event_ready){esp_err_t error=esp_event_loop_create_default();
        if(error!=ESP_OK&&error!=ESP_ERR_INVALID_STATE)return error;
        event_ready=true;
    }
    return ESP_OK;
}
