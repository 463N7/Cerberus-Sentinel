"""Exercise the production Remote ID renderer and painted-row hit testing."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-ui-checks"
out.mkdir(parents=True, exist_ok=True)
display = (root / "s3_main/main/display.c").read_text()
start = display.index("static const uint8_t *font5x7(")
(out / "display_font.h").write_text(display[start:display.index("\nesp_err_t ", start)])

env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-ui-check.exe" if os.name == "nt" else "remote-id-ui-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/remote_id_ui_check.c"),
    str(root / "s3_main/main/remote_id_inventory.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe), str(out / "remote-id")], check=True)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


for path in sorted(out.glob("remote-id-*.ppm")):
    data = path.read_bytes()
    header = b"P6\n320 480\n255\n"
    assert data.startswith(header) and len(data) == len(header) + 320 * 480 * 3
    pixels = data[len(header):]
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 480, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(b"".join(b"\0" + pixels[y*960:(y+1)*960] for y in range(480)))) +
        chunk(b"IEND", b""))
print(out)
