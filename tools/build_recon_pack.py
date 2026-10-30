#!/usr/bin/env python3
"""Build one indexed, provenance-preserving SD file from reviewed local sources.

No downloads or C/C++ evaluation. Each OUI retains vendor-name alternatives and
source-specific hints; a hint never replaces the registry vendor identity.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

MAGIC = b"CERBREC1"
HEADER = struct.Struct("<8s8I")
INDEX = struct.Struct("<IIII")
MAX_RECORD = 4096


def build(source: Path, destination: Path):
    rows = {}
    sources = {}

    def load(name, path, url, license_name):
        raw = (source / path).read_bytes()
        sources[name] = {"url": url, "sha256": hashlib.sha256(raw).hexdigest(),
                         "license": license_name, "path": path}
        return raw.decode("utf-8-sig")

    def row(prefix):
        compact = prefix.replace(":", "").upper()
        if not re.fullmatch(r"[0-9A-F]{6}", compact):
            raise ValueError(f"Not a /24 OUI: {prefix}")
        return rows.setdefault(int(compact, 16), {"vendor": "Unknown", "vendors": [], "hints": []})

    def vendor(prefix, name, origin, preferred=False):
        entry = row(prefix)
        fact = {"name": name, "source": origin}
        if fact not in entry["vendors"]: entry["vendors"].append(fact)
        if preferred or entry["vendor"] == "Unknown": entry["vendor"] = name

    def revision(repo):
        return json.loads((source / (repo.split('/')[-1] + '-tree.json')).read_text())["sha"]

    cym_revision = revision("JimGat/CYM")
    cym = load("cym-ieee", "CYM/docs/support_files/oui.csv",
        f"https://github.com/JimGat/CYM/blob/{cym_revision}/docs/support_files/oui.csv",
        "IEEE-derived assignment facts; repository has no root license")
    for item in csv.DictReader(cym.splitlines()):
        if item["Registry"] == "MA-L": vendor(item["Assignment"], item["Organization Name"], "cym-ieee")
    manuf = load("chimera-manuf", "ChimeraBLE/manuf",
        "https://www.wireshark.org/download/automated/data/manuf",
        "IEEE-derived registry used by ChimeraBLE; downloaded registry attribution retained")
    for line in manuf.splitlines():
        parts = line.split("\t")
        if len(parts) >= 2 and re.fullmatch(r"[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){2}", parts[0]):
            vendor(parts[0], parts[-1].strip(), "chimera-manuf", True)
    dragon = load("dragon", "DragonDetector/vendor-prefixes.csv",
        "https://valleytech-dragon-detector.pages.dev/reference/docs/data/vendor-prefixes.csv", "MIT")
    for item in csv.DictReader(dragon.splitlines()):
        row(item["prefix"])["hints"].append({"source": "dragon", "label": item["family"],
            "confidence": item["confidence"], "evidence": item["evidence"], "kind": "oui_hint"})
    squach_revision = revision("skizzophrenic/SquachWatch-CYD")
    squach = load("squach", "SquachWatch-CYD/src/signatures.cpp",
        f"https://github.com/skizzophrenic/SquachWatch-CYD/blob/{squach_revision}/src/signatures.cpp", "GPL-3.0")
    table = squach.split("const OuiEntry kOuiTable[] = {", 1)[1].split("\n};", 1)[0]
    table = re.sub(r"//[^\n]*", "", table)
    pattern = r'\{\{0x([0-9A-Fa-f]{2}),\s*0x([0-9A-Fa-f]{2}),\s*0x([0-9A-Fa-f]{2})\},\s*"([^"]+)",\s*DetectionType::(\w+),\s*Confidence::(\w+)\}'
    matches = list(re.finditer(pattern, table))
    if len(matches) != table.count("{{"): raise ValueError("Unrecognized SquachWatch OUI row; update importer")
    for match in matches:
        a, b, c, label, family, confidence = match.groups()
        row(a+b+c)["hints"].append({"source": "squach", "label": label,
            "family": family, "confidence": confidence, "kind": "oui_hint"})

    # Keep reference rules and attribution inside the same SD file. They are
    # reference material, not automatically executed BLE detection predicates.
    archive = {}
    for path in ("SquachWatch-CYD/src/signatures.cpp", "SquachWatch-CYD/include/signatures.h",
                 "SquachWatch-CYD/docs/DETECTIONS.md", "SquachWatch-CYD/LICENSE", "ChimeraBLE/LICENSE",
                 "ChimeraBLE/tools/make_oui_bin.py", "DragonDetector/LICENSE", "DragonDetector/facts.json"):
        archive[path] = (source / path).read_text(encoding="utf-8")
    metadata = {"format": 1, "sources": sources, "archive": archive,
                "chimera_revision": revision("MatthewKuKanich/ChimeraBLE"),
                "limitations": ["Only /24 OUIs indexed", "OUI is vendor evidence, never product identity",
                    "Do not attribute random/local addresses using an OUI", "BLE reference rules are not runtime predicates"],
                "registry_header": '\n'.join(x for x in manuf.splitlines() if x.startswith('#'))}
    blob = bytearray(); index = bytearray()
    blob_start = HEADER.size + len(rows) * INDEX.size
    for key, value in sorted(rows.items()):
        name = value["vendor"].encode("utf-8")[:192]
        payload = bytes([len(name)]) + name + json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
        if len(payload) > MAX_RECORD: raise ValueError(f"Record too large: {key:06X}")
        crc = zlib.crc32(struct.pack("<I", key) + payload)
        index.extend(INDEX.pack(key, blob_start + len(blob), len(payload), crc))
        blob.extend(payload)
    meta_start = blob_start + len(blob)
    meta = json.dumps(metadata, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
    body = index + blob + meta
    header = HEADER.pack(MAGIC, 1, len(rows), HEADER.size, blob_start, meta_start,
                         HEADER.size + len(body), zlib.crc32(body), 0)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".tmp")
    temporary.write_bytes(header + body)
    verify(temporary)
    temporary.replace(destination)
    return {"records": len(rows), "bytes": destination.stat().st_size,
            "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            "squach_oui_hints": len(matches), "path": str(destination)}


def verify(path):
    data = path.read_bytes()
    if len(data) < HEADER.size: raise ValueError("Truncated header")
    magic, version, count, start, blob, meta, total, crc, reserved = HEADER.unpack_from(data)
    if magic != MAGIC or version != 1 or reserved or total != len(data) or start != HEADER.size or blob != start + count * INDEX.size or not blob <= meta <= total:
        raise ValueError("Invalid header")
    if zlib.crc32(data[start:]) != crc: raise ValueError("Corrupt body")
    previous = -1
    for i in range(count):
        key, offset, length, expected = INDEX.unpack_from(data, start + i * INDEX.size)
        if key <= previous or key > 0xFFFFFF or not 0 < length <= MAX_RECORD or offset < blob or offset + length > meta:
            raise ValueError("Invalid record index")
        payload = data[offset:offset + length]
        if zlib.crc32(struct.pack("<I", key) + payload) != expected: raise ValueError("Corrupt record")
        name_length = payload[0]
        if not name_length or name_length + 1 >= len(payload): raise ValueError("Invalid vendor label")
        json.loads(payload[1 + name_length:]); previous = key
    json.loads(data[meta:])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sources", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.sources, args.output), indent=2))
