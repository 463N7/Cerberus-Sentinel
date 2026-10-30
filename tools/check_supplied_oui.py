"""Audit a supplied CYM OUI pack against Cerberus's SD database sources."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct


def csv_rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        rows = [tuple(row[key] for key in ("Registry", "Assignment", "Organization Name", "Organization Address"))
                for row in csv.DictReader(stream)]
    return rows


def check(cym_assets, retained_csv, cerberus_db):
    supplied_csv = cym_assets / "oui.csv"
    supplied = csv_rows(supplied_csv)
    retained = csv_rows(retained_csv)
    if sorted(supplied) != sorted(retained):
        raise ValueError("Supplied and retained IEEE assignment facts differ; rebuild RECON.DB")

    prefixes = {}
    for registry, assignment, name, _ in supplied:
        if registry != "MA-L" or len(assignment) != 6 or not name:
            raise ValueError(f"Unsupported assignment: {registry} {assignment}")
        prefixes.setdefault(int(assignment, 16), name.strip().encode("utf-8")[:28])

    oui = (cym_assets / "sdcard/lab/ouilist.bin").read_bytes()
    if len(oui) < 8 or oui[:4] != b"OUI1":
        raise ValueError("Invalid CYM OUI1 header")
    count = struct.unpack_from("<I", oui, 4)[0]
    if count != len(prefixes) or len(oui) != 8 + count * 32:
        raise ValueError("CYM OUI1 entry count/length mismatch")
    previous = -1
    for index in range(count):
        record = oui[8 + index * 32:8 + (index + 1) * 32]
        key = int.from_bytes(record[:3], "big")
        if key <= previous or record[3:32].split(b"\0", 1)[0] != prefixes[key]:
            raise ValueError(f"CYM OUI1 record mismatch at {index}")
        previous = key

    db = cerberus_db.read_bytes()
    if len(db) < 40 or db[:8] != b"CERBREC1":
        raise ValueError("Invalid Cerberus database header")
    db_count = struct.unpack_from("<I", db, 12)[0]
    db_keys = {struct.unpack_from("<I", db, 40 + index * 16)[0] for index in range(db_count)}
    if not prefixes.keys() <= db_keys:
        raise ValueError("Cerberus RECON.DB is missing supplied registry prefixes")
    return {
        "supplied_csv_sha256": hashlib.sha256(supplied_csv.read_bytes()).hexdigest(),
        "supplied_oui1_sha256": hashlib.sha256(oui).hexdigest(),
        "cerberus_db_sha256": hashlib.sha256(db).hexdigest(),
        "csv_rows": len(supplied),
        "unique_ma_l_prefixes": count,
        "cerberus_index_entries": db_count,
        "semantic_rows_equal": True,
        "all_supplied_prefixes_in_cerberus_db": True,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cym-assets", type=Path, required=True)
    parser.add_argument("--retained-csv", type=Path, required=True)
    parser.add_argument("--cerberus-db", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.cym_assets, args.retained_csv, args.cerberus_db), indent=2))
