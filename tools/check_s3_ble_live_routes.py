"""Run the actual BLE/live tap dispatch and scan-start context gate on the host."""
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
out=root/'artifacts/ble-live-route-checks';out.mkdir(parents=True,exist_ok=True)
live=(root/'s3_main/main/sentinel_live_ui.h').read_text()
ui=(root/'s3_main/main/ui.c').read_text()
def section(s,a,b): return s[s.index(a):s.index(b,s.index(a))]
parts=section(live,'static bool live_is_view(', 'static void live_status(')
parts+=section(live,'static uint32_t live_ble_recent_mask(', 'static void live_make_view(')
make_start=live.index('static void live_make_view(')
ble_start=live.index('    if (tool == SENTINEL_SCAN_BLE) {',make_start)
ble_end=live.index('    if(tool==SENTINEL_SCAN_WIFI)',ble_start)
parts+='static void live_make_view(void) {\n' \
    'sentinel_scan_tool_t tool=SENTINEL_SCAN_BLE;\n' \
    'live_test_record_t *record=&live_records[tool];\n'+live[ble_start:ble_end]+'}\n'
parts+=section(live,'static void handle_live_scan_tap(', 'static esp_err_t live_connect_selected_wifi(')
parts+=section(ui,'static void handle_ble_tap(', '    if (ble_mode == UI_BLE_MODE_ANALYZER)')+'}\n'
start=live.index('    case SENTINEL_SCAN_BLE:',live.index('static esp_err_t live_execute('))
end=live.index('    case ',start+8)
parts+='static esp_err_t actual_ble_start(bool start) {\n'+live[start:end].replace('    case SENTINEL_SCAN_BLE:','')+'}\n'
(out/'ble_live_route_impl.h').write_text(parts)
exe=out/'ble-live-route-check.exe'
subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O1','-Wall','-Wextra','-Werror',
 '-I',str(out),'-I',str(root/'tools/host_checks/stubs'),'-I',str(root/'s3_main/main'),'-I',str(root/'shared'),
 str(root/'tools/host_checks/s3_ble_live_routes_check.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
