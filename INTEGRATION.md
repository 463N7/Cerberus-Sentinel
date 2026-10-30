# Unified Cerberus development workspace

This is the integration working copy. Sentinel now hosts a first expansion survey
service through a shared SPI owner, with SD vendor lookup and snapshot export.
See [the current implementation checkpoint](docs/FIELD_KIT_PROGRESS.md) for exact
scope and remaining work. A successful target build is not a completed feature
merge or a hardware-approved release.

## Build measurements — 2026-10-27

All five targets passed the final build matrix with unchanged input fingerprints.
The build-report checks passed (3 tests), and Wardriving's existing host UI suite
passed. The original 250 distinct source files were rechecked against the import
hashes and remain unchanged. The latest matched S3/onboard-C5 candidate has now
been flashed with verification; both processors boot and the new 32 GB microSD
mounts. One complete USB SD copy/eject/return cycle passed: native USB returned
without reset, and firmware hashed the full database and resolved a known vendor.
A live 16-common-port scan completed successfully; further scan modes and SD
export verification are still in progress. The table below is the newer
`candidate-tabs` build, ready for S3 installation; the device still runs the
previous `candidate-network` S3 image until that installation is recorded.

| Target | Application image bytes | Application partition bytes | Static RAM bytes |
| --- | ---: | ---: | ---: |
| S3 Scan/Attack tab candidate | 1,284,112 | 7,340,032 | 177,344 |
| Onboard C5 | 1,572,800 | 2,031,616 | 65,048 |
| S3 bridge | 255,248 | 7,340,032 | 29,968 |
| WROOM expansion C5 | 925,616 | 1,048,576 | 61,176 |
| XIAO expansion C5 | 929,856 | 1,048,576 | 61,184 |
| Original Wardriving S3 baseline | 1,333,760 | 7,340,032 | 91,248 |

Static RAM excludes runtime allocations. Each processor needs its own image;
one user-facing application does not mean one binary for every chip. The expansion images currently use
1 MiB application partitions even when their flash chips are larger. Both C5
application budgets deserve attention before adding services.

The 6,333,004-byte vendor/reference database
is a separate SD file and is not linked into the application. Native renderer,
Recon policy, database, and expansion protocol host checks passed. These memory
numbers do not establish runtime heap headroom or sustained radio throughput.

Machine-readable evidence: `artifacts/integration/build-report.json` and
`../integration/baseline-summary.json`. The board documentation and running S3
firmware have been inspected. ROM identification confirms the S3 has 16 MB flash
and 2 MB quad PSRAM, and the onboard C5 has 8 MB physical flash with a 48 MHz
crystal. Its existing 2 MB software layout is retained. Expansion variant still
needs physical confirmation; expansion firmware has not been flashed.

## Layout

- Root, `s3_main`, `c5_firmware`, `shared`: imported Sentinel foundation.
- `expansion/c5_scanner`, `expansion/shared`, `expansion/boards`: imported Wardriving expansion firmware and its independent wire protocol. Keeping these separate avoids conflicting protocol names and pin definitions.
- `tools/build_unified.py`: build-only target matrix with logs, file sizes, SHA-256 checksums and input fingerprints. It never flashes hardware.
- `docs/SENTINEL_UPSTREAM_README.md`: original Sentinel documentation. Existing Sentinel packaging tools still describe Sentinel targets only; they are not unified release packaging.

## Build

Use a Python environment containing PlatformIO:

```powershell
python tools/build_unified.py --list
python tools/build_unified.py --target all
```

Targets: `s3`, `onboard`, `bridge`, `expansion-wroom`, `expansion-xiao`. Both expansion targets are compiled for coverage; choose only the physically correct one for a future installation. The S3 target assumes production N16R2 hardware and is not authorization to flash an unidentified board.

Reports go to `artifacts/integration/build-report.json`. ELF/map files and build logs support subsequent flash and RAM analysis. Generated dependency locks may change on the first build; rerun to establish an unchanged-input receipt. Reports explicitly do not claim hardware validation.

The imported S3/onboard CMake projects still carry upstream `1.0.0` version
strings. Use the source fingerprints and image hashes to identify this local
candidate. A future unified release needs its own versioning, matched image
packaging and recovery receipt before distribution.

## C5 build correction

The original `espressif32@7.0.1` platform selected a nonexistent Xtensa compiler
for ESP32-C5. C5 targets now use a project-local copy of that platform with C5
added to its RISC-V classifications. The patch and retained upstream license are
in `platform/espressif32-c5/`. Expansion builds also explicitly pin the same IDF
6.0.1 package as Sentinel. No global platform source files were modified.

Build tooling checks run with `python tools/check_build_unified.py`.

## Other clean-build fixes

- The S3 bridge explicitly passes both SDK defaults files to CMake; the original
  PlatformIO option did not enable TinyUSB CDC on a fresh configuration.
- Expansion CMake uses an explicit development version so builds do not require
  a Git commit to exist.
- S3 defaults explicitly request size optimization with assertions retained.
  The pinned compiler repeatedly crashed in IDF's RGB driver with generated
  debug optimization; size optimization compiled successfully. This changes
  build optimization and still needs ordinary device regression testing.
- The matrix limits compilation to four concurrent jobs.

The original Wardriving S3 baseline also builds successfully in
`../integration/baselines/wardriving`. Its host UI tests passed with documented
`ziglang==0.14.1`; the installed older MinGW compiler rejected a signedness
warning under `-Werror`. No original Wardriving source was changed for that test.

## Implemented transport and next work

The latest matched pair adds C5-owned TCP port scanning, connected-LAN SSH and
Telnet discovery, and S3 controls/SD CSV exports. It advertises firmware revision
32 through the existing protocol V2. Host planner, actual UI and USB lifecycle
tests pass; physical scan outcomes remain to be tested. See
[the feature ledger](docs/FEATURE_PARITY.md) for scope and limits. Firmware and
ELF hashes, recovery backups, flash logs and boot diagnostics are preserved in
`../integration/device-test-2026-10-27/candidate-network/` and its parent folder.

The shared SPI3 owner now registers both links and serializes complete exchanges,
including manual expansion chip selects. The expansion codec retains Wardriving
timing and rejects malformed/version-mismatched replies. A background worker and
Field survey screen provide bounded observations and explicit Start/Stop/Save.
Unacknowledged stop requests remain pending. Protocol and database host tests run
with `python tools/check_field_kit.py`; physical timing remains unverified.

Next work includes continuous SD logging, fair channel scheduling, merged R0–R6
observations, typed BLE recognition, and later GPS. Refer to the workspace-level
`INTEGRATION_PLAN.md` for parity, USB, SD asset and hardware acceptance milestones.
