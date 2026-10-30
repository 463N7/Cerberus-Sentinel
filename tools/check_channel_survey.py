"""Build the production passive survey metric helper and verify its SPI contract."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/channel-survey-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "channel-survey-check.exe"
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
    str(root / "tools/host_checks/channel_survey_check.c"),
    "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
