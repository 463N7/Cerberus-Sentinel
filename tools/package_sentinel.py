#!/usr/bin/env python3
"""Package matched firmware; stable releases require explicit owner acceptance."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
from datetime import date, datetime, timezone
from pathlib import Path

from build_toolchain import toolchain_provenance

ROOT = Path(__file__).resolve().parents[1]
RECEIPT = ROOT / "artifacts" / "build" / "matched-build.json"
TARGETS = {
    "s3": (ROOT, "s3-main", "esp32s3", 16 * 1024 * 1024),
    "c5": (ROOT / "c5_firmware", "c5-onboard", "esp32c5", 2 * 1024 * 1024),
    "bridge": (ROOT, "s3-c5-bridge", "esp32s3", 16 * 1024 * 1024),
}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def source_snapshot() -> dict:
    """Capture build inputs, including uncommitted files and component locks."""
    paths = set()
    for directory in ("shared", "boards", "s3_main", "c5_firmware/main"):
        for path in (ROOT / directory).rglob("*"):
            if path.is_file() and not any(p in {".pio", "managed_components", "__pycache__"} for p in path.parts):
                paths.add(path)
    for directory in (ROOT, ROOT / "c5_firmware"):
        for pattern in ("CMakeLists.txt", "platformio.ini", "platformio.local.ini", "partitions*.csv", "sdkconfig.defaults", "sdkconfig.*.defaults", "dependencies.lock"):
            paths.update(directory.glob(pattern))
    for project, environment, _, _ in TARGETS.values():
        effective_config = project / f"sdkconfig.{environment}"
        if effective_config.is_file():
            paths.add(effective_config)
    files = {path.relative_to(ROOT).as_posix(): sha256(path) for path in sorted(paths)}
    digest = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return {"sha256": digest, "files": files}


def read_partitions(data: bytes, flash_size: int) -> list[dict]:
    """Read the generated ESP-IDF partition binary and check bounds + MD5."""
    partitions = []
    md5_seen = False
    for index in range(0, len(data), 32):
        entry = data[index:index + 32]
        if len(entry) != 32:
            raise ValueError("Truncated partition table entry")
        magic = struct.unpack_from("<H", entry)[0]
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            if hashlib.md5(data[:index]).digest() != entry[16:32]:
                raise ValueError("Partition table MD5 mismatch")
            md5_seen = True
            break
        if magic != 0x50AA:
            raise ValueError(f"Unknown partition magic 0x{magic:04x}")
        _, kind, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", entry)
        name = label.split(b"\0", 1)[0].decode("ascii")
        if offset < 0x9000 or size <= 0 or offset + size > flash_size:
            raise ValueError(f"Partition {name} exceeds the flash map")
        alignment = 0x10000 if kind == 0 else 0x1000
        if offset % alignment or size % 0x1000:
            raise ValueError(f"Partition {name} has invalid alignment")
        if flags:
            raise ValueError("Encrypted/flagged partitions need an explicit packaging workflow")
        partitions.append({"name": name, "type": kind, "subtype": subtype, "offset": offset, "size": size})
    if not partitions or not md5_seen:
        raise ValueError("Expected a nonempty partition table with an MD5 record")
    previous_end = 0x9000
    for partition in sorted(partitions, key=lambda p: p["offset"]):
        if partition["offset"] < previous_end:
            raise ValueError("Partition regions overlap")
        previous_end = partition["offset"] + partition["size"]
    return partitions


def inspect_target(name: str) -> dict:
    project, environment, chip, flash_size = TARGETS[name]
    build = project / ".pio" / "build" / environment
    metadata = json.loads((build / "flasher_args.json").read_text())
    description = json.loads((build / "project_description.json").read_text())
    configuration = json.loads((build / "config" / "sdkconfig.json").read_text())
    if metadata["extra_esptool_args"]["chip"] != chip or description["target"] != chip:
        raise ValueError(f"{name}: wrong chip in generated build metadata")
    if configuration["ESPTOOLPY_FLASHSIZE"] != f"{flash_size // (1024 * 1024)}MB":
        raise ValueError(f"{name}: unexpected flash size")
    settings = metadata["flash_settings"]
    if settings["flash_size"] != configuration["ESPTOOLPY_FLASHSIZE"]:
        raise ValueError(f"{name}: flash arguments disagree with the configured flash size")
    if settings["flash_mode"] != configuration["ESPTOOLPY_FLASHMODE"] or \
            settings["flash_freq"] != configuration["ESPTOOLPY_FLASHFREQ"]:
        raise ValueError(f"{name}: flash arguments disagree with the configured mode/frequency")
    if chip == "esp32s3":
        if not configuration.get("SPIRAM_MODE_QUAD") or configuration.get("SPIRAM_MODE_OCT"):
            raise ValueError(f"{name}: production S3 must use quad PSRAM")
        if not configuration.get("SPIRAM_USE_CAPS_ALLOC"):
            raise ValueError(f"{name}: PSRAM must use explicit capability allocations")
    elif configuration.get("SPIRAM"):
        raise ValueError("C5 build must not require external PSRAM")
    images = []
    aliases = {"bootloader": "bootloader.bin", "partition-table": "partitions.bin", "app": "firmware.bin"}
    flash_files = metadata["flash_files"]
    if len(flash_files) != len(aliases):
        raise ValueError(f"{name}: unexpected flash image set")
    for role, fallback in aliases.items():
        entry = metadata[role]
        address = int(entry["offset"], 0)
        if entry.get("encrypted") not in (False, "false", None):
            raise ValueError("Encrypted builds need a separate release workflow")
        if flash_files.get(entry["offset"]) != entry["file"]:
            raise ValueError(f"{name}: inconsistent flash file metadata")
        # PlatformIO renames CMake's outputs. Resolve only the known exact alias.
        source = build / entry["file"]
        if not source.is_file():
            source = build / fallback
        if not source.is_file() or not source.resolve().is_relative_to(build.resolve()):
            raise ValueError(f"{name}: missing or invalid {role} binary")
        images.append({"role": role, "offset": address, "size": source.stat().st_size,
                       "source": source.relative_to(ROOT).as_posix(), "sha256": sha256(source)})
    by_role = {image["role"]: image for image in images}
    expected_bootloader = 0x2000 if chip == "esp32c5" else 0
    if by_role["bootloader"]["offset"] != expected_bootloader:
        raise ValueError(f"{name}: unexpected bootloader offset")
    if by_role["partition-table"]["offset"] != configuration["PARTITION_TABLE_OFFSET"]:
        raise ValueError(f"{name}: partition table offset mismatch")
    table = ROOT / by_role["partition-table"]["source"]
    partitions = read_partitions(table.read_bytes(), flash_size)
    factory = [p for p in partitions if p["type"] == 0 and p["subtype"] == 0]
    if len(factory) != 1:
        raise ValueError(f"{name}: expected exactly one factory app")
    app = by_role["app"]
    if app["offset"] != factory[0]["offset"] or app["size"] > factory[0]["size"]:
        raise ValueError(f"{name}: application does not fit its factory partition")
    if by_role["partition-table"]["offset"] + by_role["partition-table"]["size"] > min(p["offset"] for p in partitions):
        raise ValueError(f"{name}: partition image overlaps a partition")
    previous_end = 0
    for image in sorted(images, key=lambda i: i["offset"]):
        if image["size"] <= 0 or image["offset"] < previous_end or image["offset"] + image["size"] > flash_size:
            raise ValueError(f"{name}: invalid/overlapping image region")
        previous_end = image["offset"] + image["size"]
    return {"environment": environment, "chip": chip, "flash_size": flash_size,
            "project_version": description["project_version"], "idf_version": description["git_revision"],
            "toolchain": toolchain_provenance(build, chip),
            "flash_settings": metadata["flash_settings"], "partitions": partitions, "images": images}


def package(output: Path | None = None, *, release: bool = False, accepted_on: str | None = None) -> Path:
    receipt = json.loads(RECEIPT.read_text())
    current_source = source_snapshot()
    if current_source != receipt["source"]:
        raise ValueError("Build inputs changed. Run tools/build_sentinel.py --target all again")
    targets = {name: inspect_target(name) for name in TARGETS}
    if targets != receipt["targets"]:
        raise ValueError("Build outputs changed or are mismatched. Rebuild all three targets")
    versions = {target["project_version"] for target in targets.values()}
    if len(versions) != 1:
        raise ValueError("S3, C5 and bridge project versions must match")
    version = next(iter(versions))
    if not all(c.isalnum() or c in ".-_" for c in version):
        raise ValueError("Unsafe version string")
    if release:
        if not re.fullmatch(r"\d+\.\d+\.\d+", version):
            raise ValueError("A stable release requires a numeric major.minor.patch version")
        if not accepted_on or date.fromisoformat(accepted_on).isoformat() != accepted_on:
            raise ValueError("A stable release requires --accepted-on YYYY-MM-DD for owner-reported acceptance")
    elif accepted_on is not None:
        raise ValueError("--accepted-on requires --release")
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = output or ROOT / "dist" / f"cerberus-sentinel-{version}-{timestamp}"
    destination = destination.resolve()
    if destination.exists():
        raise ValueError(f"Output already exists: {destination}")
    destination.mkdir(parents=True)
    for name, target in targets.items():
        directory = destination / name
        directory.mkdir()
        lines = [f"--flash_mode {target['flash_settings']['flash_mode']} "
                 f"--flash_size {target['flash_settings']['flash_size']} "
                 f"--flash_freq {target['flash_settings']['flash_freq']}"]
        for image in target["images"]:
            filename = image["role"] + ".bin"
            shutil.copy2(ROOT / image["source"], directory / filename)
            if sha256(directory / filename) != image["sha256"]:
                raise ValueError(f"{name}: image changed while packaging; rebuild the matched set")
            image["file"] = f"{name}/{filename}"
            lines.append(f"0x{image['offset']:x} {filename}")
            del image["source"]
        (directory / "flash_args.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    status = "stable" if release else "development candidate"
    validation = f"Owner-reported acceptance on {accepted_on}; packaging performs no hardware tests" if release else "not performed by packaging"
    manifest = {"format": 1, "product": "Cerberus Sentinel", "status": status,
                "hardware_validation": validation,
                "built_utc": receipt["built_utc"], "packaged_utc": timestamp,
                "platformio": receipt["platformio"], "source": current_source, "targets": targets}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(ROOT / "docs" / "SENTINEL_BUILD.md", destination / "BUILD_AND_RECOVERY.md")
    # Keep protocol provenance and redistribution notices with binary builds.
    for name in ("WATCH_SIGNATURES.md", "WATCH_THIRD_PARTY_NOTICES.md"):
        shutil.copy2(ROOT / "docs" / name, destination / name)
    (destination / "README.txt").write_text(
        f"CERBERUS SENTINEL {version} — {status.upper()}\n\n"
        "Production N16R2 S3 + onboard C5 (2 MB flash budget).\n"
        f"{validation}.\n"
        "S3, C5 and bridge are a matched set. Preserve their separate chip destinations.\n"
        "C5 bootloader starts at 0x2000. Never write a C5 image to the S3.\n"
        "flash_args.txt describes each target's actual image offsets; it is NOT a one-step installer.\n"
        "Use the source project's documented USB bridge workflow for the onboard C5.\n"
        "The current partition maps have a single factory application and no OTA rollback slot.\n"
        "No hardware has been flashed by creating this package.\n", encoding="utf-8")
    checksums = "".join(f"{sha256(path)}  {path.relative_to(destination).as_posix()}\n"
                        for path in sorted(destination.rglob("*")) if path.is_file())
    (destination / "SHA256SUMS.txt").write_text(checksums, encoding="utf-8")
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="New directory to create (never overwritten)")
    parser.add_argument("--release", action="store_true", help="Package a stable release explicitly approved by the owner")
    parser.add_argument("--accepted-on", help="Owner-reported acceptance date (YYYY-MM-DD); required with --release")
    args = parser.parse_args()
    try:
        destination = package(args.output, release=args.release, accepted_on=args.accepted_on)
    except (OSError, ValueError, KeyError) as error:
        print(f"Cannot package: {error}", file=sys.stderr)
        return 1
    print(f"Verified {'release' if args.release else 'development'} package: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
