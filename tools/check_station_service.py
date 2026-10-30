"""Run the real private C5 station service against fake Wi-Fi and time."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1];out=root/'artifacts/station-service-checks';out.mkdir(parents=True,exist_ok=True)
exe=out/'station-service-checks.exe'
subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-O1','-Wall','-Wextra','-Werror',
    '-I',str(root/'shared'),'-I',str(root/'tools/host_checks/stubs'),'-I',str(root/'c5_firmware/main'),
    str(root/'tools/host_checks/station_service_check.c'),str(root/'shared/wifi_stations.c'),str(root/'shared/wifi_ap_job.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
