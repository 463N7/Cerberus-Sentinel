"""Fault-inject the production PSRAM scan snapshot allocation and publication."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/scan-display-store-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/sentinel_live_ui.h").read_text(encoding="utf-8")
types_start = source.index("typedef struct {")
types_end = source.index("} live_scan_record_t;", types_start) + len("} live_scan_record_t;")
(out / "scan_display_record_type.h").write_text(source[types_start:types_end], encoding="utf-8")
start = source.index("static void live_status(")
end = source.index("static bool live_observe(", start)
(out / "scan_display_capture_impl.h").write_text(source[start:end], encoding="utf-8")
exe = out / "scan-display-store-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/scan_display_store_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
