"""Check exact selected BLE identity, live RSSI freshness and UI lifecycle."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
scanner = (root / "s3_main/main/s3_ble_scanner.c").read_text(encoding="utf-8")
ui = (root / "s3_main/main/ui.c").read_text(encoding="utf-8")
assert scanner.count("cerberus_s3_ble_tracker_observe(&tracker, report.address,") == 2
assert "scan_epoch == (uint32_t)(uintptr_t)arg" in scanner
assert "cerberus_s3_ble_tracker_host_reset(&tracker)" in scanner
assert "cerberus_s3_ble_scanner_track_end();" in ui
assert "ble_fox_hunt_tick((uint32_t)now_ms, screen_asleep);" in ui
assert "ble_scan_status.state == CERBERUS_BLE_SCAN_RUNNING &&" in ui

out = root / "artifacts/ble-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "s3-ble-tracker-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_ble_tracker_check.c"), "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
print("S3 BLE Fox Hunt exact identity, live RSSI, stale age, wrap, reset and UI wiring: pass")
