"""Check selected-AP packet layouts and receiver parsing without transmitting."""
from pathlib import Path
import subprocess, sys
root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/ap-frame-checks'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'ap-frames.exe'
subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
    '-I', str(root / 'shared'), str(root / 'tools/host_checks/ap_frames_check.c'),
    str(root / 'shared/wifi_ap_job.c'), str(root / 'shared/wifi_ap_rx.c'),
    str(root / 'shared/wifi_sae_commit.c'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
