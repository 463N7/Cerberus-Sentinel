"""Exercise the production C5 passive Remote ID job and receive callback."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-priority-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "c5_firmware/main/wifi_scanner.c").read_text()
start = source.index("static void wifi_promiscuous_callback(")
(out / "remote_id_priority_callback.h").write_text(source[start:source.index("static void reset_defense_capture(", start)])
start = source.index("esp_err_t cerberus_wifi_defense_survey_start(void)")
(out / "remote_id_priority_survey_start.h").write_text(source[start:source.index("esp_err_t cerberus_wifi_defense_survey_stop(void)", start)])
start = source.index("esp_err_t cerberus_wifi_connection_disconnect(void)")
(out / "remote_id_priority_disconnect.h").write_text(source[start:source.index("void cerberus_wifi_connection_get_status(", start)])
source = (root / "c5_firmware/main/main.c").read_text()
start = source.index("    case CERBERUS_SPI_COMMAND_REMOTE_ID_START:")
(out / "remote_id_priority_dispatch.h").write_text(source[start:source.index("    case CERBERUS_SPI_COMMAND_BLE_ACTIVITY:", start)])
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-priority-check.exe" if os.name == "nt" else "remote-id-priority-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "shared"), "-I", str(root / "c5_firmware/main"),
    str(root / "tools/host_checks/remote_id_priority_check.c"),
    str(root / "shared/remote_id_channel_plan.c"), str(root / "shared/cerberus_watch_capture.c"),
    str(root / "shared/sentinel_watch_protocol.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
