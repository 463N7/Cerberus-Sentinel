"""Exercise bounded BLE discovery history using the actual production module."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-ble-discovery-inventory-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "s3-ble-discovery-inventory-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_ble_discovery_inventory_check.c"),
    str(root / "s3_main/main/s3_ble_discovery_inventory.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
