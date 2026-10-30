# Marauder port work, 2026-10-28

## Revision 81 installed: navigation milestone and requested pause, 2026-10-30

The owner confirmed Observed Clients rows on rev80, then reported that there
was no Back control. Rev81 makes the parent footer explicit: BACK TO AP,
BACK TO CLIENTS and BACK TO SCAN. The Observed Clients footer is wider so its
label fits at the existing font size; the duplicate detail-body control is
removed. Existing navigation routes and radio behavior are retained.

The actual parent renderer and tap dispatcher now share fixture coverage for
loading, ready, detail, empty, error and allocation-failure exits. Back does
not start a scan; the explicit scan control does. All 79 host suites and five
stable-input native targets passed, followed by matched packaging and three
verified S3 flash writes. C5 firmware 60 is byte-identical to paired rev80
and was retained. Both static RAM deltas are zero. Startup confirms mounted
SD, C5 online at 40 MHz, ten S3 tasks and no previous reset fault.

This checkpoint completes the navigation correction atop the partial
AP/client observation workflow and integrated Wardriving CSV v4 path.
Physical acceptance of the revised Back controls and subsequent scan is
pending. The full port is unfinished; all 95 coverage statuses are unchanged.
Prior NO MEM, Association Sleep and Bad Message reports remain unresolved.
GPS and the six-C5 backboard are absent; Remote ID Follow testing is deferred.

The owner requested a pause after this milestone to conserve usage. Resume
with the device Back/rescan check before opening another feature. Exact
images, evidence and remaining work are recorded in the
[rev81 receipt](../../integration/device-test-2026-10-27/rev81-flash-receipt.json)
and [resume checkpoint](../../integration/device-test-2026-10-27/rev81-resume-checkpoint.json).

## Revision 80 installed: ordinary AP/client observations, 2026-10-30

C5 firmware 60 now collects bounded directional address-header observations
during its existing passive AP scan. S3 adds exact sweep binding, incremental
readout, a selected-AP Observed Clients page, separate AP/station signal and
age evidence, selected SD vendor lookup and bounded report export. The same
bank enters integrated Wardriving's unlocated CSV v4 route with independent
deduplication and resumable Stop handling. No new task is introduced.

Independent reviews caught and fixed two provenance risks: reuse of the wire's
eight-bit scan generation and reuse of another selected AP's publication.
Full-width producer proof now brackets AP collection and remains fixed during
row transfer; selection also checks the exact BSSID/channel. Failed relation
proofs preserve valid AP rows without substituting an unrelated evidence bank.
The new UI is covered by actual control/render tests and inspected previews.

The [rev80 receipt](../../integration/device-test-2026-10-27/rev80-flash-receipt.json)
binds 79 host suites, five stable-input builds, a matched package and nine
verified paired writes. Startup has C5 firmware 60 online at 40 MHz, SD mounted,
ten S3 tasks and no new reset fault. Relation evidence correctly reports
not_complete before a scan; startup does not establish received-frame acceptance.

The on-demand UI PSRAM store is 4,744 target bytes; Wardriving's session workspace
is 20,864 (+2,248). Static internal RAM increases 128 bytes on S3 and 1,456 on
C5. Idle free internal RAM is 116,064 and 64,468 respectively; S3 free PSRAM
remains 1,428,324. These are bounded costs, not throughput or peak-heap results.
See [relationship semantics](WIFI_RELATION_PARITY.md).

All 95 mode statuses stay unchanged; only AP_STA 49 evidence is advanced.
The owner confirmed that Observed Clients opens with rows for the selected
test AP. The read-only console corroborates a completed C5 bank with two
retained relationships, four matching callbacks and no capacity drops; these
are whole-bank observations, not an associated-client count for the selected
AP. See [initial owner acceptance](../../integration/device-test-2026-10-27/rev80-owner-clients-acceptance.json).
Direction/detail presentation, SD export, repeated cleanup and response-time
acceptance remain pending. Active transmission implementations, historical
owner reports and deferred tests remain unchanged. Wi-Fi NO MEM, Association
Sleep and Bad Message are unresolved; GPS and the six-C5 backboard are absent.

## Revision 79 installed: focused live BLE discovery, 2026-10-30

The ordinary Bluetooth list now publishes active scan results during each
finite eight-second sweep, targeting a 200 ms UI refresh. Focused families
admit matching sources before the 32-row limit, with a bounded eight-original
cache for strictly matched response-only hints. Retained earlier observations
stay distinct from current evidence. C5 Wi-Fi remains independent, and the
BLE list defers optional C5 telemetry that could delay its UI task.

Actual painted rows are copied for selection before the native attachment
drains. Stop, Back, Tools, family changes and sleep preserve ownership until
cleanup succeeds. Full-width source-time anchors prevent old retained evidence
from acquiring a false fresh age after low32 timer wrap. Native banks, history,
controller and UI tests cover these boundaries; no new task or per-publication
allocation is introduced. Active transmission engines remain unchanged.

The [rev79 receipt](../../integration/device-test-2026-10-27/rev79-flash-receipt.json)
binds 75 passing host suites, five stable-input builds, matched images and
three verified S3 writes. C5 firmware 59 is byte-identical to the verified
paired rev78 images and was retained. The first native build exposed GCC's
cross-enum assertion warning; explicit casts fixed it. All 85 first-attempt
evidence files are preserved separately from the final validation.

Target UI PSRAM store: 17,032 bytes; separate focused binding cache: 1,764;
shared scanner snapshot: 8,912 (+16). Static internal RAM grows 80 bytes on
S3 and zero on C5. Settled S3 free internal RAM is 116,192, free PSRAM 1,428,324,
with ten tasks; C5 internal free remains 65,924. SD is mounted, C5 is online at
40 MHz, diagnostics are idle, and no new reset fault is recorded. These costs
and startup checks do not prove dense-scan throughput or touchscreen latency.

All 95 mode statuses remain unchanged. Ordinary/focused BLE remains partial
pending physical reception, response-only admission, sorting/selection and
repeated Stop/Back/restart/sleep acceptance. Wi-Fi NO MEM, Association Sleep
and Bad Message remain unresolved. Remote ID Follow remains owner-deferred;
GPS and the six-C5 backboard are absent. Full Marauder parity remains open.

## Revision 78 installed: combined passive MAC Monitor, 2026-10-30

Scan -> MAC MONITOR now provides explicit Start, one combined Wi-Fi/BLE
inventory, count/signal/recent sorting, frozen selected details and Stop/Back.
C5 firmware 59 reuses the scanner worker for passive legal-channel Wi-Fi
reception; S3 reuses its passive BLE scanner and owns the inventory and UI.
The two radios operate independently. No GPS or movement inference is added.
Opening the page does not start capture, and exit retains state until both
owned producers release. [Detailed semantics](MAC_MONITOR_PARITY.md) distinguish
typed sources, callback reports, partial/response evidence and retained totals.

Immutable C5 banks, bounded chunk transfers, full CRC/row validation and
boot/job/epoch ownership prevent stale or mixed snapshots. A lost ACK does not
double-count or refresh source age. The first request-time anchor survives
retries. Active S3 snapshots use a persistent session collector across finite
passive BLE sweeps. No new resident task or per-report allocation is added.
Compiler-only fixes separate conditional statements and explicitly bound a
display label; failed early build evidence remains separate from final success.

The [rev78 receipt](../../integration/device-test-2026-10-27/rev78-flash-receipt.json)
binds 72 passing suites, five stable-input builds, matched images and nine
verified paired writes. The S3 session store is 13,600 target bytes in PSRAM;
the C5 session store is 2,632 bytes. Static RAM increases 48/280 bytes. Settled
free internal memory is 116,272 bytes on S3 and 65,924 on C5; idle S3 PSRAM and
ten-task count are unchanged. Both MAC diagnostics are idle and error-free,
C5 is online at 40 MHz and the SD card is mounted.

Only WIFI_SCAN_DETECT_FOLLOW moves from needs_review to partial. Physical
reception, sustained transfer, peak memory and repeated touchscreen cleanup
remain pending. GPS-derived following is not implemented. Earlier Wi-Fi NO MEM,
Association Sleep and Bad Message reports remain unresolved; Remote ID Follow
remains deferred for the owner's later test. Full Marauder parity remains open.

## Revision 77 installed: selected-AP CSI Motion Monitor, 2026-10-30

Scan Wi-Fi, select an AP, and open CSI MOTION beside Packet Monitor. START
uses C5 passive CSI capture while S3 owns calibration, signal-variation features,
reliability, history and the controls. Opening the page only freezes the target
and allocates its state. Recalibrate releases the prior capture before starting
another; Stop/Back retain ownership until cleanup succeeds. GPS and association
are not required, and S3 BLE remains independent. The original model is not
claimed numerically equivalent to Marauder or physically validated.

Review closed target-collision and stale-delivery edges: a complete different
target proven before epoch adoption releases only the local claim; stale sample
delivery hides current scores even if status polls remain fresh. Source-time
history stays unchanged while a record or ACK is blocked. A fresh callback newer
than the prior polled clock uses a conservative age bound without false staleness.

The [rev77 receipt](../../integration/device-test-2026-10-27/rev77-flash-receipt.json)
binds 65 passing suites, five stable-input builds and nine verified paired image
writes, with C5 firmware 58. Initial build evidence is retained separately; the
SDK normalized one configuration comment before the final clean build. The
4,040-byte S3 PSRAM store and 1,984-byte C5 ring are session allocations. Static
RAM grows 16/152 bytes; settled C5 free RAM is 66,212 bytes, a net idle cost of
5,264 bytes versus rev76. S3 retains ten tasks and unchanged idle PSRAM free space.
The link, SD and CSI idle diagnostics are healthy. See [CSI details](CSI_MOTION_PARITY.md).

Only WIFI_SCAN_CSI_MOTION moves from needs_review to partial. RF format/reception,
sensitivity, sustained transfer, live memory peaks and repeated physical cleanup
remain pending. Earlier Wi-Fi NO MEM, Association Sleep and Bad Message reports
remain unresolved; Remote ID Follow remains deferred for the owner's later test.

## Revision 76 installed: BLE recent retention and connection recovery, 2026-10-30

Generic scans previously rejected every new typed address after the first 32
rows filled. The existing bank now accepts independently valid new originals
by replacing its oldest original-report row, clearing all prior row evidence.
Responses cannot evict, and completed selected copies retain their own lifetime.
SCAN ORDER replaces the inaccurate FIRST SEEN badge when slots can be reused.
Focused admission before capacity remains open; filters still apply to the
retained results. [Detailed semantics](BLE_RECEIVE_LIST_PARITY.md) describe
the separate bounded replacement/rejection counters and provenance rules.

The generic connection page gains explicit Cancel and Retry Close controls,
with a fresh state read and exact typed-peer check at each tap. A failed close
after late connection success during cancellation now retains its error.
Aggregate console diagnostics expose ownership and GATT activity without
controller initialization. The historical rev75 CENTRAL-owned snapshot
cannot distinguish an expected open connection from stalled cleanup; this
increment is not presented as proof of, or a repair for, a demonstrated leak.

The [rev76 receipt](../../integration/device-test-2026-10-27/rev76-flash-receipt.json)
records 56 passing suites, five stable-input builds and three verified S3
writes; C5 firmware 57 remains byte-identical to the paired rev75 installation.
The initial aggregate had one harness extraction failure, corrected without
changing firmware behavior. Initial logs and the clean rerun are preserved.
Static RAM is unchanged; the PSRAM scan bank grows eight bytes to 8,896.
Settled startup has the C5 online, SD mounted, ten tasks and idle BLE connection
diagnostics. Screen interaction, dense RF reception and close-retry acceptance
remain pending. Wi-Fi NO MEM, Association Sleep and Bad Message remain unresolved.

The [CSI feasibility audit](../../integration/rev76-csi-feasibility.json)
identifies a supported C5 receive-side path and an explicit format/memory
contract for a later increment. It is proposed work, not an implementation or
an accepted feature. Full Marauder parity remains open. Remote ID Follow stays
deferred for the owner's later test, and all historical acceptance retains its
original build scope.

## Revision 75 installed: ordinary Wardriving probe evidence, 2026-10-30

The source audit found the reachable receive-side gap in ordinary upstream
Wardrive: matching probe-request names and OUI clues. The separate station
Wardrive constant has no reachable dispatch in the checked develop tree.
Cerberus adds the passive subset to the existing C5 scan and S3 Wardriving
session, without another menu, scanner task, active stimulus or GPS requirement.
The [v3 CSV description](WARDRIVE_WIGLE_FORMAT.md) distinguishes requested names
from APs and preserves binary SSIDs and callback time.

Independent reviews corrected two boundaries before release: a supported C5
proof becoming an older peer cannot silently fall back to AP-only logging, and
a callback delayed before admission cannot enter a newly armed scan bank.
Service checks cover prewrite coherence, empty AP banks with probe evidence,
Stop at AP/probe boundaries, immutable staged suffixes, accepted-start resets,
SD failure behavior and bounded counters. The existing Stop fixture also
checks BLE drain progress while Wi-Fi capture is retrying. These host checks
do not establish live RF performance or saved-file acceptance.

Remote ID Follow remains deferred at the owner's request. Existing Wi-Fi
NO MEM, Association Sleep and Bad Message reports remain unresolved, and full
Marauder parity is still open. Prior owner acceptance keeps its original build
scope; no rev75 physical acceptance has been inferred from it.

The [rev75 installation receipt](../../integration/device-test-2026-10-27/rev75-flash-receipt.json)
records all 54 host suites, five stable-input builds, matched images and nine
verified writes. The first host run found two stale harness dependencies;
their narrow include/declaration/extraction fixes passed on rerun, and the
initial failure report/logs are retained. C5 firmware 57 and the restored main
S3 application boot with SD mounted and a 40 MHz link. Applications are
1,630,272 C5 bytes and 1,704,080 S3 bytes. Static growth is 1,056 C5 / 32 S3
bytes; the Wardriving session workspace grows 1,716 PSRAM bytes. The initial
diagnostic snapshot retained BLE CENTRAL ownership; an authorized preparatory
reset released it before the final idle gate and flash. This is not claimed
as a repair of BLE lifecycle behavior.

## Revision 43 installed: common AP error fixed, selected AP flow clarified, 2026-10-29

The owner reported that every selected-AP action showed "STOPPED WITH ERROR."
Read-only rev42 diagnostics captured a C5 AP job stopping in 8 ms with
`ESP_FAIL` and zero driver-accepted frames, while the S3 receiver still saw AP
beacons. The shared worker called `esp_wifi_config_80211_tx_rate()` after the
C5 scan had already started Wi-Fi. The installed ESP-IDF 6.0.1 header requires
that call between init and start, and restricts it for 11a/11ac/11ax. Revision
43 removes the runtime call, uses the driver's rate, and records the failing
C5 worker stage in the AP status byte if a future job fails. The screen and
read-only `radio` console can show that stage. No SPI frame size changed.

The Attack picker now says "SELECT APS FROM SCAN" and the ordinary scan target
card says "ATTACK THIS AP." The selected-AP menu keeps AP Clone Spam distinct
from the still-unimplemented Evil Portal/AP Clone service. AP Clone Spam uses a
bounded local clone identity per selected AP; C5 reads the real AP beacon,
transmits clone beacons, and S3 counts matching 2.4 GHz frames. Host tests cover
per-target frozen identities and batch reports. Its stable BSSID per job differs
from upstream Marauder's per-beacon randomized clone identity and remains a
parity gap. SAE Commit Flood and Evil Portal remain unavailable in rev43.

All five native targets, the guarded C5 raw-link check, AP/selection/receiver
host checks, nine frozen image hashes and paired S3/C5 boot checks passed.
The attached S3/C5 pair identifies as `14:63:93:36:78:f4` and
`14:c1:9f:6f:53:30`; C5 reports firmware 43 and capabilities `0x7fffffff`.
The SD card is mounted and boot radio jobs are idle. The owner's selected
channel-10 test AP then produced these live diagnostics:

| Action | C5 result | S3 2.4 GHz observation | Owner screen report |
| --- | --- | --- | --- |
| Probe Request | Complete, 601 driver-accepted frames, error 0 | 601 matching frames | Complete |
| Deauth | Owner stopped after 29 accepted frames, error 0 | 13 matching frames | Stopped or left screen intentionally |
| Channel Switch | Complete, 600 accepted frames, error 0 | 600 matching frames | Complete |
| AP Clone Spam | Complete, 600 accepted frames, error 0 | 600 matching frames | Complete |
| Quiet Time | Complete, 600 accepted frames, error 0 | 600 matching frames | Complete |

The evidence is in
`../../integration/device-test-2026-10-27/attack-fix-verification.json`
and its referenced console logs. Driver acceptance plus independent frame
matching checks these five transport paths; it does not measure effects on
clients or establish every mode, band, scope or Marauder feature. The requested
upstream `develop` commit `79cc6bc` has the same Git tree as the previously
pinned `master` commit, as recorded in
[the develop baseline](MARAUDER_DEVELOP_BASELINE.md). Wi-Fi remains the next
priority, then Bluetooth, then the complete GPS-optional Wardriving merge in
this application.

After the five live jobs, the radio is idle and no new reset fault is present.
Read-only memory diagnostics show 118,152 S3 internal free bytes, 1,419,332
PSRAM free bytes and 82,480 C5 internal free bytes. Those are lower than fresh
boot by 8,796, 8,992 and 780 bytes respectively; a single post-job snapshot
does not distinguish lazy Wi-Fi/selection allocations from a leak. The saved
`attack-fix-after-five-jobs.txt` is a baseline for repeated stress cycles.

## Revision 42 installed: selected client lists per AP, 2026-10-29

`candidate-multi-station` is installed on the identified S3/C5 pair. In each
selected AP profile, Deauthentication offers Broadcast, All Observed Clients,
or Picked Clients. Bad Message and Association Sleep offer All Observed or
Picked Clients. The station screen supports independent multi-pick, Pick All,
and Clear Picks for each AP. A fresh capture preserves only MACs still seen on
that same BSSID/channel. The other three implemented AP modes remain AP scoped.

C5 revision 42 stages an immutable 2–32 client list over commands 73–74, then
revalidates every client against its own frame evidence before starting one
timed job. The existing 20 frames/second limit is aggregate across the list;
deauthentication alternates both directions for each client. C5's lazy evidence
cache holds up to 32 APs × 32 exported clients, with a one-hour expiry and a
24 KiB internal-heap floor. Allocation happens before capture, never in the RX
callback. S3 freezes each AP's list for the batch, validates one target/client
per UI poll before any RF, and can cancel before the first transmit. It receives
independently on 2.4 GHz, matching selected client traffic without double
counting AP beacons. S3 has no 5 GHz receiver; it controls and records C5's
5 GHz job. Reports audit frozen client MACs separately from driver and receiver
counts and use a bounded 256 KiB Wi-Fi report limit.

Host checks pass for 32 APs × 32 clients, recapture/expiry/overflow, staging
retries and conflicts, per-client C5 frame rotation, S3 classifier identity,
incremental no-RF preflight, Stop races, selection UI and CSV. The rendered
station and AP setup screens were inspected. All five native targets passed
with stable source inputs and the C5 raw-link gate. Frozen app SHA-256 values:
S3 `bce5bc029b5a6495fc7fefa7dc8f84f885dca924c79ff5005e1c5feb9c6b234d`
(1,455,168 bytes); C5 `5b296fe06bda5a5a49e30d57f3163084784d7ce0625b10503ba2da7f8fb49208`
(1,594,944 bytes). After bridge and C5 identity checks, all three flash
regions per chip hash-verified. Boot and a later idle check show C5 firmware
42, 40 MHz SPI, SD mounted, no previous reset fault and all jobs idle. S3
internal/PSRAM idle free are 126,996/1,428,324 bytes; C5 idle free is 83,260
bytes. Evidence and the recoverable frozen images are under
`../../integration/device-test-2026-10-27/multi-station-*` and
`candidate-multi-station`; the structured result is
`multi-station-verification.json`.

Live RF results, simultaneous S3 reception, and physical touchscreen behavior
on the owner's test network remain unverified. SAE Commit Flood, AP Clone Spam
and Evil Portal/AP Clone still need backends; much of Marauder remains pending.
The [source-pinned coverage ledger](MARAUDER_COVERAGE_LEDGER.md) records all 95
upstream mode symbols as overlapping feature evidence. The later complete
Wardriving merge stays in this main application and must work without GPS;
see [its source inventory](WARDRIVING_SOURCE_INVENTORY.md). No-GPS observations
need a distinct unlocated session log, while WiGLE rows require a fresh real fix.

## Revision 41 installed: multiple AP selection and coordinated batches, 2026-10-29

`candidate-multi-ap` is installed on the identified S3/C5 pair. The attack
picker now toggles up to 32 copied AP identities, retains selections across
pages, shows the selected count, provides Clear and opens the attack menu only
when Attacks is tapped. Same-name networks remain distinct by BSSID/channel.
Ordinary Scan still opens the network tools card; its Attack action explicitly
chooses that single AP. Selecting or browsing never starts an AP transmit job.

All six implemented AP modes use a sequential batch coordinator on S3. It
freezes the target/station choices, resolves every AP against C5's current
inventory, checks identity/SSID/security and validates every target with the
new read-only command 72 before starting either radio. C5 revision 41 is
required for batches; capability bits remain `0x3fffffff`. C5 independently
revalidates each individual job at startup. Index hints avoid rereading an
entire inventory when its ordering has not changed, and batch preflight reads
each inventory row at most once.

Tap the AP name in setup to cycle through selected networks and choose one
observed station for each when needed. C5 retains 64 frame-derived station
observations across AP captures, scoped to BSSID/channel/MAC with a 30-minute
lifetime and oldest-entry eviction. Invalid, evicted or expired choices fail
validation and need another capture. This is observation evidence, not proof
of association. Multiple/all stations per AP remain pending.

The batch waits for C5 and required S3 receiver cleanup before advancing.
Leaving the screen, Stop, failed startup or a radio error prevents subsequent
targets from starting. Reports retain per-target attempted state, completion
count, mode, scope, channel, job, error and independent driver/RX counts.
The configured 10/20/30 seconds applies to each AP. Five GHz jobs retain S3
control/reporting; S3 has no 5 GHz receiver.

Selection, immutable batch rows, inventory and CSV buffers prefer PSRAM with
a checked internal-memory fallback. Larger Wi-Fi reports use a temporary
16 KiB buffer and matching storage/USB-read limit. The pre-existing resident
diagnostic arrays remain 4 KiB. No new resident task was added. S3 static RAM
rose by 16 bytes to 180,864; C5's bounded evidence cache adds 1,280 bytes, for
67,344 static bytes. Idle S3 internal free/minimum: 127,412/126,644 bytes;
PSRAM free: 1,428,324; ten tasks. C5 idle free: 82,332 bytes.

Native checks cover 32-AP completion and large reports, same-SSID selection,
allocation/selection limits, frozen queues, reordered and changed inventories,
all-target preflight, per-AP station choices, cancellation before/after TX,
cleanup delay, stop-on-error, cache eviction/expiry/channel scope and inactive
callbacks. The production storage writer was exercised with real temporary
8/16 KiB files and exact-content checks. Existing radio, S3 receiver, frame,
station, USB, target and field-menu checks pass; picker and AP screens were
rendered and visually inspected.

All five firmware builds passed with unchanged source fingerprints and the
final C5 raw-link gate. Both processors' three image regions were written and
hash-verified after carrier/C5 identity checks; the bridge returned to the main
application. Boot verifies revision 41, SPI 40 MHz online, SD mounted, five idle
job records and `previous_reset_fault=no`. Historical crash data remains.
Evidence is `../../integration/device-test-2026-10-27/multi-ap-*`, with frozen
images/ELFs/build/link receipts in `candidate-multi-ap` and structured checks in
`multi-ap-verification.json`. S3 application: 1,449,728 bytes,
SHA-256 `6ac8a1a05a0df7391ca01c49ca58bc0298c6a0484f7c31eedb3f17aa0b64fc73`;
C5: 1,592,560 bytes,
SHA-256 `47429cbc62cf23b1c5e23fa99548118ac82b9a065309b6da87aef9351bbf14a5`.

Live RF/batch reception and physical touchscreen acceptance remain unverified.
SAE Commit Flood, AP Clone Spam, Evil Portal/AP Clone, station multi-selection
and the rest of the Marauder inventory remain open. The owner's later phase
is the complete preserved Wardriving integration in this same main application.
GPS is currently absent: unlocated scanning/logging must work, while WiGLE
exports require genuine fresh fixes. The source logger's GPS gate must not
simply be removed. Source paths, scope and no-GPS acceptance are in `../TODO.md`.
That full merge remains pending; this checkpoint does not claim completion.

## Revision 40 installed: four additional AP engines, 2026-10-29

Source and native checks now cover **Bad Message**, **Association Sleep**,
**Channel Switch** and **Quiet Time**. These are real C5 packet builders/worker
routes plus S3 controls, independent 2.4 GHz reception and mode-labelled CSV.
This brings the selected-AP menu to six available engines. SAE Commit Flood,
AP Clone Spam and Evil Portal/AP Clone remain disabled. Full Marauder parity,
multi-AP selection and multi-station iteration remain open.

C5 revision 40 adds capability bits 26..29 while retaining the 20-byte job
request and existing on-demand 4,096-byte worker. Bad Message uses a fresh
131/153-byte EAPOL template, random nonce and 64-bit replay counter. Association
Sleep uses the upstream PM association-request layout and a narrow current-
buffer adapter that constructs its admitted packet itself. All other raw calls
retain the pinned vendor validator. The linked ELF gate verifies both admitted
wrappers and the vendor fallback. SDK libraries remain unchanged.

C5 snapshots SSID and raw authentication mode under the scanner lock; station
jobs must reference an address in the completed selected-AP capture. Channel
Switch/Quiet first obtain capability bytes from a matching, structurally valid
beacon using the existing C5 RX callback, with a two-second deadline. A failed
listen/cleanup cannot proceed to TX; uncertain cleanup retains radio ownership.
S3 now admits data reception only for Bad Message and classifies all four modes.
No new resident worker or packet queue is added. All modes retain the bounded
20 frames/second rate, explicit Start, stop-on-exit and control leases.

Important upstream differences remain explicit in `MARAUDER_AP_MENU.md`:
single-AP/single-station scope, bounded rate/duration, fresh beacon timestamps,
non-DFS channel set, and raw-IDF authentication classification. The original
association RSN/rate/class and Quiet IE layouts are preserved; hardware/client
applicability is not established by the builders. Derived layouts retain the
upstream MIT notice in `LICENSES/ESP32Marauder.txt`. Banshee/GhostESP was consulted
only through architecture documentation; no code from it was imported. See the
new reference/design section in `PROCESSOR_ALLOCATION.md`.

Checks passed: `check_ap_frames.py`, `check_radio.py`, `check_station_service.py`,
`check_s3_receiver.py`, `render_ap_job.py`, `render_field_menu.py` and
`render_wifi_target.py`. These cover 0..32-byte SSIDs, canary/buffer bounds,
EAPOL variant reset, replay/nonce fields, IE structure, malformed/truncated RX,
station/AP identity, capability gates, native touch bounds, per-mode reports,
receiver readiness/filtering, no-beacon timeout, cancellation during preparation,
driver failures and retained ownership after cleanup failure. The Channel
Switch screen was visually inspected. All five firmware builds pass with
stable inputs and the actual C5 adapter link gate passes. An earlier build
caught and corrected two GCC misleading-indentation errors; its outputs were
not flashed. A later source review added WPA2/WPA3 transition classification,
then repeated the affected test and the full build matrix.

`candidate-ap-four-final` is installed. The C5 was written from the preceding
`candidate-ap-four` package; all three C5 images are byte-identical in the final
package. The final S3 additionally measures the RX duration from receiver
readiness, so slow Wi-Fi startup cannot consume the preparation/TX window.
A 1.2-second startup-delay regression check passes. The final all-five build
matrix passes with current inputs. Bridge identity, C5 identity and all three
regions per chip were hash-verified; the normal S3 application was restored.
Boot reports firmware 40, capabilities `0x3fffffff`, SPI online, SD mounted,
no previous-reset fault and all five diagnostic job IDs zero. The historical
stored crash record is unchanged. Live RF effects, independent physical
reception and physical touch acceptance remain pending.

S3 app: 1,445,024 bytes, SHA-256
`7148b41a96e6227a3ff001eea1ed13356dde90e4303bb88cdbbe405dcc570bd5`.
C5 app: 1,592,096 bytes, SHA-256
`7087e72c6ea4b97213d9be6ddae6b67f7bce6d7c1f35f38cec25ab4bdce2c0ae`.
S3 internal free remains 127,428 bytes; minimum 126,660, PSRAM free 1,428,324,
ten tasks. C5 idle free is 83,612 bytes, 96 below the preceding installed
snapshot; static RAM is 66,064 bytes (+104). This is idle evidence, not peak
job memory or radio throughput. Evidence uses the `ap-four-` prefix under
`../../integration/device-test-2026-10-27/`, including the startup console,
three flash logs, identity logs and `ap-four-verification.json`.

Next work remains full source-to-feature acceptance mapping, multiple AP and
station selection/scheduling, the three remaining AP engines, other Marauder
feature groups and live RF checks. No benchmark establishing superiority to
Banshee or other devices has been performed.


## Revision 39 installed: AP-directed Probe Requests, 2026-10-29

The Probe Request Flood entry now has a bounded AP-directed implementation,
using the same exclusive C5 worker, 20-byte job request, lease and S3 controls.
C5 validates the current AP snapshot and copies up to 32 SSID bytes while
holding the scanner lock. Each packet has that AP as destination/BSSID, a fresh
locally administered unicast source MAC, SSID IE and band-appropriate rate IE.
The driver handles supported probe requests through its original validator;
the deauthentication adapter remains narrow and unchanged.

S3 presents the selected AP, fixed probe scope, duration and counts, prepares
its 2.4 GHz receiver before TX, cancels both workers on exit and exports a
mode-labelled report. RX matching now distinguishes probe jobs from deauth
jobs and checks probe packet structure. Capability bit 25 and C5 revision 39
gate the new menu route; the SPI payload size is unchanged. No resident C5
worker or result table is added.

This remains a partial port: upstream broadcasts selected SSIDs with three
packets per burst, while this implementation uses AP-directed requests at a
bounded rate. Multi-AP iteration and the broader broadcast variant remain
pending. The other seven AP attack engines still have no backend. Native
worker, builder, snapshot, receiver, capability/menu/touch and CSV checks pass,
including SSID lengths 0..32, source address validity, packet bounds and vendor
fallback, stale SSID rejection and mode/scope-labelled CSV output. The generated
probe screen was visually checked. All five builds pass with stable inputs.
`candidate-ap-probe-final` is installed on both chips: identity-checked bridge,
three hash-verified C5 regions and three hash-verified final S3 regions. Boot
reports revision 39, capabilities `0x03ffffff`, online SPI, mounted SD and all
jobs idle. Live RF and physical touch acceptance are not established.

S3 application: 1,442,816 bytes, SHA-256
`2bf9be4ef64ef21bd6a24facca895eb792c6fe930e7c9d112054fb7b6f035079`.
C5 application: 1,588,896 bytes, SHA-256
`b1fab98c26d6dd142e6e72c94ce7731d997717c83409af39d548f3fe3596f505`.
S3 internal free 127,428 bytes, minimum 126,660, largest block 86,016;
PSRAM free 1,428,324; ten tasks. C5 idle free remains 83,708 bytes, with unchanged
65,960-byte static RAM usage. Relative to the preceding picker/RX build, the
probe mode adds 16 bytes of S3 idle RAM and no C5 resident allocation.

Evidence files under `../../integration/device-test-2026-10-27/` use the
`ap-probe-` prefix: before, bridge-write, bridge-identity, c5-identity,
c5-reset/c5-reset-write, c5-write, s3-write and startup-console. The frozen
package also retains the final raw-driver link verification. The earlier
`candidate-ap-probe` package was superseded before flashing by the final
stale-SSID/report fixes; it was never installed.

Next work: multi-AP selection/scheduling, additional AP engines, and an explicit
passive hardware receiver test followed by bounded probe RF acceptance. The
USB production console remains read-only; use a separate diagnostic image for
autonomous hardware exercises, then restore the normal application.

## Installed network picker and S3 receiver, 2026-10-29

The picker now retains a completed C5 snapshot across return navigation instead
of automatically restarting its sweep. Explicit **Again** still rescans. Each
card shows its full SSID, BSSID/channel, security/band and selected marker;
same-name APs remain distinct. Attack selection identifies its purpose and has
a direct return to the retained target's attacks. One AP per job remains the
current limit; multi-AP selection/scheduling is not implemented.
Job startup now checks the selected row first: an unchanged inventory needs
one AP-record transfer, with a bounded search only if that index moved. The
fresh C5 generation, BSSID and channel are still validated on both processors.
Cancellation also handles a valid response showing that the C5 no longer owns
the requested job, avoiding a stuck Stop state after a dropped start or reset.

For 2.4 GHz deauthentication, the S3 now starts an independent passive receiver
before dispatching C5 TX. It counts observed management frames, target beacons
and matching unprotected reason-7 frames. RX readiness gates TX; a failed RX
stops the C5 job. Stop, leaving, sleep and worker leases cancel both sides.
5 GHz remains C5 RF with S3 control/reporting because S3 has no 5 GHz receiver.
Screen and CSV separate C5 driver acceptance from S3 heard counts; neither
counter by itself proves a client's disconnection or physical transmitter.

`s3_wifi_owner.c` arbitrates the web AP and receiver, retains shared netif/event
facilities, and retains the owner on cleanup uncertainty. The receiver uses one
on-demand 4,096-byte worker and bounded Wi-Fi buffers, rejects active BLE tests,
and requires 64 KiB free internal memory before startup. No resident RX worker
or packet table is added. USB `radio` gains read-only S3_RX telemetry.

Host checks passed: `check_s3_receiver.py`, `check_radio.py`, `render_ap_job.py`,
`render_network_picker.py`, `render_wifi_target.py`, `check_usb.py`. These cover
driver initialization failures, owner exclusion, frame filtering, duration,
leases, cancellation, RX readiness/failure, reports, stable target routing and
native 320x480 geometry. Generated AP and picker images were visually checked.
All five firmware targets passed with unchanged build inputs. The S3-only
`candidate-picker-s3-rx` package is installed; all three flashed regions verified
their hashes. The C5 binary matches the installed revision-38 `candidate-ap-deauth`
image byte for byte and was not rewritten. Startup confirms healthy reset,
online C5, mounted SD and idle S3_RX, S3 BLE and C5 jobs. The historical stored
crash record remains; `previous_reset_fault=no` describes the latest boot.

S3 application: 1,442,048 bytes, SHA-256
`1ae87974cae0e6ac2f8a0ef08a65a7185540f1766d07a2ceb52c744939f5586e`.
S3 internal free 127,444 bytes, minimum 126,676, largest block 86,016;
PSRAM free 1,428,324; ten tasks. Idle RAM cost versus the preceding installed
candidate is 288 bytes. C5 free remains 83,708 bytes. Evidence:
`picker-rx-before.txt`, `picker-rx-s3-write.log`, `picker-rx-startup-console.txt`
under `../../integration/device-test-2026-10-27/`.

Physical touch, peak live RX heap and over-air reception remain unverified.
The other eight AP attack engines still need implementation, not label changes.
Next implementation work is AP-directed probe requests and multi-AP scheduling;
passive hardware receiver acceptance also remains necessary before claiming
verified S3/C5 RF cooperation.

The owner explicitly requested autonomous work, recoverable flashing as needed,
and full Marauder feature parity while asleep. The named owner test SSID is
recorded in the conversation. No BSSID, password, station identity or RF test
result is inferred from that name. Routine implementation and builds can proceed
without further guidance; no disruptive RF acceptance was performed here.

## Reference and scope

Pinned upstream commit: `511bb3694d0c4dc064888e762bde44fcc9b7f48a`.
The local source is `../../integration/marauder-source`.
`tools/inventory_marauder.py` verifies the commit and clean reference files,
then extracts 95 mode symbols, 265 literal menu entries and 58 top-level CLI
commands into `MARAUDER_SOURCE_INVENTORY.json`. These overlap; they are not a
count of independent features. Dynamic menus, settings and conditional board
features still require manual mapping. Numeric value 85 is shared by upstream
`BT_SCAN_IBEACON` and `BT_FINDMY_SOUND`; never copy those IDs into the SPI protocol.

All source entries are pending unless documented as implemented or partial in
this worklog or `FEATURE_PARITY.md`. Existing Cerberus capabilities must be
compared against upstream semantics, not counted by matching their names.
GPS, external-radio, storage and companion-device dependencies stay explicit.

## Installed AP menu correction

The S3-only `candidate-ap-menu-source` package is installed and its application
hash verified. Bootloader/partition images match `candidate-target-flow`; only
the application region was written. C5 remains revision 33 at this checkpoint.
Both chips boot, the 32 GB card mounts, and both test jobs remain idle. Native
screen/tap tests pass, including selection return and identity preservation;
physical touchscreen acceptance remains pending.

Evidence in `../../integration/device-test-2026-10-27/`:

- `candidate-ap-menu-source/`: frozen binaries, ELF, manifest and stable build receipt.
- `ap-menu-s3-write.log`: esptool application hash verification.
- `ap-menu-startup-console.txt`: matched S3 identity, healthy boot, C5/SD and memory telemetry.

The AP menu lists eight AP-dependent modes plus Evil Portal's AP-clone variant,
without standalone beacon-spam entries. All nine remain explicitly unavailable:
this change fixes the selector and navigation, not their transmit engines.

S3 startup memory from the new read-only `memory` command: internal free
128,756 bytes, minimum 127,988, largest block 86,016; PSRAM free 1,428,324,
largest block 1,409,024. C5 idle free/minimum 73/69 KiB. Version/build strings
are inherited; use manifest hashes to identify candidates.

## C5 worker consolidation: installed and memory measured

The three exclusive 802.15.4 modes now use one resident 4,096-byte worker.
Estimated saving: 8,192 stack bytes plus two task control blocks. All modes
retain their original radio operations, channel sweeps and result formats.
Stale notifications cannot rerun a completed sweep. Initialization reuses an
existing worker and frees allocation state on failed setup so retry is possible.
C5 firmware revision is 34; the wire protocol and capabilities are unchanged.

`tools/check_radio_worker.py` compiles the real C5 implementation with fake time
and drivers. It passes mode switching, 16-channel results, overlapping starts,
stale wakeups, timer wraparound, setup/driver failure, cleanup and retry checks.
The existing C5 Radio Lab and S3 BLE worker checks also pass.

All five build targets passed with stable source inputs. The frozen
`candidate-shared-worker` pair is installed: C5 application hash verified via
the identity-checked bridge, then final S3 images hash verified after automatic
USB return. The C5 bootloader and partition hashes matched the prior installed
pair, so its update wrote only the application region. Settings and SD remain.

`worker-startup-console.txt` reports revision 34, healthy startup, no new fault
reset, mounted SD and idle radio jobs. The C5 diagnostic snapshot increased
from 75,612 to 84,492 free bytes: **8,880 bytes reclaimed** at idle. Rounded
free/minimum telemetry rose from 73/69 KiB to 82/78 KiB. This confirms memory
savings; physical 802.15.4 reception remains untested. Saved evidence:
`worker-bridge-identity.txt`, `worker-c5-identity.log`, `worker-c5-write.log`,
`worker-s3-write.log`, `worker-startup-console.txt`.

## Additional service discovery: installed; live scan acceptance pending

SMTP (TCP 25), DNS (TCP 53), HTTP (TCP 80), HTTPS (TCP 443) and RDP (TCP 3389)
now use the existing bounded C5 socket worker. This matches upstream service
port discovery in `WiFiScan.cpp::RunPortScan` / `pingScan`; it does not verify
application protocols, perform DNS queries, establish TLS or log into services.
SSH banner recognition remains the existing additional check.

Revision 35 adds capability bit 21; older C5 firmware keeps only the original
four scan modes visible and rejects attempts to start an unsupported mode.
No SPI payload sizes or existing mode numbers change. The UI labels subnet
scope, ignores irrelevant host/range entry for LAN modes, and exports subnet,
netmask and actual requested service port. Each mode excludes self, network
and broadcast addresses and is bounded to /24 through /31. The existing lease,
cancellation, result cap and one-socket memory budget remain in force.

The planner tests cover every service port and subnet endpoint; native UI tests
cover all nine modes, stale input, old-C5 capability gating, scope export and
existing connection/cancellation behaviour. Device execution is not yet tested.

All five targets passed with stable inputs. `candidate-service-scans` is now
installed on both processors. The C5 application and all final S3 images passed
esptool hash verification, and the temporary bridge returned automatically.
`services-startup-console.txt` confirms the same chip, C5 revision 35,
capabilities `0x003fffff`, SD mounted, no new fault reset, and idle jobs. C5
snapshot free memory remains 84,492 bytes (82 KiB rounded); the service modes
add no resident worker. S3 app/static RAM: 1,425,136/179,520 bytes. C5
app/static RAM: 1,578,704/65,176 bytes. These are installed binary sizes and
build RAM estimates, distinct from measured runtime free heap.

The frozen candidate, `services-bridge-identity.txt`, `services-c5-identity.log`,
`services-c5-write.log`, `services-s3-write.log` and the startup console log are
in `../../integration/device-test-2026-10-27/`. No service scan was automatically
started. The previous revision-34 pair remains a recoverable checkpoint.

## Ping and ARP discovery: installed; live scan acceptance pending

Revision 36 adds two connected-subnet modes using the same on-demand C5 network
worker. Capability bit 22 gates both; older firmware still exposes its own
supported modes. Existing mode numbers and SPI payload sizes are unchanged.

- Ping sends one small ICMP echo per peer. A result requires the expected source,
  destination, type/code, identifier, sequence, per-job random nonce and valid
  IPv4/ICMP checksums. Truncated, fragmented and unrelated replies are rejected.
  The receive deadline is fixed even when other traffic arrives. One nonblocking
  socket is closed on every exit; no additional ping task is created.
- ARP sends a normal request on the connected STA interface. Cache lookup and
  request execute on the lwIP core via synchronous callbacks, with interface
  address and link checks. It never clears shared cache entries. A preexisting
  entry is explicitly labelled `CACHE`; otherwise the result says resolved.
  Neither label claims an authenticated host or guaranteed fresh reply.
- Both reuse /24 through /31 scope, self/network/broadcast exclusions, 64-result
  retention, cancellation and the five-second control lease. Connection epoch
  is checked before probing and before committing a result. Normal polling is
  bounded to 100 ms for sockets and 20 ms for ARP; synchronous lwIP core work
  must finish before the stack callback context can be released.
- UI and SD CSV distinguish TCP, ICMP and ARP results. Ping exports RTT; ARP
  exports MAC, resolution time and cache status. Old firmware capability checks
  cover both selecting a mode and starting it.

`check_network.py`, `check_network_worker.py` and `render_network.py` pass. The
worker check compiles the actual C5 implementation and exercises stale replies,
core-context access, cache hit/miss, memory/driver failures, cancel, lease expiry,
link epoch changes, timer wrap, task cleanup and TCP regression. The AP selector
and target screen checks also pass. `artifacts/network-preview/arp-preview.png`
was inspected. These are deterministic fakes, not live network acceptance.

All five build targets passed with stable inputs. Frozen candidate:
`candidate-host-discovery`. S3 app/static RAM: 1,425,872/179,520 bytes;
C5 app/static RAM: 1,582,480/65,176 bytes. No resident RAM increase is reported
by the build. Live scan and physical touch acceptance remain pending.

Both processors are now running this candidate. The C5 application and all
three final S3 image regions passed esptool hash verification. The identity-
checked bridge returned automatically to the same S3. `hosts-startup-console.txt`
confirms C5 revision 36, capabilities `0x007fffff`, mounted SD, no new fault reset
and idle network/radio jobs. C5 snapshot free heap remains 84,492 bytes; S3
internal/PSRAM free memory remains 128,756/1,428,324 bytes. The existing stored
crash record is historical and was not erased. Logs beginning `hosts-` and
the frozen candidate are in `../../integration/device-test-2026-10-27/`.
No live discovery probe was started. The revision-35 pair is retained for recovery.

## Selected-AP station discovery: installed; live reception pending

The installed pair is revision 37, capability bit 23 and SPI commands 67–70. The
selected AP's tools now include **Stations / Passive Capture**. Starting resolves
the BSSID/channel against a fresh completed C5 scan, and the C5 independently
checks generation, index, BSSID and channel. It requires the C5 STA to be
disconnected; no join, probing or attack transmission is part of this capture.

The existing Wi-Fi scan worker and promiscuous callback are reused. A bounded
32-entry table retains directed infrastructure data/management observations.
WDS, IBSS, control, group addresses and frames from another AP/channel are
excluded. Uplink/downlink counters and flags distinguish a station heard
transmitting from an address merely seen in AP traffic. Downlink RSSI is not
reported as a station-origin signal. EAPOL EtherType and SAE authentication
algorithm flags are metadata only: payload/handshake capture is still pending.

Capture runs for 15, 30 or 60 seconds, stops on request or five-second lease
expiry, and publishes a consistent final table. Counts saturate; frames from
new identities beyond the 32-entry capacity increment an explicit overflow
counter. Cleanup restores automatic band selection. A cleanup failure retains
radio ownership until restart instead of reporting that another job may start.
The status command renews only the explicitly named matching job; diagnostic
reads with ID zero do not renew it. Other C5 radio operations are excluded.

The S3 provides station selection tied to the AP, paging, address masking and
SD CSV with direction and signal provenance. Selecting a station does not
transmit or enable an unported attack. Tables and selection are session state;
multi-AP profiles, durable selection and the combined AP/station overview remain
pending. This is a targeted station-discovery implementation, not full capture
or attack parity.

`check_wifi_stations.py`, `check_station_service.py`, `render_stations.py`,
`render_wifi_target.py` and the existing radio/network worker checks pass.
They exercise parsing, ownership, stale identities, loss of START acknowledgement,
lease/cancel, driver failures, final-table transfer, selection and CSV export.
The station and selected-AP screens were rendered and visually inspected.
Physical reception and touch acceptance remain pending.

All five targets passed with stable inputs. `candidate-stations` is installed
on both processors. A full rebuild regenerated both bootloader binaries; SDK
configuration inputs and both partition tables were unchanged. This update
wrote and hash-verified bootloader, partition table and application on each
chip, preserving NVS/SD. The bridge identity and C5 base MAC matched before
writing, and the carrier returned automatically. Evidence files are prefixed
`stations-` in `../../integration/device-test-2026-10-27/`.

`stations-startup-console.txt` confirms firmware 37, capabilities `0x00ffffff`,
no new fault reset, SD mounted and all jobs idle. The new read-only `stations`
command successfully returns the C5 status through SPI without renewing a
lease. No live capture was started. The stored earlier crash record remains.
S3 app/static RAM: 1,432,160/180,384 bytes; C5 app/static RAM:
1,585,952/65,936 bytes. Measured S3 internal free: 127,892 bytes; PSRAM free:
1,428,324. Measured C5 snapshot free: 83,740 bytes, 752 below revision 36 and
still 8,128 above the pre-consolidation 75,612-byte snapshot. S3 task count stays
10; the station service shares the preexisting C5 worker.

### Ping/ARP transport correction found during integration

Revision 36's S3 transport validator still required a nonzero TCP port and only
TCP flags. Its C5 discovery worker and UI mocks passed, but real ICMP/ARP results
would be rejected at SPI decode. The new source replaces that check with a
protocol-specific shared validator. Tests now pass real C5 worker result records
through that same validator, and reject contradictory flags, malformed ARP MAC
payloads and mismatched job/index values. The correction is included in the
installed revision-37 pair. Live ping/ARP scans remain unverified.

## Next implementation stages

1. Port AP-dependent engines against verified driver/toolchain support. Include
   start/stop/error lifecycle and target context; retain unavailable state until
   the backend exists. Extend to multi-AP and per-station scopes explicitly.
2. Extend station discovery with bounded frame capture and target profiles.
   Add probe/deauth/EAPOL/SAE/raw captures, packet views and PCAP output. Keep
   bounded radio work on C5 and bulk storage/interpretation on S3. Obtain live
   passive reception evidence using a separate temporary test image if useful;
   preserve the production USB console's read-only scope.
3. Extend the shared S3 BLE service with scan/advertising capabilities and account
   for host memory/coexistence; port detector, tracking and BLE feature parity.
4. Port general workflows, resource/profile persistence, Recon/wardrive, update
   and CLI compatibility after their core services. Preserve working Cerberus
   recovery, SD ownership and existing tools throughout.

Full parity remains open. Hardware-independent implementation can progress while
RF acceptance and physical touch testing are pending; do not mislabel either
as completed, or treat missing optional hardware as an implemented feature.

## Raw-transmit feasibility evidence for the next port

The current C5 IDF 6.0.1 `libnet80211.a` exports both
`esp_wifi_80211_tx` and `ieee80211_raw_frame_sanity_check` as strong globals
from `ieee80211_output.o`, confirmed using the installed RISC-V `nm`.
Upstream's `WiFiScan.cpp` defines the latter symbol, and its
`.github/workflows/build_installer_manifests.yml` applies `-Wl,-zmuldefs`
for Arduino 3.3.4. Its parallel workflow dispatches separate private C5 builds.
This is a feasibility lead, not proof that the public linker setting works
with our IDF 6.0.1 binary. No override, global toolchain patch or active AP
transmission has been installed by this checkpoint. Keep any experiment
project-local and verify the linked implementation and receiver behaviour.

## Revision 38: first AP-dependent transmit job, installed 2026-10-29

The source now implements bounded deauthentication for one selected AP with
broadcast or one observed-station scope. It reuses the C5 Radio Lab TX worker,
mutex, three-second control lease and status transport (mode 3); AP START is
command 71 and capability bit 24. No additional resident worker or radio-state
bit is consumed. Request records bind job, current scan generation/index,
BSSID, channel, duration and optional station. C5 independently checks that
identity against its scan/station tables. Duplicate starts do not restart a
finished job. The UI exposes 10/20/30 seconds at 20 frames/second total, with
both directions alternating for a single station. Broadcast sends only the
AP-to-broadcast direction. BSSID remains the AP in either station direction.

The S3 setup screen provides station discovery/selection, explicit Start/Stop,
status and CSV. Hidden/asleep controls request Stop without renewing the lease.
Lost START acknowledgement is resolved by status; local UI assumptions never
count as an acknowledged stop. The running target is fixed even if another AP
is later selected. Reports distinguish driver acceptance from over-air success.
Other AP attack entries remain unavailable. Multi-AP, all-observed-station
iteration, manual addresses and continuous deauth are still pending.

`prepare_c5_raw_library.py` verifies the SHA-256 of the installed IDF 6.0.1 C5
`libnet80211.a`, extracts its TX object to a temporary directory, weakens only
`ieee80211_raw_frame_sanity_check`, and adds an alias at the original function.
The resulting archive is local to the C5 build. `raw_wifi_tx.c` preserves the
four-argument ABI and delegates ordinary calls to the vendor function; only
the exclusive worker's current, validated 26-byte deauth buffer takes the
adapter path. No global toolchain mutation or `-zmuldefs` flag is applied.
This is new Cerberus code; upstream's public override was a feasibility
reference, not copied into the firmware. Final ELF verification remains required.

Native worker and UI tests pass for packet directions/bounds, stale identities,
station/AP mismatch, busy rejection, duplicates, stop, lease expiry, allocation
failure, lost acknowledgements and report data. The existing transmit worker
now retains radio ownership on restoration failure until reboot. The S3 BLE,
station-service, station-screen, target and field-menu regressions pass. The
new screen was rendered and visually checked. These tests use fake drivers;
no deauthentication transmission or receiver effect has been measured yet.

The first matrix build passed four targets but failed C5 linking: PlatformIO
classified the generated archive below its build directory as an internal
component and omitted it. The archive now sits in a sibling build directory,
first in library search, preserving its original basename for IDF linker
fragments. No installed SDK files changed. `check_c5_raw_link.py` is now part
of the onboard build check. It verifies distinct strong adapter/vendor symbols,
the actual `esp_wifi_80211_tx -> adapter -> vendor fallback` call path, and the
current-buffer guard in the ELF. It records the ELF and original-archive hashes.

All five targets then passed with stable source inputs. The frozen package is
`../../integration/device-test-2026-10-27/candidate-ap-deauth`, including final
ELFs and adapter-disassembly evidence. Both chips' bootloader, partition table
and application were written and hash-verified after carrier/C5 identity checks.
The native USB carrier returned automatically. NVS and SD were not erased.

`ap-job-startup-console.txt` verifies C5 revision 38, capabilities `0x01ffffff`,
SPI online, all jobs idle, SD mounted and `previous_reset_fault=no`. The older
stored crash record remains. S3 app/static RAM: 1,436,784/180,544 bytes; C5:
1,588,112/65,960 bytes. Idle C5 free memory is 83,708 bytes (32 below revision 37,
8,096 above the pre-consolidation snapshot). S3 internal free: 127,732 bytes;
PSRAM free: 1,428,324. S3 remains at ten tasks. No new resident TX task was added.

This is a built and installed first implementation, not full attack parity or
RF acceptance. Broadcast and one-observed-station scopes have deterministic
tests; live receiver evidence and physical touch testing remain open. The next
radio optimization should introduce shared S3 Wi-Fi ownership (currently owned
by `web_ui.c`) before using S3 as an independent 2.4 GHz receiver alongside C5.
The owner's 2026-10-29 requirement to use both processors in every function is
recorded with per-workflow roles and pending dual-radio work in
`PROCESSOR_ALLOCATION.md`. Do not equate UI participation with proven simultaneous
RF operation. Full Marauder parity remains active.

## 2026-10-30: rev55 receive-side selection and memory checkpoint

Installed `candidate-ble-sort-psram-sd-cache` after all five target builds
passed with stable inputs, the matched package passed, and three S3 writes
were hash-verified. The C5 49 bootloader/partitions/application are identical
to the prior verified install and were retained. NVS and PHY were preserved.
The frozen package and [receipt](../../integration/device-test-2026-10-27/rev55-flash-receipt.json)
retain exact hashes and validation boundaries. Only coverage metadata was
updated after packaging; firmware inputs remained unchanged.

BLE lists now offer Signal, Name A-Z and First Seen without moving raw
records or losing observation time/address type. The owner confirms sorting
and selected details. The 5,664-byte UI snapshot/staging allocation moves to
PSRAM with validated publication and failure retention; net internal static
usage falls 5,584 bytes. Idle free heap is 109,512 bytes, compared with the
rev54 reading of 104,056. Capture, ordering, live-route and renderer host
checks pass, with an independent UI review. Masked unnamed-row formatting
was reviewed in code rather than exercised by the new fixture renderer.

The eight-entry OUI cache now retains definitive unknown prefixes, using
header checks plus mounted-session epochs to invalidate across card/USB
changes. Fault injection and all 40,187 public-OUI records pass. Host repeated
unknown lookup reads drop from 19 to one. The device verifies the full SD
database hash and a Cisco vendor lookup; live latency remains unmeasured.
Three Watch read helpers now use the instrumented link mutex wrappers;
production framing, failure/publication and timing checks pass.

The owner confirms no-GPS Wardriving Start/Stop. Console records STOPPED at
10,575 ms, 2 C5 Wi-Fi sweeps, 1 S3 BLE sweep, 60 unlocated rows, one saved
file, no service/status errors, and a released BLE host. Post-Stop free
internal heap is 50,716 bytes, minimum 43,256. SPI initial timeouts/fallbacks
remain at the four-event startup baseline, with zero mutex/fall/high/driver
errors. Longer/repeated rev55 sessions, saved CSV content and physical
GPS/six-C5 backboard remain unverified.

Pinned upstream MultiSSID receive criteria have a strict parser and bounded
32-row host-tested foundation, but no capture/hopping/UI integration; it
remains pending. Dormant SIMPLE/SIMPLE_TWO and Espressif symbols are audited
without inventing duplicate working menu entries. Full parity is incomplete.
The owner reports NO MEM across Wi-Fi actions and Association Sleep/Bad
Message still unresponsive on rev55. These remain unresolved; general
memory improvements do not establish an attack repair.

## 2026-10-30: rev56 passive MultiSSID integration

Installed the paired `candidate-passive-multissid-hop` package with onboard
C5 firmware 50. All five targets passed with stable inputs, the final S3
READY ISR gate and matched-package checks passed, and nine bridge/C5/S3
writes were hash-verified against the identified Cerberus chips. NVS and
PHY were preserved. The frozen package and
[receipt](../../integration/device-test-2026-10-27/rev56-flash-receipt.json)
retain image hashes, memory comparison and device evidence.

C5 now receives beacon-only captures while hopping through supported
2.4/5 GHz channels, honoring SDK/country rejection. S3 analyzes complete
CRC-verified frames in a bounded PSRAM table, keeps exact SSID evidence,
and offers immutable details, optional PCAP and separate fixed-channel
selected-transmitter Follow. Capture frame/block buffers move from 8,240
internal static bytes into session-only PSRAM; analysis-only operation
needs no SD. Net S3 internal static use falls 7,904 bytes. Settled idle free
heap is 117,328 bytes, minimum 116,516, a 7,816-byte increase over rev55.
C5 idle free heap is 77,140 bytes with 32 additional static bytes.

Production host checks cover parsing, bounded accumulation, C5 filter,
radio/callback lifecycle, SPI framing, S3 capture worker, analysis ownership
and UI routes/rendering. Forty capture-worker fault cases pass. Review
resolved local generation reuse after failed starts and a stale Stop race.
Cleanup retains ownership until radio callbacks drain and SD close finishes;
observer delivery occurs once across ACK retries. The C5 admission audit
applies to its current single-core Wi-Fi/control task priorities, not to
future multicore or deferred-callback designs.

The owner reports increasing MultiSSID beacons, successful Stop and a
following normal Wi-Fi scan. Read-only same-boot console evidence confirms
two successful normal scans with zero failures, idle radio and no reset
fault, but retained capture generation/received are zero. It does not
corroborate MultiSSID reception or capture-to-scan handoff. A repeat with
the screen left open was requested. At 4,211 SPI exchanges, mutex/fall/high/
driver errors remain zero; initial READY timeouts/fallbacks remain four
each, unchanged from startup. Controlled three-variant detection, both-band
reception, live Follow, saved PCAP contents and repeated/long operation
remain unverified.

Navigation review found that Scan -> Wi-Fi opens the AP list, and the
generic hub is behind TOOLS. A retained AP selection changes that action
to TARGET, making generic tools difficult to reach. This remains in the
frozen rev56 image and is queued for a small navigation correction with a
test covering the complete entry path. The user-facing test instruction
omitted the intermediate TOOLS step; no cause for the missing capture
counters is asserted yet.

The coverage ledger advances MultiSSID from pending to partial, retaining
all remaining validation gaps. The inventory has 70 partial, 11 pending,
10 needs-review and four hardware-limited mode symbols; these are not a
completion percentage. Earlier BLE sorting, SD resources and no-GPS
Wardriving acceptance remain prior-version evidence. The reported NO MEM,
Association Sleep and Bad Message failures remain unresolved; receive-side
memory and navigation improvements do not establish attack repairs.

## 2026-10-30: rev57 always-reachable Wi-Fi tools

Installed S3 `candidate-wifi-tools-navigation` with the exact C5 50 images
retained from the verified paired rev56 flash. The two-line production
change keeps the normal Wi-Fi AP-list footer labeled TOOLS and routes it
to the generic hub regardless of retained AP selection. The selected AP
snapshot, index and valid flag remain intact, and selecting its row still
opens target details. Explicit target-selection navigation is unchanged.
The full entry path is Scan -> Wi-Fi -> TOOLS -> MULTISSID -> START.

The new navigation regression executes production launch defaults, the
live handler, real footer/hub hit-tests and Wi-Fi dispatch guards through
the actual MultiSSID Start handler. Selected and unselected cases pass,
as do row-to-details and existing selection-mode routing. The existing
MultiSSID UI suite and BLE live-route checks pass. Independent review
found no blockers. All five firmware builds pass with stable inputs,
including the final S3 ISR gate; the matched package passes and three
S3 writes are hash-verified. All C5 and bridge images are byte-identical
to rev56, so those chips were not reflashed.

The S3 application is 1,637,440 bytes, 32 bytes smaller than rev56;
internal static allocation is unchanged. Boot reports no reset fault,
SD mounted and C5 50 online at 40 MHz. Settled idle S3 free heap is
117,328 bytes, minimum 116,648; C5 free heap is 77,140 bytes. The
[rev57 receipt](../../integration/device-test-2026-10-27/rev57-flash-receipt.json)
records hashes and counters. NVS, PHY and SD data were preserved.

Rev57 touch acceptance is pending. The rev56 owner report remains separate
from the zero retained capture counters; it does not establish live
MultiSSID reception or handoff. Controlled three-variant groups, both-band
reception, live Follow, PCAP contents and repeated/long operation remain
unverified. The capture engines are unchanged. The Wi-Fi action failures
remain unresolved and no attack repair is claimed.

## 2026-10-30: rev58 passive PineScan and shared beacon sessions

Installed S3 `candidate-pinescan-shared-beacon`, retaining the byte-identical
C5 50 images from the verified paired rev56 installation. All five targets
passed with stable inputs, including the final S3 READY ISR gate. The matched
package passed, and three S3 writes were hash-verified after carrier identity
validation. NVS, PHY and SD data were preserved. Exact images and validation
boundaries are recorded in the
[rev58 receipt](../../integration/device-test-2026-10-27/rev58-flash-receipt.json).

PineScan is now a direct entry at Scan -> Wi-Fi -> TOOLS -> PINESCAN. The
original Cerberus detector covers the 13 exact addr2 prefix/privacy-bit rules
and minimal-beacon layout predicate from current Marauder develop
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`, rechecked through GitHub on this
date. Full beacon structure is validated before classification. Malformed
tails and unsafe upstream reads are not reproduced. Prefix and layout
matches remain explicitly weak hints, not device identification.

The S3 retains qualifying evidence separately from the newest observation,
with immutable MATCH/LATEST details, exact escaped SSID bytes, address
privacy, source time and separately labeled delivery ages. Selected Follow
uses the frozen transmitter and latest RX channel; its later ordinary
beacons do not become fresh PineScan classifications. Optional transactional
PCAP uses the existing C5 receive/S3 capture path; analysis-only needs no SD.

MultiSSID and PineScan share one private session manager, mutex, capture
observer and lazy PSRAM union bank. Successful mode switches replace retained
results only after capture Start succeeds; failed starts preserve the old
bank. Stale other-mode status/pages/Stop/release/Follow cannot adopt or free
the new owner. The original MultiSSID API remains available as a thin wrapper.
No extra resident radio task or capture payload buffer was added.

Production classifier fixtures, expanded shared-session ownership/fault
checks, 40 capture-worker cases, both full navigation/UI/render suites and
independent classifier/lifecycle/UI reviews pass. Native four-button menu,
qualifying/latest binary-SSID and Follow previews were inspected. The new
workflow works with or without a selected AP in the production route tests.

S3 app size is 1,649,344 bytes, 11,904 above rev57; internal static use grows
304 bytes. Frozen-target DWARF reports a shared 6,648-byte evidence bank,
eight bytes above rev57, a lazy 1,584-byte PineScan UI store and a 160-byte
PineScan record. A transactional Start can temporarily retain the old and
pending banks. Boot has ten S3 tasks, 117,024 internal bytes free, minimum
116,344, and 1,428,324 PSRAM bytes free. C5 idle free remains 77,140 bytes.
SD mounts, C5 is online at 40 MHz, and no reset fault occurs. At 157 startup
SPI exchanges, mutex/fall/high/driver errors are zero; initial READY timeouts
and fallbacks are four each. These are allocation/boot measurements, not
live capture throughput.

PineScan advances from pending to partial: 71 partial, ten pending, ten
needs-review and four hardware-limited mode symbols. Counts are not a
completion percentage. Live reception/positive cases, Follow, both-band
coverage, repeated mode/Stop transitions and saved PCAP contents remain
unverified. A device test is pending. Prior MultiSSID owner reception reports
remain separate from the zero retained capture counters. Wi-Fi action
failures remain unresolved; this work does not establish an attack repair.

## 2026-10-30: rev59 Find My discovery evidence installed

Installed the frozen `candidate-ble-findmy-provenance` S3 package on verified
Cerberus COM76 / `14:63:93:36:78:f4`; all three write hashes passed. Every C5
image is byte-identical to the retained paired rev56/C5 50 installation. The
five-target stable-input build, matched three-target package and final S3
READY interrupt gate passed. NVS, PHY and SD were preserved. Exact images and
limits are in the [rev59 receipt](../../integration/device-test-2026-10-27/rev59-flash-receipt.json).

Find My discovery now independently recognizes exact Apple manufacturer
frames, FMNA UUID16/32/Bluetooth-base128 lists, DULT UUID128 and mixed hint
sets. Source metadata binds matched responses to the current original's exact
identity/type/sweep/sequence/SID and two-second window. New originals clear
prior response evidence even when malformed/incomplete and the raw API keeps
an older packet. Source completeness is distinct from the bounded raw copy's
truncation; no raw response buffer is added. Scan responses do not refresh
original packet time, RSSI or raw data.

The list, detail and Monitor separate ADV/RSP hints and show original source
ages. Same-millisecond source ordering prevents older data replacing newer
monitor evidence. The existing list/Monitor active discovery requests normal
scan responses; Watch/Wardriving/Fox Hunt/Analyzer retain passive sweeps.
Sound eligibility, GATT discovery and Beep All logic remain unchanged. Added
negative fixtures show wider passive hints cannot bypass the original raw
action gate; the core/gate/central source hashes match rev58.

The parser's 737 fixtures, production snapshot/provenance, legacy/extended
scanner events, monitor/cache, UI lifecycle/renderer, scan-store publication
and BLE live-route checks pass. Independent review caught and resolved
impossible event-type admission and same-millisecond stale replacement.
Native Monitor previews and the changed detail block fit; the list badge is
bounded to the existing 33-character display capacity. A read-only console
summary reports complete-sweep ADV/response/mixed hints, UUID widths and raw
truncation, discarding a result if its generation changes during retrieval.

S3 application size is 1,652,384 bytes (+3,040 over rev58); internal static
allocation increases 64 bytes. Exact frozen-target DWARF shows 32 bytes per
provenance record, a 7,408-byte scanner snapshot (+1,028), a 6,152-byte monitor
(+1,024) and a 192-byte frozen row (+32), with the banks in PSRAM. Boot has ten
S3 tasks, 116,960 internal bytes free, minimum 116,280, and 1,428,324 PSRAM
bytes free. C5 free remains 77,140 bytes, SD mounts and the C5 link is 40 MHz.
No new reset fault is recorded; the previously stored crash record remains.
At 75 startup SPI exchanges, mutex/fall/high/driver errors are zero and the
initial READY timeout/fallback counters are four each. These are boot and
allocation measurements, not throughput results.

Current-build physical new-format/response-only discovery, repeated touch and
Stop/Back transitions, and simultaneous Wi-Fi/BLE acceptance remain pending.
The owner was asked to check the new source labels and Monitor lifecycle.
Coverage remains 71 partial, ten pending, ten needs-review and four
hardware-limited mode symbols, not a completion percentage. Prior no-GPS
Wardriving and BLE acceptance remains historical evidence. Wi-Fi NO MEM,
Association Sleep and Bad Message reports remain unresolved. GPS/six-C5
hardware is absent; no attack repair or full Marauder parity is claimed.

Next receive-only BLE gap identified by read-only audit: upstream Flipper
sniffing accepts service patterns 3081/3082/3083 without a matching name;
Cerberus currently requires complete-name corroboration in the same packet.
The proposed next patch is exact service-list hints with separate provenance
and immutable selection through the existing filter/details/Fox Hunt flow,
without treating advertised variant codes as verified physical identity.

## 2026-10-30: rev59 owner acceptance; rev60 Flipper discovery installed

The owner confirms rev59 Find My hint display and Monitor STOP/Back on their
own tag. `rev59-findmy-accepted-console.txt` corroborates BLE generation 3,
idle scanner, released host and no service error or new reset fault after
exit. Stop clears row evidence, so specific packet bytes, response-only
formats and the number of completed sweeps are not inferred from that log.
Post-use memory is 58,600 internal bytes free, minimum 58,520, largest block
31,744, with 1,410,264 PSRAM bytes free and 12 tasks. The initialized BLE host
is unowned. This is a post-use baseline rather than a leak diagnosis or a
repair claim for other modes. Both observations are retained in the rev59
receipt and the subsequent checkpoint's history.

Installed `candidate-ble-flipper-discovery` on the verified S3 COM76 device.
All five builds had stable inputs; matched package, final READY interrupt
gate and three S3 write hashes passed. All C5 images match the retained C5 50
installation. NVS, PHY and SD were preserved; the rev59 package is retained
for rollback. Details and validation limits are recorded in the
[rev60 receipt](../../integration/device-test-2026-10-27/rev60-flash-receipt.json).

The original Flipper parser uses exact service fields: unnamed/custom-name
3081/3082/3083 UUID lists qualify, while base 3080 and service-data compatibility
retain the same-report complete-name requirement. Official Flipper dev
`66613d2b46e4aa533aeaf8181d2a1e6566616a4e` confirms the advertised 16-bit codes;
standard 32-bit/base128 equivalents are explicitly supported, while its distinct
custom serial GATT UUID and arbitrary substring matches are excluded.
Optional name/data markers never combine evidence from different reports.

Flipper and Find My now share one generic source/provenance record. Both hint
families clear on new originals, including rejected/incomplete originals,
and use the same exact identity/type/sweep/sequence/SID/two-second response
binding. Original raw data/time and all active gates remain unchanged.
The focused list and detail show separate ADV/RSP code hints and ages;
sorted/frozen selection reaches the existing Fox Hunt route without a new
menu or radio worker. Read-only console summaries include both families.

The parser's 820 fixtures, shared snapshot, legacy/extended scanner events,
Find My cache/UI regressions, raw-gate negative cases and scan-store checks
pass. The new production UI test covers full list/sort/native hit testing,
source-overwrite-safe selection, exact typed Fox Hunt target and delayed
Back cleanup. Native list and changed detail previews fit. Independent
snapshot/UI reviews found no blocker. Marauder develop was rechecked through
GitHub and remains `79cc6bcba1db6679baee1dab2b0c1ad5010e6363`.

S3 app size is 1,655,856 bytes (+3,472). Internal static allocation is unchanged.
Frozen-target sizes are 36 bytes shared provenance, 7,536 snapshot, 6,280
Monitor and 196 per frozen row: +4 bytes per row / +128 per 32-row PSRAM bank.
Boot has ten tasks, 116,960 internal bytes free, minimum 116,280 and 1,428,324
PSRAM bytes free. C5 free is 77,140; SD mounts and C5 is online at 40 MHz. No new
reset fault occurs. At 109 startup SPI exchanges, mutex/fall/high/driver errors
are zero; initial timeout/fallback counts are four each.

Rev60 physical variants, matched-response cases, repeated Fox Hunt/scan
transitions and Wi-Fi/BLE concurrency remain unverified. A question about an
owned Flipper test device is pending. Coverage remains 71 partial, ten pending,
ten needs-review and four hardware-limited symbols, not a completion fraction.
Wi-Fi NO MEM/Association Sleep/Bad Message remain unresolved. GPS and six-C5
hardware are absent; this is neither attack repair nor full Marauder parity.

Next source-proven receive gap: Meta filtering and Analyzer ignore 32-bit and
Bluetooth-base128 equivalents of allowed and blocked service identifiers.
The next patch should normalize exact UUIDs in lists/leading service data,
preserve manufacturer-ID context and blocked-ID precedence, and retain
ADV/RSP source identity/time. Custom 128-bit substrings must not impersonate a
Bluetooth-base value; labels remain unauthenticated identifier hints.

## 2026-10-30: rev61 Meta evidence and complete-report Analyzer installed

Installed `candidate-ble-meta-evidence-compact` on verified S3 COM76 after
fourteen affected host suites, independent parser/backend/UI/diagnostic
reviews, native previews, five stable-input builds, matched package and
final READY interrupt validation passed. Three flash-write hashes verified.
All C5 images match the retained verified C5 50 installation; NVS, PHY and SD
were preserved and rev60 remains frozen for rollback. The
[rev61 receipt](../../integration/device-test-2026-10-27/rev61-flash-receipt.json)
records exact files, hashes, tests and target layout.

The original shared Meta/HC parser now normalizes exact UUID16, zero-extended
UUID32 and Bluetooth-base UUID128 fields. It retains manufacturer, UUID-list
and leading-service-data contexts separately in a ten-byte matrix for eleven
inherited IDs across seven contexts. All 6,241 parser fixtures pass, including
allowed/blocked order, full-report validation, malformed suffixes, normalized
equivalents, custom-UUID negatives and unchanged exact HC name criteria.

Meta discovery now uses the existing exact ADV/matched-response provenance.
New originals retire current positive evidence; valid blocked-ID sweep
history stays separately labeled. Analyzer accepts complete reports through
255 bytes for Meta/HC and retains Watch/iBeacon's legacy 31-byte criteria.
Every later ordinary or invalid event clears earlier displayed Meta evidence.
The list, frozen details, existing Fox Hunt route and Analyzer display exact
field pairs and original source ages with explicit remainder counts. Stronger
Watch evidence remains visible. Diagnostics enumerate exact aggregate pairs
only after a coherent completed-sweep read. No radio schedule or active
engine changed.

The target memory audit found four avoidable padding bytes per focus/row;
two adjacent Boolean bitfields recover 128 bytes per 32-row bank. Usage and
locking review found no member-address, wire-format or shared-write hazard.
Final target sizes are 56 bytes provenance, 100 focus, 208 scanner record,
8,176 snapshot, 6,920 Monitor and 216 frozen row. Each bank grows 640 PSRAM
bytes over rev60. Analyzer core/view are 244/240 bytes (+12 each). The S3 app
is 1,658,800 bytes (+2,944), and internal static allocation grows 48 bytes.

Boot shows ten tasks, 116,912 internal bytes free (minimum 116,232),
1,428,324 PSRAM bytes free, mounted SD and C5 online at 40 MHz with 77,140
bytes free. No new reset fault occurs. At 92 startup SPI exchanges,
mutex/fall/high/driver errors are zero; initial timeout/fallback counts are
four each. BLE generation zero and all jobs idle mean no RF acceptance is
inferred from startup. Owner testing of scan/Analyzer/Stop/Back/following
Wi-Fi scan is pending; broader physical encodings and concurrency are also
unverified. Prior rev59 Find My owner acceptance is preserved separately.

Coverage remains 71 partial, ten pending, ten needs-review and four
hardware-limited symbols. Wi-Fi NO MEM, Association Sleep and Bad Message
remain unresolved. GPS/six-C5 hardware is absent; no attack repair or full
Marauder parity is claimed.

Next receive-path audit identified stale first-match iBeacon payloads beside
new raw observation times, the whole-report 31-byte iBeacon limit, and Flock
evidence hidden by Watch's single winning category or ignored in responses.
Candidate follow-up work is bounded full-report iBeacon parsing with explicit
duplicate/malformed-tail rules and source times, plus independent Flock hint
bits using shared provenance. Advertised identities remain unverified. The
upstream Flock mode also transmits Wi-Fi beacons; that behavior is outside
this receive-only follow-up.

## 2026-10-30: rev62 iBeacon source evidence installed

Installed `candidate-ble-ibeacon-provenance` on verified S3 COM76. Fifteen
affected host suites, independent parser/backend/UI/console review, native
previews, five stable-input builds, matched package, final READY interrupt
check and all three flash-write hashes passed. C5 images remain byte-identical
to verified C5 50. NVS/PHY/SD were preserved, and rev61 is frozen for rollback.
See the [rev62 receipt](../../integration/device-test-2026-10-27/rev62-flash-receipt.json)
and [iBeacon source/acceptance notes](BLE_IBEACON_PARITY.md).

The shared parser now accepts exact iBeacon manufacturer fields inside full
complete AD containers through 255 bytes, validates the complete stream,
coalesces identical repeats and rejects conflicting tuples. Every non-FOUND
result clears output. Its 2,908 fixture checks cover structural rules,
duplicates, boundaries, network byte order and all signed calibration values.

The snapshot replaces its first-match cache with the latest qualifying
payload plus its own time/sequence/origin. Current valid ADV or matched-RSP
evidence is distinguished from retained earlier same-sweep evidence without
duplicating payloads or borrowing current SID/time for history. Analyzer
shows only its latest event's tuple and still gives stronger Watch evidence
priority. The normal filtered/sorted list, immutable detail and Fox Hunt/Back
flow preserves exact payloads and source ages. Console summaries distinguish
current ADV, current RSP and earlier matching rows only after coherent reads.
No radio scheduling, active engine or Sound eligibility changed.

Target sizes are 56 bytes shared provenance, 108 focus, 216 scanner record,
8,432 snapshot, 7,176 Monitor, 224 frozen row and 268/264 Analyzer core/view.
Each 32-row PSRAM bank grows 256 bytes; the S3 app grows 928 bytes to
1,659,728. Internal static allocation grows 16 bytes. Fresh boot has ten
tasks, 116,896 internal bytes free (minimum 116,216), 1,428,324 PSRAM bytes
free, mounted SD and C5 online at 40 MHz with 77,140 bytes free. No new reset
fault occurs. At 183 startup SPI exchanges, mutex/fall/high/driver errors
are zero; initial timeout/fallback counts remain four each. All jobs are idle
and BLE generation is zero, so no RF acceptance is inferred from startup.

GitHub develop was rechecked at 07:55:33 UTC and remains
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. The pending normal scan/Analyzer/
STOP/Back/following-Wi-Fi check should use rev62. Physical iBeacon variants,
response-only cases and repeated/concurrent operation remain unverified.
General list/detail RSSI remains the per-address sweep peak, distinct from
the retained tuple's source time; future signal-display work should make
that distinction more explicit. Rev59 Find My owner acceptance is preserved
separately. Coverage remains 71 partial, ten pending, ten needs-review and
four hardware-limited symbols. Wi-Fi NO MEM/Association Sleep/Bad Message
remain unresolved; GPS and six-C5 hardware are absent.

Next read-only Flock audit confirms the dedicated filter loses evidence when
Watch chooses another category, ignores response-only signatures and rejects
extended containers. An independent structured classifier can reuse current
ADV/RSP binding and retain exact accessory UUID, same-report 3100/3200 pair,
and name/manufacturer evidence without combining packets. Manufacturer-only,
bare decimal names and broad OUIs are weaker clues, not verified cameras or
serials. Existing SD RECON.DB supplies attributed vendor/module context
outside radio callbacks; its OUIs do not identify Bluetooth company 09C8.
Upstream's automatic Wi-Fi transmissions are outside this receive-path work.

## 2026-10-30: rev63 independent Flock receive evidence installed

Installed `candidate-ble-flock-evidence` on verified S3 COM76. Sixteen affected
host suites, independent parser/backend/console/UI review, native previews,
five stable-input builds, matched package, the final S3 READY interrupt check
and three flash-write hashes passed. The first target build identified a
possible format truncation in the display remainder label. Its buffer was
expanded to fit the full unsigned representation before rerunning host checks
and the build matrix. The first failure log is preserved separately. C5 images
are byte-identical to verified firmware 50; only S3 was flashed, preserving
NVS/PHY/SD and the frozen rev62 rollback package. See the
[rev63 receipt](../../integration/device-test-2026-10-27/rev63-flash-receipt.json)
and [Flock source and validation notes](BLE_FLOCK_PARITY.md).

The dedicated FLOCK filter now has independent evidence instead of relying on
Watch's single winning classification. Complete reports through 255 bytes retain
exact accessory UUID and service contexts, same-report 3100/3200 pairs, battery
name patterns and separate name/manufacturer corroboration. Weak decimal/unnamed
09C8 cases and the 27 public/global address-prefix candidates remain explicit
clues. Facts never combine across ADV and scan response. Strict existing source
binding, immutable selection, separate source ages and privacy masking carry
through details and Fox Hunt/Back. Analyzer replaces facts with each event and
preserves stronger Watch/iBeacon and meaningful Meta payload presentation.
General BLE details now explicitly label the per-address sweep peak RSSI.
Console summaries require a coherent completed-sweep read and distinguish
packet levels from address-only candidates. No radio scheduling, active engine,
Watch rule or Sound eligibility changed.

The pure parser/address fixture passes 16,676 checks. Target sizes are 64 bytes
shared provenance, 116 focus, 224 scanner record, 8,688 snapshot, 7,432 Monitor,
232 frozen row and 276/272 Analyzer core/view. Each 32-row PSRAM bank grows
256 bytes. The S3 app grows 5,488 bytes to 1,665,216; internal static use grows
16 bytes. Fresh boot has ten tasks, 116,880 internal bytes free (minimum
116,200), 1,428,324 PSRAM bytes free, mounted SD and C5 online at 40 MHz with
77,140 bytes free. No new reset fault occurs. At 31 startup SPI exchanges,
mutex/fall/high/driver errors are zero and initial timeout/fallback counts are
four each. All jobs are idle with BLE generation zero; no RF acceptance is
inferred from these boot checks.

GitHub develop was rechecked at 08:30:13 UTC and remains
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. The pending normal Bluetooth scan,
Analyzer, STOP/Back and following Wi-Fi scan check should use rev63. Physical
Flock patterns and repeated/concurrent operation remain unverified. Rev59 Find
My acceptance is retained separately. Coverage remains 71 partial, ten pending,
ten needs-review and four hardware-limited symbols. Wi-Fi NO MEM, Association
Sleep and Bad Message remain unresolved; GPS and the six-C5 backboard are absent.

The next read-only audit found two bounded receive reliability gaps: HC name
hints are set before strict event/SID admission and have no exact current
source/variant, and unavailable RSSI 127 can replace a valid sweep peak and
prevent later recovery. HC facts may fit two bytes of existing provenance
padding; target measurement must confirm. A receive-only valid-RSSI predicate
can reuse the list/tracker's existing -127 through 20 range without changing
raw action eligibility. The optional upstream recent-30-second list filter is
a lower-priority follow-up. These are findings, not implemented fixes; details
are in the [next receive audit](../../integration/rev63-next-receive-audit-2026-10-30.md).

## 2026-10-30: rev64 HC provenance and receive signal recovery installed

Installed `candidate-ble-hc-rssi` on verified S3 COM76. Sixteen stable-input
host suites, independent parser/backend/UI/console reviews, native previews,
five target builds, matched package, final S3 READY interrupt placement and
all three flash-write hashes passed. C5 images remain byte-identical to the
verified firmware 50 installation. Only S3 was flashed; NVS/PHY/SD and the
frozen rev63 rollback package were preserved. See the
[rev64 receipt](../../integration/device-test-2026-10-27/rev64-flash-receipt.json)
and [HC source and validation notes](BLE_HC_NAME_PARITY.md).

HC-03/05/06 now have exact independent current ADV/RSP codes under the same
typed identity, generation, event, SID, sequence and response-window admission
used by the other receive families. New ordinary/invalid originals clear current
HC evidence; matched responses cannot refresh original raw bytes or source time.
The dedicated filter, sort, frozen selection, detail and Fox Hunt/Back retain
each code and its own age. Analyzer replaces its HC code with each accepted
event, preserves stronger evidence and clears ordinary/invalid later reports.
Names remain unauthenticated hints, not proof of a skimmer or hardware identity.

Scan signal peaks now accept only the existing -127 through 20 measurement
range. Unavailable 127 and other unsupported values cannot overwrite a valid
peak, and an invalid first observation can recover on a valid original.
Responses remain excluded from the original peak. List/tracker/live presentation
share one validity helper; detail and Analyzer show unavailable instead of a
numeric sentinel. Invalid history samples are skipped. Actual callback fixtures
verify unchanged raw bytes, observation times and report counts. The shared AD
fixture passes 6,569 checks; no active engine, Sound gate, Watch rule or radio
scheduling changed.

HC codes fit the existing 64-byte provenance record. Removing the redundant
HC text pointer saves four target bytes per row / 128 per 32-row PSRAM bank.
Target focus/record sizes are 112/220, snapshot 8,560, Monitor 7,304 and frozen
row 228; Analyzer core/view remain 276/272. The app grows 2,304 bytes to
1,667,520 while internal static use falls 16 bytes. Fresh boot has ten tasks,
116,896 internal bytes free (minimum 116,216), 1,428,324 PSRAM bytes free,
mounted SD and C5 online at 40 MHz with 77,140 bytes free. No new reset fault
occurs. At 29 startup SPI exchanges, mutex/fall/high/driver errors are zero;
initial timeout/fallback counts remain four each. All jobs are idle and BLE
generation is zero, so no RF acceptance is inferred from startup.

GitHub develop was rechecked at 08:46:57 UTC and remains
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. The pending normal Bluetooth scan,
Analyzer, STOP/Back and following Wi-Fi scan check should use rev64. Physical
HC broadcasts and prolonged/concurrent operation remain unverified. Prior
rev59 Find My acceptance is retained separately. Coverage remains 71 partial,
ten pending, ten needs-review and four hardware-limited symbols. Wi-Fi NO MEM,
Association Sleep and Bad Message remain unresolved; GPS/six-C5 hardware is absent.

The next receive-workflow audit recommends the upstream recent-30-second list
option for Find My and iBeacon. Existing qualifying BLE source timestamps and
an inexpensive visibility mask can support it without another task or evidence
bank. AP recent filtering needs per-AP RF timestamps that the current C5 result
does not carry; whole-snapshot receipt time is insufficient. The audit also
distinguishes the existing packet timeline and WiGLE runtime adapter from real
remaining gaps such as focused Flock Wardriving and Remote ID workflows. These
are findings, not implemented features; see the
[next receive audit](../../integration/rev64-next-receive-audit-2026-10-30.md).

The stale source-only wording in [WiGLE format notes](WARDRIVE_WIGLE_FORMAT.md)
was also corrected against the existing adapter, storage code and rev52/54/55
receipts. The notes distinguish current result-receipt geotagging from unavailable
RF observation times, preserve GPS/backboard/saved-file acceptance limits and
describe no-GPS unlocated logging. This was a documentation correction, not a
new Wardriving implementation or hardware retest.

## 2026-10-30 — rev65 installed: source-age options in existing BLE lists

Installed `candidate-ble-recent-list` on the identified S3 only. C5 firmware 50
images match the retained verified installation byte-for-byte. All 18 host
suites, five stable-input target builds, S3 READY ISR gate, matched package,
three flash write hashes and boot checks pass. Frozen rev64 rollback images,
NVS, PHY and SD were preserved. See the
[rev65 receipt](../../integration/device-test-2026-10-27/rev65-flash-receipt.json)
and [receive-list contract](BLE_RECEIVE_LIST_PARITY.md).

Find My and iBeacon lists now offer All ages by default and WITHIN 30S beside
the existing sort control. The inclusive 30,000-ms limit uses each qualifying
source's own time: current admitted ADV or matched response for Find My, and
the retained qualifying iBeacon match, including explicitly EARLIER evidence.
A separate 64-bit publication expiry ceiling prevents stale snapshots from
reappearing after a full 32-bit timer revolution. It never supplies a newer
source time. Existing UI ticks expire rows even in completed or paused lists,
clamp pagination, and preserve the last painted index mapping and frozen typed
selection. Empty lists do not initiate scanning. Existing parsers, Monitor
retention, Analyzer, scheduling and active gates are unchanged.

Native production UI tests exercise expiration, sort/filter interaction,
painted-row selection at the expiry boundary, failed/empty publications and
Fox Hunt/Back. Renderer tests cover split control bounds and cache updates.
An apparent preview discrepancy was checked against native pixels: PNG and
PPM outputs match, including every pixel of the WITHIN 30S label; no source
correction was needed. These are host checks, not touchscreen acceptance.

The S3 app is 1,668,576 bytes, 1,056 above rev64. Total internal static use
grows 48 bytes. Target scan view/render cache sizes are 152/792 bytes
(+4/+16); scan, focus, Monitor and Analyzer structures are unchanged. Fresh
boot has ten tasks, 116,848 internal bytes free (minimum 116,168), 1,428,324
PSRAM bytes free and mounted SD. C5 is online at 40 MHz with 77,140 bytes free.
At 42 startup SPI exchanges, mutex/fall/high/driver errors are zero; initial
timeout/fallback counts are four each. No new reset fault was reported. All
jobs are idle and BLE generation is zero; startup proves no RF behavior.

Develop was checked at 09:14 UTC and remains
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. Current-build Find My/iBeacon
filtering, touchscreen placement, expiration and prolonged/concurrent use
remain pending. Prior owner reports remain historical. Coverage remains
71 partial, ten pending, ten needs-review and four hardware-limited symbols.
AP recent filtering still needs per-AP RF timestamps; snapshot receipt time
is insufficient. Wi-Fi NO MEM, Association Sleep and Bad Message remain
unresolved; GPS and the six-C5 backboard remain absent.

## 2026-10-30 — rev66 paired install: Wi-Fi presence-age filtering

Installed `candidate-wifi-source-age` on the verified S3 and onboard C5,
advancing C5 to firmware 51. All 23 frozen-input host suites, five target
builds (S3, onboard C5, bridge, expansion WROOM and expansion XIAO), matched
package and S3 READY ISR placement passed. The temporary bridge, C5 and final
S3 each produced three verified write hashes, nine total. The frozen rev65
paired rollback package, NVS, PHY and SD were preserved. See the
[rev66 receipt](../../integration/device-test-2026-10-27/rev66-flash-receipt.json)
and [AP receive-list evidence](WIFI_RECEIVE_LIST_PARITY.md).

The ordinary Wi-Fi list now defaults to ALL AGES and offers an inclusive
WITHIN 30S option. C5 passive-scan callbacks validate beacon/probe-response
frames and retain exact BSSID/raw-SSID/receive-channel tuples in a bounded
64-entry table. Strongest callback rank governs capacity retention with
stable ties; it does not replace or date SDK aggregate RSSI. A later weaker
callback still updates a retained tuple's presence time. Ambiguous binary
SSID tails, unmatched rows and evicted evidence remain unavailable. Channel
177 is included. The unchanged 22-byte result response transports optional
source age; S3 adds full-width elapsed request time conservatively without
comparing chip clock origins or substituting sweep-publication time.

S3 stages AP rows and timing together in the existing PSRAM pool and commits
only after complete generation/count revalidation. Failed capture retains
the previous bank and timing. Existing UI ticks expire and paginate rows
without RF starts, and taps use the last painted original index before
copying the selected AP. Watch and explicit target selection remain
unfiltered. A separate pending-publication flag allows a witnessed new cycle
or accepted START to publish fresh results when an eight-bit generation
repeats; unchanged COMPLETE polls do not recopy the old bank. Exactly 256
wholly unobserved completed cycles with the same byte/state remain
indistinguishable. BLE scheduling, source admission and active gates did not
change.

Production host fixtures cover 30,450 C5 parser/table checks and 16,061 C5
lifecycle checks, timing transfer/cleanup failures, AP expiration and frozen
selection, and 256 actual BLE scanner starts in both legacy and extended
callback builds. Native preview and independent lifecycle/UI/C5 reviews
passed. These are host validations, not RF or touchscreen acceptance.

The S3 app is 1,671,152 bytes (+2,576 over rev65); C5 app is 1,613,968
(+2,736). Internal static use grows 32 bytes on S3 and 3,600 on C5. Target
layout measures a 3,608-byte C5 table with 56-byte entries, 16-byte S3 timing
records and a 6,688-byte shared PSRAM display pool (+1,024). Scan view/render
cache remain 152/792 bytes; BLE snapshot/focus/record, Monitor and Analyzer
structures are unchanged. These are ELF layout measurements, separate from
runtime heap counters.

Startup has ten S3 tasks, 171,724 internal bytes total, 116,816 free (minimum
116,136), 1,428,324 PSRAM free, mounted SD and C5 firmware 51 online at 40 MHz
with 73,540 internal bytes free. No prior reset fault is reported. At 21 SPI
exchanges, mutex/fall/high/driver errors are zero; initial timeout/fallback
counts are four each. All scans/jobs are idle, and `wifi_seen=not_complete`,
state 0, radio error 0 is the expected result before a scan. This confirms
the read-only command's idle path, not callback coverage or live recent-row
counts. The PSRAM display pool has not been exercised by this idle baseline.

Develop was rechecked at 09:53:32 UTC and remains
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. A separate source audit found
`BT_SCAN_FLOCK_WARDRIVE` only as the dormant define at `WiFiScan.h:169`, with
no callback/start/menu/CLI workflow. Reclassifying that symbol from pending
to needs-review changes coverage to 71 partial, nine pending, 11 needs-review
and four hardware-limited symbols; it does not implement a new Flock
Wardriving feature. Existing owner evidence remains historical.

Optional current-build acceptance remains: normal Wi-Fi scan, WITHIN 30S
expiration, ALL AGES restoration and selecting Yasin Family Home. Physical
callback coverage, touchscreen routing, repeated transitions and concurrent
Wi-Fi/BLE operation remain pending. Prior Wi-Fi NO MEM, Association Sleep
and Bad Message reports remain unresolved. No attack repair or full Marauder
parity is claimed; GPS and the six-C5 backboard remain absent.

## 2026-10-30 — rev67 S3 install: passive Remote ID sweeps and source selection

Installed `candidate-remote-id-sweeps` on the identified S3 COM76. All 30
stable-input host suites, five target builds, matched package, S3 READY ISR
placement and three S3 flash-write hashes passed. C5 firmware 51 remains
installed from rev66: its bootloader, partition table and app hashes, sizes
and offsets match the previous package and verified installation receipt.
No C5 or bridge flash occurred; the bridge was built and packaged only.
The frozen rev66 rollback package, NVS, PHY and SD were preserved. See the
[rev67 receipt](../../integration/device-test-2026-10-27/rev67-flash-receipt.json)
and [Remote ID source and validation scope](REMOTE_ID_PARITY.md).

Scan → Remote ID now opens without radio calls. START runs independent,
repeated passive C5 Wi-Fi and S3 BLE sweeps through the existing receivers.
Completed Watch snapshots are validated before publishing separate bounded
source banks, including row/status consistency and Pwn records before
filtering. The twelve-record producer limit per radio is shared with Pwn
observations; dropped reports and unavailable capture remain explicit.
Selection copies the last painted source, then follows that exact radio,
transport and typed address through later sweeps. It shows current, absent
or unavailable evidence without changing the frozen selection or borrowing
coordinates from an earlier payload. A broadcast address or claimed ID does
not establish a physical aircraft's identity.

The UI labels full-width S3 sweep and last-match receipt ages separately;
these are not RF timestamps or per-field freshness. Independent controllers
distinguish known rejected START from ambiguous delivery, check source epoch
and generation, retry bounded capture failures, and drain/retry STOP before
freeing state. READY and fully released STOPPED sessions do not poll either
radio. The retained C5 STOP command has no generation argument: the adapter
rechecks identity immediately before calling it and relies on serialized UI
callers plus exclusion of background Wardriving. This is not a new atomic
wire ownership guarantee. Read-only `remote_id` console diagnostics copy
aggregate state under a bounded lock without radio calls, source identifiers
or PSRAM pointers.

Production host suites cover 2,704 receive/parser/collector checks, 1,255
inventory checks, 5,962 controller/scheduler checks, 2,613 renderer checks and
5,617 live-adapter/navigation checks. The diagnostics suite adds 48 assertions
and 100,000 coherent concurrent reads. Coverage includes malformed/truncated
inputs, Pwn coexistence, changed-ID clearing, rejected and ambiguous starts,
foreign ownership, generation wrap, failed atomic publication, immutable
painted selection, host cleanup and quiescent zero-poll behavior. Independent
reviews and native previews passed. These are host results, not acceptance
with a physical Remote ID transmitter or touchscreen.

The S3 app is 1,683,648 bytes, 12,496 above rev66; internal static use grows
64 bytes. C5 images and internal static use are unchanged. Exact S3 target
layout measures one 6,088-byte on-demand PSRAM live store, containing the
1,888-byte inventory, 264-byte controller, 472-byte view and 3,184-byte render
cache plus adapter state. Those contained structures are not separate
allocations to add again. Diagnostics occupy 32 target bytes of static state.
Existing BLE snapshot/focus/record, Monitor, Analyzer and scan display-pool
sizes are unchanged. Layout measurements describe ELF structures, not live
heap consumption.

Startup reports 171,660 S3 internal bytes total, 116,752 free (minimum
116,072), 1,428,324 PSRAM free, mounted SD and C5 firmware 51 online at 40 MHz
with 73,540 internal bytes free. No prior reset fault is reported. At 40 SPI
exchanges, mutex/fall/high/driver errors are zero; initial timeout/fallback
counts remain four each. Remote ID is closed with zero sweeps, sources,
dropped reports, errors and ownership on both radios. This confirms the idle
diagnostic path; the on-demand allocation and live RF workflow were not
exercised by this startup baseline.

Develop was rechecked at 10:34:48 UTC and remains
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. Both actual upstream symbols,
`REMOTE_ID_SCAN_ALL` and `REMOTE_ID_SCAN_TARGET`, remain partial. Selection
uses ordinary sweeps rather than full upstream dedicated target priority.
The existing supported BLE/beacon/fixed-layout NAN decoder is reused;
legacy OUI and variable NAN forms, complete operator fields, coded-PHY
hardware acceptance, GPS/range, maps and export parity remain open. No GPS
is required to receive claimed coordinates, and no receiver distance is
calculated. Physical discovery, selected-source updates, absence/return,
repeated Stop/Back and concurrent radio acceptance remain pending.

Coverage remains 71 partial, nine pending, 11 needs-review and four
hardware-limited symbols, 95 total. Other mode statuses and historical
owner reports, including limited rev59 Find My and prior MultiSSID evidence,
remain unchanged. Wi-Fi NO MEM, Association Sleep and Bad Message remain
unresolved. No active-path repair or full Marauder parity is claimed; GPS
and the six-C5 backboard remain absent.

## 2026-10-30 — rev68 paired install: additional Remote ID receive framing

Installed `candidate-remote-id-framing` on the verified S3 and onboard C5,
advancing C5 to firmware 52. All 31 stable-input host suites, five target
builds, matched package and S3 READY ISR placement passed. The temporary
bridge, C5 and restored normal S3 each produced three verified flash-write
hashes, nine total. The frozen rev67 rollback package, NVS, PHY and SD were
preserved. See the
[rev68 receipt](../../integration/device-test-2026-10-27/rev68-flash-receipt.json)
and [Remote ID framing scope](REMOTE_ID_PARITY.md).

The shared receive decoder now accepts the legacy `90:3a:e6` beacon OUI as
well as `fa:0b:bc`, with the same exact subtype, counter and strict
MessagePack framing. NAN parsing now accepts supported Publish, Subscribe
and Follow-up service descriptors, arbitrary instance/requestor bytes, the
range flag, optional two-byte binding bitmap and bounded matching/response
filters. It requires the exact `51:6f:9a:01:00:00` destination and Remote ID
service identifier. The service-info length must exactly match the remaining
descriptor and contain the counter plus a valid MessagePack. Reserved flags,
type 3, duplicate relevant descriptors and malformed enclosing suffixes
reject the whole interpretation. Opaque filters cannot supply payload data.
Canonical Publish SDA followed by SDEA is covered, but SDEA payload semantics
are not newly decoded. Bare single-message Wi-Fi payloads remain unsupported.

The expanded receive suite passes 171,310 checks, including all supported
optional-field combinations, absent/empty/maximum filter lengths, repaired
inner truncations, canonical and variable NAN vectors, both beacon OUIs,
duplicate and false-positive cases, strict pack boundaries and Pwn coexistence.
A separate 3,313-check suite feeds actual C5 callback output through the real
C5 Watch producers, S3 Watch getter and inventory. It verifies FCS exclusion,
source address/channel/signal, generation barriers and wrap, whole-payload
replacement, immutable selected evidence, source absence and retention after
bad frames. Independent parser and fixture reviews passed. These host checks
do not establish reception from a physical transmitter.

Only the shared decoder source/header and protocol firmware-version header
changed among production inputs. Existing scanner callbacks, controllers,
collectors, inventory, UI, scheduling, active engines, wire records and
allocation paths remain unchanged. The shared decoder component serves C5
Wi-Fi and S3 BLE; the changed Wi-Fi framing path runs on C5. No additional
task or memory bank was introduced. Source keys still describe
radio/transport/typed-address observations rather than verified aircraft
identity, and displayed ages remain completed-sweep receipt evidence.

The C5 app is 1,614,192 bytes, 224 above rev67. S3 app size remains 1,683,648
bytes, with a new image hash. Internal static deltas are zero on both chips,
and all measured existing target layouts are unchanged, including the
6,088-byte on-demand Remote ID PSRAM store and 32-byte diagnostics value.
Startup reports 171,660 S3 internal bytes total, 116,752 free (minimum
116,072), 1,428,324 PSRAM free, mounted SD and C5 firmware 52 online at 40 MHz
with 73,540 internal bytes free. No prior reset fault is reported. At 126 SPI
exchanges, mutex/fall/high/driver errors are zero; initial timeout/fallback
counts remain four each. Remote ID is closed with zero counters and ownership
on both radios. This is an idle baseline, not an exercised live-scan result.

Develop was rechecked at 10:57:40 UTC and remains
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. Both Remote ID mode entries remain
partial; totals remain 71 partial, nine pending, 11 needs-review and four
hardware-limited symbols, 95 total. Physical legacy/variable-NAN reception,
selected-source updates and repeated concurrent scans remain pending.
Dedicated Target/NAN priority, extended-service payloads, fragments, complete
operator fields, coded-PHY hardware acceptance, GPS/range, maps and logging
parity remain open. Historical owner reports are preserved. Wi-Fi NO MEM,
Association Sleep and Bad Message remain unresolved; no attack repair or
full Marauder parity is claimed. GPS and the six-C5 backboard remain absent.

## 2026-10-30 — rev69 paired installation: Remote ID callback-age bounds

The installed `candidate-remote-id-receive-age` adds per-source receive-callback age to the existing
passive Remote ID workflow. C5 Wi-Fi and S3 BLE capture full-width callback
time before locks/decoding, then associate it with the same retained source
that received a qualifying decoded report. Unrelated/malformed traffic and
BLE scan responses cannot refresh it. A qualifying untimed update clears old
timing. The collector still merges claims within a sweep: callback age does
not establish the age of every position, altitude or identifier field.

Firmware 53 adds read-only Watch timing command 89 without changing the
46-byte row, 16-byte status or existing chunk protocol. The observed S3
transport brackets chunks with two matching full-width capture epochs,
consistent availability and nondecreasing age, then checks final status and
fresh peer identity. Older Watch firmware receives no timing query and returns
unavailable evidence. Supported malformed/failed timing rejects the complete
read; ordinary Watch calls retain their prior transactions.

S3 stores source-computed elapsed age with its own request-start timestamp.
The display adds local elapsed time and rounds upward, including millisecond
quantization, to show `RX CALLBACK AGE <=Ns`. Selected details keep a separate
last-match bound alongside the existing sweep-receipt ages. Publication,
painted selection, duplicate-token handling and absent-source retention copy
payload/timing together. No radio restart or inferred aircraft identity is
introduced by selecting or viewing evidence.

Individual host suites pass: timing arithmetic/property 129,705, observed
transport 1,857, inventory 1,294, session 6,501, renderer 3,436 and production
live adapter 6,313 checks. Existing receive 171,310 and actual C5 callback
3,313 checks also pass; real S3 legacy/extended callback and snapshot fixtures
cover lock delay, time zero, wrap and response exclusion. The new shared/C5
producer timing runner adds 636 checks, including Wi-Fi epoch turnover before
other receive consumers and both BLE callback builds. All 34 aggregate suites
pass with stable inputs, as recorded in the
[host receipt](../../integration/device-test-2026-10-27/rev69-host-checks.json).
Independent source reviews separate this host evidence from physical reception.
All five stable-input target builds and the matched package now pass. The
initial bridge build rejected the disabled BLE stub's multi-statement formatting
under `-Werror=misleading-indentation`; separating the statements changed no
behavior. All 34 host suites and five targets were revalidated after that fix.
The [rev69 receipt](../../integration/device-test-2026-10-27/rev69-flash-receipt.json)
verifies all nine bridge/C5/S3 writes and normal S3 restoration. C5 firmware 53
is online at 40 MHz SPI, SD is mounted and no previous reset fault is reported.
S3 idle internal free heap is 116,752 bytes (minimum 116,072), PSRAM free
1,428,324 and task count 10, unchanged from rev68; C5 internal free is 73,268.
Remote ID was closed with zero diagnostics, so this is startup verification,
not physical reception or callback-age acceptance. The frozen rev68 pair is
retained for rollback; NVS, PHY and SD contents were preserved. Expansion
targets were built, not flashed. Rev69 physical/owner acceptance remains pending.

Frozen target ELF comparison measures the Remote ID live PSRAM store at 7,240
bytes (+1,152): inventory 2,800, controller 264, view 616 and render cache 3,280.
The S3 snapshot is 8,680 (+120); each Watch collector is 688 (+116). Local timing
metadata is 24 bytes and the reply 22. These are contained sizes, not additional
independent allocations. S3 internal static allocation is unchanged and C5
grows 264 bytes. S3 app size is 1,686,672 (+3,024); C5 is 1,615,920 (+1,728).
Image lengths differ from padded transport counts; ELF sizes do not establish
free heap or running-session peak use. See the
[target layout](../../integration/device-test-2026-10-27/rev69-memory-layout.json)
and [comparison](../../integration/device-test-2026-10-27/rev69-candidate-comparison.json).

The implementation adds bounded timestamp sidecars and 24-byte timing metadata
in the existing lazy PSRAM state, with no new task, scan schedule, active
engine or GPS requirement. Both Remote ID modes remain partial. Dedicated
Target/NAN priority, field-level RF timing, complete operator presentation,
navigation and logging remain open. Historical owner reports and unresolved
Wi-Fi NO MEM, Association Sleep and Bad Message regressions are unchanged;
this is not a full Marauder parity or attack-repair claim.

During this work the owner answered the prior rev67-worded Remote ID prompt:
**“Both counts increase; both runs stop.”** Rev68/C5 firmware 52 was installed
when the answer arrived, but the exact image used for those runs was not
independently established. The captured console found Remote ID already closed
and all diagnostics zero; it cannot confirm the reported counts. The
[owner record](../../integration/device-test-2026-10-27/rev69-prior-owner-acceptance.json)
therefore records only reported basic two-radio sweep/repeated-Stop behavior,
without source-decoding, callback-age, selected-source or rev69 acceptance.

## 2026-10-30 — rev70 paired installation: bounded Remote ID priority reception

Installed `candidate-remote-id-priority` adds a firmware-54 C5 passive receive job to
the existing Remote ID page, with independent S3 BLE sweeps. The adapter asks
for 8-second windows, 3-second renewable leases and 250 ms dwells. The pure
planner interleaves approved 6/149/6 channels with advancing background
coverage, preserving its background cursor between windows. The C5 performs
country/SDK channel checks with RX admission closed, reuses the scanner worker,
and drains admitted callbacks before retuning or releasing ownership.

New commands 90–93 use exact monotonic 64-bit job identities. Idempotent
Start/Stop/renewal retain terminal evidence; expected-peer preflight and
post-transfer checks bind control to the same C5. Review found and addressed
lost-ACK generation guesses, terminal-first recovery, renewal racing completion
and peer changes between UI polling and control. DATA_READY and final source
checks protect atomic Watch publication. Ordinary AP-list publication remains
separate, and legacy peers keep their ordinary sweep path.

Focused host suites pass: planner 27,197, production transport 12,867, C5
service 4,285, controller 6,619 and live routes 15,391 checks. These cover
ownership, cleanup and publication failures. The read-only diagnostics
now expose priority job state, channel, lease and validity without starting
radio work. The [rev70 receipt](../../integration/device-test-2026-10-27/rev70-flash-receipt.json)
verifies all 37 aggregate suites, five target builds and the matched package
with stable inputs, plus nine bridge/C5/S3 writes and normal S3 restoration.
C5 firmware 54 is online at 40 MHz SPI, SD is mounted and no previous reset
fault is reported. The frozen rev69 pair is retained for rollback; NVS, PHY
and SD contents were preserved. Expansion targets were built, not flashed.

Measured live PSRAM state is 7,320 bytes (+80), including the controller at
280 (+16); diagnostics are 48 (+16), and the new C5 planner type is 92 bytes.
Existing inventory/selection/view/render cache and BLE/ordinary AP layouts are
unchanged. S3 internal static allocation grows 32 bytes and C5 grows 304.
S3 app size is 1,689,296 (+2,624), and C5 is 1,622,624 (+6,704), measured as
raw image lengths rather than padded flash counts. Startup S3 internal free
heap is 116,720 (minimum 116,040), PSRAM free 1,428,324 and task count 10;
C5 internal free is 72,964. The page was closed with all 22 diagnostic fields
zero. This idle baseline does not establish active peak memory or RF coverage.
See the [layout](../../integration/device-test-2026-10-27/rev70-memory-layout.json)
and [comparison](../../integration/device-test-2026-10-27/rev70-candidate-comparison.json).
Rev70 physical priority reception, selected updates, lease cleanup and repeated
concurrent windows remain pending.

No GPS, extra task or active-engine change is introduced. Existing source
callback ages remain conservative bounds, not RF or individual field ages.
Full selected-target following, complete operator fields, maps/export and
overall Remote ID parity remain open. Historical owner reports stay scoped
to their recorded tests; Wi-Fi NO MEM, Association Sleep and Bad Message remain
unresolved. GPS and the six-C5 backboard are still absent.

## 2026-10-30 — rev71 paired installation: passive Remote ID Follow

Installed `candidate-remote-id-follow` with C5 firmware 55. A frozen Wi-Fi
Remote ID detail now offers explicit Follow/Unfollow; selecting the detail
alone does not start reception. S3 keeps desired selection separate from an
immutable attempted-job source, so changing selection cannot relabel an
in-flight Follow. Unsupported-peer Follow is rejected without generic
downgrade, and Unfollow remains available during downgrade/cleanup.

C5 independently binds prior DATA_READY epoch/generation, exact beacon/NAN
transport and transmitter, channel and valid full-width callback evidence.
Approved target/background slots share the existing bounded priority worker.
Missing, expired or SDK-unaccepted proof becomes explicit SEARCH before first
admission. Bound callbacks stop at the earlier source-plus-30-second or
requested-window deadline; renewals, new reports and repeated START do not
extend or rebind it. Protected bank insertion requires an actual exact-source
report and clears an evicted row's payload/timing. No placeholder is displayed.

Focused host checks pass: transport 90,942, planner 32,661, C5 service 12,236,
collector timing 209, inventory 1,844, diagnostics 56 plus 100,000 concurrent
reads, renderer 4,316 and actual live adapter 27,382. Independent reviews
identified and closed desired/in-flight identity, downgrade/Unfollow and
cleanup-label issues. The [rev71 receipt](../../integration/device-test-2026-10-27/rev71-flash-receipt.json)
verifies all 37 aggregate host suites, five target builds and the matched
package with stable inputs, plus nine bridge/C5/S3 writes. Normal S3 was
restored; C5 firmware 55 is online at 40 MHz SPI, SD mounted and no prior reset
fault reported. The frozen rev70 pair remains available for rollback; NVS,
PHY and SD were preserved. Expansion targets were built, not flashed.

Target layout measures the live PSRAM store at 7,344 bytes (+24), its view at
624 (+8), render cache at 3,288 (+8), each Watch collector at 696 (+8) and
the enclosing S3 snapshot at 8,688 (+8). Diagnostics remain 48 bytes and the
C5 planner 92. Other inventory/controller/selection and ordinary AP/BLE
evidence layouts are unchanged. S3 static use is unchanged and C5 grows
32 bytes. S3 app size is 1,691,536 (+2,240), C5 1,625,072 (+2,448), using raw
image lengths. Contained sizes are not independent allocations or heap use.
See the [layout](../../integration/device-test-2026-10-27/rev71-memory-layout.json)
and [comparison](../../integration/device-test-2026-10-27/rev71-candidate-comparison.json).

Startup S3 free internal heap is 116,720 bytes (minimum 116,040), PSRAM free
1,428,324 and task count 10; C5 internal free is 72,932. All 25 Remote ID
diagnostics were zero on the closed page. These idle figures do not confirm
RF coverage or active peak use. Physical Follow/SEARCH, protected capacity,
selected updates and repeated Stop/Back/Unfollow acceptance remain pending.
The reviewed ledger updater, generator and consistency check pass with all
95 mode statuses preserved: 71 partial, 9 pending, 11 needs_review and
4 hardware_limited. Both Remote ID symbols remain partial.

Both Remote ID modes remain partial. Full upstream Target behavior, remaining
receive formats, complete operator fields, maps/export and hardware acceptance
are open. Callback age is not RF arrival time or each merged field's age. No
active engine, GATT, sound, new worker or receiver-GPS requirement is added.
Historical owner evidence remains narrowly scoped; Wi-Fi NO MEM, Association
Sleep and Bad Message remain unresolved, and GPS/six-C5 hardware is absent.

## 2026-10-30 — rev72 installed: advertised Remote ID motion

The shared parser and collector preserve horizontal speed, direction and
height/reference from supported BLE, beacon and NAN Location messages.
Primary OpenDroneID references are pinned in the external
[rev72 index](../../integration/device-test-2026-10-27/rev72-reference-sources.json).
The value is eight bytes with explicit presence/validity; valid zero speed and
direction remain distinct from unavailable values. A new Location replaces
coordinates, altitude and motion together. Basic ID-only reports retain that
group's own timestamp; changed ID, eviction and capture reset clear it.
Root review caught a clock regression after a newer Basic ID callback; the
fixed collector checks both source and Location timestamps before assigning
fresh motion timing, including when the claimed ID changes.

Additive firmware-56 queries retain the 46-byte legacy row. S3 publication
validates paired values/timing before committing; supported failures cannot
fall back to legacy data. The existing detail offers Motion/Details, preserves
frozen typed selection and distinguishes Location/source callback bounds.
No radio scheduler, resident task, GATT, sound or attack path is added.

Focused checks pass: motion 1,064,928; existing receive 171,310; production
renderer 5,977 and live adapter 30,839. Independent shared, transport,
inventory, UI and helper reviews found no remaining blocker. The
[rev72 receipt](../../integration/device-test-2026-10-27/rev72-flash-receipt.json)
verifies all 38 host suites, five stable-input builds, the matched package,
S3 READY ISR gate and nine bridge/C5/S3 write hashes. Normal S3 was restored
after the C5 update; NVS, PHY and SD were preserved.

Measured target collector size is 896 (+200), PSRAM snapshot 8,888 (+200),
and separate on-demand Remote ID PSRAM store 8,888 (+1,544). The latter
contains the expanded inventory/selection/view/cache; these type sizes are
not additional allocations. S3 internal static grows 336 bytes and C5 400.
Startup records C5 firmware 56 online at 40 MHz, mounted SD, no prior reset
fault and all 25 closed Remote ID diagnostic fields zero. S3 internal free
heap is 116,384 (minimum 115,704), PSRAM free 1,428,324, task count 10 and
C5 internal free 72,532.

Physical motion values/reference, own Location-age behavior, unavailable
replacement and repeated Motion/Details/Follow/Stop/Back acceptance remain
pending, alongside prior Follow/SEARCH validation. Both Remote ID modes
remain partial. Prior owner evidence retains its historical scope; Wi-Fi
NO MEM, Association Sleep and Bad Message reports remain unresolved.

## 2026-10-30 — rev73 installed: BLE Wardriving and analysis

The existing Wardriving service now consumes one atomic completed S3 BLE bank
with compact, independently bound Flock ADV/response facts and typed-address
OUI context. Its no-GPS/fallback CSV v2 records source-specific fields and
callback uptime; a 41-bit evidence key permits changed clues without treating
every new timestamp as a new sighting. Classified counters describe processed
rows across sweeps. A PSRAM cursor resumes an unprocessed suffix during Stop
drain and resets after an accepted new scan, avoiding prefix double-counting.
WiGLE schema/routing/dedupe stays unchanged; located evidence export remains
open. The dormant upstream Flock Wardriving constant remains needs_review.

Analyzer peak/mean statistics use completed 200 ms buckets from the existing
64-bucket history, exclude the partial bucket and label saturated lower
bounds. The exact 64-bucket boundary is corrected. The BLE detail vendor cache
now binds found/unknown values to the storage epoch and retries transient
failures on the next render. Its guards use a zero-wait attempt on the existing
storage mutex, avoiding the resources console's database-hash lock wait; the
blocking epoch getter and cold recon lookup remain unchanged. The focused
host suite reports 5,609 checks using actual extracted storage/UI code,
including held-lock failure and recovery, without a device latency claim.
Production scope now includes storage.c/.h (13 paths). No active engine, new
radio task or scan schedule changes.

The [installed rev73 receipt](../../integration/device-test-2026-10-27/rev73-flash-receipt.json)
for `candidate-ble-wardrive-analysis` records 52 hash-bound host suites, five
stable-input builds, matched-package/READY checks, three verified S3 writes
and application reset on 2026-10-30. C5 firmware 56 retains all three exact
rev72 image hashes. The current bridge is build-only, with rebuilt metadata
hash changes; rev72 flash verification applies only to its historical images.

Measured S3 static growth is +16 bytes and C5 is unchanged. Wardriving's
session-only PSRAM workspace grows 13,440 to 16,900 bytes (+3,460), containing
two 1,280-byte dedupe-bank increases, 896 bytes of evidence staging and the
four-byte cursor. Status grows 320 to 336 bytes. The 72-byte vendor cache,
276-byte Analyzer/272-byte view, 8,888-byte snapshot and 8,888-byte Remote ID
workspace are unchanged. Contained types are not additional allocations.

Startup shows S3 internal free 116,368/total 171,276, minimum
115,688, PSRAM free 1,428,324, C5 internal free 72,532, and ten
S3 tasks. C5 is online at 40 MHz, SD is mounted, no prior reset fault is
recorded, and all 25 Remote ID fields plus four Wardriving hint counters are
idle/zero. This verifies startup only. Physical saved CSV, row counters,
repeated Stop/restart, Analyzer accuracy and USB/remount cache behavior remain
pending. Remote ID rev72/history, prior owner evidence and all unresolved
Wi-Fi action reports retain their existing scope; full Marauder parity is open.

The owner deferred the earlier Follow test while rev72 was installed; the
[separate deferral record](../../integration/device-test-2026-10-27/rev73-owner-follow-deferral.json)
does not change pending Follow/motion acceptance or the earlier owner-reported
basic sweep/Stop evidence.

## 2026-10-30 - rev74 installed: shared selected-vendor cache

Wi-Fi and BLE detail pages now use one file-scope cache in `ui.c`. The key
retains exact selected bytes and distinct Wi-Fi/BLE-public/BLE-public-identity
sources; only the cold lookup MAC is normalized to registry order. Healthy
repeated Wi-Fi detail draws avoid repeated recon reads. Both callers now use
zero-wait pre/post epoch guards, 30-second found/unknown reuse, stale-label
clearing and transient retry. Private/local selections do not access SD.
Cold lookup, storage implementation, labels, radio scheduling, scan selection
and active paths remain unchanged.

Independent source/fixture review accepted the focused 31,342-check suite.
It extracts the actual helper, both caller/label blocks and storage guard,
covering cross-radio/type identity, cache lifetime, remount/USB, held-lock
ownership, failed lookup/draw and bounded text. The host cache remains 72 bytes;
frozen target measurement confirms the same size, zero static growth on both
chips and unchanged other measured types. The S3 application is 1,699,344 bytes
(+32). The [rev74 receipt](../../integration/device-test-2026-10-27/rev74-flash-receipt.json)
records all 52 host suites, five stable-input targets, matched-package/READY
checks, three S3 write hashes and successful application reset/startup for
`candidate-shared-vendor-cache`. Idle memory/task counts match rev73; the
40 MHz C5 link is online, SD mounted, no prior reset fault and receive
diagnostics closed/zero. C5 firmware 56 is retained, with the bridge build-only.
This optimization leaves all 95 mode statuses/mappings, prior owner evidence and unresolved regressions
unchanged. Physical detail transitions, SD recovery and latency remain pending,
as do rev73 Wardriving/Analyzer acceptance and the deferred Follow/motion test.
