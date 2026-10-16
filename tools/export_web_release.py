#!/usr/bin/env python3
"""Validate a matched package and export only its public web-flasher assets."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import sys
import tempfile

from package_sentinel import ROOT, read_partitions

PROFILE = {
    "bridge": ("esp32s3", 16 * 1024 * 1024, 9),
    "c5": ("esp32c5", 2 * 1024 * 1024, 23),
    "s3": ("esp32s3", 16 * 1024 * 1024, 9),
}
ROLES = ("bootloader", "partition-table", "app")
NOTICES = ("WATCH_SIGNATURES.md", "WATCH_THIRD_PARTY_NOTICES.md")
SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,159}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def digest(data: bytes, algorithm: str = "sha256") -> str:
    return hashlib.new(algorithm, data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def safe_file(root: Path, name: str) -> Path:
    """Require portable relative file paths, with no links or escape routes."""
    if not isinstance(name, str) or not name or "\\" in name or ":" in name:
        raise ValueError(f"Invalid relative asset path: {name!r}")
    relative = PurePosixPath(name)
    if relative.is_absolute() or any(part in {"", ".", ".."} for part in name.split("/")):
        raise ValueError(f"Unsafe asset path: {name!r}")
    path = root
    for part in relative.parts:
        path = path / part
        if path.is_symlink():
            raise ValueError(f"Symbolic links are not release assets: {name}")
    if not path.resolve().is_relative_to(root.resolve()) or not path.is_file():
        raise ValueError(f"Missing or external asset: {name}")
    return path


def checked_files(root: Path) -> dict[str, bytes]:
    """Check every declared file, and reject undeclared files and symlinks."""
    checksum_file = safe_file(root, "SHA256SUMS.txt")
    result = {}
    for line in checksum_file.read_text(encoding="utf-8").splitlines():
        expected, separator, name = line.partition("  ")
        if not separator or not SHA256.fullmatch(expected) or name == "SHA256SUMS.txt":
            raise ValueError("Malformed checksum entry")
        if name in result:
            raise ValueError(f"Duplicate checksum entry: {name}")
        data = safe_file(root, name).read_bytes()
        if digest(data) != expected:
            raise ValueError(f"SHA-256 mismatch: {name}")
        result[name] = data
    if not result:
        raise ValueError("Empty checksum list")
    for path in root.rglob("*"):
        if path.is_symlink():
            raise ValueError("Symbolic links are not release assets")
        if path.is_file() and path.relative_to(root).as_posix() != "SHA256SUMS.txt" and path.relative_to(root).as_posix() not in result:
            raise ValueError(f"File is missing its checksum: {path.relative_to(root)}")
    return result


def integer(value: object, label: str, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise ValueError(f"Invalid {label}")
    return value


def validate_images(name: str, parts: list, files: dict[str, bytes], partitions: list,
                    flash_size: int, *, web: bool = False) -> None:
    chip, expected_size, image_chip = PROFILE[name]
    if flash_size != expected_size or not isinstance(parts, list) or len(parts) != 3:
        raise ValueError(f"{name}: invalid flash profile or image set")
    if any(not isinstance(part, dict) for part in parts):
        raise ValueError(f"{name}: invalid image metadata")
    if {part.get("role") for part in parts} != set(ROLES):
        raise ValueError(f"{name}: expected bootloader, partition-table, and app")
    previous_end = 0
    by_role = {}
    for part in sorted(parts, key=lambda item: integer(item.get("offset"), "image offset")):
        role = part["role"]
        path = part.get("path" if web else "file")
        if path != f"{name}/{role}.bin" or path not in files:
            raise ValueError(f"{name}: unexpected image path")
        data = files[path]
        offset = integer(part.get("offset"), "image offset")
        size = integer(part.get("size"), "image size", 1)
        # Every write erases whole 4 KiB sectors; never straddle an adjacent region.
        end = offset + ((size + 4095) // 4096) * 4096
        if offset % 4096 or offset < previous_end or end > flash_size:
            raise ValueError(f"{name}: overlapping or out-of-bounds image sectors")
        if size != len(data) or digest(data) != part.get("sha256"):
            raise ValueError(f"{name}: image size or SHA-256 mismatch")
        if web and digest(data, "md5") != part.get("md5"):
            raise ValueError(f"{name}: image MD5 mismatch")
        if role != "partition-table":
            if len(data) < 24 or data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != image_chip:
                raise ValueError(f"{name}: image header does not target {chip}")
        by_role[role] = part
        previous_end = end
    offsets = {"bootloader": 0x2000 if name == "c5" else 0, "partition-table": 0x8000, "app": 0x10000}
    if any(by_role[role]["offset"] != offset for role, offset in offsets.items()):
        raise ValueError(f"{name}: unexpected bootloader/partition/app offsets")
    table_path = f"{name}/partition-table.bin"
    actual_partitions = read_partitions(files[table_path], flash_size)
    if actual_partitions != partitions:
        raise ValueError(f"{name}: partition metadata differs from binary")
    factory = [p for p in partitions if p["type"] == 0 and p["subtype"] == 0]
    expected_factory_size = 0x1F0000 if name == "c5" else 0x700000
    if len(factory) != 1 or factory[0]["offset"] != 0x10000 or factory[0]["size"] != expected_factory_size:
        raise ValueError(f"{name}: unexpected factory partition")
    app_end = by_role["app"]["offset"] + ((by_role["app"]["size"] + 4095) // 4096) * 4096
    if app_end > factory[0]["offset"] + factory[0]["size"]:
        raise ValueError(f"{name}: application exceeds factory partition")
    first_partition = min(partition["offset"] for partition in partitions)
    if by_role["partition-table"]["offset"] + ((by_role["partition-table"]["size"] + 4095) // 4096) * 4096 > first_partition:
        raise ValueError(f"{name}: partition image erases application data")
    nvs = [p for p in partitions if p["type"] == 1 and p["subtype"] == 2]
    if len(nvs) != 1 or nvs[0]["offset"] != 0x9000 or nvs[0]["size"] != 0x6000:
        raise ValueError(f"{name}: expected preserved NVS partition")


def validate_package(package: Path) -> tuple[dict, dict[str, bytes]]:
    files = checked_files(package)
    if "manifest.json" not in files or any(name not in files for name in NOTICES):
        raise ValueError("Package manifest or redistribution notices are missing")
    manifest = json.loads(files["manifest.json"])
    if manifest.get("format") != 1 or manifest.get("product") != "Cerberus Sentinel":
        raise ValueError("Unsupported package format or product")
    targets = manifest.get("targets", {})
    if set(targets) != set(PROFILE):
        raise ValueError("Package must contain matched bridge, C5, and S3 targets")
    versions = {target.get("project_version") for target in targets.values()}
    if len(versions) != 1 or not isinstance(next(iter(versions)), str) or not SAFE_NAME.fullmatch(next(iter(versions))):
        raise ValueError("Firmware versions do not match")
    source = manifest.get("source", {})
    if not isinstance(source.get("sha256"), str) or not SHA256.fullmatch(source["sha256"]):
        raise ValueError("Missing source provenance hash")
    source_files = source.get("files", {})
    if not isinstance(source_files, dict) or not source_files:
        raise ValueError("Missing source provenance file list")
    source_digest = digest(json.dumps(source_files, sort_keys=True, separators=(",", ":")).encode())
    if source_digest != source["sha256"]:
        raise ValueError("Source provenance hash mismatch")
    for name, target in targets.items():
        chip, flash_size, _ = PROFILE[name]
        settings = target.get("flash_settings", {})
        if target.get("chip") != chip or target.get("flash_size") != flash_size:
            raise ValueError(f"{name}: unexpected chip or memory profile")
        if settings.get("flash_size") != f"{flash_size // (1024 * 1024)}MB" or settings.get("flash_mode") != "dio" or settings.get("flash_freq") != "80m":
            raise ValueError(f"{name}: unexpected flash settings")
        validate_images(name, target.get("images"), files, target.get("partitions"), flash_size)
    return manifest, files


def read_expectations(manifest: dict, source_root: Path) -> dict:
    """Read the protocol contract only from a source file attested by the build."""
    name = "shared/cerberus_protocol.h"
    data = safe_file(source_root.resolve(), name).read_bytes()
    if digest(data) != manifest["source"]["files"].get(name):
        raise ValueError("Protocol source differs from packaged build; use its matching source checkout")
    source = data.decode("utf-8")
    expectations = {}
    for field, define in (("protocol", "CERBERUS_PROTOCOL_VERSION"), ("c5Firmware", "CERBERUS_C5_FIRMWARE_VERSION")):
        matches = re.findall(r"^\s*#define\s+" + define + r"\s+(\d+)U?\s*$", source, re.MULTILINE)
        if len(matches) != 1 or int(matches[0]) <= 0:
            raise ValueError(f"Missing or ambiguous protocol contract: {define}")
        expectations[field] = int(matches[0])
    return expectations


def export_release(package: Path, output: Path, *, release_id: str | None = None,
                   set_latest: bool = False, source_root: Path = ROOT) -> Path:
    package = package.resolve()
    release_id = release_id or package.name
    if not SAFE_NAME.fullmatch(release_id):
        raise ValueError("Release ID must be a portable directory name")
    manifest, files = validate_package(package)
    expectations = read_expectations(manifest, source_root)
    output = output.resolve()
    if output == package or output.is_relative_to(package) or package.is_relative_to(output):
        raise ValueError("Package and export directories must be separate")
    destination = output / release_id
    if destination.exists():
        raise ValueError(f"Release already exists: {destination}")
    targets = {}
    for name, (chip, flash_size, _) in PROFILE.items():
        target = manifest["targets"][name]
        targets[name] = {
            "chip": chip, "flashSizeBytes": flash_size,
            "flashSize": target["flash_settings"]["flash_size"],
            "flashMode": target["flash_settings"]["flash_mode"],
            "flashFrequency": target["flash_settings"]["flash_freq"],
            "baud": 115200 if name == "c5" else 460800,
            "eraseRegions": [{"offset": 0, "size": 4096, "reason": "legacy-boot-header"}] if name == "c5" else [],
            "parts": [{"role": image["role"], "path": image["file"],
                       "offset": image["offset"], "size": image["size"],
                       "sha256": image["sha256"], "md5": digest(files[image["file"]], "md5")}
                      for image in target["images"]],
            "partitions": target["partitions"],
        }
    exported = {
        "schemaVersion": 1, "product": "Cerberus Sentinel", "releaseId": release_id,
        "version": manifest["targets"]["s3"]["project_version"],
        "status": manifest["status"], "hardwareValidation": manifest.get("hardware_validation", "not recorded"),
        "builtUtc": manifest["built_utc"], "sourceSha256": manifest["source"]["sha256"],
        "packageManifestSha256": digest(files["manifest.json"]),
        "hardware": {"s3FlashBytes": 16777216, "s3PsramBytes": 2097152, "c5MinimumFlashBytes": 2097152},
        "expectations": expectations, "notices": list(NOTICES), "targets": targets,
    }
    output.mkdir(parents=True, exist_ok=True)
    # Windows dev-server watchers can hold directory handles that prevent a
    # rename. Copy to a new immutable directory, verify that exact copy, and
    # update latest.json only afterwards. No existing release is overwritten.
    # The temporary directory owns its cleanup; no user directory is deleted.
    with tempfile.TemporaryDirectory(prefix=".web-release-", dir=output) as temporary:
        staging = Path(temporary) / release_id
        staging.mkdir()
        for name in PROFILE:
            (staging / name).mkdir()
            for part in targets[name]["parts"]:
                (staging / part["path"]).write_bytes(files[part["path"]])
        for name in NOTICES:
            (staging / name).write_bytes(files[name])
        (staging / "manifest.json").write_bytes(json_bytes(exported))
        checksums = "".join(f"{digest(path.read_bytes())}  {path.relative_to(staging).as_posix()}\n"
                            for path in sorted(staging.rglob("*")) if path.is_file())
        (staging / "SHA256SUMS.txt").write_text(checksums, encoding="utf-8")
        validate_web_release(staging)
        if staging.parent.parent.resolve() != output or destination.parent.resolve() != output:
            raise ValueError("Unexpected export destination")
        shutil.copytree(staging, destination)
        validate_web_release(destination)
    if set_latest:
        pointer = {"manifest": f"{release_id}/manifest.json", "sha256": digest((destination / "manifest.json").read_bytes())}
        latest = output / "latest.json"
        if latest.is_symlink():
            raise ValueError("Latest pointer cannot be a symbolic link")
        with tempfile.NamedTemporaryFile(prefix=".latest-", suffix=".json", dir=output, delete=False) as stream:
            stream.write(json_bytes(pointer))
            temporary_pointer = Path(stream.name)
        temporary_pointer.replace(latest)
    return destination


def validate_web_release(directory: Path) -> dict:
    files = checked_files(directory)
    expected_files = {"manifest.json", *NOTICES, *(f"{name}/{role}.bin" for name in PROFILE for role in ROLES)}
    if set(files) != expected_files:
        raise ValueError("Web release contains missing or non-public files")
    manifest = json.loads(files["manifest.json"])
    if manifest.get("schemaVersion") != 1 or manifest.get("product") != "Cerberus Sentinel" or manifest.get("releaseId") != directory.name:
        raise ValueError("Unsupported web manifest or release ID")
    if manifest.get("notices") != list(NOTICES) or set(manifest.get("targets", {})) != set(PROFILE):
        raise ValueError("Web manifest is missing target or notice metadata")
    for field in ("protocol", "c5Firmware"):
        integer(manifest.get("expectations", {}).get(field), f"expected {field}", 1)
    for name, target in manifest["targets"].items():
        if target.get("chip") != PROFILE[name][0] or target.get("flashSize") != f"{PROFILE[name][1] // (1024 * 1024)}MB" or target.get("flashMode") != "dio" or target.get("flashFrequency") != "80m":
            raise ValueError(f"{name}: invalid web target profile")
        expected_erase = [{"offset": 0, "size": 4096, "reason": "legacy-boot-header"}] if name == "c5" else []
        if target.get("eraseRegions") != expected_erase:
            raise ValueError(f"{name}: unsafe erase region")
        validate_images(name, target.get("parts"), files, target.get("partitions"), target.get("flashSizeBytes"), web=True)
    return manifest


def validate_site(directory: Path) -> dict:
    directory = directory.resolve()
    pointer = json.loads(safe_file(directory, "latest.json").read_bytes())
    manifest_path = safe_file(directory, pointer.get("manifest"))
    if manifest_path.name != "manifest.json" or manifest_path.parent.parent != directory:
        raise ValueError("Latest pointer must address one release directory")
    if digest(manifest_path.read_bytes()) != pointer.get("sha256"):
        raise ValueError("Latest manifest SHA-256 mismatch")
    selected = None
    for path in directory.iterdir():
        if path.is_symlink():
            raise ValueError("Public firmware cannot contain symbolic links")
        if path.name == "latest.json":
            continue
        if not path.is_dir() or not SAFE_NAME.fullmatch(path.name):
            raise ValueError(f"Unexpected public firmware asset: {path.name}")
        manifest = validate_web_release(path)
        if path == manifest_path.parent:
            selected = manifest
    if selected is None:
        raise ValueError("Selected release was not found")
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, nargs="?", help="Existing matched package directory")
    parser.add_argument("--output", type=Path, default=ROOT / "web/public/firmware")
    parser.add_argument("--release-id", help="Immutable directory name; defaults to the package name")
    parser.add_argument("--source-root", type=Path, default=ROOT, help="Matching source checkout for the attested protocol contract")
    parser.add_argument("--set-latest", action="store_true", help="Select this export in the web flasher; never changes release qualification")
    parser.add_argument("--verify-only", action="store_true", help="Validate the input package without exporting")
    parser.add_argument("--verify-site", type=Path, help="Validate an existing public firmware directory and selected release")
    args = parser.parse_args()
    if args.verify_site and (args.package or args.verify_only or args.set_latest or args.release_id):
        parser.error("--verify-site cannot be combined with package/export options")
    if not args.verify_site and args.package is None:
        parser.error("Provide a matched package directory or --verify-site")
    try:
        if args.verify_site:
            manifest = validate_site(args.verify_site)
            print(f"Verified web release: {manifest['releaseId']}")
        elif args.verify_only:
            validate_package(args.package.resolve())
            print("Package hashes, chip headers, partitions, and image bounds are valid. No files changed.")
        else:
            print(f"Exported web release: {export_release(args.package, args.output, release_id=args.release_id, set_latest=args.set_latest, source_root=args.source_root)}")
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"Cannot export web release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
