"""Validate and package all currently supported SD resources, without device I/O."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile
from build_recon_pack import verify, HEADER

ROOT = Path(__file__).resolve().parents[1]
SD = ROOT / "sdcard"
db = SD / "CERBERUS/RECON.DB"
verify(db)
raw = db.read_bytes()
header = HEADER.unpack_from(raw)
metadata = json.loads(raw[header[5]:])
files = {"CERBERUS/RECON.DB": raw}
files["CERBERUS/SCRIPTS/HELLO.CBS"] = (ROOT / "examples/HELLO.CBS").read_bytes()
portal = (SD / "CERBERUS/PORTAL/INDEX.HTM").read_bytes()
if not 0 < len(portal) <= 2048 or b'action="/submit"' not in portal or b'name="name"' not in portal:
    raise ValueError("Cerberus portal asset is missing, oversized or incompatible")
files["CERBERUS/PORTAL/INDEX.HTM"] = portal
files["CERBERUS/HELP/START.TXT"] = b"""CERBERUS FIELD KIT - DEVELOPMENT BUILD

S3: display, touch, SD files, reports, BLE passive observation and advertising.
Onboard C5: 2.4/5 GHz Wi-Fi, packet survey and connected-network probes.
Selected-AP 2.4 GHz tests temporarily use S3 Wi-Fi reception and pause S3 BLE.
C5 still handles selected-device BLE GATT reads until its S3 migration is ready.
Expansion C5s: Field survey. GPS is not installed on this unit.

RECON.DB contains 40,210 /24 vendor entries and source references from
Dragon Detector, CYM, SquachWatch and ChimeraBLE's registry source.
Wi-Fi AP details, Field survey, public-address BLE details and Watch findings
use its vendor labels. Vendor identity
alone does not identify a particular product or prove malicious activity.
Archived detection rules are references, not new enabled detections.

Tools > SD files browses this card. Tools > Scripts can run HELLO.CBS.
Scan reports and device logs are written beneath CERBERUS/LOGS.

USB SD: start the drive, copy files, eject in the computer, then use
RETURN SD TO FIRMWARE. The device cannot scan the filesystem while the
computer owns the card. Do not remove a card while either is writing.

The native serial console's resources command verifies RECON.DB from SD.
RESOURCES.JSON records the complete bundle with file sizes and SHA-256.
The database embeds source/license metadata; NOTICES.TXT lists attribution.

The complete Marauder Wi-Fi/BLE feature inventory remains an integration
target. This package does not install unimplemented attack services.
"""
files["CERBERUS/HELP/RADIO.TXT"] = b"""RADIO LAB - DEVELOPMENT TEST ENGINES

Home > Attack > RADIO LAB / S3 + C5.
C5 sends one standard Wi-Fi beacon every 100 ms for the selected SSID.
S3 sends one standard, nonconnectable BLE name advertisement every 100 ms.
These discovery tests are not the complete Marauder attack feature set.

Tap the mode, channel or duration to configure; START explicitly transmits.
STOP, leaving the screen, sleep or three seconds without UI contact stops.
Wi-Fi supports channels 1, 6, 11, 36, 40, 44 and 48, subject to driver support.
The C5 must be disconnected and its other radio jobs stopped before a beacon
test. It restores the previous channel/band afterward. S3 BLE is separate.
The UI offers 10, 20 or 30 seconds. Nothing transmits automatically at boot.

CERBERUS/TESTS/WIFI.TXT is one printable SSID, 1-32 bytes, optional final newline.
CERBERUS/TESTS/BLE.TXT is one printable BLE name, 1-26 bytes, optional newline.
Missing files use the displayed Cerberus lab names; malformed files are rejected.
These are text profiles, not executable code. Firmware stays in processor flash.

Wi-Fi counts report driver acceptance, not confirmed reception over the air.
BLE does not report a measured transmitted-packet count. SAVE REPORT records
the terminal state/error and sampled minimum internal heap in CERBERUS/LOGS.
The radio serial command reads both jobs without renewing their control leases.
The reports serial command reads bounded network CSVs without USB SD handover.

Selected-AP tests use manual Stop and display live C5 accepted-frame and S3
received-frame rates where S3 reception is available. This is separate from
the timed Radio Lab discovery test. Stop before leaving the test unattended.
"""
files["CERBERUS/TESTS/WIFI.TXT"] = b"CERBERUS-LAB\n"
files["CERBERUS/TESTS/BLE.TXT"] = b"Cerberus Lab\n"
files["CERBERUS/HELP/NETWORK.TXT"] = b"""CONNECTED NETWORK SCANNING - C5 FIRMWARE 32+

Wi-Fi > Networks > select AP > Connect > PORT / SSH / TELNET SCAN.
Enter the network password on the device. Credentials stay in RAM.

MODE cycles: 16 common TCP ports, a custom TCP port/range, SSH LAN
discovery, and Telnet LAN discovery. Custom ranges accept 1-65535.
TARGET selects an IPv4 host on the connected subnet. The gateway is
the initial default. LAN sweeps support /24 through /31, skip this
device, and never silently scan only part of a larger subnet.

START begins a job. STOP, leaving the screen or sleep cancels it.
A lost connection also stops the job. Each connection times out after
500 ms; only one socket is probed at a time. Large ranges take time.
At most 64 open results are retained; the total-open counter continues.
The screen and CSV disclose truncation and partial/cancelled results.

SSH BANNER means the server actually sent an SSH identification prefix.
TCP OPEN on port 22 or 23 alone does not identify SSH or Telnet.
No login, password guessing, or SSH/Telnet session is performed.
No-reply includes timeout or unreachable; it is not proof of filtering.

NEXT pages results. SAVE TO SD exports a completed or stopped job to
CERBERUS/LOGS/WIFInnnn.CSV, including counts and banner-prefix hex.
Common TCP ports: 21,22,23,25,53,80,110,135,139,143,443,445,554,3389,8080,8443.
"""
notices = ["CERBERUS RECON SOURCE ATTRIBUTION", "Full retained notices and reference material are embedded in RECON.DB.", ""]
for name, source in sorted(metadata["sources"].items()):
    notices.extend([name, source["url"], source["license"], "SHA-256: " + source["sha256"], ""])
files["CERBERUS/HELP/NOTICES.TXT"] = ("\n".join(notices) + "\n").encode()
manifest = {"format": 1, "kind": "Cerberus development SD resources", "database_entries": header[2],
    "network_scanner_min_c5_firmware": 32,
    "radio_lab_min_c5_firmware": 33,
    "directories": ["CERBERUS/LOGS", "CERBERUS/FIRMWARE", "CERBERUS/SCRIPTS", "CERBERUS/HELP", "CERBERUS/TESTS", "CERBERUS/PORTAL"],
    "files": [{"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
              for name, data in sorted(files.items())]}
files["CERBERUS/RESOURCES.JSON"] = (json.dumps(manifest, indent=2) + "\n").encode()
for name, data in files.items():
    destination = SD / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination != db: destination.write_bytes(data)
out = ROOT / "artifacts/sd-resources"
out.mkdir(parents=True, exist_ok=True)
archive = out / "cerberus-sd-resources.zip"
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as z:
    for name, data in sorted(files.items()):
        entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        entry.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(entry, data)
print(json.dumps({"files": len(files), "bytes": sum(map(len, files.values())), "zip": str(archive),
                  "zip_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}, indent=2))
