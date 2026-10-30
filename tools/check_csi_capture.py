"""Run the actual C5 CSI callback, worker, commands and SPI dispatch on host."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/csi-capture-checks'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'c5_firmware/main/main.c').read_text()
start = source.index('    case CERBERUS_SPI_COMMAND_CSI_START:')
(out / 'csi_dispatch.h').write_text(source[start:source.index('    case CERBERUS_SPI_COMMAND_EVENT_QUEUE_STATUS:', start)])
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / 'zig-cache'))
exe = out / ('csi-capture-check.exe' if os.name == 'nt' else 'csi-capture-check')
subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
    '-I', str(out), '-I', str(root / 'shared'), '-I', str(root / 'c5_firmware/main'),
    str(root / 'tools/host_checks/csi_capture_check.c'), '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
