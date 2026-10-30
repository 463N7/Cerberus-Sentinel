# SD resources

Copy the contents of this `sdcard/` directory to the FAT32 card root. The
`CERBERUS/RECON.DB` file is the single runtime vendor database; it includes
source/checksum/license metadata. `CERBERUS/PORTAL/INDEX.HTM` is the optional
portal page consumed by the selected-AP local test service.
It is a local development candidate. On 2026-10-29 the device's read-only
`resources` command verified the replacement card's database SHA-256 and a
vendor lookup. This confirms the installed file and lookup path at idle, not
the other Recon and BLE runtime paths.
No database is required to boot or scan.

The firmware displays registry vendor names in Field survey, AP details and
selected BLE device details (public addresses only). Stored
family hints and BLE reference material are not yet executed as alert rules.

`python tools/build_sd_resources.py` validates this database and packages all
currently supported SD resources into `artifacts/sd-resources/cerberus-sd-resources.zip`.
It adds the existing HELLO.CBS local script, readable help and attribution, and
`CERBERUS/RESOURCES.JSON` with hashes. The database remains one combined file;
help and examples are separate readable resources. Empty LOGS/FIRMWARE folders
are created by the firmware as needed. The resource package does not contain
unimplemented radio services or automatically run scripts.

After a copy, eject the computer volume before returning SD to firmware. The
native serial `resources` command reads RECON.DB on the device, prints SHA-256,
and exercises a vendor lookup. `usb` reports handover status/errors.

The owner replaced the first card with a newly formatted 32 GB card. The
2026-10-29 read-only receipt refers to this replacement card.

The prebuilt `CERBERUS/RECON.DB` is included and can be used and validated with
`tools/build_sd_resources.py` from a fresh clone. Rebuilding that database from
source requires separately obtaining the exact historical source snapshot; the
parent-workspace directory in the example below is not included in this repository.
Source URLs, content hashes and license descriptions are retained in the database's
embedded metadata and `CERBERUS/HELP/NOTICES.TXT`. Some source URLs are live rather
than commit-pinned, so downloading their latest contents may not reproduce this
snapshot. Retain the recorded source notices and verify the recorded input hashes
before claiming a byte-identical reconstruction.

With the exact source snapshot available in the indicated location:

```powershell
python tools/build_recon_pack.py --sources ../integration/recon-sources --output sdcard/CERBERUS/RECON.DB
python tools/check_field_kit.py
```

Use the existing USB SD mode only after installing and validating a matched
firmware set, or copy with a card reader while the device is powered off. Do not
let the firmware and a host computer mount the FAT filesystem concurrently.

Format: 40-byte little-endian `CERBREC1` header (version/count/index/blob/metadata/
total/body CRC/reserved), sorted 16-byte OUI index entries (key/offset/length/CRC),
record blobs (one-byte vendor length, label, JSON facts), then JSON provenance.
Per-record CRC covers the little-endian key and the complete record blob. The
builder verifies the whole file; the device validates the requested record and
rechecks the header around reads. CRC detects accidental corruption, not forgery.
