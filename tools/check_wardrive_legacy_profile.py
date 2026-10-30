"""Exercise the production field worker's expansion profile transition."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/field_survey.c").read_text(encoding="utf-8")
worker = source.index("static void worker(void *unused)")
start = source.index("bool profile_config_failed = false;", worker)
end = source.index("state.worker_seen_epoch = epoch;", start)
(out / "wardrive_legacy_profile_impl.h").write_text(
    source[start:end], encoding="utf-8"
)
exe = out / "wardrive-legacy-profile-check.exe"
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
    str(root / "s3_main/main/expansion_protocol.c"),
    str(root / "tools/host_checks/wardrive_legacy_profile_check.c"),
    "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
