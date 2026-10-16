#!/usr/bin/env python3
"""Compile the actual scan-session scheduler and run workflow/simulation tests.

Uses the host C compiler named by CC, or Python's ziglang package, or cc/clang/gcc.
Does not import ESP-IDF and does not access a connected board.
"""
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
        compiler = next(([path] for name in ("cc", "clang", "gcc") if (path := shutil.which(name))), None)
        if compiler is None:
            raise SystemExit("Install a host C compiler or: python -m pip install ziglang==0.14.1")
    with tempfile.TemporaryDirectory(prefix="cerberus-scan-session-") as temp:
        executable = Path(temp) / ("test_scan.exe" if os.name == "nt" else "test_scan")
        subprocess.run(compiler + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-I", str(root / "s3_main/main"),
            str(root / "s3_main/main/sentinel_scan_session.c"),
            str(root / "tools/tests/sentinel_scan_session_test.c"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
