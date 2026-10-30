"""Exercise actual BLE Wardriving capture and counting with bank/SD/Stop faults."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-ble-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/wardrive_service.c").read_text(encoding="utf-8")
parts = []
for start, stop in (("static void publish_sighting(", "static void publish_invalid("),
                    ("static esp_err_t capture_ble(", "static esp_err_t capture_expansions(")):
    begin = source.index(start)
    parts.append(source[begin:source.index(stop, begin)])
(out / "wardrive_ble_impl.h").write_text("\n".join(parts), encoding="utf-8")
exe = out / "wardrive-ble-check.exe"
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=gnu11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"), "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/wardrive_ble_capture_check.c"),
    str(root / "s3_main/main/wardrive_core.c"), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
