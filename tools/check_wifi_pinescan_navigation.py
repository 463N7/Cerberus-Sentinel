"""Check Wi-Fi launch -> live Tools -> generic hub -> PineScan -> Start.

Uses full production launch/live handlers and the full pre-hub Wi-Fi dispatch
prefix, not a second implementation of the menu routing. Unrelated screens,
radio services and rendering are stubbed. No device or firmware build.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
# Prepare and check the existing production PineScan UI/renderer fixture.
subprocess.run([sys.executable, str(root / "tools/check_wifi_pinescan_ui.py")], check=True)
out = root / "artifacts/wifi-pinescan-ui-checks"
ui = (root / "s3_main/main/ui.c").read_text()
live = (root / "s3_main/main/sentinel_live_ui.h").read_text()
target = (root / "s3_main/main/wifi_target_ui.h").read_text()

def block_end(text, opening):
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if not depth:
                return pos + 1
    raise ValueError("unclosed production block")

def function(text, signature):
    start = text.index(signature + "\n{")
    return text[start:block_end(text, text.index("{", start))]

types = re.findall(r"typedef enum \{[^}]*\} \w+;", ui)
assert any("ui_page_t" in item for item in types)
(out / "wifi_navigation_types.h").write_text("\n".join(types))

# Keep every mode/early-return guard before the generic menu. Later Wi-Fi
# detail-page handlers cannot run on any state under test and are omitted.
dispatch = function(ui, "static void handle_wifi_tap(uint16_t x, uint16_t y)")
menu = dispatch.index("    if (wifi_mode == UI_WIFI_MODE_MENU) {")
dispatch = dispatch[:block_end(dispatch, dispatch.index("{", menu))] + "\n}"
controls = live.index("    if(tool==SENTINEL_SCAN_WIFI){")
controls = live[controls:block_end(live, live.index("{", controls))]
(out / "wifi_navigation_routes.h").write_text("\n\n".join([
    function(ui, "static void open_tool_page(ui_page_t page)"),
    function(live, "static bool live_is_view(void)"),
    function(target, "static void wifi_target_open_attacks(void)"),
    function(target, "static void wifi_target_finish_selection(void)"),
    "static void actual_wifi_controls(void) { sentinel_scan_tool_t tool=SENTINEL_SCAN_WIFI;\n" + controls + "\n}",
    function(live, "static void handle_live_scan_tap(uint16_t x, uint16_t y)"),
    dispatch,
]))
exe = out / "wifi-pinescan-navigation-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_pinescan_navigation_check.c"),
    str(root / "s3_main/main/ui_widgets.c"), str(root / "s3_main/main/sentinel_hub.c"),
    str(root / "s3_main/main/sentinel_scan_view.c"),
    str(root / "s3_main/main/sentinel_scan_session.c"),
    str(root / "s3_main/main/privacy.c"), str(root / "shared/wifi_pinescan.c"),
    str(root / "shared/wifi_beacon_analysis.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True)
