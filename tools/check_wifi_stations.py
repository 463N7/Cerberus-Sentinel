"""Check AP/station address parsing and bounded inventory without radio access."""
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
out=root/'artifacts/station-checks';out.mkdir(parents=True,exist_ok=True)
exe=out/'station-checks.exe'
subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O1','-Wall','-Wextra','-Werror',
    '-I',str(root/'shared'),str(root/'tools/host_checks/station_checks.c'),
    str(root/'shared/wifi_stations.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
