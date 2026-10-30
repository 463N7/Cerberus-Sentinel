"""Render the actual firmware Recon view with its real 5x7 font; no hardware I/O."""
from pathlib import Path
import subprocess
import sys
import struct
import zlib
ROOT = Path(__file__).resolve().parents[1]
out = ROOT / "artifacts/recon-preview"
out.mkdir(parents=True, exist_ok=True)
display = (ROOT / "s3_main/main/display.c").read_text()
start = display.index("static const uint8_t *font5x7(")
end = display.index("\nesp_err_t ", start)
(out / "display_font.h").write_text(display[start:end])
exe = out / "recon-render.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(ROOT / "tools/host_checks/stubs"), "-I", str(ROOT / "s3_main/main"), "-I", str(out), "-I", str(ROOT / "shared"),
    str(ROOT / "tools/host_checks/recon_render.c"), str(ROOT / "s3_main/main/sentinel_watch_view.c"),
    str(ROOT / "s3_main/main/sentinel_watch.c"), "-o", str(exe)], check=True)
subprocess.run([str(exe), str(out / "recon")], check=True)
def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
for path in out.glob("recon-*.ppm"):
    header = b"P6\n320 480\n255\n"
    raw = path.read_bytes()
    assert raw.startswith(header) and len(raw) == len(header) + 320 * 480 * 3
    pixels = raw[len(header):]
    rows = b"".join(b"\0" + pixels[y * 960:(y + 1) * 960] for y in range(480))
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 480, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
print(out / "recon-live.png")
