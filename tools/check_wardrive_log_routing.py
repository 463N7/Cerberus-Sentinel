"""Run production Wardriving row routing with GPS transitions and SD faults."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/wardrive-checks'
out.mkdir(parents=True, exist_ok=True)
source = (root / 's3_main/main/wardrive_service.c').read_text()
start = source.index('static esp_err_t begin_log_stream(')
end = source.index('\n}', source.index('static esp_err_t append_observation(', start)) + 2
(out / 'wardrive_log_routing_impl.h').write_text(source[start:end])
exe = out / 'wardrive-log-routing-check.exe'
subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O1',
    '-Wall', '-Wextra', '-Werror', '-I', str(out),
    '-I', str(root / 'tools/host_checks/stubs'),
    '-I', str(root / 's3_main/main'), '-I', str(root / 'shared'),
    str(root / 's3_main/main/wardrive_core.c'),
    str(root / 's3_main/main/wardrive_wigle.c'),
    str(root / 'tools/host_checks/wardrive_log_routing_check.c'),
    '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
