"""Exercise completed-sweep diagnostic output from the production console."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/ble-discovery-console-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/usb_console.c").read_text()
begin = source.index("static void print_ble_meta_pairs(")
# The central and inventory printers have their own production-route suite.
# Keep this extraction scoped to the existing discovery helpers it exercises.
end = source.index("/* BEGIN BLE CENTRAL DIAGNOSTICS:", begin)
(out / "discovery_console_impl.h").write_text(source[begin:end])
exe = out / "ble-discovery-console-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/ble_discovery_console_check.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
