"""Exercise build reporting failure paths without a toolchain or hardware."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import build_unified as builder


class BuildReportChecks(unittest.TestCase):
    def run_build(self, exit_code, outputs):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            build = root / ".pio/build/example"
            build.mkdir(parents=True)
            for name in outputs:
                (build / name).write_bytes(b"test output")
            with patch.object(builder, "ROOT", root), \
                 patch.object(builder, "TARGETS", {"example": (root, "example")}), \
                 patch("sys.argv", ["build_unified.py"]), \
                 patch.object(builder.subprocess, "run", return_value=SimpleNamespace(returncode=exit_code)) as run, \
                 contextlib.redirect_stdout(io.StringIO()):
                code = builder.main()
                command = run.call_args.args[0]
                self.assertNotIn("upload", command)
                self.assertNotIn("erase", command)
            report = json.loads((root / "artifacts/integration/build-report.json").read_text())
            return code, report

    def test_failed_build_does_not_report_stale_binaries(self):
        code, report = self.run_build(1, ["firmware.bin", "firmware.elf", "partitions.bin"])
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "failed")
        self.assertNotIn("files", report["targets"]["example"])

    def test_success_without_required_images_is_failure(self):
        code, report = self.run_build(0, ["firmware.bin"])
        self.assertEqual(code, 1)
        self.assertEqual(report["targets"]["example"]["missing_outputs"], ["firmware.elf", "partitions.bin"])

    def test_complete_build_records_checksums_without_hardware_claim(self):
        code, report = self.run_build(0, ["firmware.bin", "firmware.elf", "partitions.bin"])
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "passed")
        self.assertFalse(report["hardware_validated"])
        self.assertEqual(len(report["targets"]["example"]["files"]["firmware.bin"]["sha256"]), 64)


if __name__ == "__main__":
    unittest.main()
