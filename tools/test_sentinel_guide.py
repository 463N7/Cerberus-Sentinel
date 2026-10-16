#!/usr/bin/env python3
"""Compile and exercise the actual Field Guide C code on the host.

Uses a system C compiler, or `python -m pip install ziglang==0.14.1`.
Run with --preview-dir PATH to export 320x480 SVG previews as a side effect.
The display/NVS fakes check integration boundaries; lesson logic is the real C.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview-dir", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if os.environ.get("CC"):
        compiler = shlex.split(os.environ["CC"])
    elif importlib.util.find_spec("ziglang") is not None:
        compiler = [sys.executable, "-m", "ziglang", "cc"]
    else:
        compiler = next(([path] for name in ("cc", "clang", "gcc") if (path := shutil.which(name))), None)
        if compiler is None:
            parser.error("Install a host C compiler or run: python -m pip install ziglang==0.14.1")
    stubs = root / "tools/tests/sentinel_guide"
    main_dir = root / "s3_main/main"
    if args.preview_dir:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="cerberus-guide-") as temp:
        executable = Path(temp) / ("test_guide.exe" if os.name == "nt" else "test_guide")
        subprocess.run(compiler + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-I", str(stubs), "-I", str(main_dir),
            str(main_dir / "sentinel_guide.c"), str(main_dir / "ui_widgets.c"),
            str(stubs / "test_guide.c"), "-o", str(executable),
        ], check=True)
        command = [str(executable)]
        if args.preview_dir:
            command.append(str(args.preview_dir.resolve()))
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
