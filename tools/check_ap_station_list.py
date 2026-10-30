"""Check the shared AP station-list wire bounds and frame safety."""
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
out=root/'artifacts/ap-station-list-checks'
out.mkdir(parents=True,exist_ok=True)
exe=out/'ap-station-list.exe'
subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O1','-Wall','-Wextra','-Werror',
    '-I',str(root/'shared'),'-I',str(root/'tools/host_checks/stubs'),
    str(root/'tools/host_checks/ap_station_list_check.c'),str(root/'shared/wifi_ap_job.c'),
    '-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
