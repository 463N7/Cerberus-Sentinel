#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "usb_mass_storage.h"
#include "storage.h"
#include "tinyusb.h"
#include "tinyusb_msc.h"
static bool console_on=true,usb_on,msc_on,medium_on,card_on,mounted=true;
static int owner=CERBERUS_STORAGE_OWNER_FIRMWARE;
static bool fail_mount,fail_delete,fail_install;
static unsigned detach_ms;
static sdmmc_card_t card;
static tinyusb_msc_driver_config_t msc_config;
esp_err_t cerberus_usb_console_start(void){assert(!usb_on);console_on=true;return ESP_OK;}
esp_err_t cerberus_usb_console_stop(void){console_on=false;return ESP_OK;}
cerberus_storage_owner_t cerberus_storage_get_owner(void){return owner;}
esp_err_t cerberus_storage_reserve_for_usb(void){assert(!usb_on);owner=CERBERUS_STORAGE_OWNER_USB;mounted=false;return ESP_OK;}
esp_err_t cerberus_storage_acquire_usb_card(sdmmc_card_t **out){assert(owner==CERBERUS_STORAGE_OWNER_USB);card_on=true;*out=&card;return ESP_OK;}
esp_err_t cerberus_storage_release_usb_card(void){assert(!medium_on&&!usb_on);card_on=false;return ESP_OK;}
esp_err_t cerberus_storage_release_from_usb(void){assert(!card_on);owner=CERBERUS_STORAGE_OWNER_NONE;return ESP_OK;}
esp_err_t cerberus_storage_mount(void){assert(owner!=CERBERUS_STORAGE_OWNER_USB);if(fail_mount)return ESP_FAIL;mounted=true;owner=CERBERUS_STORAGE_OWNER_FIRMWARE;return ESP_OK;}
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *c){assert(!msc_on);msc_on=true;msc_config=*c;return ESP_OK;}
esp_err_t tinyusb_msc_uninstall_driver(void){assert(msc_on&&!medium_on);msc_on=false;return ESP_OK;}
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *c,tinyusb_msc_storage_handle_t *h){assert(card_on&&msc_on&&c->medium.card==&card);assert(c->fat_fs.do_not_format&&!c->fat_fs.config.format_if_mount_failed);medium_on=true;*h=&card;return ESP_OK;}
esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t h){assert(h==&card&&medium_on&&!usb_on);if(fail_delete)return ESP_FAIL;medium_on=false;return ESP_OK;}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *c){assert(!console_on&&medium_on);if(fail_install)return ESP_FAIL;usb_on=true;detach_ms=0;tinyusb_event_t event={.id=TINYUSB_EVENT_ATTACHED};c->event_cb(&event,NULL);return ESP_OK;}
esp_err_t tinyusb_driver_uninstall(void){assert(usb_on&&detach_ms>=250);usb_on=false;return ESP_OK;}
bool tud_disconnect(void){assert(usb_on);return true;}
void vTaskDelay(unsigned ms){detach_ms+=ms;}
static void eject(void){tinyusb_msc_event_t event={TINYUSB_MSC_EVENT_MOUNT_COMPLETE,TINYUSB_MSC_STORAGE_MOUNT_APP};msc_config.callback(&card,&event,NULL);}
static cerberus_usb_mass_storage_status_t status(void){cerberus_usb_mass_storage_status_t s;cerberus_usb_mass_storage_get_status(&s);return s;}
int main(void)
{
    assert(cerberus_usb_mass_storage_start()==ESP_OK);
    assert(status().active&&status().host_attached&&!status().safe_to_release);
    assert(cerberus_usb_mass_storage_stop()==ESP_ERR_INVALID_STATE&&usb_on);
    eject();assert(cerberus_usb_mass_storage_stop()==ESP_OK);
    assert(console_on&&mounted&&!status().active&&!usb_on&&!msc_on&&!card_on);
    assert(cerberus_usb_mass_storage_start()==ESP_OK);eject();fail_mount=true;
    assert(cerberus_usb_mass_storage_stop()==ESP_FAIL);
    assert(console_on&&!status().active&&status().last_error==ESP_FAIL&&!usb_on);
    fail_mount=false;assert(cerberus_storage_mount()==ESP_OK);
    assert(cerberus_usb_mass_storage_start()==ESP_OK);eject();fail_delete=true;
    assert(cerberus_usb_mass_storage_stop()==ESP_FAIL&&status().active&&console_on&&!usb_on);
    fail_delete=false;assert(cerberus_usb_mass_storage_stop()==ESP_OK&&!status().active);
    fail_install=true;
    assert(cerberus_usb_mass_storage_start()==ESP_FAIL&&console_on&&!msc_on&&!medium_on&&mounted);
    fail_install=false;
    for(unsigned i=0;i<25;++i){assert(cerberus_usb_mass_storage_start()==ESP_OK);eject();assert(cerberus_usb_mass_storage_stop()==ESP_OK);}
    puts("USB lifecycle checks passed: attach race, eject requirement, mount failure, cleanup retry, install failure and 25 round trips.");
}
