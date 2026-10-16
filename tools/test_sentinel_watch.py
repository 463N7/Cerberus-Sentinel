#!/usr/bin/env python3
"""Compile and exercise Watch's actual bounded history and alert controller."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    if os.environ.get('CC'):
        cc = shlex.split(os.environ['CC'])
    elif importlib.util.find_spec('ziglang') is not None:
        cc = [sys.executable, '-m', 'ziglang', 'cc']
    else:
        cc = next(([p] for name in ('cc', 'clang', 'gcc') if (p := shutil.which(name))), None)
    if not cc:
        raise SystemExit('A host C compiler is required')
    with tempfile.TemporaryDirectory(prefix='cerberus-watch-') as temp:
        exe = Path(temp) / ('watch.exe' if os.name == 'nt' else 'watch')
        subprocess.run(cc + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
            '-I', str(ROOT / 's3_main/main'), '-I', str(ROOT / 'shared'), str(ROOT / 's3_main/main/sentinel_watch.c'),
            str(ROOT / 'tools/tests/sentinel_watch_test.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
