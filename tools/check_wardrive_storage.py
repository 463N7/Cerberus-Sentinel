"""Exercise production Wardriving append stream with real temporary files."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/storage.c").read_text(encoding="utf-8")
usb_start = source.index("esp_err_t cerberus_storage_reserve_for_usb(")
usb_end = source.index("esp_err_t cerberus_storage_release_from_usb(", usb_start)
state_start = source.index("typedef struct {", source.index("/* Fixed Wardriving paths"))
state_end = source.index("\n}", source.index("static bool wardrive_storage_busy", state_start)) + 2
ward_start = source.index("static esp_err_t wardrive_stream_begin(")
ward_end = source.index("static void write_le16(", ward_start)
(out / "wardrive_storage_impl.h").write_text(
    source[state_start:state_end] + "\n" + source[usb_start:usb_end] + "\n" + source[ward_start:ward_end], encoding="utf-8")
exe = out / "wardrive-storage-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"),
    str(root / "s3_main/main/wardrive_core.c"),
    str(root / "tools/host_checks/wardrive_storage_check.c"), "-o", str(exe),
], check=True, env=environment)
with tempfile.TemporaryDirectory(prefix="files-", dir=out) as temporary:
    directory = Path(temporary).resolve()
    assert directory.is_relative_to(out.resolve())
    (directory / "CERBERUS/WARD").mkdir(parents=True)
    subprocess.run([str(exe), str(directory)], check=True, env=environment)
