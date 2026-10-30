#!/usr/bin/env python3
"""Build the integration target matrix without uploading or publishing firmware."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "s3": (ROOT, "s3-main"),
    "onboard": (ROOT / "c5_firmware", "c5-onboard"),
    "bridge": (ROOT, "s3-c5-bridge"),
    "expansion-wroom": (ROOT / "expansion/c5_scanner", "c5-expansion-wroom"),
    "expansion-xiao": (ROOT / "expansion/c5_scanner", "c5-expansion-xiao"),
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs() -> dict[str, str]:
    """Fingerprint firmware inputs, including effective generated SDK configs."""
    result = {}
    excluded = {".pio", ".git", "managed_components", "artifacts", "dist", "__pycache__", "node_modules", "web"}
    for path in ROOT.rglob("*"):
        relative = path.relative_to(ROOT)
        if any(part in excluded for part in relative.parts) or not path.is_file():
            continue
        if path.name.endswith(".old"):
            continue
        if path.suffix in {".c", ".h", ".cpp", ".py", ".S", ".ld", ".lf", ".ini", ".csv", ".json", ".yml", ".lock", ".defaults"} or path.name == "CMakeLists.txt" or path.name.startswith("sdkconfig"):
            result[relative.as_posix()] = digest(path)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=["all", *TARGETS], default="all")
    parser.add_argument("--list", action="store_true", help="List targets without building")
    args = parser.parse_args()
    if args.list:
        for name, (project, environment) in TARGETS.items():
            print(f"{name}: {project.relative_to(ROOT)} / {environment}")
        return 0
    output = ROOT / "artifacts/integration"
    output.mkdir(parents=True, exist_ok=True)
    before = inputs()
    report = {"format": 1, "started_utc": datetime.now(timezone.utc).isoformat(),
              "status": "building", "hardware_validated": False, "targets": {}}
    report_path = output / "build-report.json"

    def save() -> None:
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    save()
    for name in TARGETS if args.target == "all" else [args.target]:
        project, environment = TARGETS[name]
        log = output / f"{name}.log"
        command = [sys.executable, "-m", "platformio", "run", "-j", "4", "-d", str(project), "-e", environment]
        print(f"Building {name}; log: {log}", flush=True)
        with log.open("w", encoding="utf-8") as stream:
            process = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        entry = {"environment": environment, "exit_code": process.returncode,
                 "status": "passed" if process.returncode == 0 else "failed", "log": str(log.relative_to(ROOT))}
        if process.returncode == 0 and name == "s3":
            try:
                from check_c5_ready_iram import verify as verify_ready_iram
                entry["ready_iram"] = verify_ready_iram(ROOT)
            except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
                entry["status"] = "failed"
                entry["ready_iram_error"] = str(error)
        if process.returncode == 0 and name == "onboard":
            try:
                from check_c5_raw_link import verify
                entry["raw_tx_link"] = verify(ROOT)
            except (RuntimeError, OSError, subprocess.SubprocessError) as error:
                entry["status"] = "failed"
                entry["raw_tx_link_error"] = str(error)
        # Never report stale output as successful after a failed build.
        if process.returncode == 0:
            entry["files"] = {}
            for filename in ("firmware.bin", "firmware.elf", "firmware.map", "partitions.bin", "flasher_args.json"):
                path = project / ".pio/build" / environment / filename
                if path.is_file():
                    entry["files"][filename] = {"bytes": path.stat().st_size, "sha256": digest(path),
                                                "path": path.relative_to(ROOT).as_posix()}
            missing = sorted({"firmware.bin", "firmware.elf", "partitions.bin"} - entry["files"].keys())
            if missing:
                entry["status"] = "failed"
                entry["missing_outputs"] = missing
        report["targets"][name] = entry
        save()
        print(f"{name}: {entry['status']}", flush=True)
    after = inputs()
    report["inputs_before"] = before
    report["inputs_after"] = after
    report["inputs_changed"] = before != after
    passed = all(item["status"] == "passed" for item in report["targets"].values())
    report["status"] = "passed" if passed and before == after else "needs-review" if passed else "failed"
    report["completed_utc"] = datetime.now(timezone.utc).isoformat()
    save()
    print(f"Report: {report_path}")
    if before != after:
        print("Build inputs changed (possibly dependency resolution); rerun for a stable receipt.")
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
