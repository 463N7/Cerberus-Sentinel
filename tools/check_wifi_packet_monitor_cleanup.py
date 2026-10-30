"""Exercise the production C5 monitor's reserved-radio restore path."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
source = (root / "c5_firmware/main/wifi_packet_monitor_service.h").read_text()
start = source.index("static esp_err_t packet_monitor_restore_radio(void)")
end = source.index("static void packet_monitor_run(void)", start)
impl = source[start:end]
program = r'''
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_FAIL 1
typedef int esp_err_t;
typedef int wifi_band_mode_t;
typedef int wifi_second_chan_t;
static wifi_band_mode_t packet_monitor_previous_band_mode;
static uint8_t packet_monitor_previous_channel;
static wifi_second_chan_t packet_monitor_previous_second;
static bool packet_monitor_restore_pending;
static bool packet_monitor_disable_pending;
static atomic_bool packet_monitor_radio_reserved;
static int fail_disable, fail_band, fail_channel;
static unsigned disable_calls, band_calls, channel_calls;
static esp_err_t esp_wifi_set_promiscuous(bool enabled)
{assert(!enabled);++disable_calls;return fail_disable ? ESP_FAIL : ESP_OK;}
static esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t mode)
{assert(mode==9);++band_calls;return fail_band ? ESP_FAIL : ESP_OK;}
static esp_err_t esp_wifi_set_channel(uint8_t channel,wifi_second_chan_t second)
{assert(channel==6 && second==2);++channel_calls;return fail_channel ? ESP_FAIL : ESP_OK;}
''' + impl + r'''
static void reset(void)
{
    packet_monitor_previous_band_mode=9;
    packet_monitor_previous_channel=6;
    packet_monitor_previous_second=2;
    packet_monitor_restore_pending=true;
    packet_monitor_disable_pending=true;
    atomic_store(&packet_monitor_radio_reserved,true);
    fail_disable=fail_band=fail_channel=0;
    disable_calls=band_calls=channel_calls=0;
}
int main(void)
{
    reset();
    assert(packet_monitor_restore_radio()==ESP_OK);
    assert(!atomic_load(&packet_monitor_radio_reserved));
    assert(!packet_monitor_restore_pending && !packet_monitor_disable_pending);
    assert(disable_calls==1 && band_calls==1 && channel_calls==1);

    reset();fail_disable=1;
    assert(packet_monitor_restore_radio()==ESP_FAIL);
    assert(atomic_load(&packet_monitor_radio_reserved));
    assert(packet_monitor_restore_pending && packet_monitor_disable_pending);
    assert(disable_calls==1 && band_calls==0 && channel_calls==0);

    reset();fail_band=1;
    assert(packet_monitor_restore_radio()==ESP_FAIL);
    assert(atomic_load(&packet_monitor_radio_reserved));
    assert(packet_monitor_restore_pending && !packet_monitor_disable_pending);
    fail_band=0;
    assert(packet_monitor_restore_radio()==ESP_OK);
    assert(!atomic_load(&packet_monitor_radio_reserved));
    assert(disable_calls==1 && band_calls==2 && channel_calls==1);

    reset();fail_channel=1;
    assert(packet_monitor_restore_radio()==ESP_FAIL);
    assert(atomic_load(&packet_monitor_radio_reserved));
    assert(packet_monitor_restore_pending && !packet_monitor_disable_pending);
    fail_channel=0;
    assert(packet_monitor_restore_radio()==ESP_OK);
    assert(!atomic_load(&packet_monitor_radio_reserved));
    assert(disable_calls==1 && band_calls==2 && channel_calls==2);
    return 0;
}
'''
out = root / "artifacts/packet-monitor-checks"
out.mkdir(parents=True, exist_ok=True)
fixture = out / "wifi_packet_monitor_cleanup_check.c"
fixture.write_text(program, encoding="utf-8")
exe = out / "wifi-packet-monitor-cleanup-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    str(fixture), "-o", str(exe)], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
print("Wi-Fi Packet Monitor disable/restore reservation and retry: pass")
