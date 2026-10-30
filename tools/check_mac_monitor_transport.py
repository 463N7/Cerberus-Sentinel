"""Exercise actual incremental MAC SPI adapter, including pin/retry timing."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/mac-monitor-transport-checks"
out.mkdir(parents=True, exist_ok=True)
(out / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n"
    "enum {ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT, "
    "ESP_ERR_NOT_SUPPORTED, ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_CRC};\n")
exe = out / "mac-monitor-transport-check.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/mac_monitor_transport_check.c"),
    str(root / "shared/mac_observation.c"), str(root / "s3_main/main/csi_session.c"),
    str(root / "shared/csi_motion_model.c"), "-lm", "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
