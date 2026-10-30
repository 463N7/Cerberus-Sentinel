#pragma once
/* Small resident report buffers retain their existing bound. Multi-AP reports
 * use a larger temporary PSRAM buffer, without enlarging those static arrays. */
#define CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES 4096
#define CERBERUS_STORAGE_WIFI_REPORT_MAX_BYTES (256U * 1024U)
