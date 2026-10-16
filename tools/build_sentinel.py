#!/usr/bin/env python3
"""Build Cerberus Sentinel in PlatformIO; never uploads to hardware."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone

from package_sentinel import RECEIPT, ROOT, TARGETS, inspect_target, package, source_snapshot


def build(name: str) -> None:
    project, environment, _, _ = TARGETS[name]
    command = [sys.executable, "-m", "platformio", "run", "-d", str(project), "-e", environment]
    log_path = ROOT / "artifacts" / "build" / f"{environment}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"\nBuilding {environment}; log: {log_path}", flush=True)
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding="utf-8", errors="replace")
        assert process.stdout is not None
        for line in process.stdout:
            log.write(line)
            print(line, end="", flush=True)
        if process.wait():
            raise RuntimeError(f"{environment} build failed; see {log_path}")
    target = inspect_target(name)
    application = next(image for image in target["images"] if image["role"] == "app")
    factory = next(partition for partition in target["partitions"] if partition["type"] == 0 and partition["subtype"] == 0)
    print(f"{environment}: {application['size']:,} / {factory['size']:,} application bytes", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=["all", *TARGETS], default="all")
    parser.add_argument("--package", action="store_true", help="Package after a matched all-target build")
    args = parser.parse_args()
    if args.package and args.target != "all":
        parser.error("--package requires --target all")
    try:
        platformio = subprocess.check_output([sys.executable, "-m", "platformio", "--version"], text=True).strip()
        initial = source_snapshot()
        if args.target == "all":
            RECEIPT.unlink(missing_ok=True)
        for name in TARGETS if args.target == "all" else [args.target]:
            build(name)
        if args.target == "all":
            final = source_snapshot()
            if initial != final:
                raise RuntimeError("Build inputs changed during compilation. Run the matched build again after edits/dependency resolution finish")
            receipt = {"format": 1, "built_utc": datetime.now(timezone.utc).isoformat(),
                       "platformio": platformio, "source": final,
                       "targets": {name: inspect_target(name) for name in TARGETS}}
            RECEIPT.parent.mkdir(parents=True, exist_ok=True)
            RECEIPT.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
            print(f"Matched build receipt: {RECEIPT}")
            if args.package:
                print(f"Development package: {package()}")
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Build stopped: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
