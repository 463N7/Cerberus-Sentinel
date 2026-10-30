"""Exercise the production CSV writer with real temporary files and host locks."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/report-storage-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/storage.c").read_text(encoding="utf-8")
start = source.index("static esp_err_t write_diagnostic_report_with_prefix(")
end = source.index("esp_err_t cerberus_storage_wifi_capture_begin(", start)
(out / "report_storage_impl.h").write_text(source[start:end], encoding="utf-8")
exe = out / "report-storage-checks.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "s3_main/main"),
    "-I", str(root / "tools/host_checks/stubs"),
    str(root / "tools/host_checks/report_storage_check.c"), "-o", str(exe),
], check=True)
with tempfile.TemporaryDirectory(prefix="files-", dir=out) as temporary:
    directory = Path(temporary).resolve()
    assert directory.is_relative_to(out.resolve())
    (directory / "CERBERUS/LOGS").mkdir(parents=True)
    subprocess.run([str(exe), str(directory)], check=True)
