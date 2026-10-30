"""Exercise the survey cache and Wardriving's direct R1-R6 FIFO drain."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/field_survey.c").read_text(encoding="utf-8")
start = source.index("static void add(const expansion_record_t *record)")
end = source.index("static void worker(void *unused)", start)
(out / "wardrive_expansion_add_impl.h").write_text(source[start:end], encoding="utf-8")
service = (root / "s3_main/main/wardrive_service.c").read_text(encoding="utf-8")
start = service.index("static esp_err_t capture_expansions(")
end = service.index("static void publish_completed_wifi(", start)
(out / "wardrive_expansion_capture_impl.h").write_text(service[start:end], encoding="utf-8")
exe = out / "wardrive-expansion-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"),
    "-I", str(root / "tools/host_checks/stubs"),
    str(root / "tools/host_checks/wardrive_expansion_epoch_check.c"),
    "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
