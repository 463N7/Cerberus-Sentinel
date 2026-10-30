"""Exercise the actual selected-device connection tap route and diagnostic output."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/s3-ble-connection-ui-checks'
out.mkdir(parents=True, exist_ok=True)
source = (root / 's3_main/main/ui.c').read_text()
start = source.index('/* BEGIN BLE CONNECTION CONTROL:')
end = source.index('/* END BLE CONNECTION CONTROL. */', start)
(out / 'ble_connection_control_impl.h').write_text(source[start:end])
assert source.count('ble_handle_connection_action();') == 1
assert '(cerberus_ui_rect_t){166, 330, 144, 54}, link_control.label, 2U,' in source
assert 'link_control.action != CERBERUS_BLE_LINK_NONE, UI_WIDGET_THEME)' in source
console = (root / 's3_main/main/usb_console.c').read_text()
start = console.index('/* BEGIN BLE CENTRAL DIAGNOSTICS:')
end = console.index('/* END BLE CENTRAL DIAGNOSTICS. */', start)
(out / 'ble_central_console_impl.h').write_text(console[start:end])
start = console.index('/* BEGIN BLE INVENTORY DIAGNOSTICS:')
end = console.index('/* END BLE INVENTORY DIAGNOSTICS. */', start)
(out / 'ble_inventory_console_impl.h').write_text(console[start:end])
exe = out / 's3-ble-connection-ui-check.exe'
subprocess.run([sys.executable, '-m', 'ziglang', 'cc', '-std=c11', '-O1',
    '-Wall', '-Wextra', '-Werror', '-I', str(out),
    '-I', str(root/'s3_main/main'), '-I', str(root/'shared'),
    '-I', str(root/'tools/host_checks/stubs'),
    str(root/'tools/host_checks/s3_ble_connection_ui_check.c'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
