#!/usr/bin/env python3
"""Compile and test the actual allocation-free Watch classifier (no RF access)."""
from __future__ import annotations
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    if os.environ.get("CC"):
        compiler = shlex.split(os.environ["CC"])
    elif importlib.util.find_spec("ziglang") is not None:
        compiler = [sys.executable, "-m", "ziglang", "cc"]
    else:
        compiler = next(([p] for n in ("cc", "clang", "gcc") if (p := shutil.which(n))), None)
        if compiler is None:
            raise SystemExit("Install a host C compiler or python -m pip install ziglang==0.14.1")
    with tempfile.TemporaryDirectory(prefix="cerberus-watch-detect-") as temp:
        executable = Path(temp) / ("watch_test.exe" if os.name == "nt" else "watch_test")
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
            str(root / "s3_main/main/sentinel_watch_detect.c"),
            str(root / "shared/sentinel_watch_protocol.c"),
            str(root / "tools/tests/sentinel_watch_protocol_test.c"),
            str(root / "tools/tests/sentinel_watch_detect_test.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
