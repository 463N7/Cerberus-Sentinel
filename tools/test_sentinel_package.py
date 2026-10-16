#!/usr/bin/env python3
"""Exercise package rejection gates with isolated generated-build fixtures."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import package_sentinel as release


def partition(offset=0x10000, size=0x10000, *, kind=0, flags=0):
    return struct.pack("<HBBII16sI", 0x50AA, kind, 0, offset, size, b"factory", flags)


def table(*entries):
    body = b"".join(entries)
    return body + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(body).digest() + b"\xff" * 32


class PartitionChecks(unittest.TestCase):
    def test_valid_map(self):
        result = release.read_partitions(table(partition()), 0x200000)
        self.assertEqual(result[0]["offset"], 0x10000)
        self.assertEqual(result[0]["size"], 0x10000)

    def test_corrupt_and_missing_digest(self):
        data = bytearray(table(partition()))
        data[20] ^= 1
        for invalid in (bytes(data), partition(), b"\xff" * 32, b"\x00", b"\x00" * 32):
            with self.subTest(data=invalid[:8]), self.assertRaises(ValueError):
                release.read_partitions(invalid, 0x200000)

    def test_alignment_overlap_and_bounds(self):
        invalid = [table(partition(offset=0x11000)), table(partition(size=0x10001)),
                   table(partition(offset=0x8000)), table(partition(size=0)),
                   table(partition(size=0x200000)), table(partition(flags=1)),
                   table(partition(), partition(offset=0x10000, kind=1))]
        for data in invalid:
            with self.subTest(data=data[:16]), self.assertRaises(ValueError):
                release.read_partitions(data, 0x200000)


class PackageChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="sentinel-package-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        targets = {
            "s3": (self.root, "s3-main", "esp32s3", 16 * 1024 * 1024),
            "c5": (self.root / "c5_firmware", "c5-onboard", "esp32c5", 2 * 1024 * 1024),
            "bridge": (self.root, "s3-c5-bridge", "esp32s3", 16 * 1024 * 1024),
        }
        patches = patch.multiple(release, ROOT=self.root, TARGETS=targets,
                                 RECEIPT=self.root / "receipt.json")
        patches.start()
        self.addCleanup(patches.stop)
        (self.root / "docs").mkdir()
        (self.root / "docs/SENTINEL_BUILD.md").write_text("Fixture documentation")
        (self.root / "docs/WATCH_SIGNATURES.md").write_text("Fixture signature provenance")
        (self.root / "docs/WATCH_THIRD_PARTY_NOTICES.md").write_text("Fixture redistribution notices")
        (self.root / "platformio.ini").write_text("fixture build input")
        self.builds = {}
        for name, (project, environment, chip, flash_size) in targets.items():
            build = project / ".pio/build" / environment
            (build / "config").mkdir(parents=True)
            self.builds[name] = build
            config = {"ESPTOOLPY_FLASHSIZE": f"{flash_size // (1024 * 1024)}MB",
                      "ESPTOOLPY_FLASHMODE": "dio", "ESPTOOLPY_FLASHFREQ": "80m",
                      "PARTITION_TABLE_OFFSET": 0x8000,
                      "SPIRAM_MODE_QUAD": chip == "esp32s3",
                      "SPIRAM_USE_CAPS_ALLOC": chip == "esp32s3"}
            metadata = {"extra_esptool_args": {"chip": chip},
                        "flash_settings": {"flash_size": config["ESPTOOLPY_FLASHSIZE"],
                                           "flash_mode": "dio", "flash_freq": "80m"},
                        "flash_files": {}}
            for role, filename, offset in (("bootloader", "bootloader.bin", 0x2000 if name == "c5" else 0),
                                           ("partition-table", "partitions.bin", 0x8000),
                                           ("app", "firmware.bin", 0x10000)):
                entry = {"offset": hex(offset), "file": filename, "encrypted": False}
                metadata[role] = entry
                metadata["flash_files"][entry["offset"]] = filename
                data = table(partition()) if role == "partition-table" else b"fixture binary" * 16
                (build / filename).write_bytes(data)
            self.write(build / "flasher_args.json", metadata)
            self.write(build / "config/sdkconfig.json", config)
            self.write(build / "project_description.json",
                       {"target": chip, "project_version": "0.2.0-dev", "git_revision": "v6.0.1"})
        self.receipt()

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def mutate(self, name, file, modify):
        path = self.builds[name] / file
        data = json.loads(path.read_text())
        modify(data)
        self.write(path, data)

    def receipt(self):
        self.write(release.RECEIPT, {"source": release.source_snapshot(),
                   "targets": {name: release.inspect_target(name) for name in release.TARGETS},
                   "built_utc": "2026-01-01T00:00:00Z", "platformio": "fixture"})

    def test_package_checksums_offsets_and_no_overwrite(self):
        output = release.package(self.root / "package")
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertEqual(manifest["status"], "development candidate")
        self.assertEqual(manifest["targets"]["c5"]["images"][0]["offset"], 0x2000)
        args = (output / "c5/flash_args.txt").read_text()
        self.assertIn("--flash_size 2MB", args)
        self.assertIn("0x2000 bootloader.bin", args)
        self.assertEqual((output / "WATCH_SIGNATURES.md").read_text(), "Fixture signature provenance")
        self.assertEqual((output / "WATCH_THIRD_PARTY_NOTICES.md").read_text(), "Fixture redistribution notices")
        for line in (output / "SHA256SUMS.txt").read_text().splitlines():
            digest, filename = line.split("  ", 1)
            self.assertEqual(release.sha256(output / filename), digest)
        with self.assertRaisesRegex(ValueError, "already exists"):
            release.package(output)

    def test_changed_source_rejected(self):
        (self.root / "platformio.ini").write_text("changed")
        with self.assertRaisesRegex(ValueError, "inputs changed"):
            release.package(self.root / "bad")
        self.assertFalse((self.root / "bad").exists())

    def test_changed_binary_rejected(self):
        (self.builds["c5"] / "firmware.bin").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "outputs changed"):
            release.package(self.root / "bad")

    def test_effective_config_is_source_input(self):
        before = release.source_snapshot()
        (self.root / "sdkconfig.s3-main").write_text("CONFIG_FIXTURE=y")
        self.assertNotEqual(before, release.source_snapshot())

    def test_mismatched_versions(self):
        self.mutate("c5", "project_description.json", lambda d: d.update(project_version="different"))
        self.receipt()
        with self.assertRaisesRegex(ValueError, "versions must match"):
            release.package(self.root / "bad")

    def test_wrong_chip(self):
        self.mutate("c5", "project_description.json", lambda d: d.update(target="esp32c6"))
        with self.assertRaisesRegex(ValueError, "wrong chip"):
            release.inspect_target("c5")

    def test_memory_profiles(self):
        for name, change in (("s3", {"SPIRAM_MODE_QUAD": False}),
                             ("bridge", {"SPIRAM_MODE_OCT": True}),
                             ("s3", {"SPIRAM_USE_CAPS_ALLOC": False}),
                             ("c5", {"SPIRAM": True}),
                             ("c5", {"ESPTOOLPY_FLASHSIZE": "4MB"})):
            path = self.builds[name] / "config/sdkconfig.json"
            saved = path.read_bytes()
            self.mutate(name, "config/sdkconfig.json", lambda d: d.update(change))
            with self.subTest(change=change), self.assertRaises(ValueError):
                release.inspect_target(name)
            path.write_bytes(saved)

    def test_flash_arguments_disagree(self):
        self.mutate("c5", "flasher_args.json", lambda d: d["flash_settings"].update(flash_size="4MB"))
        with self.assertRaisesRegex(ValueError, "arguments disagree"):
            release.inspect_target("c5")

    def test_oversized_application_and_bootloader(self):
        app = self.builds["c5"] / "firmware.bin"
        app.write_bytes(bytes(0x10001))
        with self.assertRaisesRegex(ValueError, "does not fit"):
            release.inspect_target("c5")
        app.write_bytes(b"fixture")
        (self.builds["c5"] / "bootloader.bin").write_bytes(bytes(0x7000))
        with self.assertRaisesRegex(ValueError, "overlapping image"):
            release.inspect_target("c5")

    def test_corrupted_partition_binary(self):
        (self.builds["c5"] / "partitions.bin").write_bytes(bytes(32))
        with self.assertRaisesRegex(ValueError, "partition magic"):
            release.inspect_target("c5")

    def test_missing_and_external_image(self):
        (self.builds["s3"] / "firmware.bin").unlink()
        with self.assertRaisesRegex(ValueError, "missing or invalid"):
            release.inspect_target("s3")
        outside = self.root / "outside.bin"
        outside.write_bytes(b"outside")
        def change(data):
            data["app"]["file"] = str(outside)
            data["flash_files"]["0x10000"] = str(outside)
        self.mutate("s3", "flasher_args.json", change)
        with self.assertRaisesRegex(ValueError, "missing or invalid"):
            release.inspect_target("s3")


if __name__ == "__main__":
    unittest.main(verbosity=2)
