#!/usr/bin/env python3
"""Run the 79 host suites used for the rev81 development checkpoint.

The checked-in synthetic fixtures need no board, radio, or private captures.
Generated fixtures, native executables, previews, logs, and the result receipt
remain in artifacts/. UI suites with shared fixture headers run serially.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "artifacts" / "host-checks"

CHECKS = (
    'check_wifi_relation_observation.py',
    'check_s3_wifi_relation.py',
    'check_wifi_relation_transport.py',
    'check_s3_ble_discovery_session.py',
    'check_s3_ble_discovery_inventory.py',
    'check_mac_observation.py',
    'check_mac_capture.py',
    'check_mac_monitor_transport.py',
    'check_mac_monitor_inventory.py',
    'check_mac_monitor_session.py',
    'check_csi_motion_model.py',
    'check_csi_capture.py',
    'check_csi_transport.py',
    'check_csi_session.py',
    'check_station_service.py',
    'check_wifi_capture_radio.py',
    'check_s3_ble_central.py',
    'check_s3_ble_connection_ui.py',
    'check_wifi_probe_observation.py',
    'check_wifi_probe_transport.py',
    'check_ble_vendor_cache.py',
    'check_wardrive_ble_capture.py',
    'check_wardrive_core.py',
    'check_wardrive_log_routing.py',
    'check_wardrive_memory.py',
    'check_wardrive_stop.py',
    'check_wardrive_storage.py',
    'check_wardrive_wigle.py',
    'check_wardrive_gps_optional.py',
    'check_wardrive_gps_core.py',
    'check_wardrive_expansion.py',
    'check_wardrive_expansion_activation.py',
    'check_recon_store.py',
    'check_s3_ble_recent.py',
    'check_remote_id_inventory.py',
    'check_remote_id_receive.py',
    'check_remote_id_c5_receive.py',
    'check_watch_receive_timing.py',
    'check_watch_capture_timing.py',
    'check_watch_receive_transport.py',
    'check_watch_motion.py',
    'check_remote_id_channel_plan.py',
    'check_remote_id_transport.py',
    'check_remote_id_priority.py',
    'check_remote_id_session.py',
    'check_remote_id_ui.py',
    'check_remote_id_live_ui.py',
    'check_remote_id_diagnostics.py',
    'render_field_menu.py',
    'check_wifi_scan_observation.py',
    'check_s3_wifi_observation.py',
    'check_wifi_seen_console.py',
    'check_wifi_packet_monitor_worker.py',
    'check_wardrive_wifi_capture.py',
    'check_ble_flock_hints.py',
    'check_ibeacon.py',
    'check_ble_ad_hints.py',
    'check_ble_analyzer.py',
    'check_ble_flipper_hints.py',
    'check_ble_findmy_hints.py',
    'check_s3_ble_snapshot.py',
    'check_s3_ble_events.py',
    'check_s3_findmy_monitor.py',
    'check_s3_ble_tracker.py',
    'check_s3_ble_list.py',
    'check_s3_ble_live_routes.py',
    'check_scan_display_store.py',
    'check_s3_findmy_ui_route.py',
    'check_ble_discovery_console.py',
)

SERIAL_CHECKS = (
    'check_wifi_relation_ui.py',
    'check_s3_ble_discovery_live.py',
    'check_mac_monitor_ui.py',
    'check_mac_monitor_live_ui.py',
    'check_csi_ui.py',
    'check_csi_live_ui.py',
    'render_wifi_target.py',
    'check_wardrive_ui.py',
    'check_s3_ble_discovery_ui.py',
    'render_network_picker.py',
)


def run(name: str) -> dict:
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools" / name)], cwd=ROOT,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    path = OUTPUT / (name.removesuffix(".py") + ".log")
    path.write_bytes(result.stdout)
    print(f"{name}: {'pass' if result.returncode == 0 else 'FAIL'}", flush=True)
    if result.returncode:
        print(result.stdout[-3000:].decode("utf-8", errors="replace"), flush=True)
    return {
        "check": name, "exit_code": result.returncode,
        "log": path.relative_to(ROOT).as_posix(),
        "log_sha256": hashlib.sha256(result.stdout).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List suites without running them")
    parser.add_argument("--jobs", type=int, default=3, help="Parallel compiler jobs (default: 3)")
    args = parser.parse_args()
    names = (*CHECKS, *SERIAL_CHECKS)
    if len(names) != 79 or len(set(names)) != 79:
        parser.error("Expected the 79 unique rev81 suites; do not silently skip a suite")
    if args.list:
        for name in names:
            print(name)
        return 0
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    missing = [name for name in names if not (ROOT / "tools" / name).is_file()]
    if missing:
        parser.error("Missing suites: " + ", ".join(missing))
    if importlib.util.find_spec("ziglang") is None:
        parser.error("Install the host compiler first: python -m pip install ziglang==0.14.1")
    from build_unified import inputs

    OUTPUT.mkdir(parents=True, exist_ok=True)
    before = inputs()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(run, CHECKS))
    # These harnesses create shared generated UI fixture headers.
    results.extend(run(name) for name in SERIAL_CHECKS)
    after = inputs()
    changed = sorted(k for k in before.keys() | after.keys() if before.get(k) != after.get(k))
    receipt = {
        "format": 1,
        "suite_set": "rev81",
        "recorded_utc": datetime.now(timezone.utc).isoformat(),
        "status": "passed" if not changed and all(r["exit_code"] == 0 for r in results) else "failed",
        "hardware_validated": False,
        "python_version": sys.version.split()[0],
        "ziglang_version": importlib.metadata.version("ziglang"),
        "inputs_changed": changed,
        "inputs_after": after,
        "checks": results,
    }
    path = OUTPUT / "host-checks.json"
    path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"{receipt['status']}: {len(results)} suites; receipt: {path}", flush=True)
    if changed:
        print("Source/configuration inputs changed during validation; rerun on stable inputs.")
    return 0 if receipt["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
