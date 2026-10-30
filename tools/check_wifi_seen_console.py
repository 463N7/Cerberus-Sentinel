"""Exercise the real read-only wifi_seen console command and dispatch."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-seen-console-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/usb_console.c").read_text(encoding="utf-8")

def between(first, after):
    begin = source.index(first)
    return source[begin:source.index(after, begin)]

parts = between("static void print_help(void)", "static void print_info(void)")
parts += between("static void print_wifi_seen_error(", "static void print_scan_tools(void)")
parts += between("static void execute_command(", "static void usb_console_task(")
(out / "wifi_seen_console_impl.h").write_text(parts, encoding="utf-8")
exe = out / "wifi-seen-console-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_seen_console_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
