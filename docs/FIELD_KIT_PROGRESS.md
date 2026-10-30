# Cerberus field kit — implementation checkpoint, 2026-10-27

Current installed checkpoint, 2026-10-29: `candidate-multi-station`, C5 revision
42. The selected-AP picker retains up to 32 BSSID/channel identities. Each AP
can use its own Broadcast, All Observed or Picked Client scope where supported;
Deauthentication supports all three, while Bad Message and Association Sleep
require clients. C5 validates and cycles up to 32 clients in one bounded job;
S3 runs incremental batch preflight, controls Stop and independently receives
on 2.4 GHz. C5 retains 32 AP × 32 client observations lazily for one hour.
All five builds, six flash-region hashes, paired boot, SPI and SD pass. No GPS
module is installed. Live RF and touchscreen acceptance remain open, as do
the three remaining selected-AP engines and substantial Marauder parity.
The later Wardriving merge must run in this same application with an unlocated
logging path when GPS is absent; see [its inventory](WARDRIVING_SOURCE_INVENTORY.md)
and [revision 42 evidence](MARAUDER_PORT_WORKLOG.md).

Previous installed checkpoint, 2026-10-29: `candidate-multi-ap`, C5 revision 41.
The AP picker supports up to 32 selected networks with Clear and explicit
Attacks navigation. All six implemented modes support sequential multi-AP
jobs, per-AP station choices, all-target preflight, coordinated S3/C5 cleanup
and per-target CSV. C5 retains a bounded, expiring cross-AP station cache.
Native checks, all five builds, flash hashes, paired boot, SPI and SD pass.
Live RF and physical touch acceptance, multiple/all stations per AP, the three
remaining AP engines and substantial other Marauder features remain pending.
The full Wardriving merge follows Marauder work, stays in this main application
and must operate without a GPS module; see `../TODO.md` for its acceptance scope.
See [revision 41 evidence](MARAUDER_PORT_WORKLOG.md).

Previous installed checkpoint, 2026-10-29: `candidate-ap-four-final`, C5 revision
40. **Bad Message, Association Sleep, Channel Switch and Quiet Time** now join
Deauthentication and AP-directed Probe Requests as implemented bounded engines.
All six have C5 work plus S3 control/reporting and independent 2.4 GHz reception.
Bad Message/Association Sleep require one observed station; Channel Switch/Quiet
read the selected AP's beacon first. Native checks, all five builds, paired
flash hashes, boot, SPI and SD pass. S3 idle internal free remains 127,428 bytes;
C5 idle free is 83,612 bytes. Live RF and physical touch acceptance remain open.
SAE Commit Flood, AP Clone Spam, Evil Portal/AP Clone, multi-AP/multi-station
selection and substantial other Marauder parity work remain outstanding. The
Banshee research used architecture documentation only; no GhostESP source was
imported. See [the worklog](MARAUDER_PORT_WORKLOG.md) and
[processor roles/reference decisions](PROCESSOR_ALLOCATION.md).

The checkpoints below describe earlier source/installed states.

Latest work, 2026-10-29: [Marauder port worklog](MARAUDER_PORT_WORKLOG.md).
The current pair is `candidate-ap-probe-final`, C5 revision 39. AP-directed
Probe Requests now has a real backend and configuration screen, alongside
deauthentication. Both use the S3 receiver on 2.4 GHz and the existing C5 TX
worker. All five builds, six final flash-region hashes, boot, SPI and SD pass.
Seven AP attack engines, multi-AP scheduling and live RF acceptance remain
pending. C5 idle free remains 83,708 bytes; S3 idle free is 127,428 bytes.

The preceding S3 update was `candidate-picker-s3-rx`, installed with C5 revision 38
unchanged. It improves network selection (retained scans, complete SSIDs,
BSSID/channel identity and selected marker), reduces target verification to one
AP record when the index is unchanged, and adds S3 passive reception to the
2.4 GHz deauth workflow. Host checks, five builds, S3 image hashes, boot, SPI and
SD pass. Live RX and touchscreen acceptance remain pending. S3 idle internal
free memory is 127,444 bytes with ten tasks; no resident receiver task was added.

Previous paired checkpoint:
The installed `candidate-ap-deauth` pair adds C5 revision 38 and bounded
selected-AP deauthentication, with broadcast or one observed station and S3
configuration, stop, lease and reports. AP selection, passive station discovery,
ping/ARP and service scans from earlier revisions remain. All five builds,
linked-driver checks, six flash regions, boot, SPI, SD and idle state pass.
Live RF/network acceptance and physical touch checks for new features remain
pending. C5 idle memory is 83,708 bytes, a net gain of 8,096 over the earlier
pre-consolidation snapshot. No new resident transmit worker was added.
Full Marauder parity remains in progress;
the historical checkpoints below do not imply that unported attacks work.

## Hardware evidence

The [board reference](https://463n7.github.io/Cerberus-Dev-Board/hardware.html)
documents an S3 N16R2 and C5 modules with 4 MB flash. The existing 2 MB onboard
C5 layout and 1 MiB expansion application slots are software allocations; they
have not been expanded in this change. The [pin reference](https://463n7.github.io/Cerberus-Dev-Board/pinouts.html)
matches the shared SPI3 and six-select expansion wiring used here.

During the later requested test-flash preparation on 2026-10-27, ROM queries
confirmed this unit's S3 has 16 MB flash / 2 MB quad PSRAM, and its onboard C5
actually has **8 MB flash**, revision v1.2 and a 48 MHz crystal. The firmware's
two-megabyte C5 allocation is retained for the first hardware test. Installation
results and recovery coverage are recorded separately in
`../../integration/device-test-2026-10-27/`; the earlier no-flash statements below
describe the pre-installation development checkpoint.

Read-only `chipinfo` on COM76 reported **Cerberus Prototype Wardriving Expansion
1**, ESP32-S3 revision v0.2, IDF 6.0.1. No reset, flash write, or radio scan was
requested by that probe. GPS is not installed, as confirmed by the owner.

## Implemented in this checkpoint

- Dashboard **Recon**: original animated pixel Cerberus, passive scan state,
  evidence log, confidence/type filters, alert mute, ignore/un-ignore, timed
  snooze and exact-address signal tracking. These use the existing Sentinel
  radio scheduler and classifier. Preferences/history are session-only. See
  [the feature ledger](FEATURE_PARITY.md) for SquachWatch and Marauder gaps.
- One S3 SPI3 owner registers the 40 MHz onboard link and a 250 kHz expansion
  transport. A common mutex covers whole protocol exchanges and manual selects.
- Namespaced expansion codec with bounds checks and atomic batch publication.
  It supports the supplied scanner's protocol 3 / firmware 1, separate from
  Sentinel's onboard protocol 2. Unsupported replies are not accepted.
- **Wi-Fi Watch → Tools → Field survey**: explicit Start/Stop, R1–R6 status,
  paged observations, SD vendor labels and Save. Leaving the page or sleeping
  requests Stop. Lost STOP acknowledgements remain pending and are retried.
- Background expansion polling keeps radio transfers out of the touch handler.
  All six radios currently cover the complete conservative US channel plan.
  Existing scanner firmware uses active Wi-Fi scanning on this non-DFS plan;
  this is not advertised as a passive-only scanner.
- A bounded 32-BSSID cache, eviction and dropped-record counters. Save exports
  that cache through the existing SD ownership service to `CERBERUS/LOGS`.
  SSIDs are hex-encoded to preserve bytes and avoid CSV formula interpretation.
  The report explicitly has no GPS and is not a geotagged WiGLE upload file.
- One `CERBERUS/RECON.DB` SD resource file, 40,210 indexed /24 assignments,
  vendor alternatives, source-specific hints, revision/checksum provenance and
  source/license material. Runtime currently uses the **vendor labels** only.
- Binary search requires at most 21 small reads in the host corpus, a 4 KiB
  scratch allocation (PSRAM preferred), and eight cached vendor labels.
  No database table is linked into executable flash. Reads use storage.c's
  existing ownership lock; missing SD/USB handoff does not prevent radio work.
  Locally administered and multicast MACs are not attributed. BLE callers must
  additionally reject random-address types before using any OUI lookup.

## Database sources and limits

| Source | Imported/reference material |
| --- | --- |
| [CYM](https://github.com/JimGat/CYM) | Bundled IEEE MA-L CSV at `59a86a86a7bfb35635ebd3a8cac9165bdc6ca238`; registry vendor facts. Root repository license was not declared in the retrieved metadata. |
| [ChimeraBLE](https://github.com/MatthewKuKanich/ChimeraBLE) | Its documented Wireshark-derived /24 registry source, retrieved directly; converter/reference at `2330423dae8f8fa163617172310784889d8972a7`. GPL notice retained. No MITM, cloning, or HID implementation imported. |
| [SquachWatch](https://github.com/skizzophrenic/SquachWatch-CYD) | 110 OUI hint rows from `3e9be6c02265b14c798e58da01a0913866143b0f`; original confidence retained as source opinion. Signature source and GPL notice embedded as reference material. |
| [Dragon Detector](https://valleytech-dragon-detector.pages.dev/guides/CREDITS) | Published vendor-prefix CSV and public-identifier fact ledger; downloaded contents identified by SHA-256; MIT notice retained. |

Duplicate prefixes keep their vendor alternatives and hints. Hint labels do not
replace the registry owner, and OUI-only evidence never proves a model, operator,
recording activity or threat. Broad or contradictory source hints are retained
for review, not automatic alerts. BLE UUID/manufacturer/payload rules require
typed predicates and tests; storing their reference material is not equivalent
to enabling those detections. /28 and /36 registry blocks are not indexed yet.

The generated database contains differently licensed source material and is not
declared wholly MIT. Keep the embedded notices and retained source inputs when
preparing any future distribution; this checkpoint creates a local candidate.

## Marauder capability mapping

Reference: [official project](https://github.com/justcallmekoko/ESP32Marauder)
and [wiki](https://github.com/justcallmekoko/ESP32Marauder/wiki), reviewed 2026-10-27.
This is a capability roadmap, not a claim of complete Marauder compatibility.

| Capability group | Current Cerberus status |
| --- | --- |
| Dual-band AP discovery, channel/security summaries | Existing Sentinel; expansion survey now added |
| BLE advertisements, signal inspection, selected GATT reads | Existing Sentinel onboard C5 |
| Broadcast tracker/device hints and drone Remote ID | Existing bounded Sentinel Watch rules; SD reference pack does not expand live Watch rules yet |
| Deauth/disassociation observation and network changes | Existing defensive survey and baseline comparison; broader packet analysis remains work |
| Wi-Fi/BLE packet capture to SD, capture browsing | Unified streaming capture format, queue budgeting and integration still pending |
| GPS wardriving, WiGLE/WDGWARS uploads | Original Wardriving source preserved; not wired into the new survey; no GPS installed |
| Distributed channel scheduling and merged R0–R6 inventory | Pending; current survey handles R1–R6 and preserves R0 Sentinel tools |
| Local control, USB compatibility, settings migration | Sentinel retained; full Wardriving remote/USB parity pending |
| Disruptive transmission floods, credential harvesting | Not implemented in this work |
| CC1101/nRF24/NFC/LoRa features | Require applicable hardware, documented wiring and separate integrations |

## Verification and next work

Final matrix: S3 application, onboard C5, S3 bridge, expansion WROOM and expansion
XIAO all build successfully with unchanged input fingerprints. The S3 image is
1,274,832 bytes and static RAM is 175,760 bytes. The 250 distinct original source
files in the import manifest still match their recorded hashes. This was the
pre-flash checkpoint; physical testing has since started as described below.

Host checks exercise every globally administered database entry (40,187),
bounded read counts, corrupt/truncated/missing files, invalid offsets, private
addresses, protocol sequencing, truncated batches and 100,000 malformed frames.
The additional Recon tests cover filtering without losing observations,
queue pruning, exact address/type policies, snooze clock wrap and bounded policy
capacity. Native renderer checks exercise screen bounds, touch gaps, unchanged
frames, paused/offline animation, options/actions, and stale signal presentation.
Preview images in `artifacts/recon-preview/` are host-rendered fixtures, not
screenshots or findings from the physical unit.

These checks do not establish SPI electrical behavior, actual throughput or successful
stopping of six physical radios. See the final build matrix in
`artifacts/integration/build-report.json`; C5 images retain their existing protocols.

Next: validate shared-bus timing on the physical unit with a recoverable matched
image set; measure dropped records and heap high-water marks; implement fair
channel allocation, streaming SD sessions, typed BLE recognition rules, and
onboard/expansion observations in one inventory. Preserve existing Sentinel
features throughout. Firmware code and radio timing remain on the processors;
large databases, assets and session files belong on SD.

## Physical test checkpoint: 2026-10-27

The owner's S3 and onboard C5 were backed up, then flashed with hash verification
using the temporary S3-to-C5 bridge. S3 reports 16 MB flash and 2 MB quad PSRAM;
the onboard C5 reports 8 MB physical flash and a 48 MHz crystal. Its current
application partition layout covers the first 2 MB. The expansion type remains
unconfirmed and its firmware has not been changed. The owner confirmed display
and touch after the paired boot; C5 protocol 2 / firmware 31 is online. These
checks do not establish scan or full feature acceptance.

The physical microSD is accessed through the S3 SDMMC controller in 1-bit mode,
up to 20 MHz. The API's `sdmmc_card_t` does not mean an eMMC chip or virtual disk.
The mounted filesystem reports 62,559,617,024 bytes of capacity.

Starting USB SD mode produced a main-task panic. The saved core dump, decoded
with the exact flashed ELF, shows logging through the already-uninstalled USB
Serial/JTAG driver recursively logging its own "driver is not initialized"
error (ending in `xQueueSemaphoreTake`). The correction switches the VFS to
nonblocking hardware access before driver removal, waits for the console task
to finish commands, and explicitly manages the internal USB PHY in both
directions. USB attach status is initialized before callbacks can update it.
The corrected S3 image builds successfully and was flashed with verification.
The owner confirmed USB SD mode stays open; Windows enumerated the matching
64 GB card as FAT32. At the owner's request, all card files were backed up with
hash checks and cleared, retaining the filesystem. Only `/CERBERUS/RECON.DB`
was reinstalled (6,333,004 bytes; SHA-256
`bb6a366efd96e1f562a3ce235ce84b115d1c4c295be09e92844dacd6c8cc2357`).
Windows safely ejected the volume. The owner reported return without reboot,
but native USB did not re-enumerate, including after a cable reconnect and warm
reset. A full power cycle restored COM76 and the same S3 identity. The owner
also replaced the card with a formatted 32 GB card (mounted capacity
30,932,402,176 bytes); the first copy does not apply to this replacement.
The follow-up handover correction adds an explicit detach interval, restores
the console even if filesystem remount fails, makes cleanup retryable, and
displays return errors. Injected-failure host tests pass. The subsequent matched
candidate and its successful first physical return are recorded below.
Original and corrected candidate images, backups, core dump,
ELFs and logs are kept in `../integration/device-test-2026-10-27/`.

## Network and SD resource checkpoint

The next matched candidate, `candidate-network`, has been flashed and verified
on both the S3 and onboard C5. All five build targets pass with unchanged source
fingerprints. S3 application/static RAM are 1,281,680/177,344 bytes; onboard C5
application/static RAM are 1,572,800/65,048 bytes. Native USB diagnostics show
both processors online, C5 firmware revision 32, no fault reset, and the new
32 GB microSD mounted. The stored earlier USB crash record remains available
for diagnosis and is not evidence of a new crash.

The connected-network screen adds common/custom TCP port scans and SSH/Telnet
LAN discovery. C5 owns the bounded socket worker; S3 owns selection, progress,
cancel and SD CSV export. See [the feature ledger](FEATURE_PARITY.md) for exact
scope. Planner and actual UI host tests pass; RF acceptance remains pending.

The owner then completed a live common-port run and saved it to SD. Read-only
C5 diagnostics confirm all 16 probes completed, with 3 open and 13
no-reply/unreachable outcomes, zero job error, and no processor fault reset.
Minimum C5 free heap reached 10 KiB across discovery/join/scanning; the current
free heap recovered to 70 KiB. The low point's exact phase is not yet isolated.
Other modes and concurrent-load acceptance remain open.

The replacement card now holds the combined 6,333,004-byte RECON.DB, three help
and notice files, a sample local script, and RESOURCES.JSON. All six copied
files passed host SHA-256 verification. Existing logs were preserved. Windows
ejected the filesystem and the owner confirmed return to the file browser
without restart. Native USB automatically returned as the same S3 endpoint.
Firmware reports MSC inactive, ESP_OK, a mounted card, and an exact match for
the full database SHA-256. A live lookup of prefix 00000C returned Cisco Systems,
Inc. This establishes one successful complete USB SD cycle on the replacement
card; repeated cycling and concurrent workload stress remain untested. The
host copy receipt is
`../../integration/device-test-2026-10-27/new-card-resources.json`.
Firmware evidence is `new-card-firmware-read.txt` in the same folder.

## Radio Lab pair checkpoint: 2026-10-28

`candidate-radio-lab` is now installed on both the S3 and onboard C5, with every
written image hash verified. The temporary bridge matched the carrier S3 MAC
and returned automatically to native USB after programming C5. The optional
expansion boards were not flashed. All five build targets passed from stable
inputs; host worker, native UI, network planner, Recon and USB checks passed.

The home Scan/Attack launchers and three-tab field menu are now on the device.
Attack links to Radio Lab, with a C5 standard Wi-Fi beacon test and an S3
standard BLE advertiser. Both require explicit START, offer 10/20/30 seconds,
stop on leaving/sleep/control-lease expiry, and can save a diagnostic CSV.
All 20 Marauder attack catalog entries still remain unimplemented. The two
new ordinary discovery tests are infrastructure, not full attack parity.

S3 application/static RAM are 1,421,312/179,344 bytes; C5 are
1,578,608/65,184 bytes. Physical startup diagnostics report C5 firmware 33,
capabilities `0x001fffff`, both test job IDs/states zero, no fault reset, and
the 30,932,402,176-byte microSD mounted. S3 free heap is 129,152 bytes at this
checkpoint (minimum 128,296); C5 free/minimum are 73/69 KiB. S3's Bluetooth
configuration therefore costs runtime memory beyond the small static-RAM
increase. First BLE initialization and RF reception still need measurement.

The new nine-file SD bundle is prepared locally but not yet copied. It retains
the combined 6,333,004-byte database and adds small Wi-Fi/BLE name profiles plus
Radio Lab help. The currently inserted card still has the previous six-file
bundle. The second USB SD return was confirmed without reboot and restored
native USB (`second-return-console.txt`). Read-only `reports` sees one log
entry and no generated network CSV; export acceptance remains open. Save
errors now persist in the UI and the network CSV buffer respects the 4 KiB
storage limit.

Build fingerprints and exact images/ELFs are in
`../../integration/device-test-2026-10-27/candidate-radio-lab/`; physical boot
evidence is `radio-startup-console.txt` beside it. These verify installation
and idle startup, not over-the-air acceptance of either new worker.

## Target selection checkpoint: 2026-10-28

The S3 `candidate-target-flow` image is flashed with all three written images
hash verified. C5 remains the matching revision-33 `candidate-radio-lab` image.
Wi-Fi scan selection now opens an action card, and the Attack menu retains a
direct link to that target. Connection identity is tracked by BSSID/SSID/security
rather than display name alone. Returning to port tools after selecting a new
AP clears the previous connection's cached report; an unchanged connection
retains its existing target/results. The network and target host tests cover
that distinction, stale connections, touch gaps, and no automatic transmission.

Final S3 image size is 1,423,504 bytes (+2,192 from Radio Lab); static RAM is
179,456 bytes (+112). The S3 build passed with stable inputs. This UI-only
checkpoint rebuilt S3, not all five radio targets. Boot diagnostics confirm the
same S3 identity, no fault reset, onboard C5 online, SD mounted and both radio
test workers idle. S3 free/minimum heap are 128,908/128,052 bytes. The exact
source fingerprint, images and ELF are preserved in
`../../integration/device-test-2026-10-27/candidate-target-flow/` and startup
evidence is `target-flow-startup-console.txt` beside it.

Owner touch acceptance of the new card is pending. Deauthentication and the
remaining Marauder attack engines have not been implemented by this update.
The Deauth All scope question remains open (clients on the selected network
versus explicitly selected networks). The new SD profile bundle remains
prepared but not copied, and the first BLE/C5 transmit acceptance is still
pending; the owner's response instead requested this target-flow change.
