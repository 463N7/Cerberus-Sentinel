"""Compile the real bounded diagnostics store and read-only console route."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-diagnostics-checks"
(out / "freertos").mkdir(parents=True, exist_ok=True)
(out / "freertos/FreeRTOS.h").write_text("""#pragma once
#include <stdatomic.h>
typedef struct { atomic_flag flag; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { ATOMIC_FLAG_INIT }
void remote_id_test_enter(portMUX_TYPE *lock);
void remote_id_test_exit(portMUX_TYPE *lock);
#define portENTER_CRITICAL(lock) remote_id_test_enter(lock)
#define portEXIT_CRITICAL(lock) remote_id_test_exit(lock)
""", encoding="utf-8")
source = (root / "s3_main/main/usb_console.c").read_text(encoding="utf-8")

def between(first, after):
    begin = source.index(first)
    return source[begin:source.index(after, begin)]

parts = between("static void print_help(void)", "static void print_info(void)")
parts += between("static void print_remote_id(void)", "static void usb_console_task(")
(out / "remote_id_console_impl.h").write_text(parts, encoding="utf-8")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-diagnostics-check.exe" if os.name == "nt" else "remote-id-diagnostics-check")
command = [
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/remote_id_diagnostics_check.c"),
    str(root / "s3_main/main/remote_id_diagnostics.c"), "-o", str(exe),
]
if os.name != "nt":
    command.append("-pthread")
subprocess.run(command, check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
