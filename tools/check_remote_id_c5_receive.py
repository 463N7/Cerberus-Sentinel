"""Feed real C5 receive/Watch producers through the S3 getter into inventory."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-c5-receive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "c5_firmware/main/wifi_scanner.c").read_text()
pieces = []
for first, after in (
    ("static void wifi_promiscuous_callback(void *buffer,", "static void reset_defense_capture(void)"),
    ("esp_err_t cerberus_wifi_watch_get_status(", "esp_err_t cerberus_wifi_scanner_init(void)"),
):
    start = source.index(first)
    pieces.append(source[start:source.index(after, start)])
(out / "remote_id_c5_producer_impl.h").write_text("\n".join(pieces))
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-c5-receive-check.exe" if os.name == "nt" else "remote-id-c5-receive-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/remote_id_c5_receive_check.c"),
    str(root / "shared/sentinel_watch_protocol.c"), str(root / "shared/cerberus_watch_capture.c"),
    str(root / "s3_main/main/s3_ble_snapshot.c"), str(root / "s3_main/main/sentinel_watch_detect.c"),
    str(root / "s3_main/main/remote_id_inventory.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
