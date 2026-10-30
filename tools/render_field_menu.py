"""Render actual home and field-menu C code, including its real touch handlers."""
from pathlib import Path
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/field-menu-preview"
out.mkdir(parents=True, exist_ok=True)
display = (root / "s3_main/main/display.c").read_text()
start = display.index("static const uint8_t *font5x7(")
(out / "display_font.h").write_text(display[start:display.index("\nesp_err_t ", start)])
framebuffer = (root / "tools/host_checks/recon_render.c").read_text().split("/* Exercise the real private controls", 1)[0]
(out / "field_framebuffer.h").write_text(framebuffer)
exe = out / "field-render.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"), "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/field_menu_render.c"), str(root / "s3_main/main/ui_widgets.c"),
    str(root / "s3_main/main/field_menu.c"), str(root / "s3_main/main/sentinel_dashboard.c"), "-o", str(exe)], check=True)
subprocess.run([str(exe), str(out / "field")], check=True)

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

for path in out.glob("field-*.ppm"):
    raw = path.read_bytes()
    header = b"P6\n320 480\n255\n"
    assert raw.startswith(header) and len(raw) == len(header) + 320 * 480 * 3
    pixels = raw[len(header):]
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 480, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(b"".join(b"\0" + pixels[y*960:(y+1)*960] for y in range(480)))) + chunk(b"IEND", b""))
print(out)
