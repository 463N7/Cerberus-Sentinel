"""Check the actual S3 READY ISR instructions and internal-memory placement."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from build_toolchain import resolve_build_tools


def verify(root: Path, object_path: Path | None = None) -> dict:
    build = root / ".pio/build/s3-main"
    tools = resolve_build_tools(build, "esp32s3")
    image = object_path.resolve() if object_path else build / "firmware.elf"
    objdump = str(tools["objdump"])
    symbols = subprocess.check_output([objdump, "-t", str(image)], text=True)

    def symbol(name: str) -> tuple[int, str, int]:
        matches = re.findall(
            r"^([0-9a-f]+)\s+\w+\s+[FO]\s+(\.\S+)\s+([0-9a-f]+)\s+" + re.escape(name) + r"$",
            symbols, re.M,
        )
        if len(matches) != 1:
            raise RuntimeError(f"Missing or ambiguous READY symbol: {name}")
        address, section, size = matches[0]
        return int(address, 16), section, int(size, 16)

    isr_address, isr_section, isr_size = symbol("ready_falling_isr")
    counter_address, counter_section, counter_size = symbol("ready_fall_epoch")
    if not isr_section.startswith(".iram") or isr_size == 0:
        raise RuntimeError("READY ISR is not in IRAM")
    if not counter_section.startswith(".dram") or counter_size != 4 or counter_address % 4:
        raise RuntimeError("READY epoch is not an aligned 32-bit object in internal DRAM")
    disassembly = subprocess.check_output(
        [objdump, "-d", "-j", isr_section, f"--start-address={isr_address}",
         f"--stop-address={isr_address + isr_size}", str(image)], text=True
    )
    decoded = re.findall(
        r"^\s*([0-9a-f]+):\s+[0-9a-f]+\s+([a-z][a-z0-9.]*)\b(.*)$", disassembly, re.M
    )
    if any(not isr_address <= int(address, 16) < isr_address + isr_size
           for address, _, _ in decoded):
        raise RuntimeError("Objdump returned instructions outside the READY ISR symbol")
    instructions = [(op, operands) for _, op, operands in decoded]
    allowed = {"entry", "l32r", "memw", "l32i", "l32i.n", "s32i", "s32i.n",
               "addi", "addi.n", "retw", "retw.n", "nop", "nop.n"}
    if not instructions or any(op not in allowed for op, _ in instructions):
        raise RuntimeError("READY ISR contains a call, RMW, branch, or unreviewed instruction")
    ops = [op for op, _ in instructions]
    if (ops.count("l32r") != 1 or sum(op.startswith("l32i") for op in ops) != 1
            or sum(op.startswith("s32i") for op in ops) != 1 or ops.count("memw") < 2):
        raise RuntimeError("READY ISR no longer has the reviewed inline load/store and barriers")

    # In the linked ELF the sole l32r literal must itself reside in IRAM and
    # contain exactly the DRAM counter address. A relocatable probe checks
    # counter section alignment; only the final linked gate proves its literal.
    headers = subprocess.check_output([objdump, "-h", str(image)], text=True)
    if object_path is None:
        literal_line = next(args for op, args in instructions if op == "l32r")
        literal_match = re.search(r",\s*([0-9a-f]+)\b", literal_line)
        if literal_match is None:
            raise RuntimeError("Cannot resolve READY ISR literal address")
        literal_address = int(literal_match[1], 16)
        sections = re.findall(r"^\s*\d+\s+(\.\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)", headers, re.M)
        if not any(name.startswith(".iram") and int(address, 16) <= literal_address
                   and literal_address + 4 <= int(address, 16) + int(size, 16)
                   for name, size, address in sections):
            raise RuntimeError("READY ISR literal is not in IRAM")
        data = subprocess.check_output([objdump, "-s", f"--start-address={literal_address}",
            f"--stop-address={literal_address + 4}", str(image)], text=True)
        word = re.search(r"^\s*" + format(literal_address, "x") + r"\s+([0-9a-f]{8})\b", data, re.M)
        if word is None or int.from_bytes(bytes.fromhex(word[1]), "little") != counter_address:
            raise RuntimeError("READY ISR literal does not address the DRAM epoch")
    else:
        counter_header = re.search(r"^\s*\d+\s+" + re.escape(counter_section)
            + r"\s+.*\s2\*\*(\d+)\s*$", headers, re.M)
        if counter_header is None or int(counter_header[1]) < 2:
            raise RuntimeError("READY probe DRAM section lacks four-byte alignment")

    output = root / "artifacts/integration"
    output.mkdir(parents=True, exist_ok=True)
    stem = "s3-ready-iram-probe" if object_path else "s3-ready-iram"
    (output / f"{stem}.txt").write_text(disassembly, encoding="utf-8")
    result = {
        "status": "passed", "image": str(image),
        "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        "objdump": objdump, "isr_address": hex(isr_address), "isr_section": isr_section,
        "counter_address": hex(counter_address), "counter_section": counter_section,
        "instructions": ops, "hardware_validated": False,
    }
    (output / f"{stem}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("S3 READY ISR verified: inline aligned 32-bit load/store, memory barriers, IRAM and DRAM placement.")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--object", type=Path, help="Inspect an isolated production translation-unit probe")
    args = parser.parse_args()
    verify(args.root.resolve(), args.object)
