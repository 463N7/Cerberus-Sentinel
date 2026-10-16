#!/usr/bin/env python3
"""Exercise web export integrity, chip separation, private-file exclusion, and selection."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

import export_web_release as release


def partition(name, kind, subtype, offset, size):
    return struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, name.encode(), 0)


def partition_table(name):
    body = partition("nvs", 1, 2, 0x9000, 0x6000)
    body += partition("phy_init", 1, 1, 0xF000, 0x1000)
    body += partition("factory", 0, 0, 0x10000, 0x1F0000 if name == "c5" else 0x700000)
    if name != "c5":
        body += partition("coredump", 1, 3, 0x710000, 0x40000)
    return body + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest() + b"\xff" * 32


def image(chip):
    data = bytearray(128)
    data[0] = 0xE9
    struct.pack_into("<H", data, 12, chip)
    return bytes(data)


class ExportChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="sentinel-web-export-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.package = self.root / "cerberus-sentinel-0.5.0-dev-fixture"
        self.output = self.root / "public/firmware"
        self.package.mkdir()
        (self.root / "shared").mkdir()
        header = b"#define CERBERUS_PROTOCOL_VERSION 2U\n#define CERBERUS_C5_FIRMWARE_VERSION 31U\n"
        (self.root / "shared/cerberus_protocol.h").write_bytes(header)
        source_files = {"shared/cerberus_protocol.h": release.digest(header)}
        self.manifest = {
            "format": 1, "product": "Cerberus Sentinel", "status": "development candidate",
            "hardware_validation": "not performed by packaging", "built_utc": "2026-10-16T12:00:00Z",
            "source": {"files": source_files, "sha256": release.digest(json.dumps(source_files, sort_keys=True, separators=(",", ":")).encode())},
            "targets": {},
        }
        for name, (chip, size, chip_id) in release.PROFILE.items():
            (self.package / name).mkdir()
            data = partition_table(name)
            target = {"chip": chip, "flash_size": size, "project_version": "0.5.0-dev",
                      "flash_settings": {"flash_mode": "dio", "flash_size": f"{size // (1024 * 1024)}MB", "flash_freq": "80m"},
                      "partitions": release.read_partitions(data, size), "images": []}
            for role, offset in (("bootloader", 0x2000 if name == "c5" else 0), ("partition-table", 0x8000), ("app", 0x10000)):
                payload = data if role == "partition-table" else image(chip_id)
                filename = f"{name}/{role}.bin"
                (self.package / filename).write_bytes(payload)
                target["images"].append({"role": role, "offset": offset, "size": len(payload), "file": filename, "sha256": release.digest(payload)})
            self.manifest["targets"][name] = target
        for name in release.NOTICES:
            (self.package / name).write_text("Required redistribution attribution\n")
        (self.package / "BUILD_AND_RECOVERY.md").write_text("Private workstation notes must not be exported")
        (self.package / "README.txt").write_text("Private package notes must not be exported")
        self.save()

    @staticmethod
    def checksums(directory):
        entries = "".join(f"{release.digest(path.read_bytes())}  {path.relative_to(directory).as_posix()}\n"
                          for path in sorted(directory.rglob("*")) if path.is_file() and path.name != "SHA256SUMS.txt")
        (directory / "SHA256SUMS.txt").write_text(entries, encoding="utf-8")

    def save(self):
        (self.package / "manifest.json").write_bytes(release.json_bytes(self.manifest))
        self.checksums(self.package)

    def export(self, output=None, **kwargs):
        return release.export_release(self.package, output or self.output, source_root=self.root, **kwargs)

    def test_export_has_exact_parts_offsets_hashes_and_no_private_files(self):
        exported = self.export(set_latest=True)
        manifest = release.validate_site(self.output)
        self.assertEqual(manifest["version"], "0.5.0-dev")
        self.assertEqual(manifest["status"], "development candidate")
        self.assertEqual(manifest["expectations"], {"protocol": 2, "c5Firmware": 31})
        self.assertEqual(manifest["targets"]["c5"]["parts"][0]["offset"], 0x2000)
        self.assertEqual(manifest["targets"]["c5"]["eraseRegions"], [{"offset": 0, "size": 4096, "reason": "legacy-boot-header"}])
        self.assertEqual(manifest["targets"]["c5"]["baud"], 115200)
        self.assertEqual(manifest["targets"]["s3"]["baud"], 460800)
        self.assertEqual(len(list(exported.rglob("*.bin"))), 9)
        self.assertFalse((exported / "BUILD_AND_RECOVERY.md").exists())
        self.assertFalse((exported / "README.txt").exists())
        self.assertNotIn("files", manifest)
        for target in manifest["targets"].values():
            for part in target["parts"]:
                data = (exported / part["path"]).read_bytes()
                self.assertEqual(part["sha256"], release.digest(data))
                self.assertEqual(part["md5"], release.digest(data, "md5"))

    def test_does_not_select_or_overwrite_implicitly(self):
        self.export()
        self.assertFalse((self.output / "latest.json").exists())
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.export(set_latest=True)
        self.assertFalse((self.output / "latest.json").exists())

    def test_all_checksum_entries_verified_before_output_created(self):
        (self.package / "README.txt").write_text("changed")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            self.export()
        self.assertFalse(self.output.exists())

    def test_missing_duplicate_and_undeclared_files_rejected(self):
        path = self.package / "SHA256SUMS.txt"
        original = path.read_text()
        path.write_text(original + original.splitlines()[0] + "\n")
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            release.validate_package(self.package)
        path.write_text(original)
        (self.package / "unlisted.bin").write_bytes(b"unlisted")
        with self.assertRaisesRegex(ValueError, "missing its checksum"):
            release.validate_package(self.package)

    def test_path_traversal_and_absolute_names_rejected(self):
        for bad in ("../outside.bin", "/outside.bin", "C:/outside.bin", "s3\\app.bin", "./manifest.json", "s3//app.bin"):
            with self.subTest(name=bad), self.assertRaises(ValueError):
                release.safe_file(self.package, bad)
        with self.assertRaisesRegex(ValueError, "portable"):
            self.export(release_id="../outside")

    def test_wrong_chip_header_rejected_even_with_updated_hash(self):
        part = self.manifest["targets"]["c5"]["images"][2]
        payload = image(9)
        (self.package / part["file"]).write_bytes(payload)
        part["sha256"] = release.digest(payload)
        self.save()
        with self.assertRaisesRegex(ValueError, "image header"):
            release.validate_package(self.package)

    def test_wrong_offsets_and_cross_chip_paths_rejected(self):
        part = self.manifest["targets"]["c5"]["images"][0]
        part["offset"] = 0
        self.save()
        with self.assertRaisesRegex(ValueError, "offsets"):
            release.validate_package(self.package)
        part["offset"] = 0x2000
        part["file"] = "s3/bootloader.bin"
        self.save()
        with self.assertRaisesRegex(ValueError, "image path"):
            release.validate_package(self.package)

    def test_version_and_source_mismatch_rejected(self):
        self.manifest["targets"]["c5"]["project_version"] = "other"
        self.save()
        with self.assertRaisesRegex(ValueError, "versions"):
            release.validate_package(self.package)
        self.manifest["targets"]["c5"]["project_version"] = "0.5.0-dev"
        self.manifest["source"]["sha256"] = "0" * 64
        self.save()
        with self.assertRaisesRegex(ValueError, "provenance hash mismatch"):
            release.validate_package(self.package)

    def test_partition_metadata_and_missing_notices_rejected(self):
        self.manifest["targets"]["c5"]["partitions"][0]["size"] += 4096
        self.save()
        with self.assertRaisesRegex(ValueError, "partition metadata"):
            release.validate_package(self.package)
        self.manifest["targets"]["c5"]["partitions"][0]["size"] -= 4096
        (self.package / release.NOTICES[0]).unlink()
        self.save()
        with self.assertRaisesRegex(ValueError, "notices"):
            release.validate_package(self.package)

    def test_rounded_image_sectors_cannot_touch_next_region(self):
        part = self.manifest["targets"]["c5"]["images"][0]
        payload = image(23) + bytes(0x6001 - 128)
        part["size"] = len(payload)
        part["sha256"] = release.digest(payload)
        (self.package / part["file"]).write_bytes(payload)
        self.save()
        with self.assertRaisesRegex(ValueError, "overlapping"):
            release.validate_package(self.package)

    def test_corrupt_export_and_pointer_rejected(self):
        exported = self.export(set_latest=True)
        pointer = self.output / "latest.json"
        original = pointer.read_bytes()
        data = json.loads(original)
        data["sha256"] = "0" * 64
        pointer.write_bytes(release.json_bytes(data))
        with self.assertRaisesRegex(ValueError, "Latest manifest SHA-256"):
            release.validate_site(self.output)
        pointer.write_bytes(original)
        (exported / "s3/app.bin").write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            release.validate_site(self.output)

    def test_web_erase_region_cannot_expand_into_nvs(self):
        exported = self.export()
        manifest_path = exported / "manifest.json"
        manifest = json.loads(manifest_path.read_bytes())
        manifest["targets"]["c5"]["eraseRegions"][0]["size"] = 0x10000
        manifest_path.write_bytes(release.json_bytes(manifest))
        self.checksums(exported)
        with self.assertRaisesRegex(ValueError, "unsafe erase"):
            release.validate_web_release(exported)

    def test_export_keeps_package_and_destination_separate(self):
        for destination in (self.package, self.package / "web", self.root):
            with self.subTest(destination=destination), self.assertRaisesRegex(ValueError, "separate"):
                self.export(destination)

    def test_protocol_metadata_requires_matching_build_source(self):
        header = self.root / "shared/cerberus_protocol.h"
        header.write_text("#define CERBERUS_PROTOCOL_VERSION 3U\n#define CERBERUS_C5_FIRMWARE_VERSION 32U\n")
        with self.assertRaisesRegex(ValueError, "Protocol source differs"):
            self.export()
        self.assertFalse(self.output.exists())

    def test_public_tree_rejects_undeclared_files(self):
        self.export(set_latest=True)
        (self.output / "private-notes.txt").write_text("private notes")
        with self.assertRaisesRegex(ValueError, "Unexpected public"):
            release.validate_site(self.output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
