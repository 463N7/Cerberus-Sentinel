# Validating the rev81 contribution

This contribution contains the rev81 development checkpoint. Host checks use synthetic fixtures, compile selected production C code for the computer, and exercise parsers, state machines, transport handling, storage, and UI routes. They do not program a board or start RF operations. Passing these checks does not establish hardware acceptance or complete Marauder feature parity.

## Host checks

Use Python 3.11 or newer and the tested Zig Python package in the same environment:

```sh
python -m pip install ziglang==0.14.1
python tools/run_host_checks.py --list
python tools/run_host_checks.py
```

The runner names the exact 79 suites used at the rev81 checkpoint: 69 independent suites use three workers by default, followed by 10 serial UI suites that share generated fixture headers. Use `--jobs 1` to reduce compiler memory usage. It fails if a suite is missing, a suite fails, or fingerprinted source/configuration inputs change while it runs. Keep the checkout stable during validation and run firmware builds separately.

All required source and synthetic fixtures are in this repository. No sibling integration folder, device console transcript, captured network identity, or archived binary is required. Some individual older scripts have a Windows MinGW fallback; the full runner requires Zig, so that fallback is not used.

The result is `artifacts/host-checks/host-checks.json`, with an exit status and SHA-256 for each log, source/configuration fingerprints, Python/Zig versions, and `hardware_validated: false`. Per-suite logs live alongside it. Native executables, generated headers, and rendered UI previews stay in other ignored `artifacts/` subdirectories. Logs can contain local absolute compiler paths; review them before sharing.

The runner preserves the rev81 set rather than claiming to run every historical check in `tools/`. Additional scripts can be run individually when their corresponding functionality changes.

## Firmware build matrix

Use an environment with PlatformIO Core installed and the package versions pinned by the repository's PlatformIO configurations. The checked-in C5 platform adapter in `platform/espressif32-c5/` is required. Run:

```sh
python tools/build_unified.py --list
python tools/build_unified.py --target all
```

The five targets are the main S3 application, onboard C5, S3 programming bridge, WROOM C5 expansion, and XIAO C5 expansion. This command builds only; it does not flash hardware. The report is `artifacts/integration/build-report.json`. It also checks the built S3 READY interrupt placement and onboard C5 raw-transmit linkage.

A first build can resolve dependencies or generate SDK configurations and return `needs-review` because its inputs changed. Inspect those changes, then rerun on the resolved inputs for a stable receipt. Never substitute an old report or binary for a failed build. Do not run builds and host checks concurrently when generating a stable input receipt.

## Evidence and remaining checks

The original local rev81 checkpoint recorded 79 passing host suites and five passing firmware builds. Those historical results apply to that local checkpoint; a fresh checkout must produce its own receipts. Private device captures and installation logs are intentionally not repository fixtures.

The owner confirmed that rev80's scan → select AP → Observed Clients path displayed rows. Rev81 changes the Back labels and footer layout; its complete navigation and next-scan path are exercised by a host harness, but the updated controls still require an on-device check. Earlier reports of Wi-Fi `NO MEM`, Association Sleep, and Bad Message behavior remain unresolved.

GPS reception, full expansion-radio operation, long-running capture, and other feature-specific hardware limits must be accepted separately. Consult [the coverage ledger](MARAUDER_COVERAGE_LEDGER.md) for the current partial implementations and pending evidence. Build success and simulated UI output do not prove those behaviors on a board.
