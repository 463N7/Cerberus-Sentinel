#!/usr/bin/env python3
"""Fault-test the unchanged start/stop C functions with simulated ESP resources.

The two functions are extracted from web_ui.c so tests execute the production
ownership and cleanup code. This does not emulate Wi-Fi or HTTP traffic.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREAMBLE = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "web_ui.h"
typedef void *httpd_handle_t;
typedef void *esp_event_handler_instance_t;
typedef struct { int unused; } esp_netif_t;
typedef struct { int unused; } wifi_init_config_t;
typedef struct { struct { char ssid[32], password[64]; unsigned ssid_len, channel,
    max_connection, authmode; struct { bool required; } pmf_cfg; } ap; } wifi_config_t;
typedef struct { unsigned stack_size, max_uri_handlers; bool lru_purge_enable; } httpd_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define WEB_SESSION_COUNT 3
#define WIFI_EVENT 1
#define ESP_EVENT_ANY_ID 2
#define ESP_MAC_WIFI_SOFTAP 3
#define WIFI_MODE_AP 4
#define WIFI_IF_AP 5
#define WIFI_STORAGE_RAM 6
#define WIFI_AUTH_WPA2_PSK 7
#define HTTP_GET 8
#define HTTP_POST 9
#define ESP_ERR_WIFI_NOT_STARTED 0x3002
#define ESP_RETURN_ON_ERROR(call,tag,message) do { int rc=(call); if(rc) return rc; } while(0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static httpd_handle_t web_server;
static esp_netif_t *web_netif;
static bool web_netif_initialized, web_wifi_initialized, web_wifi_started, web_handler_registered;
static esp_event_handler_instance_t web_wifi_handler;
static cerberus_web_ui_status_t web_status;
static char web_sessions[72], password_salt[16], password_hash[32];
static unsigned attempt, fail_at, stop_fail_at, cleanup_attempt;
static bool live_netif, live_wifi, live_radio, live_handler, live_http;
static bool live_entropy;
static esp_netif_t netif;
static int step(void) { ++attempt; return attempt == fail_at ? ESP_FAIL : ESP_OK; }
static int cleanup_step(void) { ++cleanup_attempt; return cleanup_attempt == stop_fail_at ? ESP_FAIL : ESP_OK; }
static int load_or_create_configuration(void) {
    assert(live_entropy && !live_wifi && !live_radio);
    strcpy(web_status.access_point_password,"unique-test-password"); return step();
}
static void bootloader_random_enable(void) {
    assert(!live_entropy && !live_wifi && !live_radio); live_entropy=true;
}
static void bootloader_random_disable(void) {
    assert(live_entropy && !live_wifi && !live_radio); live_entropy=false;
}
static int esp_netif_init(void) { return step(); }
static int esp_event_loop_create_default(void) { return step(); }
static esp_netif_t *esp_netif_create_default_wifi_ap(void) {
    assert(!live_netif); if(step()) return NULL; live_netif=true; return &netif;
}
static int esp_wifi_init(const wifi_init_config_t *c) {
    (void)c; assert(live_netif && !live_wifi && !live_entropy); int e=step(); if(!e) live_wifi=true; return e;
}
static int esp_wifi_set_storage(int s) { assert(s==WIFI_STORAGE_RAM && live_wifi); return step(); }
static int esp_read_mac(uint8_t *mac, int kind) { (void)kind; memset(mac,1,6); return step(); }
static int wifi_event;
static int esp_event_handler_instance_register(int a,int b,int fn,void *c,void **h) {
    (void)a;(void)b;(void)fn;(void)c; assert(!live_handler); int e=step();
    if(!e){live_handler=true;*h=&netif;} return e;
}
static int esp_wifi_set_mode(int m) { (void)m;return step(); }
static int esp_wifi_set_config(int i,const wifi_config_t *c) { (void)i;assert(c->ap.authmode==WIFI_AUTH_WPA2_PSK);return step(); }
static int esp_wifi_start(void) { assert(live_wifi);int e=step();if(!e)live_radio=true;return e; }
static int httpd_start(void **server,const httpd_config_t *c) {
    assert(live_radio && c->max_uri_handlers>=14);int e=step();if(!e){live_http=true;*server=&netif;}return e;
}
static int register_uri(const char *s,int m,int fn) { (void)s;(void)m;(void)fn;assert(live_http);return step(); }
static int root_get,setup_post,login_post,logout_get,status_get,radio_get,files_get,
    download_get,qr_get,upload_post,settings_post,file_post,reboot_post,recovery_post;
static int httpd_stop(void *s) { assert(s && live_http);int e=cleanup_step();if(!e)live_http=false;return e; }
static int esp_wifi_stop(void) { assert(live_radio && !live_http);int e=cleanup_step();if(!e)live_radio=false;return e; }
static int esp_event_handler_instance_unregister(int a,int b,void *h) {
    (void)a;(void)b;(void)h; assert(live_handler && !live_radio);
    int e=cleanup_step();if(!e)live_handler=false;return e;
}
static int esp_wifi_deinit(void) { assert(live_wifi && !live_radio);int e=cleanup_step();if(!e)live_wifi=false;return e; }
static void esp_netif_destroy_default_wifi(void *p) { assert(p && live_netif && !live_wifi && !live_handler); live_netif=false; }
'''
TESTS = r'''
static void clean(void) {
    assert(!live_http && !live_radio && !live_wifi && !live_netif && !live_handler && !live_entropy);
    assert(!web_server && !web_netif && !web_wifi_initialized && !web_wifi_started && !web_handler_registered);
}
int main(void) {
    assert(cerberus_web_ui_start()==ESP_OK);
    unsigned max_steps=attempt;
    assert(web_status.running && live_http && live_radio);
    assert(cerberus_web_ui_start()==ESP_OK && attempt==max_steps);
    assert(cerberus_web_ui_stop()==ESP_OK); clean();
    assert(cerberus_web_ui_stop()==ESP_OK); clean();
    for(unsigned fault=1;fault<=max_steps;++fault) {
        web_netif_initialized=false; attempt=0; fail_at=fault; cleanup_attempt=0;
        assert(cerberus_web_ui_start()!=ESP_OK);clean();assert(!web_status.running);
        fail_at=0; attempt=0;
        assert(cerberus_web_ui_start()==ESP_OK);
        assert(cerberus_web_ui_stop()==ESP_OK);clean();
    }
    /* If stopping a live resource fails, keep its ownership and retry safely. */
    for(unsigned fault=1;fault<=4;++fault) {
        fail_at=0; attempt=0;
        assert(cerberus_web_ui_start()==ESP_OK);
        cleanup_attempt=0;stop_fail_at=fault;
        assert(cerberus_web_ui_stop()!=ESP_OK);
        stop_fail_at=0;
        assert(cerberus_web_ui_stop()==ESP_OK);clean();
    }
    puts("PASS: web start/stop/restart, every startup fault, cleanup ownership, and stop retries.");
    return 0;
}
'''


def main():
    source = (ROOT / "s3_main/main/web_ui.c").read_text()
    a = source.index("esp_err_t cerberus_web_ui_stop(void)")
    b = source.index("void cerberus_web_ui_get_status", a)
    with tempfile.TemporaryDirectory(prefix="sentinel-web-") as temp:
        path = Path(temp)
        (path / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NO_MEM 0x101\n")
        harness = path / "test_web.c"
        harness.write_text(PREAMBLE + source[a:b] + TESTS)
        executable = path / ("web.exe" if os.name == "nt" else "web")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=gnu11", "-O1",
            "-Wall", "-Wextra", "-Werror", "-I", str(path), "-I", str(ROOT / "s3_main/main"),
            str(harness), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
