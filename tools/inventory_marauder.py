"""Extract a reproducible source inventory; this is not a feature-completion claim."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = "511bb3694d0c4dc064888e762bde44fcc9b7f48a"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=ROOT.parent / "integration/marauder-source")
args = parser.parse_args()
source = args.source.resolve()
revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
if revision != PIN:
    raise SystemExit(f"Reference revision changed: expected {PIN}, found {revision}")
files = {name: (source / "esp32_marauder" / name).read_text(encoding="utf-8")
         for name in ["WiFiScan.h", "MenuFunctions.cpp", "CommandLine.cpp", "CommandLine.h", "lang_var.h"]}
if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--", *[
        "esp32_marauder/" + name for name in files]], text=True).strip():
    raise SystemExit("Reference source has local changes")
header = files["WiFiScan.h"]
mode_section = header[header.index("#define OTA_UPDATE"):header.index("#define ATTACK_AP_SCOPE_NONE")]
modes = [{"symbol": m[1], "upstream_value": int(m[2]),
          "source_line": header[:header.index(m[0])].count("\n") + 1}
         for m in re.finditer(r"^#define\s+(\w+)\s+(\d+)", mode_section, re.M)]
labels = {m[1]: json.loads('"' + m[2] + '"') for m in re.finditer(
    r'const char (text\w*)\[\]\s*=\s*"((?:[^"\\]|\\.)*)"', files["lang_var.h"])}
menu_entries = []
menu_source = files["MenuFunctions.cpp"]
for m in re.finditer(r'addNodes\(\s*(&?\w+),\s*("(?:[^"\\]|\\.)*"|text_table1\[\d+\]|text\w*)\s*,', menu_source):
    owner, expr = m[1], m[2]
    if expr.startswith('"'):
        label = json.loads(expr)
    elif expr.startswith("text_table1"):
        label = labels.get("text1_" + re.search(r"\d+", expr.split("[")[1])[0], expr)
    else:
        label = labels.get(expr, expr)
    if owner == "menu":
        cases = list(re.finditer(r"case MenuBuildKind::(\w+):", menu_source[:m.start()]))
        owner = cases[-1][1] if cases else "dynamic"
    menu_entries.append({"menu": owner.lstrip("&"), "label": label.strip(),
                         "source_line": menu_source[:m.start()].count("\n") + 1})
commands = []
for m in re.finditer(r"cmd_args\.get\(0\)\s*==\s*(\w+_CMD)", files["CommandLine.cpp"]):
    if not any(c["symbol"] == m[1] for c in commands):
        commands.append({"symbol": m[1], "source_line": files["CommandLine.cpp"][:m.start()].count("\n") + 1})
collisions = {}
for mode in modes:
    collisions.setdefault(mode["upstream_value"], []).append(mode["symbol"])
report = {"format": 1, "upstream": "https://github.com/justcallmekoko/ESP32Marauder", "revision": revision,
          "scope_note": "Source modes, literal menu entries and top-level CLI commands overlap; counts are not distinct features or proof of parity. Dynamic suboptions and board-specific services require manual review.",
          "source_sha256": {name: hashlib.sha256((source / "esp32_marauder" / name).read_bytes()).hexdigest() for name in files},
          "modes": modes, "literal_menu_entries": menu_entries, "cli_commands": commands,
          "upstream_numeric_collisions": {str(k): v for k, v in collisions.items() if len(v) > 1}}
destination = ROOT / "docs/MARAUDER_SOURCE_INVENTORY.json"
destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(f"{len(modes)} source modes, {len(menu_entries)} literal menu entries, {len(commands)} top-level CLI commands")
print(destination)
print("Numeric collisions:", report["upstream_numeric_collisions"])
