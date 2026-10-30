"""Compile actual CSI UI lifetime with the real session/model/renderer."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/csi-live-ui-checks"
out.mkdir(parents=True, exist_ok=True)
ui = (root / "s3_main/main/ui.c").read_text()
assert ui.index('#include "csi_live_ui.h"') < ui.index('#include "wifi_target_ui.h"')
draw = ui[ui.index("static esp_err_t draw_detail_page(void)\n{"):]
assert draw.index("return csi_draw(true)") < draw.index("cerberus_display_fill_rect")
tap = ui[ui.index("static void handle_tap(uint16_t x, uint16_t y)"):]
assert "csi_handle_tap(x, y);\n        return;" in tap
assert tap.index("csi_handle_tap(x, y)") < tap.index("current_page == UI_PAGE_MAIN")
assert "csi_live_tick(current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CSI && !screen_asleep)" in ui

env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("csi-live-ui-check.exe" if os.name == "nt" else "csi-live-ui-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "tools/host_checks/stubs"), "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/csi_live_ui_check.c"), str(root / "s3_main/main/csi_session.c"),
    str(root / "s3_main/main/csi_ui.c"), str(root / "shared/csi_motion_model.c"), "-lm", "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
