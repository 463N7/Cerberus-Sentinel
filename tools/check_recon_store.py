"""Exercise production SD epochs and vendor cache with injected media changes."""
from pathlib import Path
import os
import re
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/recon-store-checks"
out.mkdir(parents=True, exist_ok=True)


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


storage = (root / "s3_main/main/storage.c").read_text()
signatures = (
    "static void advance_storage_read_epoch(void)",
    "static esp_err_t require_firmware_owner_locked(void)",
    "static esp_err_t unmount_locked(void)",
    "esp_err_t cerberus_storage_mount(void)",
    "esp_err_t cerberus_storage_unmount(void)",
    "esp_err_t cerberus_storage_get_read_epoch(uint32_t *epoch)",
    "esp_err_t cerberus_storage_reserve_for_usb(void)",
    "esp_err_t cerberus_storage_release_from_usb(void)",
)
(out / "storage_epoch_impl.h").write_text("\n\n".join(function(storage, s) for s in signatures))
store = (root / "s3_main/main/recon_store.c").read_text()
(out / "recon_store_impl.h").write_text(re.sub(r'^#include[^\n]*\n', '', store, flags=re.M))


def fixture(path, second):
    rows = [(key, "Fixture Two" if second else "Fixture One")
            for key in range(0x001000, 0x001020)]
    if second:
        rows[-1] = (0x00FFFE, "New Assignment")
    blob_start = 40 + len(rows) * 16
    index = bytearray()
    blob = bytearray()
    for key, name in rows:
        encoded = name.encode()
        record = bytes([len(encoded)]) + encoded + b"{}"
        index += struct.pack("<IIII", key, blob_start + len(blob), len(record),
                             zlib.crc32(struct.pack("<I", key) + record))
        blob += record
    metadata = b"{}"
    body = index + blob + metadata
    header = struct.pack("<8s8I", b"CERBREC1", 1, len(rows), 40, blob_start,
                         blob_start + len(blob), 40 + len(body), zlib.crc32(body), 0)
    path.write_bytes(header + body)


fixture(out / "first.db", False)
fixture(out / "second.db", True)
exe = out / "recon-store-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/recon_store_check.c"),
    str(root / "shared/recon_db.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe), str(out / "first.db"), str(out / "second.db"),
                str(root / "sdcard/CERBERUS/RECON.DB")], check=True, env=env)
