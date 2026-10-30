"""Render the native target screen and exercise its actual private actions."""
from pathlib import Path
import subprocess, sys, struct, zlib
root=Path(__file__).resolve().parents[1]
out=root/"artifacts/wifi-target-preview"
out.mkdir(parents=True, exist_ok=True)
display=(root/"s3_main/main/display.c").read_text()
start=display.index("static const uint8_t *font5x7(")
(out/"display_font.h").write_text(display[start:display.index("\nesp_err_t ",start)])
(out/"target_framebuffer.h").write_text((root/"tools/host_checks/recon_render.c").read_text().split("/* Exercise the real private controls",1)[0])
relation=(root/"s3_main/main/wifi_relation_ui.h").read_text()
start=relation.index("static bool wifi_relation_supported(")
(out/"relation_target_support.h").write_text(relation[start:relation.index("static esp_err_t wifi_relation_capture_before(",start)])
exe=out/"target-render.exe"
subprocess.run([sys.executable,"-m","ziglang","cc","-std=c11","-O1","-Wall","-Wextra","-Werror",
    "-I",str(out),"-I",str(root/"tools/host_checks/stubs"),"-I",str(root/"s3_main/main"),"-I",str(root/"shared"),
    str(root/"tools/host_checks/wifi_target_render.c"),str(root/"s3_main/main/ui_widgets.c"),
    str(root/"s3_main/main/wifi_target_view.c"),str(root/"s3_main/main/csi_session.c"),
    str(root/"shared/csi_motion_model.c"),"-lm","-o",str(exe)],check=True)
subprocess.run([str(exe),str(out/"target")],check=True)
def chunk(kind,data):return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
for ppm in out.glob("target-*.ppm"):
    raw=ppm.read_bytes();header=b"P6\n320 480\n255\n"
    assert raw.startswith(header) and len(raw)==len(header)+320*480*3
    pixels=raw[len(header):]
    png=ppm.with_suffix(".png")
    png.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",320,480,8,2,0,0,0))+
        chunk(b"IDAT",zlib.compress(b"".join(b"\0"+pixels[y*960:(y+1)*960] for y in range(480))))+chunk(b"IEND",b""))
    print(png)
