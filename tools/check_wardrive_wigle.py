"""Compile the WiGLE 1.6 row formatter and explicit time-provenance fixtures."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "wardrive-wigle-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"),
    str(root / "s3_main/main/wardrive_wigle.c"),
    str(root / "s3_main/main/wardrive_core.c"),
    str(root / "s3_main/main/wardrive_gps_core.c"),
    str(root / "tools/host_checks/wardrive_wigle_check.c"),
    "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
