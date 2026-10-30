"""Exercise the pure dual-radio MAC controller and production inventory."""
from pathlib import Path
import os, subprocess, sys
root=Path(__file__).resolve().parents[1];out=root/'artifacts/mac-monitor-session-checks';out.mkdir(parents=True,exist_ok=True)
exe=out/'check.exe';env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(out/'zig-cache'))
subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O1','-Wall','-Wextra','-Werror',
    '-I',str(root/'s3_main/main'),'-I',str(root/'shared'),str(root/'tools/host_checks/mac_monitor_session_check.c'),
    str(root/'s3_main/main/mac_monitor_inventory.c'),str(root/'s3_main/main/mac_monitor_session.c'),str(root/'shared/mac_observation.c'),'-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)],check=True)
