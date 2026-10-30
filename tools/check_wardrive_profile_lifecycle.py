"""Compile the actual field survey plan setter in a small host lifecycle harness."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/field_survey.c").read_text(encoding="utf-8")
start = source.index("esp_err_t field_survey_set_idle_probe_enabled(")
end = source.index("void field_survey_snapshot(", start)
(out / "wardrive_profile_lifecycle_impl.h").write_text(source[start:end], encoding="utf-8")
exe = out / "wardrive-profile-lifecycle-check.exe"
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
    "-I", str(root / "tools/host_checks/stubs"),
    str(root / "s3_main/main/wardrive_plan.c"),
    str(root / "tools/host_checks/wardrive_profile_lifecycle_check.c"),
    "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
