"""Check the actual Remote ID UI adapter, Scan route and pure schedulers.

Platform radio/status calls are fakes. Production inventory, controller,
scheduler, menu, privacy, rendering and hit tests run unchanged on the host.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-live-ui-checks"
out.mkdir(parents=True, exist_ok=True)
display = (root / "s3_main/main/display.c").read_text()
start = display.index("static const uint8_t *font5x7(")
(out / "display_font.h").write_text(display[start:display.index("\nesp_err_t ", start)])
ui = (root / "s3_main/main/ui.c").read_text()


def end_block(text, opening):
    depth = 0
    for i in range(opening, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if not depth:
                return i + 1
    raise ValueError("Unclosed production block")


enums = re.findall(r"typedef enum \{[^}]*\} ui_page_t;", ui)
assert len(enums) == 1
(out / "remote_id_route_types.h").write_text(enums[0])
start = ui.index("static void handle_tap(uint16_t x, uint16_t y)\n{")
handler = ui[start:end_block(ui, ui.index("{", start))]
guard = handler.index("    if (x >= CERBERUS_LCD_WIDTH")
remote = handler.index("    if (current_page == UI_PAGE_REMOTE_ID)")
field = handler.index("    if (current_page == UI_PAGE_FIELD_MENU)")
route = handler.index("        if (action == CERBERUS_FIELD_REMOTE_ID)", field)
# Every field-menu guard before Remote ID is retained. Other screen branches
# are excluded; fixtures enter from the real Scan menu hit-test coordinates.
parts = [handler[guard:end_block(handler, handler.index("{", guard))],
         handler[remote:end_block(handler, handler.index("{", remote))],
         handler[field:end_block(handler, handler.index("{", route))] + "\n    }"]
(out / "remote_id_routes.h").write_text(
    "static void actual_remote_id_route(uint16_t x,uint16_t y)\n{\n" +
    "\n".join(parts) + "\n}\n")
exe = out / ("remote-id-live-ui-check.exe" if os.name == "nt" else "remote-id-live-ui-check")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(root / "artifacts/remote-id-ui-checks/zig-cache"))
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/remote_id_live_ui_check.c"),
    *[str(root / "s3_main/main" / name) for name in (
        "remote_id_inventory.c", "remote_id_session.c", "sentinel_scan_session.c",
        "field_menu.c", "ui_widgets.c", "privacy.c")],
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True)
