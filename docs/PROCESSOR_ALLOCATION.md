# S3/C5 allocation for Marauder parity

Current validation is tracked in the [source-pinned ledger](MARAUDER_COVERAGE_LEDGER.md).
The revision-43 transmit results below are historical evidence, not a repair
of the later NO MEM, Association Sleep and Bad Message reports; those remain
unresolved.

For concurrent Wi-Fi and BLE discovery, the default allocation is C5 Wi-Fi
and S3 BLE, with S3 also owning presentation and SD analysis. Each chip has
one shared RF path: running Wi-Fi and BLE on the same chip divides its radio
time. C5 also switches between 2.4 and 5 GHz; it cannot receive both bands
at once. These hardware limits are documented in Espressif's
[RF coexistence guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-guides/coexist.html)
and [Wi-Fi FAQ](https://github.com/espressif/esp-faq/blob/master/docs/en/software-framework/wifi.rst).

A Wi-Fi-only profile could assign S3 to 2.4 GHz and C5 to 5 GHz, but that is
a separate unimplemented option requiring measurement against BLE availability,
channel coverage, memory, first-result latency and Stop latency. Using both
processors does not require both radios to transmit or scan the same protocol.
The allocation should follow the selected workflow and measured costs.

Installed rev80 adds AP/client header observations to the C5's existing
ordinary passive scan while S3 handles bounded readout, frozen selections,
analysis and SD reports. BLE keeps its independent S3 owner. This avoids a
second scan merely to inspect relationships observed during the first sweep.
Rows transfer incrementally, with exact sweep proof and explicit unavailable
states. The UI store uses 4,744 target bytes of PSRAM on demand; Wardriving's
workspace grows 2,248 bytes. Static internal RAM grows 128 bytes on S3 and
1,456 on C5, with no additional task. The paired update passed 79 host suites,
five target builds and nine write hashes. Physical response times remain
pending; see [ordinary AP/client observations](WIFI_RELATION_PARITY.md).

Previous rev79 retained the exact C5 firmware 59 images and improved ordinary
S3 BLE discovery. Active banks reach the UI during each finite sweep, with a
200 ms software refresh target, family admission before capacity, bounded
retained history and frozen painted selections. Optional C5 telemetry is
deferred while this list is open; each existing C5 session still owns cleanup.
The [rev79 receipt](../../integration/device-test-2026-10-27/rev79-flash-receipt.json)
binds 75 suites, five stable builds and three verified S3 writes. S3 static RAM
grows 80 bytes; C5 is unchanged. The 17,032-byte UI store and separate
1,764-byte focused binding cache use PSRAM on demand; contained history/bank
types are not additional allocations. Settled S3 free internal RAM is 116,192
bytes, with unchanged 1,428,324 free PSRAM bytes and ten tasks. Physical latency,
dense reception and repeated cleanup remain pending. See
[BLE receive semantics](BLE_RECEIVE_LIST_PARITY.md).

Previous rev78 adds Scan -> MAC MONITOR: C5 firmware 59 passively cycles
supported legal Wi-Fi channels while S3 receives BLE reports and owns the
combined inventory, sorting, selected details and controls. Both producers
reuse existing workers and allocate their collectors only when needed.
Opening the page does not start reception; Stop/Back keep state until both
reservations release. No GPS or movement inference is required or added.
See [MAC Monitor semantics](MAC_MONITOR_PARITY.md).

The [rev78 receipt](../../integration/device-test-2026-10-27/rev78-flash-receipt.json)
records 72 suites, five stable builds and nine verified paired writes. The
session-only S3 PSRAM store is 13,600 target bytes, and the C5 store is 2,632.
Static internal sections grow 48 and 280 bytes respectively. Settled S3 free
internal RAM is 116,272 bytes; PSRAM free remains 1,428,324 and task count ten.
C5 free internal RAM is 65,924 bytes, 288 below the rev77 startup baseline.
The measured idle costs are recorded separately from session memory and
unmeasured peak/performance behavior. Physical reception and repeated cleanup
acceptance remain pending.

Previous rev77 adds selected-AP CSI Motion Monitor. C5 firmware 58 captures
passively on the existing Wi-Fi worker and retains a leased 16-record ring;
S3 drains validated records, calibrates the original model and draws the UI.
Its BLE host remains independent. Opening the page does not start capture.
No new worker, GPS dependency, association or generated stimulus is added.
See [CSI semantics and acceptance limits](CSI_MOTION_PARITY.md).

The [rev77 receipt](../../integration/device-test-2026-10-27/rev77-flash-receipt.json)
records 65 suites, five stable builds and nine verified writes. The session-only
S3 PSRAM store is 4,040 target bytes; the C5 ring payload is 1,984. Static internal
sections grow 16 and 152 bytes respectively. Settled S3 free internal RAM is
116,320 bytes, PSRAM free remains 1,428,324, and task count remains ten. C5 free
internal RAM is 66,212 bytes, 5,264 below rev76 including the new runtime cost.
This adds capability at a measured idle-memory cost; peak capture memory and
sustained performance remain pending physical measurement.

Previous rev76 keeps that same C5 firmware 57 and adds S3 BLE retention and
generic connection recovery. The existing 32-row PSRAM scan bank replaces
older original reports after validating a newcomer; the counter adds eight
target bytes including alignment. Completed selections remain independent of
later bank reuse. Explicit Cancel and Retry Close operate on the selected
typed peer through the existing BLE host. Aggregate diagnostics introduce no
task, host initialization or persistent buffer. C5 code and radio scheduling
are unchanged; see [BLE semantics](BLE_RECEIVE_LIST_PARITY.md).

The [rev76 receipt](../../integration/device-test-2026-10-27/rev76-flash-receipt.json)
records 56 host suites, five stable-input builds and three verified S3 writes.
Both chips' static section sizes are unchanged. S3 app size is 1,705,792 bytes
(+1,712); its PSRAM scan bank is 8,896 bytes (+8). Settled S3 free internal
memory remains 116,336 bytes, PSRAM free 1,428,324 and task count ten; C5 free
remains 71,476. The measured S3 internal low-water value is 115,524 bytes.
The first console sample preceded UI asset initialization and is retained
separately; its higher free PSRAM is not an optimization result. Physical
dense-scan and connection recovery acceptance remains pending.

Installed rev75: C5 firmware 57 adds a bounded passive probe-hint collector to
its existing Wi-Fi scan callback. S3 reads the immutable completed bank through
additive status/chunk commands, checks its full epoch and peer identity, and
writes distinct evidence rows through the existing Wardriving SD session.
The S3 BLE radio remains independently owned by the existing scanner; no new
worker or radio schedule is introduced. Probe staging/history lives in session
PSRAM; only the small C5 table and aggregate S3 status are resident. Explicit
per-exchange mutex release and Stop checks keep the SPI reader interruptible.
This is receive-side evidence support, not proof of association or verified
device identity. See [record semantics](WARDRIVE_WIGLE_FORMAT.md).

The [paired installation receipt](../../integration/device-test-2026-10-27/rev75-flash-receipt.json)
binds 54 host suites, five builds and nine verified image writes. The new C5
table is 1,056 target bytes. S3 session PSRAM grows by 1,716 bytes to 18,616;
static status grows by 24 to 360, while the complete S3 internal section delta
is 32 bytes. Idle S3 free internal memory is 116,336 bytes, PSRAM free remains
1,428,324, and C5 internal free is 71,476. S3 still has 10 tasks. These are
startup and layout measurements; prolonged two-radio performance is pending.

Installed rev74: the S3 UI shares the existing selected-vendor entry
between Wi-Fi and BLE instead of adding another cache. Exact selected bytes
plus a radio/type key prevent cross-source reuse, and only lookup arguments
are normalized. Warm draws use the existing zero-wait storage guard; cold
lookups retain the blocking recon path. No worker, scan schedule, allocation,
SD database copy or C5 production code changes. The focused host cache remains
72 bytes, equal to the former BLE-only entry. Frozen target measurement also
confirms 72 bytes, zero S3/C5 static delta and unchanged other measured types.
The S3 application is 1,699,344 bytes (+32). The
[rev74 receipt](../../integration/device-test-2026-10-27/rev74-flash-receipt.json)
records 52 host suites, five stable-input builds, matched-package/READY checks
and three verified S3 writes. C5 firmware 56 images remain exact, and the
bridge is build-only. Idle memory matches rev73: S3 free 116,368 of
171,276 internal bytes (minimum 115,688), PSRAM free 1,428,324, C5 free
72,532, and 10 S3 tasks. Startup shows the 40 MHz link online, SD mounted,
no prior reset fault and closed/zero receive diagnostics. Physical cache
transition/recovery/latency acceptance remains pending; mode coverage is unchanged.

Installed rev73: C5 keeps the verified firmware-56 Wi-Fi work while S3
adds Flock evidence to the existing Wardriving path. One scanner lock copies
the completed BLE bank with a 28-byte sidecar per row; staging and the
Stop/resume cursor live in the session's existing PSRAM workspace. Dedupe
entries retain an exact evidence key. No extra scanner task, resident snapshot
field or per-row allocation is added. Analyzer statistics use its existing
64 completed-bucket history; no second history bank is introduced. The BLE
vendor label cache adds mounted-session coherence without a database-wide
RAM copy. Its cache guards make zero-wait attempts on the existing storage
mutex, avoiding the resources-console hash wait without creating a mutex or
allocation; cold recon lookups retain their blocking behavior.

The [rev73 receipt](../../integration/device-test-2026-10-27/rev73-flash-receipt.json)
records 52 host suites, five stable-input builds, matched-package/READY checks
and three verified S3 writes. All C5 firmware-56 image hashes match rev72.
The bridge was built but not flashed; its rebuilt metadata changes hashes, so
rev72 write verification remains attached only to the historical bridge images.

Measured S3 static growth is +16 bytes; C5 static memory is unchanged. The
Wardriving PSRAM workspace grows 3,460 bytes, from
13,440 to 16,900. Its two contained dedupe banks each grow
1,280 bytes, the 32-row evidence stage adds 896 bytes, and the progress cursor
adds four bytes; these are included in the workspace total. Status grows from
320 to 336 bytes. Observation/dedupe-entry types are 80/28 bytes;
those type sizes must not be counted again as allocations. The vendor cache
stays 72 bytes, Analyzer/view stay 276/272 bytes, and the snapshot and Remote ID
workspace each stay 8,888 bytes. Window statistics use an eight-byte temporary
value, without persistent history growth.

Idle startup reports 116,368 of 171,276 internal S3 bytes free (minimum
115,688), 1,428,324 PSRAM bytes free, 72,532 C5 internal bytes free
and 10 S3 tasks. The 40 MHz link is online, SD is mounted, no prior reset
fault is recorded, and Remote ID/Wardriving hint diagnostics are idle/zero.
These are measured idle values, not peak-memory or live-feature acceptance.
Physical CSV/counters, Analyzer accuracy and cache transitions remain pending;
the owner deferred Follow/motion testing. Existing Wi-Fi action regressions
and full Marauder parity remain open.

Installed rev72: both receivers decode a bounded
eight-byte advertised-motion value and keep a separate Location callback time
inside their existing 12-row Watch collector. C5 exposes additive firmware-56
motion/timing queries; S3 obtains its local evidence under the existing scan
lock, validates both radios' paired data, and presents Motion beside Details.
The legacy 46-byte row, radio scheduling and worker count are unchanged.
The [rev72 receipt](../../integration/device-test-2026-10-27/rev72-flash-receipt.json)
verifies 38 stable-input host suites, five builds, the matched package and nine
bridge/C5/S3 write hashes. C5 firmware 56 is online at 40 MHz, SD mounted,
no previous reset fault, and all 25 closed Remote ID diagnostics are zero.

The [target layout](../../integration/device-test-2026-10-27/rev72-memory-layout.json)
measures each collector at 896 bytes (+200), the S3 PSRAM snapshot at 8,888
(+200), and the separate on-demand Remote ID PSRAM store at 8,888 (+1,544).
The latter contains inventory 4,016 (+1,216), view 816 (+192), render cache
3,424 (+136) and unchanged controller 280; selected records are inside the
inventory. These contained sizes are not extra allocations. Motion value/wire
types measure 8/22 bytes. Diagnostics remain 48 and the C5 planner 92 bytes.
S3 internal static growth is 336 bytes: selected Watch detail +8 and Watch
state +328. C5 static growth is 400 bytes from its two collectors; data
sections are unchanged. Ordinary AP and BLE focus/Monitor/Analyzer sizes
are unchanged. Raw S3/C5 application lengths are 1,696,240 (+4,704) and
1,627,120 (+2,048), distinct from padded C5 flash write counts.

Startup S3 internal free heap is 116,384 bytes (minimum 115,704), PSRAM free
1,428,324 and task count 10; C5 internal free is 72,532. This closed-page
baseline does not establish active peak use, reception or physical motion
accuracy. Motion/Location-age and prior Follow acceptance remain pending.
See the [comparison](../../integration/device-test-2026-10-27/rev72-candidate-comparison.json).

Installed rev71 adds explicit passive Wi-Fi source Follow to the
existing C5 worker while S3 retains UI/control and independent BLE sweeps.
C5 verifies the prior completed bank's exact source and callback proof, selects
only SDK-approved channels, and closes admission at the source-proof or window
deadline. The planner alternates target/background slots without replacing its
advancing background cursor. A small collector key protects only actually
received target evidence; it does not allocate another source bank.

S3 retains an eight-byte attempted-job source hint separately from the desired
selection. The existing value-only diagnostics distinguish enabled, requested
and bound profiles. The [rev71 receipt](../../integration/device-test-2026-10-27/rev71-flash-receipt.json)
verifies 37 stable-input host suites, five target builds, the matched package
and nine bridge/C5/S3 writes, with normal S3 restored. C5 firmware 55 is online
at 40 MHz SPI, SD is mounted and no previous reset fault is recorded.

The [target layout](../../integration/device-test-2026-10-27/rev71-memory-layout.json)
measures the on-demand Remote ID live store at 7,344 bytes (+24), including
the view at 624 (+8) and render cache at 3,288 (+8). Each Watch collector is
696 (+8); the S3 snapshot containing its collector is 8,688 (+8). Inventory,
selection, controller, BLE focus/Monitor/Analyzer and ordinary AP structures
are unchanged. Diagnostics remain 48 bytes and the C5 planner 92. Contained
sizes are not separate allocations. S3 internal static use is unchanged;
C5 grows 32 bytes. Raw S3/C5 application lengths are 1,691,536 (+2,240) and
1,625,072 (+2,448), distinct from padded flash write counts.

Startup S3 internal free heap is 116,720 bytes (minimum 116,040), PSRAM free
1,428,324 and task count 10; C5 internal free is 72,932. Remote ID was closed
with all 25 diagnostic fields zero. Idle heap does not establish active peak
use or received-source coverage. Physical Follow/SEARCH and concurrent-radio
acceptance remain pending. No new task, per-refresh allocation or active-engine
change is made. See the [comparison](../../integration/device-test-2026-10-27/rev71-candidate-comparison.json).

The installed rev70 baseline assigns the bounded Remote ID priority window to the
existing C5 scanner worker and leaves passive BLE sweeps on S3. The pure
channel planner and C5 receiver keep bounded static state; the S3 adapter adds
peer/job identity to the existing on-demand PSRAM page state. No new task,
per-refresh allocation or duplicate source bank is introduced. Exact job
leases, callback drain and SDK restoration define when C5 ownership ends;
expected-peer transport checks reject a detected change before control and
invalidate a reply if the peer changes after delivery.

The internal value-only diagnostics add job/channel/lease fields and still
copy one coherent snapshot without accessing UI pointers or issuing radio
commands. Focused host tests and independent reviews cover these boundaries.
The [rev70 receipt](../../integration/device-test-2026-10-27/rev70-flash-receipt.json)
verifies 37 stable-input host suites, five target builds, the matched package
and nine bridge/C5/S3 writes. Normal S3 was restored; C5 firmware 54 is online
at 40 MHz SPI, SD is mounted and no previous reset fault is reported.

Target layout measures the live PSRAM store at 7,320 bytes (+80), including
the controller at 280 (+16); inventory, selection, view and render cache sizes
are unchanged. Aggregate diagnostics are 48 bytes (+16), and the new C5 planner
type is 92 bytes. S3 internal static allocation grows 32 bytes and C5 grows 304.
The S3 app is 1,689,296 bytes (+2,624), and C5 is 1,622,624 (+6,704), using raw
image lengths rather than padded transport counts. Contained structures and
planner storage are already included in their enclosing totals.

Startup S3 internal free heap is 116,720 bytes (minimum 116,040), PSRAM free
1,428,324 and task count 10; C5 internal free is 72,964. Remote ID was closed
with all 22 diagnostic fields zero. Idle heap does not measure the lazy live
store's active peak, channel coverage or throughput. Physical priority
reception, selected updates and repeated cleanup/window acceptance remain
pending. See the [target layout](../../integration/device-test-2026-10-27/rev70-memory-layout.json),
[comparison](../../integration/device-test-2026-10-27/rev70-candidate-comparison.json)
and [installed contract](REMOTE_ID_PARITY.md).

At the historical rev69 checkpoint, per-source Remote ID callback-age evidence was added while
retaining the same C5 Wi-Fi and S3 BLE receive workers. Each producer keeps a
bounded twelve-row timestamp sidecar under its existing capture lock. C5
firmware 53 exposes a new read-only timing query; S3 validates two timing
replies around a row transfer and retains its own request-start anchor.
The local S3 BLE getter copies row/timing together under the scanner mutex.
Neither path compares the processors' clock origins or claims per-field RF age.

The dedicated on-demand PSRAM inventory, staging, selection and painted-copy
records now carry 24-byte timing metadata. There is no new task, per-refresh
allocation or radio schedule. Ordinary AP timing/scan views and the 46-byte
Watch wire row remain compatible; the ordinary Watch getter does not add
timing transactions. Individual host arithmetic, transport, inventory, session,
callback and UI checks, all 34 stable-input aggregate suites, all five target
builds and the matched package pass. The frozen target layout measures the
Remote ID live store at 7,240 bytes (+1,152), including inventory 2,800, controller
264, view 616 and render cache 3,280. The S3 BLE snapshot is 8,680 bytes (+120);
each Watch collector is 688 bytes (+116). The local timing record is 24 bytes
and its wire reply is 22. Existing ordinary scan/Monitor/Analyzer layouts and
32-byte diagnostics remain unchanged. Do not sum contained structures again
as separate allocations.

S3 internal static allocation has no net growth; C5 grows 264 bytes. The S3
application is 1,686,672 bytes (+3,024) and C5 is 1,615,920 (+1,728), using raw
image lengths rather than padded write counts. These target measurements are
not heap measurements. See the [target layout](../../integration/device-test-2026-10-27/rev69-memory-layout.json),
[comparison](../../integration/device-test-2026-10-27/rev69-candidate-comparison.json)
and [installed contract](REMOTE_ID_PARITY.md).

The [rev69 receipt](../../integration/device-test-2026-10-27/rev69-flash-receipt.json)
verifies nine bridge/C5/S3 writes and startup with C5 firmware 53 online at
40 MHz SPI, SD mounted and no previous reset fault. S3 internal free heap is
116,752 bytes (minimum 116,072), PSRAM free 1,428,324 and task count 10,
unchanged from rev68; C5 internal free is 73,268. Remote ID was closed with
zero aggregate diagnostics. These idle measurements exclude active use of the
lazy 7,240-byte live store and do not measure running-session peak use or
throughput. Rev69 physical reception and callback-age acceptance remain pending.

The owner's two successful basic Remote ID sweep/Stop runs were reported while
rev68 was installed, answering a prior rev67-worded prompt; the exact run image
is unverified. The console was already closed/zero, so it cannot corroborate
those counters. This report does not establish source decoding, timing or rev69
acceptance; see the [owner record](../../integration/device-test-2026-10-27/rev69-prior-owner-acceptance.json).

Rev68 installs paired `candidate-remote-id-framing` firmware with onboard C5
version 52. The existing shared receive parser now accepts legacy `90:3A:E6`
beacon framing and bounded variable NAN descriptors alongside the prior
formats. C5 supplies Wi-Fi reception and decoded Watch rows; S3 retains passive
BLE reception, independent sweep control, source validation and presentation.
Radio scheduling, UI, workers, heap banks, wire records and active engines are
unchanged. Optional NAN filters are bounded opaque bytes, not interpreted
matching/Bloom filters; reception remains opportunistic during normal scans.

All measured target layouts and S3/C5 internal static use are unchanged from
rev67. The complete on-demand PSRAM live store remains 6,088 bytes (inventory
1,888, controller 264, view 472, render cache 3,184), and the internal diagnostics
value remains 32 bytes. Startup S3 internal free is 116,752 bytes (minimum
116,072), PSRAM free 1,428,324, and C5 internal free 73,540. The S3 application
image remains 1,683,648 bytes; the C5 image grows 224 bytes to 1,614,192. These
are manifest image lengths, distinct from padded flash transport counts.
Closed-page heap figures do not establish running-session peak use.

All 31 host suites pass, including 171,310 receive checks and 3,313 checks of
the actual C5 callback/getters through S3 transport decoding into inventory.
Five stable-input targets, the matched package and nine bridge/C5/S3 write
hashes pass. Normal S3 firmware was restored after the temporary bridge. C5 52
is online at 40 MHz SPI, SD is mounted, no prior reset fault is recorded, and
Remote ID diagnostics are closed/zero. Physical reception of the added formats,
selected updates, repeated Stop/Back and concurrent-radio acceptance remain
pending. Dedicated NAN priority/continuous Target, per-field RF timing,
extended-service/fragments, receiver-relative navigation and logging remain
outside this increment. See the
[rev68 receipt](../../integration/device-test-2026-10-27/rev68-flash-receipt.json)
and [current Remote ID contract](REMOTE_ID_PARITY.md).

Rev67 installs S3 `candidate-remote-id-sweeps` and retains the verified onboard
C5 firmware 51 images from rev66. Scan → Remote ID uses the existing C5 passive
Wi-Fi worker and S3 passive BLE scanner, with two independent sweep controllers
on the existing UI task. S3 validates and presents the source banks, immutable
selection and separate latest matching evidence. There is no added radio
worker, RF schedule, decoder or Watch wire-format change. Ages describe S3
completed-sweep receipt, not RF or aircraft-position time. Idle READY and fully
released STOPPED sessions make no radio calls; the console copies a small
aggregate value without accessing the PSRAM UI object.

Frozen target layout measures the complete lazy PSRAM store at 6,088 bytes,
including the 1,888-byte inventory, 264-byte two-radio controller, 472-byte view
and 3,184-byte render cache. The diagnostics value is 32 bytes in internal
static memory. Total S3 internal static use grows 64 bytes over rev66; C5's
delta is zero. Existing BLE and normal scan data/view sizes remain unchanged.
Startup internal free is 116,752 bytes (minimum 116,072), PSRAM free 1,428,324,
and C5 internal free 73,540. The closed-page PSRAM baseline excludes the lazy
store and does not measure running-session peak use. C5 51 is online at
40 MHz SPI, SD is mounted, no prior reset fault is recorded, and Remote ID
diagnostics are closed/zero.

All 30 host suites, five stable-input target builds, the matched package and
three S3 write hashes pass. C5 image hashes remain identical to its verified
rev66 install; no C5 or bridge flash occurred at this checkpoint. Physical
Remote ID reception, selected updates, repeated Stop/Back and concurrent-radio
acceptance remain pending. The feature is partial, without upstream continuous
Target/NAN-priority reception, expanded decoding or logging. See the
[rev67 receipt](../../integration/device-test-2026-10-27/rev67-flash-receipt.json)
and [Remote ID contract](REMOTE_ID_PARITY.md).

Rev66 pairs S3 `candidate-wifi-source-age` with onboard C5 firmware 51. C5
records matching passive-scan beacon/probe-response tuples and their own
callback times; S3 transports that evidence, stages immutable AP rows/timing
in PSRAM and applies the normal-list WITHIN 30S option on the existing UI
tick. The 22-byte result metadata uses existing reserved space without another
SPI query. The source age dates matching frame presence, not the driver's
aggregate RSSI/security. Watch and explicit target selection stay unfiltered;
BLE scheduling, admission and active gates are unchanged.

The C5 64-entry table measures 3,608 target bytes with 56-byte entries; its net
internal static increase is 3,600 bytes. S3 internal static grows 32 bytes.
Two 32-row arrays of 16-byte AP timing records enlarge the existing PSRAM
display pool from 5,664 to 6,688 bytes (+1,024). Scan view/render cache remain
152/792 bytes, and BLE snapshot, focus, Monitor and Analyzer sizes do not
change. Fresh boot has ten tasks, 116,816 internal bytes free (minimum
116,136), 1,428,324 PSRAM bytes free, and C5 internal free 73,540. The lazy
display pool's growth is not visible in the idle PSRAM figure. Mounted SD,
40 MHz link and no prior reset fault are confirmed; `wifi_seen=not_complete`
is expected with no completed scan. All 23 host suites, five stable-input
builds, matched package and nine bridge/C5/S3 write hashes pass. RF, touch,
expiration and concurrent-operation acceptance remain pending. See the
[rev66 receipt](../../integration/device-test-2026-10-27/rev66-flash-receipt.json)
and [AP receive-list contract](WIFI_RECEIVE_LIST_PARITY.md).

Rev65 adds the Find My/iBeacon source-age list option on S3 using the existing
UI tick and immutable scan bank. C5 Wi-Fi images remain byte-identical. A
32-bit visibility mask and a 64-bit publication expiry ceiling require no
additional radio work, task, per-row metadata, SD lookup or heap bank. The
target scan view grows four bytes to 152, and its render cache grows 16 bytes
to 792; total internal static use grows 48 bytes. Snapshot, Monitor and Analyzer
sizes remain unchanged from rev64. Fresh boot has ten tasks, 116,848 internal
bytes free and 1,428,324 PSRAM bytes free. All 18 host suites, five target
builds, matched package, S3 flash hashes and boot checks pass. Physical filter
behavior, repeated transitions and concurrent radio acceptance remain pending.
See the [rev65 receipt](../../integration/device-test-2026-10-27/rev65-flash-receipt.json).

Rev64 keeps receive classification and signal handling on S3, with unchanged
C5 Wi-Fi images and scheduling. Exact HC ADV/RSP codes fit the existing 64-byte
provenance record; removing the redundant HC text pointer saves four target
bytes per row / 128 per 32-row PSRAM bank. Target focus/record sizes are 112/220,
snapshot 8,560, Monitor 7,304 and frozen row 228. Analyzer core/view stay 276/272.
Internal static allocation falls 16 bytes; fresh-boot internal free rises to
116,896, with 1,428,324 PSRAM bytes free and ten tasks. The shared validity
predicate preserves existing list/tracker thresholds while fixing scan peak
recovery and unavailable display. No worker, raw buffer, SD lookup or SPI
command was added. Host/review/render/build/flash/boot checks pass; physical
RF, repeated transitions and concurrent-operation acceptance remain pending.
See the [rev64 receipt](../../integration/device-test-2026-10-27/rev64-flash-receipt.json).

Rev63 adds independent Flock receive evidence on the existing S3 BLE path;
C5 retains its Wi-Fi role with byte-identical firmware images. Shared provenance
grows from 56 to 64 bytes. Target focus/record sizes are 116/224 bytes,
snapshot 8,688, Monitor 7,432 and frozen row 232: eight additional bytes per
row, or 256 per 32-row PSRAM bank. Analyzer core/view grow eight bytes each
to 276/272. Internal static allocation grows 16 bytes; fresh-boot internal
free is 116,880, PSRAM free 1,428,324 and task count ten. The fixed 27-prefix
compatibility lookup uses 81 bytes of constant data and performs no SD access;
the SD recon database continues to provide separate vendor context. No worker
or raw response buffer was added. Host/review/render/build/flash/boot checks
pass; current-build RF, repeated transitions and concurrency remain unverified.
See the [rev63 receipt](../../integration/device-test-2026-10-27/rev63-flash-receipt.json).

Rev62 keeps iBeacon receive parsing and analysis on S3 while C5 retains its
independent Wi-Fi role. The shared provenance record stays 56 bytes. Latest
matching iBeacon source time/sequence add eight bytes per row / 256 per
32-row PSRAM bank: target snapshot 8,432 bytes, Monitor 7,176 and frozen row
224. Analyzer core/view grow 24 bytes to 268/264. Total internal static use
grows 16 bytes; fresh-boot internal free is 116,896 and PSRAM free 1,428,324,
with ten tasks. No radio worker or second raw buffer was added. C5 images and
its 77,140-byte free heap are unchanged. Host/review/render/build/flash/boot
checks pass; current-build RF/transition/concurrency acceptance is pending.
See the [rev62 receipt](../../integration/device-test-2026-10-27/rev62-flash-receipt.json).

Rev61 retains the S3 BLE / C5 Wi-Fi allocation while adding exact Meta
identifier evidence to the shared source record. Target sizes are 56 bytes
for provenance, 8,176 for the snapshot, 6,920 for Monitor and 216 per frozen
row: 20 bytes per row / 640 per 32-row PSRAM bank above rev60. Adjacent Boolean
bitfields eliminate four bytes of avoidable row padding (128 per bank) from
the first candidate. Analyzer core/view grow 12 bytes each to 244/240. Total
internal static use grows 48 bytes; fresh-boot internal free is 116,912,
PSRAM free 1,428,324, and task count ten. C5 images and its 77,140-byte free
heap are unchanged. No radio worker or response-payload buffer was added.
Host/review/render/build/flash/boot checks pass; current-build RF, transitions
and concurrent-operation acceptance remain pending. See the
[rev61 receipt](../../integration/device-test-2026-10-27/rev61-flash-receipt.json).

Rev60 reuses that S3 BLE source record for Flipper discovery. Exact frozen
target sizes are 36 bytes for shared provenance, 7,536 for the snapshot,
6,280 for Monitor and 196 per frozen row: four bytes more per row, or 128
per 32-row bank, than rev59. Static internal allocation and fresh-boot free
memory are unchanged; no radio task, second receiver or raw response buffer
was added. C5 images are unchanged. Source/host/build/boot checks pass,
while rev60 RF/Fox Hunt/concurrency acceptance remains pending. See the
[rev60 receipt](../../integration/device-test-2026-10-27/rev60-flash-receipt.json).

After the owner's successful rev59 Find My/Monitor Stop/Back test, the scanner
was idle with its BLE host released from ownership but still initialized.
Internal free was 58,600 bytes, minimum 58,520, largest block 31,744; PSRAM free
was 1,410,264 and there were 12 tasks. This post-use baseline is distinct from
fresh-boot figures and does not diagnose a leak or establish a repair in any
other mode. Evidence is retained in the rev59 receipt and its console log.

Rev59 adds Find My discovery evidence on the existing S3 BLE scanner. C5 50's
images are unchanged and its independent dual-band Wi-Fi role is retained;
this patch does not add a second BLE receiver or establish new concurrency
measurements. The 32-byte source/provenance record adds 1,028 bytes to the lazy
S3 snapshot in PSRAM and 1,024 bytes to each 32-row monitor/frozen-list bank.
Target sizes are 7,408 bytes for the scanner snapshot, 6,152 for Monitor and
192 per frozen row. Internal static use grows 64 bytes; idle internal free is
116,960 bytes, with 1,428,324 PSRAM bytes free before BLE initialization.
Source and host validation pass; current-build RF/transition/concurrency
acceptance remains pending. See the
[rev59 receipt](../../integration/device-test-2026-10-27/rev59-flash-receipt.json).

Rev58's passive beacon path assigns dual-band reception and complete-frame
transport to C5 50, while S3 performs MultiSSID/PineScan analysis, immutable
evidence presentation and optional SD PCAP recording. Both analyses share one
session manager, mutex, lazy 6,648-byte PSRAM evidence bank and capture worker.
PineScan adds 304 internal static bytes and a separately lazy 1,584-byte UI
store. Idle S3 internal free heap is 117,024 bytes; C5 is 77,140 bytes. Builds,
host checks and boot pass; live reception/Follow/PCAP content remain pending.
These measured allocations do not establish RF throughput or full parity.

Reference hardware: measured S3 N16R2 (16 MB flash, 2 MB PSRAM), onboard C5 with
8 MB physical flash and the existing 2 MB partition layout. Flash capacity does
not enlarge internal RAM or create a second RF chain. The S3 supports 2.4 GHz
Wi-Fi and BLE; the C5 supports 2.4/5 GHz Wi-Fi, BLE and 802.15.4. See Espressif's
[S3](https://www.espressif.com/en/products/socs/esp32-s3) and
[C5](https://www.espressif.com/en/products/socs/esp32-c5) product specifications.

## Measured starting point, 2026-10-28

`../../integration/device-test-2026-10-27/ap-menu-before.txt` records the attached
unit: S3 internal free 128,820 bytes, lifetime minimum 128,052; C5 free/minimum
69/66 KiB, firmware 33. Both Radio Lab jobs remained zero. Earlier connection
and TCP-scan testing reached a C5 lifetime minimum of 10 KiB, recovering after
the job. A lifetime minimum does not identify which operation caused it.

The new read-only USB `memory` command reports S3 internal and PSRAM totals,
free memory, lifetime minima and largest free blocks, plus C5 free/minimum and
transport status. C5 largest-block telemetry is unavailable in revision 33.
This makes allocation/fragmentation visible without starting any radio work.

## Placement decisions

Owner requirement reaffirmed on 2026-10-29: both S3 and C5 must contribute to
each function. Treat each feature as a distributed workflow, with explicit
processor responsibilities and measured memory/transport costs. UI alone does
not establish dual-radio optimization. Where a second receiver/advertiser can
improve the function, add that role under a common S3 radio owner and verify
concurrency. A 5 GHz or 802.15.4 workflow still depends on C5 RF; S3 contributes
control, buffering, interpretation and storage because its radio cannot cover
those bands/protocols.

| Workflow | S3 contribution now | C5 contribution now | Additional dual-radio work |
| --- | --- | --- | --- |
| AP/station discovery | Stable 32-AP selection, per-AP 32-client picks, direction/RSSI interpretation, CSV/SD | Dual-band reception and lazy 32-AP × 32-client evidence cache | S3 2.4 GHz reception alongside C5 scanning pending |
| Passive Remote ID sweeps / Follow | Passive BLE receive path, independent sweep controllers, exact peer/job and attempted-source control, validated PSRAM banks, immutable selection and callback/receipt-age evidence | Existing dual-band worker runs bounded priority windows; firmware 55 adds source-proven target/background Follow or explicit SEARCH with callback deadline enforcement | Rev71 build/install/startup verified; physical Follow/SEARCH, source updates, concurrent-radio progress and cleanup pending. Full upstream continuous Target behavior remains incomplete. |
| Selected-AP deauthentication | Frozen Broadcast/All/Picked profiles, incremental preflight, job control, independent 2.4 GHz beacon/frame counters and reporting | Snapshot/client validation, channel setup, timed aggregate list TX and cleanup | Owner-stopped channel-10 job accepted 29 frames with 13 S3 matches; full target/client effects and 5 GHz reception remain unverified |
| AP-directed Probe Requests | Same selection/control/report flow and 2.4 GHz receiver with probe filtering | Locked SSID snapshot, per-frame source MAC, directed probe construction, vendor TX and timed cleanup | Channel-10 job completed with 601 accepted frames and 601 S3 matches; upstream broadcast variant remains pending |
| Bad Message / Association Sleep | Per-AP All/Picked client profiles, job readiness/stop, 2.4 GHz frame classification and SD report | Locked AP/auth snapshot, every-client evidence validation, bounded aggregate EAPOL or association TX | Native checks/build/boot pass; live reception/effects pending |
| Channel Switch / Quiet Time | AP identity, orchestration, independent 2.4 GHz announcement counters and report | Bounded beacon capability read, announcement construction/TX and restore | Both completed separately with 600 accepted frames and 600 S3 matches; client effects remain unverified |
| AP Clone Spam | Stable local clone BSSID per selected AP, frozen multi-AP batch identity, 2.4 GHz receiver and report | Real AP beacon capability read, bounded local clone beacon TX and cleanup | Channel-10 job completed with 600 accepted frames and 600 S3 matches; upstream per-beacon identity variant remains pending |
| Connected network discovery | Scan configuration, result decoding, paging, CSV/SD | Associated IP session, TCP/ICMP/ARP work and cancellation | Moving sockets across chips would require a new data plane; no benefit claimed yet |
| BLE advertising | Existing lazy advertising host and job UI | Existing independent scan/GATT services | Joint TX/RX session orchestration and scheduling pending |
| 802.15.4 survey | Survey controls, interpretation and reports | Exclusive energy/beacon/activity worker | S3 has no 802.15.4 RF; it remains the processing/storage partner |

The S3 web AP and passive receiver now share `s3_wifi_owner.c`. Only the claimed
owner may initialize the driver; common netif/event-loop facilities survive
driver teardown. Cleanup uncertainty retains ownership until recovery/reset.
The receiver uses one on-demand 4,096-byte task, bounded Wi-Fi buffers and a
64 KiB pre-start heap floor. Active BLE advertising and passive Wi-Fi reception
exclude each other. Its lazy NimBLE host still consumes memory after first use.
Live receiver operation and peak heap remain unverified on the physical board.

Revision 42's verified idle boot has 126,996 bytes S3 internal free, 1,428,324
bytes PSRAM free, ten S3 tasks and 83,260 bytes C5 internal free. Static use is
181,280/66,408 bytes (S3/C5). The C5 evidence cache allocates only during AP
station captures, with an internal-heap guard; the S3 target, batch and report
buffers prefer PSRAM and have checked allocation failure paths. Its 2.4 GHz
receiver accompanies C5 AP jobs, while S3 controls/storage remain active for
5 GHz C5 jobs. These are boot and host measurements, not live RF throughput or
touch acceptance. See `../../integration/device-test-2026-10-27/multi-station-verification.json`.

Revision 43 removes an invalid runtime `esp_wifi_config_80211_tx_rate` call.
The installed IDF requires it before `esp_wifi_start`; selected-AP jobs had
failed in about 8 ms with `ESP_FAIL` before one accepted frame. The new paired
build boots with 126,948 S3 internal free bytes and 81 KiB C5 idle free. Live
Probe Request, Deauth, Channel Switch, Quiet Time and AP Clone Spam jobs now have separate
C5 driver and S3 receiver counts, recorded in
`../../integration/device-test-2026-10-27/attack-fix-verification.json`.
The S3 handles selection, receive classification, display and storage while
the C5 handles selected-AP RF. No client effect or comparative throughput has
been measured.

The following table records the revision-43 placement baseline; later receive
work and current Remote ID allocation are described above.

| Responsibility | Processor | Reason and state at that checkpoint |
| --- | --- | --- |
| Display, touch, target profiles and job control | S3 | Existing UI; keep allocations and heavy rendering away from radio callbacks |
| Vendor/detection interpretation, reports, PCAP encoding and SD writes | S3 | PSRAM and direct SD access; captures still require a bounded queue/transport implementation |
| 2.4/5 GHz AP/station observations and target Wi-Fi operations | C5 | Only onboard processor with 5 GHz; targeted station capture reuses the Wi-Fi scan worker and callback with a 32-row table; no extra resident stack |
| BLE advertising | S3 | Existing lazy NimBLE broadcaster; frees C5 radio airtime, but a second host consumes S3 internal RAM |
| BLE scan/GATT | C5 currently; evaluate S3 migration | Moving requires a complete shared S3 BLE host and coexistence with local web Wi-Fi, not a second independent host |
| 802.15.4 observations | C5 | S3 lacks this radio; mutually exclusive jobs should share worker storage |
| Connected TCP/service, ICMP and ARP discovery | C5 | One existing on-demand 5,120-byte worker; sockets and ARP cache belong to its IP stack. ICMP uses one socket; ARP dispatches bounded operations to the lwIP core. No extra resident workers |
| Local browser interface | S3 | Existing independent AP/HTTP service; shares S3 RF time with BLE |
| Selected-AP deauthentication | C5 | Shares the existing on-demand 4,096-byte transmit worker and ownership bit with beacon tests; no new resident stack. S3 resolves target identity, handles controls and writes CSV |
| Expansion scanning | Expansion C5s, if present | Independent RF hardware, coordinated by S3; never assume optional modules are fitted |

## Work sequence

1. Correct AP selection and build a source-pinned feature ledger. Distinguish
   unavailable engines from functioning actions.
2. Reduce avoidable resident C5 task stacks. There are three independent
   4,096-byte 802.15.4 workers despite exclusive modes: one worker can reclaim
   8,192 bytes plus task control blocks if lifecycle tests confirm correctness.
   Wi-Fi scan/defense have two more resident 4,096-byte workers; evaluate their
   callbacks and cancellation before sharing them.
3. Keep S3 bulk UI inventories and capture queues in explicitly allocated PSRAM
   when available. Keep DMA buffers, radio callbacks and required task stacks in
   internal RAM. Preserve an allocation-failure fallback.
4. Add bounded C5 frame transport with explicit target/job/generation, truncation
   counters, stop acknowledgement and a control lease. S3 batches SD writes;
   C5 callbacks must not block on SPI, rendering or filesystem access.
5. Port each feature onto these services. Share parsers and frame construction
   where appropriate; preserve upstream MIT notices for copied code. Verify
   driver return codes, stop/error paths and receiver observations separately.

## Driver constraint

The pinned [IDF 6.0.1 C5 transmit API](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c5/api-reference/network/esp_wifi.html#_CPPv417esp_wifi_80211_tx16wifi_interface_tPKvib)
does not establish support for all Marauder management-frame modes. Upstream
defines `ieee80211_raw_frame_sanity_check`; the current C5 driver also supplies
a strong definition in the TX object. Backend linking and over-the-air results
must be established per toolchain. Menu parity cannot resolve that constraint.

The revision-38 source prepares a build-local copy of the exact pinned C5
archive, weakens only that symbol and preserves its original implementation
under an alias. A narrow adapter permits only the exclusive worker's current
26-byte deauthentication buffer; all ordinary calls retain vendor validation.
The installed SDK is not modified and global multiple-definition flags are not
used. The preparation refuses a different source archive hash. The final linked
call path is checked in the build receipt, and the revision-43 Deauth job has
live driver acceptance and S3 matching-frame evidence. These observations do
not measure whether a target client disconnected.

## First measured implementation

The shared 802.15.4 worker in C5 revision 34 passed deterministic host lifecycle
tests and was flashed with verified hashes. The paired idle diagnostic snapshot
rose from 75,612 to 84,492 free C5 bytes: 8,880 bytes reclaimed. S3 memory stayed
unchanged at this checkpoint. See `MARAUDER_PORT_WORKLOG.md` for logs and the
remaining physical reception checks. No full dual-radio concurrency improvement
is claimed. Measure each later implementation rather than counting estimates.

Revision 37 adds a bounded station table while reusing the Wi-Fi scan worker.
C5 free heap is now 83,740 bytes, 752 below revision 36 and 8,128 above the
75,612-byte pre-consolidation snapshot. S3 internal free heap is 127,892 bytes;
PSRAM remains 1,428,324 free. These idle measurements do not establish peak
capture memory, reception rate or simultaneous-radio performance.

## Banshee architecture reference, reviewed 2026-10-29

The owner requested reference research without copying Banshee/GhostESP code.
Only their published architecture and hardware documentation was consulted;
no GhostESP source, firmware, protocol implementation or UI was imported.
[Banshee documentation](https://banshee.ghostesp.net/banshee/) assigns its display
to C5 and compute/I/O to S3. Its [GhostLink documentation](https://docs.ghostesp.net/latest/getting-started/dual-communication/)
describes UART command forwarding, reconnect and separate radio jobs, with
Banshee templates using 921,600 baud. These are useful requirements references,
not a reason to change Cerberus's board wiring or install GhostESP.

Cerberus retains S3 display/storage/control and C5 dual-band RF over the existing
40 MHz SPI link. Link clock is not an end-to-end throughput measurement. The
[vendor's Banshee specification](https://www.wiredhatters.com/product-page/the-banshee)
names C5 N8R8 and S3 N16R8 modules; Cerberus's measured S3 is N16R2. Memory and
peripheral assumptions must therefore come from this unit, not Banshee presets.

Independent design direction and acceptance criteria:

- Every feature must name its S3 and C5 responsibilities. On 2.4 GHz, use a
  second receiver when useful; on 5 GHz/802.15.4, use S3 for interpretation,
  buffering, control and storage. Do not claim nonexistent S3 radio coverage.
- Maintain one target identity and job lifecycle across processors. A link
  reconnect must not silently restart a completed or cancelled transmission.
- Reserve radio ownership before starting; receiver readiness precedes TX.
  A lost lease, leaving the view or a failed required partner cancels the job.
- Keep bounded callback queues separate from storage/rendering. Report queue
  drops, driver acceptance and independent reception separately.
- A persistent web AP and an independent passive S3 receiver compete for one
  radio. Expose the supported role choice rather than promising both on
  different channels at once. Current AP jobs require the receiver owner.
- Benchmark before claiming superiority: scan completeness/time, selection
  latency, receive/drop counts, peak heap, UI responsiveness and stop latency.
  An additional menu item or a faster nominal bus is not such a benchmark.

Revision 40 adds Bad Message, Association Sleep, Channel Switch and Quiet Time
to the shared C5 worker and S3 orchestration/receiver/report path. No additional
resident worker is introduced. The S3 filter includes data frames only for Bad
Message; other AP jobs retain management-only receive filtering. C5 briefly
listens for the chosen AP's beacon before Channel Switch/Quiet transmission,
with a two-second timeout and cleanup ownership checks. Live RF verification
and comparative benchmarks remain pending.

Revision 41 adds S3-owned multi-AP selection, immutable batch copies, inventory
resolution, per-target progress and CSV storage. C5 exposes a read-only target
validation command and retains up to 64 station observations across captures,
scoped to AP/channel/client and aged out after 30 minutes. The cache consumes
bounded static memory and reuses the existing capture lock and callback.
The target collection, batch rows, temporary inventory and larger report
buffer prefer PSRAM. No additional resident task is introduced. At most one
C5 job and one S3 receiver are active; each must release ownership before the
next target. S3 still cannot receive 5 GHz, so those jobs use its orchestration
and reporting roles. Build/installation and live acceptance are tracked in the
worklog rather than inferred from these source-level assignments.

The later wardriving merge remains part of the main application. With no GPS
currently fitted, both processors must still support discovery and unlocated
session records; GPS availability and fix freshness must be explicit. The full
preserved-source scope and phase order are recorded in `../TODO.md`.
