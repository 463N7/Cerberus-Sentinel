"""Check SAE P-256 fixture, PSA lifecycle and installed C5 IDF compile."""
from pathlib import Path
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
compile_db = root / "c5_firmware/.pio/build/c5-onboard/compile_commands.json"
entries = json.loads(compile_db.read_text(encoding="utf-8"))
sample = next(e for e in entries if e["file"].endswith("radio_test_service.c"))
source = (root / "c5_firmware/main/sae_commit_material.c").resolve()
command = sample["command"].rsplit(" -o ", 1)[0]
subprocess.run(command + " -Werror -fsyntax-only " + str(source),
    cwd=sample["directory"], check=True)

out = root / "artifacts/sae-material-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "sae-material-checks.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "tools/host_checks/sae_material_stubs"),
    "-I", str(root / "c5_firmware/main"),
    str(root / "tools/host_checks/sae_material_check.c"),
    str(source), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)

# Independent secp256r1 doubling checks the 2G fixture used by the PSA mock.
p = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
a = p - 3
b = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
gx = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
gy = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5
assert gy * gy % p == (gx * gx * gx + a * gx + b) % p
slope = (3 * gx * gx + a) * pow(2 * gy, -1, p) % p
x2 = (slope * slope - 2 * gx) % p
y2 = (slope * (gx - x2) - gy) % p
assert x2 == 0x7CF27B188D034F7E8A52380304B51AC3C08969E277F21B35A60B48FC47669978
assert y2 == 0x07775510DB8ED040293D9AC69F7430DBBA7DADE63CE982299E04B79D227873D1
assert y2 * y2 % p == (x2 * x2 * x2 + a * x2 + b) % p
print("SAE material passed C5 IDF 6.0.1 compile and independent P-256 fixture check.")
