"""Compile Wardriving's production expansion-plan activation with host fakes."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/wardrive_service.c").read_text(encoding="utf-8")
start = source.index("static esp_err_t prepare_expansion_plan(")
end = source.index("static esp_err_t wardrive_start_with_idle_probes_paused(", start)
(out / "wardrive_expansion_activation_impl.h").write_text(
    source[start:end], encoding="utf-8"
)
# The service must set the plan before opening the stream. A plan is optional
# when R1-R6 are not requested, so the onboard/no-GPS path has no new gate.
start_body = source[end:source.index("esp_err_t cerberus_wardrive_start(", end)]
assert start_body.index("prepare_expansion_plan(expansion_stage.present_mask)") < (
    start_body.index("field_survey_stream_open()")
)
assert "if (include_expansions) {\n        error = prepare_expansion_plan(" in start_body
stop_body = source[
    source.index("static void stop_expansions("):source.index("static void stop_scans_and_close_file(")
]
assert stop_body.index("field_survey_stream_close()") < (
    stop_body.index("field_survey_set_expansion_plan(NULL)")
)

exe = out / "wardrive-expansion-activation-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + [
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "s3_main/main"),
    "-I", str(root / "tools/host_checks/stubs"),
    str(root / "s3_main/main/wardrive_plan.c"),
    str(root / "tools/host_checks/wardrive_expansion_activation_check.c"),
    "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
