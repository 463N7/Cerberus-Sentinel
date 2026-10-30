"""Exercise the production Wardriving Stop state flow with scripted scans."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/wardrive_service.c").read_text(encoding="utf-8")
start = source.index("static void stop_own_scans(")
end = source.index("static void wardrive_worker(", start)
(out / "wardrive_stop_impl.h").write_text(source[start:end], encoding="utf-8")
exe = out / "wardrive-stop-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    str(root / "tools/host_checks/wardrive_stop_check.c"), "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
