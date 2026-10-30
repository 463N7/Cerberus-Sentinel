"""Compile and run the production portal DNS/form codec on the host."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/portal-protocol-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "portal-protocol-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/portal_protocol_check.c"),
    str(root / "shared/portal_protocol.c"), "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
