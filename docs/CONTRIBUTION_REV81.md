# Cerberus Unified contribution: rev81 checkpoint

This contribution collects the Cerberus development work through **2026-10-30**
for review against Sentinel. The checkpoint is S3
`candidate-wifi-client-navigation` (rev81) with onboard **C5 firmware 60**.
It preserves the stock Sentinel foundation and adds the source, host checks,
build support, SD resources, and implementation records for the integration.
It is a development checkpoint, with known failures and unfinished acceptance.

## What is included

| Area | Implemented source and review entry point |
| --- | --- |
| Integrated Wardriving | C5 Wi-Fi and S3 BLE collection, bounded session storage, GPS adapter and GPS-gated WiGLE output, a separate unlocated stream, probe/client evidence, deduplication, and resumable Stop/flush handling. See [record format](WARDRIVE_WIGLE_FORMAT.md), [GPS logging](WARDRIVE_GPS_LOGGING.md), and [processor allocation](PROCESSOR_ALLOCATION.md). |
| AP/client workflow | Frozen network selection, distinct general tools and selected-AP menus, multi-AP/client selection and capability checks, passive observed-client rows and directional details, and SD reports. Rev81 makes the actual parent navigation controls explicit: **BACK TO CLIENTS**, **BACK TO AP**, and **BACK TO SCAN**. See [relationship semantics](WIFI_RELATION_PARITY.md) and [AP navigation](MARAUDER_AP_MENU.md). |
| Wi-Fi receive and analysis | Packet monitoring, bounded capture/PCAP support, MultiSSID and PineScan analysis, passive probe hints, combined MAC Monitor, and selected-AP CSI Motion Monitor. Physical coverage varies by feature; CSI scores are heuristic. See [MultiSSID](WIFI_BEACON_ANALYSIS.md), [PineScan](WIFI_PINESCAN.md), [MAC Monitor](MAC_MONITOR_PARITY.md), and [CSI](CSI_MOTION_PARITY.md). |
| BLE discovery and tools | S3 native scanning, focused live lists and sorting, frozen selected evidence, advertisement/response provenance and age, Find My Monitor, Fox Hunt, Analyzer, and selected GATT operations. Family-specific parsing includes Find My, Flipper, Meta, iBeacon, Flock, and HC name hints. See [BLE receive semantics](BLE_RECEIVE_LIST_PARITY.md) and [Find My coverage](FINDMY_PROFILE_PARITY.md). |
| Remote ID | Bounded Wi-Fi/BLE receive workflows, source details, priority windows, passive Follow/SEARCH, and advertised motion values with independent Location age. See [Remote ID coverage](REMOTE_ID_PARITY.md). |
| Board and resource integration | Shared SPI ownership, READY-edge accounting and diagnostics, independent expansion protocol/firmware for WROOM and XIAO C5 variants, Recon presentation, indexed SD vendor resources, and resource packaging. See [integration/build layout](../INTEGRATION.md), [C5 toolchain](C5_TOOLCHAIN.md), and [SD resources](../sdcard/README.md). |
| Review and reproducibility | Native host fixtures, UI rendering/dispatch checks, a five-target build matrix, matched-package checks, and pinned source/mode inventories. See [validation](VALIDATION.md) and the [coverage ledger](MARAUDER_COVERAGE_LEDGER.md). |

The source also retains the active-mode implementations and their navigation
from earlier checkpoints. Their inclusion does not establish successful RF
effects or resolve the later Wi-Fi failures. The coverage ledger is the detailed
record of implemented, partial, pending, review-required, and hardware-limited
work; menu visibility alone is not completion.

## Validation recorded at the checkpoint

The retained rev81 development records report:

- **79 host-check suites passed** with unchanged input hashes.
- **Five firmware targets passed** with unchanged input hashes: S3 application,
  onboard C5, S3 programming bridge, WROOM expansion C5, and XIAO expansion C5.
  The matched package and S3 READY interrupt placement gate also passed.
- The rev81 S3 installation had three verified image writes. C5 firmware 60
  retained the byte-identical images from the verified paired rev80 installation;
  the current bridge was built but not flashed at rev81.
- Startup confirmed the C5 link, mounted SD, released radio ownership, and no
  prior reset fault. These startup checks do not establish sustained reception,
  peak memory headroom, or feature acceptance.
- The owner confirmed that rev80's selected-AP **Observed Clients** page opened
  with rows, then reported that Back was unclear. Rev81's footer and full-screen
  dispatch tests address that navigation issue, but the revised controls still
  need physical acceptance.

These are historical development results, separate from any fresh checks run on
the contribution branch. [Validation instructions](VALIDATION.md) describe how
to run the submitted checks. Board logs, captured device identities, local flash
receipts, build outputs, and the parent development workspace are not public
test fixtures. Historical documents can refer to those local records; such
references are provenance pointers, not files promised by this repository.

## Remaining work

1. Physically verify **BACK TO CLIENTS → BACK TO AP → BACK TO SCAN**, then start
   a normal Wi-Fi scan and confirm that it finishes on rev81.
2. Diagnose the reported Wi-Fi **NO MEM**, **Association Sleep**, and **Bad
   Message** failures. General memory improvements and earlier successful tests
   do not close these reports.
3. Verify directional signal/age presentation, saved report and CSV contents,
   repeated transitions, Stop/Back cleanup, responsiveness, and sustained load.
   Ordinary AP collection still includes synchronous work.
4. Complete deferred Remote ID Follow checks and the feature-specific RF and
   interaction acceptance listed in the coverage documents.
5. Test an actual GPS receiver and the six-C5 backboard. Neither was available
   for the recorded hardware acceptance; expansion builds alone do not prove
   fitted-radio operation.
6. Continue the full feature port. The pinned 95-mode ledger still contains
   **73 partial, 9 needs review, 9 pending, and 4 hardware-limited** entries.
   No full Marauder parity is claimed.

Observed address relationships, vendor prefixes, family hints, and Remote ID
messages remain unauthenticated evidence. They do not establish association,
physical identity, device location, or hostile intent beyond the documented
record semantics.

## Sources, licensing, and release boundaries

The Marauder reference snapshots and their equivalence are documented in
[the develop baseline](MARAUDER_DEVELOP_BASELINE.md); the imported MIT notice is
in [LICENSES/ESP32Marauder.txt](../LICENSES/ESP32Marauder.txt). The patched local
PlatformIO platform retains its own license. The SD reference database and
Watch materials retain their separate source and attribution records; see
[SD notices](../sdcard/CERBERUS/HELP/NOTICES.TXT) and
[Watch notices](WATCH_THIRD_PARTY_NOTICES.md). This contribution does not relicense
the complete repository or its mixed-source data under one blanket license.

The stock 1.0.0 web installer assets are retained as imported upstream material.
Submitting this source checkpoint does not select a new public firmware release,
deploy a flasher, or flash hardware. A future release needs an explicitly
validated matched image set, versioning, and recovery instructions.
