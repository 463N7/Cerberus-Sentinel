#include "recovery.h"

#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

bool cerberus_recovery_download_mode_available(void)
{
    return !esp_efuse_read_field_bit(ESP_EFUSE_DIS_FORCE_DOWNLOAD) &&
        !esp_efuse_read_field_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE) &&
        !esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE);
}

esp_err_t cerberus_recovery_enter_download_mode(void)
{
    if (!cerberus_recovery_download_mode_available()) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* This is the same RTC force-download mechanism used by Espressif's ROM
       USB console before a software reset. The ROM consumes the request and
       exposes its download loader; it does not write flash by itself. */
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
