#!/usr/bin/env python3
"""Run Sentinel's host checks and generate display previews; no device access."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKS = (
    "test_protocol.py",
    "test_sentinel_guide.py",
    "test_sentinel_baseline.py",
    "test_sentinel_settings.py",
    "test_sentinel_battery.py",
    "test_sentinel_tools_ui.py",
    "test_sentinel_scan_session.py",
    "test_sentinel_live_ui.py",
    "test_sentinel_watch.py",
    "test_sentinel_watch_activity.py",
    "test_sentinel_watch_detect.py",
    "test_sentinel_watch_ui.py",
    "test_sentinel_watch_transport.py",
    "test_sentinel_ble_advertisement.py",
    "test_sentinel_c5_radio_boundary.py",
    "test_sentinel_wifi_connect.py",
    "test_sentinel_web_lifecycle.py",
    "test_sentinel_package.py",
    "test_export_web_release.py",
    "test_sentinel_display.py",
    "preview_sentinel_dashboard.py",
    "preview_sentinel_screens.py",
    "preview_sentinel_scan.py",
    "preview_sentinel_watch.py",
    "preview_sentinel_battery.py",
    "preview_sentinel_tools.py",
)


def main():
    for script in CHECKS:
        print(f"\nChecking {script}", flush=True)
        result = subprocess.run([sys.executable, str(ROOT / "tools" / script)], cwd=ROOT)
        if result.returncode:
            return result.returncode
    print(f"\nPASS: all {len(CHECKS)} host check groups. Physical-board acceptance remains separate.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
