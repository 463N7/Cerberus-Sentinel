"""Exercise the production C5 MAC receiver, lifecycle and exact SPI dispatch."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/mac-capture-checks'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'c5_firmware/main/main.c').read_text()
start = source.index('    case CERBERUS_SPI_COMMAND_MAC_START:')
(out / 'mac_dispatch.h').write_text(source[start:source.index('    case CERBERUS_SPI_COMMAND_CSI_START:', start)])
scanner = (root / 'c5_firmware/main/wifi_scanner.c').read_text()
start = scanner.index('static void wifi_promiscuous_callback(void *buffer, wifi_promiscuous_pkt_type_t type)\n{')
end = scanner.index('    /* Snapshot admission', start)
prefix = scanner[start:end]
assert prefix.index('mac_admission_token') < prefix.index('esp_timer_get_time()') < prefix.index('mac_receive(')
(out / 'mac_callback_entry.h').write_text(prefix + '    (void)rx; (void)relation_epoch;\n}\n')
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / 'zig-cache'))
exe = out / ('mac-capture-check.exe' if os.name == 'nt' else 'mac-capture-check')
subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
    '-I', str(out), '-I', str(root / 'shared'), '-I', str(root / 'c5_firmware/main'),
    str(root / 'tools/host_checks/mac_capture_check.c'), str(root / 'shared/mac_observation.c'),
    '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
