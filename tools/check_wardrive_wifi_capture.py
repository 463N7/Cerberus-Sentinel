"""Exercise production Wardriving Wi-Fi capture against scripted C5/SD faults."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/wardrive_service.c").read_text(encoding="utf-8")
deadline_start = source.index("static bool before_deadline(")
deadline_end = source.index("static esp_err_t start_optional_gps(", deadline_start)
capture_start = source.index("static void reset_wifi_capture(")
capture_end = source.index("static esp_err_t capture_ble(", capture_start)
publish_start = source.index("static void publish_completed_wifi(")
publish_end = source.index("static void drain_completed_ble(", publish_start)
(out / "wardrive_wifi_capture_impl.h").write_text(
    source[deadline_start:deadline_end] + "\n" +
    source[capture_start:capture_end] + "\n" + source[publish_start:publish_end], encoding="utf-8")
exe = out / "wardrive-wifi-capture-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/wardrive_wifi_capture_check.c"),
    str(root / "s3_main/main/wardrive_core.c"), "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
