# Remote ID receive workflow: installed rev72 checkpoint

Installed 2026-10-30: `candidate-remote-id-motion` adds advertised speed,
direction and height/reference to **Scan → Remote ID → source → MOTION**,
alongside existing passive Wi-Fi Follow and independent S3 BLE sweeps.
The [rev72 receipt](../../integration/device-test-2026-10-27/rev72-flash-receipt.json)
records 38 passing host suites, five stable-input builds, a matched package,
nine verified bridge/C5/S3 writes and successful startup with C5 firmware 56.
The temporary bridge was replaced by the normal S3 application after C5 flashing.
Remote ID remains **partial**: physical motion values, independent Location-age
display, unavailable replacement and repeated Motion/Details/Follow/Stop/Back
acceptance remain pending. Prior Follow/SEARCH acceptance and full Marauder
Remote ID parity also remain open.

## Installed rev72: advertised motion with its own Location age

The receive path now preserves horizontal speed, direction and height/reference
from each supported Location message. The existing selected-source detail has
a **MOTION** panel; **DETAILS** or Back returns without changing radio work.
Values are advertised claims. Speed uses exact quarter-m/s units, direction
retains a valid advertised 360 degrees, and height distinguishes above ground
from takeoff. Unavailable components are separate from valid zero values.
These rules were checked against pinned [OpenDroneID source](https://github.com/opendroneid/opendroneid-core-c/blob/849c03adbe0cdd68997a462fafab397edcfe53f3/libopendroneid/opendroneid.c)
and [field definitions](https://github.com/opendroneid/opendroneid-core-c/blob/849c03adbe0cdd68997a462fafab397edcfe53f3/libopendroneid/opendroneid.h);
the [rev72 reference index](../../integration/device-test-2026-10-27/rev72-reference-sources.json)
binds the snapshots and their hashes. No third-party implementation was imported.

Each new Location atomically replaces coordinates, altitude and motion, including
unavailable fields. Basic ID-only reports retain that group without refreshing
its own callback time. Changed claimed ID, source eviction and new captures
clear it. Location and general source callback bounds remain separate from
receipt times and RF time; a backward clock cannot make either fresh.
Firmware-56 commands 94/95 add bounded values/timing alongside the unchanged
46-byte Watch row. Older peers expose unavailable extras; supported-peer
failures cannot silently publish a legacy fallback. Frozen selection remains
immutable while matching updates replace the separate latest record.

The new `check_watch_motion.py` passes 1,064,928 focused checks and the existing
receive suite passes 171,310. Production UI and live-adapter fixtures pass
5,977 and 30,839 checks. All [38 aggregate suites](../../integration/device-test-2026-10-27/rev72-host-checks.json),
five builds and the matched package pass with stable inputs; the S3 READY
ISR gate and all nine installation write hashes pass.

The [measured target layout](../../integration/device-test-2026-10-27/rev72-memory-layout.json)
records an 8-byte value, 22-byte wire payload and 896-byte collector (+200).
The S3 PSRAM snapshot is 8,888 bytes (+200); the separate on-demand Remote ID
store is 8,888 (+1,544), containing inventory, selection, controller and render
state. Internal static growth is S3 +336 and C5 +400 bytes; contained type
sizes must not be added again as allocations. Startup records S3 internal free
116,384 bytes (minimum 115,704), PSRAM free 1,428,324, C5 internal free 72,532
and 10 S3 tasks. C5 firmware 56 is online at 40 MHz, SD mounted, no previous
reset fault, and all 25 closed Remote ID diagnostic fields are zero. These
idle checks do not establish reception or physical motion accuracy.

Both Remote ID modes remain partial. Vertical speed, status,
authentication/operator presentation and the other gaps below remain open.

## Installed rev71: explicit passive source Follow

The installed increment adds **FOLLOW / UNFOLLOW** to a selected Wi-Fi
Remote ID detail. Merely opening a detail starts no radio work. Explicit Follow
can start the existing passive two-radio session; changes made during an owned
window apply to the next Wi-Fi job. The frozen selected evidence remains
immutable. A separate attempted-job source prevents a different selected
transmitter from being presented as the current in-flight target. Stop and
cleanup messages take precedence over active Follow text.

Firmware 55 reuses the 22-byte START/status formats. Generic firmware-54
requests keep their original zero fields. A Follow request identifies an exact
beacon or NAN transport and transmitter MAC, not a claimed UAS ID. C5 binds a
channel hint only to its prior DATA_READY bank's matching private epoch,
generation, source, channel and valid 64-bit qualifying callback. The S3's
conservative age check supplies a hint, not authority over the C5 clock.

The approved-channel planner keeps 6/149/6 priority slots and alternates the
fourth slot between the proven target channel and advancing background
coverage. Missing, expired or SDK-unaccepted proof yields explicit **SEARCH**
before the first receive admission. Bound callback admission ends at the earlier
of the requested deadline and the original callback plus 30 seconds, even
before the worker runs again; SDK disable and cleanup may finish later.
Renewals, new reports
and repeated START do not extend or rebind that job. Retained BOUND status is a
profile fact, not a claim that reception remains active during cleanup.

The collector never seeds a selected row. If a full bank actually receives the
exact selected source, it replaces the last non-target row and clears that
row's old payload and timing. The overflow counter describes a discarded
record, not lost RF packets. Source identity remains transport plus address;
equal advertised IDs do not merge sources. Independent S3 BLE sweeps, source
callback bounds, receipt times, exact peer/job control and DATA_READY checks
remain in place. Desired Follow cannot silently become generic operation on
an older peer; Unfollow remains available during downgrade or cleanup.

Focused host evidence passes: transport 90,942 checks, planner
32,661, production C5 service 12,236, collector timing 209, inventory 1,844,
diagnostics 56 plus 100,000 concurrent reads, renderer 4,316 and actual live
adapter 27,382. Review fixes cover desired versus in-flight identity and
retained cleanup status. All [37 aggregate host suites](../../integration/device-test-2026-10-27/rev71-host-checks.json),
five target builds and the matched package pass with stable inputs. Paired
installation and startup pass; physical Follow/SEARCH, protected capacity,
selected updates and repeated Stop/Back/Unfollow acceptance remain pending.

The [target layout](../../integration/device-test-2026-10-27/rev71-memory-layout.json)
and [comparison](../../integration/device-test-2026-10-27/rev71-candidate-comparison.json)
measure these changes from rev70:

| Target structure or image | Rev71 bytes | Change |
| --- | ---: | ---: |
| S3 Remote ID live PSRAM store | 7,344 | +24 |
| Contained view / render cache | 624 / 3,288 | +8 / +8 |
| Watch collector, each processor | 696 | +8 |
| S3 BLE snapshot containing its collector | 8,688 | +8 |
| Internal aggregate diagnostics | 48 | 0 |
| C5 channel planner | 92 | 0 |
| S3 application image | 1,691,536 | +2,240 |
| C5 application image | 1,625,072 | +2,448 |

S3 internal static allocation is unchanged; C5 grows 32 bytes. Inventory,
selection, controller, ordinary AP and BLE focus/Monitor/Analyzer layouts are
unchanged. Contained types are already included in their enclosing allocations;
do not sum them again. Image lengths are raw files, not padded flash counts.

Startup reports C5 firmware 55 online at 40 MHz SPI, mounted SD and no previous
reset fault. S3 internal free heap is 116,720 bytes (minimum 116,040), PSRAM
free 1,428,324 and task count 10; C5 internal free is 72,932. Remote ID was
closed with all 25 diagnostics zero. This idle baseline does not establish
active peak memory, received sources or channel coverage. The frozen rev70
pair is retained for rollback; NVS, PHY and SD were preserved. Expansion
targets were built, not flashed. S3 application SHA-256 is
`a54dae601b6fd8840f32e5f3694dab6e86825a7bb43421cb122f4b399e0c8771`;
C5 application SHA-256 is
`38fc427b313d5713b730579d077643b25e69b95c7535b942006392b99f8914a7`.

This remains a partial Remote ID port. Full Marauder Target behavior, complete
operator fields, remaining receive formats, coded-PHY hardware acceptance,
GPS/range, maps and export are still open. No new worker, receiver GPS
requirement, active engine, GATT or sound workflow is introduced.

## Rev70 installation record: bounded Wi-Fi priority reception (historical baseline)

The installed `candidate-remote-id-priority` increment keeps **Scan → Remote ID**
and its independent S3 BLE sweeps, but gives supported onboard C5 firmware 54
a dedicated passive Wi-Fi receive window on the existing scanner worker.
The adapter requests 8-second windows with a 3-second renewable lease and
250 ms channel dwells. The bounded planner interleaves approved channels as
6, 149, 6, then the next background channel; unavailable priorities are omitted.
The background cursor persists across windows. Channels must pass the configured
country/SDK checks, and RX admission is closed during probing and retuning.
A scheduled tune is not evidence that a transmitter was heard.

Commands 90–93 provide START, STATUS, STOP and RENEW with nonzero monotonic
64-bit job identities. Exact START/config retries preserve the existing job,
including terminal state; STATUS never renews a lease. STOP and renewal are
scoped to that ID. S3 binds each command to the expected peer under its SPI
mutex and rejects a detected peer change before delivery; a post-command change
invalidates the reply. Lost START replies
are resolved by the exact ID, not a guessed scan-generation byte. Renewal
ceases on Stop/Back and does not revive completed, expired or cleanup-only work.
Radio ownership remains held until callback drain and SDK restoration finish.

The existing Watch row/timing format is reused. DATA_READY, exact job identity,
completed-sweep status and peer checks guard publication before and after the
row transfer. Selected evidence remains frozen, while matching updates retain
their own callback-age bounds and receipt times. The read-only `remote_id`
console snapshot adds job state, channel, lease and validity fields without
starting RF work. Older supported Watch peers retain ordinary Wi-Fi sweeps;
an error from a supported priority service does not select that fallback.

Implementation and focused host evidence are available in the
[planner](../shared/remote_id_channel_plan.c),
[C5 service](../c5_firmware/main/wifi_remote_id_service.h),
[S3 transport](../s3_main/main/c5_remote_id_link.h) and
[live adapter](../s3_main/main/remote_id_live_ui.h). The planner suite passes
27,197 checks and transport passes 12,867; production C5 service passes 4,285,
controller 6,619 and live-route fixtures 15,391. These cover lifecycle and
publication failures. All [37 aggregate host suites](../../integration/device-test-2026-10-27/rev70-host-checks.json),
five target builds and the matched package pass with stable inputs. Paired
installation and startup are verified. Physical priority reception, selected
updates, lease cleanup and repeated windows remain pending.

The [target layout](../../integration/device-test-2026-10-27/rev70-memory-layout.json)
and [comparison](../../integration/device-test-2026-10-27/rev70-candidate-comparison.json)
measure these changes from rev69:

| Target structure or image | Rev70 bytes | Change |
| --- | ---: | ---: |
| S3 Remote ID live PSRAM store | 7,320 | +80 |
| Contained Remote ID controller | 280 | +16 |
| Internal aggregate diagnostics | 48 | +16 |
| C5 channel planner | 92 | New type |
| S3 application image | 1,689,296 | +2,624 |
| C5 application image | 1,622,624 | +6,704 |

S3 internal static allocation grows 32 bytes and C5 grows 304. Source banks,
selected evidence, render cache, BLE snapshot, Watch collector and ordinary AP
layouts are unchanged. Contained sizes and the planner's static storage are
already included in their enclosing allocations/section totals; do not add them
again. Raw image lengths differ from padded C5 flash transport counts.

Startup reports C5 firmware 54 online at 40 MHz SPI, SD mounted and no previous
reset fault. S3 internal free heap is 116,720 bytes (minimum 116,040), PSRAM
free is 1,428,324 and task count is 10; C5 internal free is 72,964. The Remote ID
page was closed with all 22 diagnostic fields zero. This is an idle baseline,
not running-session peak use, channel coverage or evidence of received sources.
The frozen rev69 pair is retained for rollback; NVS, PHY and SD were preserved.
Expansion targets were built, not flashed. S3 application SHA-256 is
`695d9feda9ce4672d4a0f64c0456e65b928f94edd666161318ae51407e1ecae7`;
C5 application SHA-256 is
`d19f8bfebd12affa57af10a816cca03d98e4e3b8b69d7ba7573bdf5451ac966f`.

No GPS, new worker or active-engine change is introduced. Source callback age
still does not establish per-field age, RF arrival time or authenticated
aircraft identity. Dedicated selected-target following, full field coverage,
operator presentation, maps and export remain incomplete; both upstream
Remote ID modes remain **partial**.

## Rev69 installation record: per-source callback age (historical)

The installed increment adds optional **RX CALLBACK AGE <=Ns** evidence to
the existing Remote ID list and **LAST MATCH RX CALLBACK AGE <=Ns** to selected
details. All 34 host suites, five stable-input target builds and the matched
`candidate-remote-id-receive-age` package pass. Paired installation and startup
are verified; rev69 physical reception and callback-age acceptance remain
pending. The rev68 installation record below remains historical evidence.

Both producers capture a full-width monotonic timestamp at receive-callback
entry, before locks and parsing. Only a qualifying decoded report committed
to that exact source updates its timing. Ordinary/malformed reports and BLE
scan responses do not refresh it. A qualifying update without valid timing
clears that row's timing rather than attaching the previous timestamp to a
changed payload. Time zero is valid; a separate validity bit marks availability.

The 46-byte Watch row, 16-byte status and existing chunk commands are unchanged.
Firmware 53 adds read-only timing command 89: a three-byte radio/generation/index
query and a 22-byte reply carrying validity, full-width elapsed age and a
nonzero 64-bit capture epoch. Each producer computes elapsed age from its own
callback clock, rounded upward. S3 never subtracts C5 uptime from its own time.
The existing ordinary Watch getter keeps its original transactions. The new
observed getter reads timing before and after the three row chunks, under the
same SPI mutex, requiring the same epoch, flags and nondecreasing age, matching
final COMPLETE status and unchanged fresh peer identity. Firmware 31–52 returns
the row with unavailable timing and receives no new timing query. A failed or
malformed supported timing response rejects the entire observed read.

S3 retains the first timing reply with its own request-start timestamp, then
adds elapsed local time when displaying a conservative age upper bound.
Full-width arithmetic, upward rounding and the upper end of the current
millisecond bin prevent transfer/formatting delay from making that bound
younger. Future/rewound clocks, impossible metadata and unsupported timing
display unavailable. This is the latest qualifying **source callback's age**,
not hardware RF time or freshness of every ID, location or altitude field in
the merged row. A new Basic ID can coexist with an older same-ID position
within a sweep; callback recency does not certify that position's age.

Publication stages the row and its 24-byte timing record together. All rows,
including ignored Pwnagotchi rows, validate before filtering and atomic bank
commit. Duplicate sweep tokens cannot refresh either age. The immutable painted
selection copies its own timing; a newer matching sweep replaces the separate
latest payload and timing together. Absent/unavailable sweeps advance only the
checked-sweep receipt and retain the last match with its original age. The
existing receipt-age fields and matching-sweep count remain distinct.

Individual host suites passed: timing arithmetic/property checks 129,705;
production S3 Watch transport 1,857; inventory 1,294; session/controller 6,501;
Remote ID renderer 3,436; and production live adapter/routes 6,313. Existing
receive 171,310 and actual C5 callback/getter 3,313 checks also passed after
compatibility-only fixture updates. Real S3 legacy/extended callback and
snapshot fixtures cover lock delay, timestamp zero, wrap and response exclusion.
The new shared/C5 producer timing runner adds 636 checks across the collector,
actual Wi-Fi callback/getter and both C5 BLE callback builds. All
[34 aggregate host suites](../../integration/device-test-2026-10-27/rev69-host-checks.json)
pass with stable inputs. These are host results, not physical radio acceptance.
No new task, radio schedule, scan mode, active engine,
GPS dependency, field-level timestamp, dedicated Target receiver or logging
workflow is introduced.

The frozen [target layout](../../integration/device-test-2026-10-27/rev69-memory-layout.json)
and [candidate comparison](../../integration/device-test-2026-10-27/rev69-candidate-comparison.json)
measure the following changes from rev68:

| Target structure or image | Rev69 bytes | Change |
| --- | ---: | ---: |
| S3 Remote ID live PSRAM store | 7,240 | +1,152 |
| Contained inventory / bank / record | 2,800 / 880 / 88 | +912 / +288 / +24 |
| Contained selection / view / render cache | 200 / 616 / 3,280 | +48 / +144 / +96 |
| Remote ID controller / diagnostics | 264 / 32 | Unchanged |
| S3 BLE snapshot | 8,680 | +120 |
| Watch collector, each producer | 688 | +116 |
| Local timing / timing reply | 24 / 22 | New metadata types |
| S3 application image | 1,686,672 | +3,024 |
| C5 application image | 1,615,920 | +1,728 |

S3 internal static allocation is unchanged; C5 internal static allocation grows
264 bytes. Ordinary BLE row/focus/Monitor/Analyzer and AP display/timing layouts
are unchanged. Contained structures and sidecars must not be counted again as
separate allocations. These are target ELF/image measurements, not startup
free heap or a running-session peak. Image lengths are distinct from padded
flash transport counts. The successful five-target matrix covers S3, onboard
C5, temporary bridge, expansion WROOM and expansion XIAO; expansion hardware
acceptance is not established.

The installed startup reports C5 firmware 53 online at 40 MHz SPI, SD mounted
and no previous reset fault. S3 internal free heap is 116,752 bytes (minimum
116,072), PSRAM free is 1,428,324 and task count is 10, all unchanged from the
rev68 idle baseline; C5 internal free is 73,268. The Remote ID page was closed
and its aggregate diagnostics were zero. This idle baseline does not measure
the lazy live store's running-session peak or establish radio reception.
The frozen rev68 pair remains available for rollback; NVS, PHY and SD contents
were preserved. Expansion targets were built, not flashed.

The owner subsequently reported **“Both counts increase; both runs stop”** in
response to the earlier rev67-worded Remote ID test prompt. The response arrived
while rev68/C5 firmware 52 was installed; the exact image used for the runs was
not independently established. The captured console found the page already
closed with zero diagnostics, so it does not confirm the reported sweep counts.
This is owner-reported basic two-radio sweep/repeated-Stop acceptance only,
not evidence of decoded sources, callback timing, selected-source behavior or
rev69 acceptance. See the [bounded owner record](../../integration/device-test-2026-10-27/rev69-prior-owner-acceptance.json).

The comparison is pinned to Marauder `develop` commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363),
using the local `integration/marauder-develop-2026-10-29` snapshot. Cerberus
orchestration, storage, presentation and expanded framing are original code.
Rev68 changed the shared decoder and C5 firmware version; at that checkpoint,
the radio schedule, UI, heap banks, workers, Watch wire format and active
engines were unchanged. Rev69 adds the timing metadata described above while
retaining the existing workers and radio schedule.

## Upstream workflow and remaining differences

Marauder exposes
[Remote ID Scan and Remote ID Devices → Target](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/MenuFunctions.cpp#L5157),
plus [CLI scan/list/track/clear](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/CommandLine.cpp#L1333).
Its [scan startup](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScanRemoteId.h#L230)
and [channel scheduler](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScanRemoteId.h#L302)
use dedicated NAN channel priority, a beacon sweep and selected-target channel
behavior. Its [decoder](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/RemoteIdDecoder.cpp#L178)
also handles the legacy beacon OUI and variable NAN descriptor fields.

Cerberus provides discovery, source selection and selected-source updates
across repeated passive sweeps, with bounded C5 priority windows on firmware 54
and explicit source-proven Follow/SEARCH on firmware 55.
It does **not** claim the upstream continuous Target receiver, identical full
channel scheduling, full field coverage, history or logging.
Opening selected details does not retune a radio or start a separate target
scan; the separate Follow control explicitly enables that bounded workflow.
Identical advertised IDs do not merge separate source addresses or
transports into an inferred aircraft.

## Current user-visible behavior

- Opening the page allocates one state object in PSRAM and draws the retained
  view. Opening, redrawing and selecting a row emit no scan-start command.
  **START** explicitly enables both independent passive sweep sessions.
- Firmware-54-or-later C5 peers run bounded passive priority windows on the existing
  scanner worker; older supported Watch peers run ordinary passive Wi-Fi scans.
  S3 calls the passive BLE scanner API. Each radio completes, publishes a coherent snapshot and
  rearms independently. One busy or unavailable radio does not prevent the
  other radio's progress. This is repeated bounded scanning, not continuous
  reception of every channel or PHY.
- A selected Wi-Fi detail offers **FOLLOW / UNFOLLOW** on firmware 55.
  Follow requests the frozen source's exact beacon/NAN transport and MAC;
  its current matching callback evidence may supply a channel hint. C5
  independently proves or rejects that hint. Missing proof is shown as SEARCH.
  A new desired target applies to the next job without relabeling the active
  job. Unfollow clears the desired profile while the current bounded window
  finishes; selecting a BLE source does not request Wi-Fi Follow.
- The inventory has two banks of at most 12 protocol rows each, with four rows
  per page. Only Remote ID rows appear. Wi-Fi protocol capacity is shared with
  Pwnagotchi observations, so the displayed maximum is 24 **sources**, not a
  guarantee of 24 aircraft. Dropped values count discarded records, including
  a full-bank row replaced by an actually received protected target; they are
  not distinct-aircraft or RF-packet-loss counts. Capture unavailable is
  distinguished from no matches.
- A tap copies the exact successfully presented record. Selection remains
  fixed to its radio, transport, six address bytes and BLE address type.
  A newer matching sweep is copied separately. The original selected ID and
  receipt time remain visible even if the same source changes its claimed ID.
- Selected details distinguish present in the last completed sweep, absent
  from that sweep, and capture unavailable. When absent or unavailable, the
  last matching payload keeps its own receipt time. A later Basic-ID-only
  sweep replaces the current payload as a whole; earlier coordinates are not
  silently carried into that new evidence.
- **STOP** requests cleanup of owned sessions. **Back** from details clears
  desired Follow and returns to the list; the current bounded job remains
  visibly in flight until release. Back from the list stops/drains both owned sessions before
  releasing the PSRAM state. A failed cleanup retains state and allows a
  repeated Stop attempt. A terminal BLE scan is not considered released while
  the scanner still owns its host lease.

IDs, positions and altitudes are unauthenticated broadcast claims. They are
not verified device identity, a receiver GPS fix, or a count of physical
aircraft. Source addresses use the existing privacy formatter. No receiver
GPS is required to display transmitted aircraft coordinates. Receiver-relative
distance, bearing and receiver-centered maps are absent. SD is optional; this
increment does not create a Remote ID CSV/GPX export or depend on SD assets.

## Identity, time and coherent publication

`remote_id_inventory.c/.h` owns the two committed banks, one reusable 12-row
staging array and a selected record with separate frozen/latest copies. It
validates all incoming rows, including ignored Pwnagotchi rows, before commit.
Validation covers completed matching status, row bounds, producer report/drop
invariants, supported transport/radio combinations, printable ID termination,
coordinate/altitude bounds and Wi-Fi nonzero unicast source addresses. BLE
identity uses address type; IEEE Wi-Fi group/local-bit rules are not applied
to the BLE byte array.

Each accepted or ambiguously delivered start receives a nonzero, increasing
64-bit session token for that radio. This distinguishes witnessed new sweeps
even when the wire's eight-bit generation wraps. Stale tokens, inconsistent
before/after status, failed row reads, a changed final source epoch/generation,
or malformed rows leave the committed bank and selected evidence unchanged.
A duplicate token never refreshes any receipt time. The session reads rows
only after a completed owned sweep and rechecks source identity before publish.

At installed rev68, every displayed age uses a full 64-bit
**S3 completed-sweep receipt time** (the installed rev69 increment above adds a
separate callback-age bound):

- `SELECTED SWEEP RECEIPT AGE` belongs to the immutable selected record.
- `LAST MATCH RECEIPT AGE` belongs to the retained matching payload.
- `LAST CHECK RECEIPT AGE` belongs to the latest committed selected-radio
  sweep that checked for that source, including an absent/unavailable result.

None is RF age, per-message age, per-field age or aircraft-position age. The
existing 46-byte Watch observation has no individual callback timestamps or
per-source packet count. It can combine ID/location claims received from the
same typed source within one sweep. Changing a represented printable ID clears
the prior accumulated coordinates in that collector; beginning the next sweep
clears all collector rows. `MATCHING SWEEPS` counts known matching completed
sweeps since selection and saturates; it is not a packet count. The newer
normal AP observation-age extension is not used as a substitute for Remote ID
timing. No receipt-based “recent RF” filter is added here.

## Ownership and compatibility boundary

`remote_id_session.c/.h` uses two existing `sentinel_scan_session` schedulers
without creating another worker. Known rejected/BUSY starts do not establish
ownership or adopt a foreign running job. A transport failure with uncertain
delivery remains tracked until fresh generation/epoch/status evidence resolves
it. Rejected starts use bounded backoff; capture publication failures retry
for a bounded period while preserving the prior bank. Exhausted 64-bit tokens
fail closed rather than repeat.

`remote_id_live_ui.h` supplies the actual adapters. Radio-start eligibility
requires ordinary live sessions to be drained and excludes pending/active
Wardriving. The adapters inspect BLE host ownership, S3 Wi-Fi receiver use,
C5 radio state, channel survey and Wi-Fi connection state rather than preempt
foreign work. The C5 Watch capability/firmware gate is retained.

Firmware-54 priority jobs use monotonic 64-bit identities and expected-peer
transport checks. Exact-ID proof resolves ambiguous Start delivery, including
a job completed before its first successful poll. STATUS never renews a lease;
renewal occurs only for established owned work while the session is running.
Stop/Back ceases renewal, and terminal state is not release until the C5 has
drained callbacks and restored SDK configuration. DATA_READY plus exact job,
generation and peer checks bracket publication of the retained Watch bank.

For the older-peer ordinary-scan fallback, the C5 START handler returns current status even when its operation
was rejected. The adapter accepts a successful new start only when the returned
generation is the previous generation plus one and its state is RUNNING or
COMPLETE. An authoritative unchanged response is rejected; a transport timeout
remains ambiguous. S3 start uses the passive API and treats a local enqueue
failure as a known rejection.

The legacy C5 and local S3 normal scan STOP APIs have no expected-generation argument. The adapter
re-polls exact source epoch/generation immediately before Stop and does not
send it to a different source. This relies on the existing serialized UI-task
scan callers, with the background Wardriving service excluded by the takeover
gate. It is not a new cross-task/global reservation protocol. Adding another
background ordinary-scan caller requires revisiting that constraint. Stop
acknowledgement alone is not release: the session drains until fresh terminal
status and the required host cleanup are observed.

## Current decoder coverage

`shared/sentinel_watch_protocol.c` and `shared/cerberus_watch_capture.c` remain
the receive foundation. Rev68 extends only Wi-Fi framing in that parser:

| Source | Supported representation | Important limit |
| --- | --- | --- |
| BLE | Complete AD stream up to 255 bytes; service-data UUID `0xFFFA`, application marker `0x0D`, counter and exact supported 25-byte message or message pack | Complete legacy and extended containers are parsed; incomplete reports, arbitrary fragments and scan-response concatenation are not used. |
| Wi-Fi beacon | Complete management frame excluding FCS; vendor OUI `FA:0B:BC` or legacy `90:3A:E6`, exact vendor type `0x0D`, counter and supported message pack | Both OUIs use the same strict payload and enclosing IE checks; bare single-message Wi-Fi payloads remain unsupported. |
| Wi-Fi NAN | Exact destination `51:6F:9A:01:00:00`, public action header and service ID `88:69:19:9D:92:09`; Publish/Subscribe/Follow-up descriptors with variable instance/requestor and bounded optional fields | Reception uses bounded priority/Follow windows on supported C5 peers or ordinary scans on older peers. Filters are opaque, length-checked fields; no matching/Bloom-filter interpretation, extended-service payload decoding or fragment assembly is added. |

The NAN descriptor cursor follows the optional two-byte binding bitmap,
length-prefixed matching filter, length-prefixed response filter, then service
information. The range-limited flag adds no bytes. Service information must
consume the descriptor exactly and contain one counter byte plus a supported
MessagePack. Missing service information, reserved type/control values,
duplicate matching descriptors and malformed later attributes reject the whole
candidate without publishing a partial ID/location. Valid unrelated attributes,
including an adjacent Service Descriptor Extension, may be skipped; this does
not decode an extended-service payload.

The former fixed control value `0x10` means **Publish with service information**,
not Follow-up (`0x12`). The field order and fixed transmitter profile were
checked against pinned primary
[Wireshark NAN](https://github.com/wireshark/wireshark/blob/bd087a07fedd54973508589208439998767fce14/epan/dissectors/packet-wifi-nan.c)
and [OpenDroneID Wi-Fi](https://github.com/opendroneid/opendroneid-core-c/blob/849c03adbe0cdd68997a462fafab397edcfe53f3/libopendroneid/wifi.c)
implementations. Their source URLs, snapshot sizes and hashes are retained in
the [reference index](../../integration/device-test-2026-10-27/rev68-reference-sources.json).
These implementation references are not normative certification, and no
third-party implementation was imported into firmware.

The parser accepts its supported versions 0–2 and exact message-pack lengths
with 1–9 messages. It exposes printable Basic ID claims, aircraft latitude and
longitude, and geodetic altitude with pressure-altitude fallback. WGS84 HAE and
pressure reference are labeled separately. Zero/unavailable fields remain
absent. Recognizing an authentication message does not authenticate it.

Full-stream length and supported-field validation clears parser output on
failure. The S3 protocol collector sees complete extended source bytes before
the ordinary list's bounded raw copy is truncated. This does not establish
physical extended/coded-PHY reception, advertising-set/SID identity, periodic
advertising synchronization, or continuous channel coverage.

Bounded selected-source Follow is implemented, with physical acceptance still
pending. Remaining gaps include full upstream continuous Target behavior,
bare Wi-Fi messages, extended-service payloads and fragments,
per-source/per-field RF time,
additional vertical-motion/status/authentication presentation, operator fields, richer
lost/reacquired history, receiver-relative navigation, and upstream-equivalent
[CSV/GPX logging](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScanRemoteId.h#L371).
The current parser validates some System/Operator messages but does not expose
operator location or identifier in this workflow. No GATT, sound, advertising
or attack change is part of this checkpoint.

## Implementation and validation record

The source path is `field_menu.c` / `ui.c` → `remote_id_live_ui.h` →
`remote_id_session.c` / `remote_id_inventory.c`, with `remote_id_ui.h` providing
presentation and painted-record hit testing. The receive path reuses C5
`wifi_scanner.c`, S3 `s3_ble_snapshot.c` / `s3_ble_scanner.c`, the shared parser
and collector, and `c5_watch_link.h`. The priority path adds
`wifi_remote_id_service.h`, `remote_id_channel_plan.c` and `c5_remote_id_link.h`
while reusing the same scanner worker. The inventory, session and value-only
diagnostics translation units are included in the S3 CMake source list.
READY and fully released STOPPED sessions do not poll either radio; START
performs fresh polls, while pending Stop continues draining. The console reads
a bounded critical-section copy of aggregate diagnostics without pointers,
radio calls or source identifiers. The installed `remote_id` command reports
published sweep/source/drop counts, state, errors, ownership and the last valid
priority job/channel/lease snapshot plus separate desired/requested/bound
Follow flags; rev71 startup recorded a closed session with all 25 fields zero. This is diagnostic startup
acceptance, not evidence that a transmitter was received.

## Rev68 installation and validation

All 30 prior suites remain in the
[31-suite host receipt](../../integration/device-test-2026-10-27/rev68-host-checks.json),
with the new C5 receive integration suite added. The expanded evidence includes:

| Suite | Rev68 result | Main evidence |
| --- | --- | --- |
| `tools/check_remote_id_receive.py` | 171,310 checks passed | Actual parser/collector/S3 publication and Watch getter; both beacon OUIs, variable NAN controls/optional-field bounds, canonical Publish SDA/SDEA and true Follow-up, malformed suffixes, duplicate candidates and transactional rejection. |
| `tools/check_remote_id_c5_receive.py` | 3,313 checks passed | Actual C5 callback and Watch getters through the S3 getter into inventory; FCS exclusion, source/channel/RSSI, admission closure and generation replacement before callback commit, byte-generation wrap, whole-payload replacement and immutable selection. |

The callback tests inject host packet metadata and replace platform/transport
dependencies. They establish code-path behavior, not physical reception or
SPI/RF throughput. The five target builds cover S3, onboard C5, bridge,
expansion WROOM and expansion XIAO. Three writes each for bridge, onboard C5
and restored S3 were hash verified. Expansion images were built, not flashed.
The frozen rev67 package remains the rollback checkpoint; NVS, PHY and SD were
preserved.

Manifest application sizes are 1,683,648 bytes for S3 (unchanged size from
rev67) and 1,614,192 bytes for C5 (+224 bytes). These are image-file lengths,
not padded transport write counts. Rev68 S3 application SHA-256 is
`1294f4054202626e3fb5a721f84d2af912870e194801271e190cf0e10608b681`;
C5 application SHA-256 is
`8fe425e99f06770d2b75a4a8c8f302d83a448de1598e223c3ce8fd2b3535f6d2`.

All measured target layouts and S3/C5 internal static allocation are unchanged
from rev67. The lazy live store remains 6,088 bytes, including inventory 1,888,
controller 264, view 472 and render cache 3,184; the aggregate diagnostics value
remains 32 bytes. Startup reports S3 internal free 116,752 bytes (minimum
116,072), PSRAM free 1,428,324 and C5 internal free 73,540. These closed-page
figures exclude the lazy store and do not measure a running-session peak.

Startup confirms C5 firmware 52 online at 40 MHz SPI, mounted SD, no prior reset
fault and closed/zero Remote ID diagnostics. Four initial READY timeouts and
four legacy fallbacks are recorded, with zero SPI mutex timeouts or driver
errors. Physical legacy-beacon/variable-NAN reception, selected-source updates,
absence/return, repeated Stop/Back and concurrent reception remain pending.

## Rev67 installation record (historical)

Installed 2026-10-30: `candidate-remote-id-sweeps` added the dedicated receive
workflow under Scan → Remote ID using the existing C5 Wi-Fi and S3 passive BLE
workers. Its [receipt](../../integration/device-test-2026-10-27/rev67-flash-receipt.json)
records 30 passing host suites, five stable-input builds, a matched package,
three verified S3 flash writes and a successful startup check. C5 firmware 51
was retained from rev66 with identical image hashes; it was not reflashed at
that checkpoint. Rev67 introduced the workflow without changing its decoder;
the legacy-beacon/variable-NAN extension above belongs to rev68.

Remote ID suites included in rev67's 30-suite stable-input
[host receipt](../../integration/device-test-2026-10-27/rev67-host-checks.json):

| Suite | Result | Main evidence |
| --- | --- | --- |
| `tools/check_remote_id_receive.py` | 2,704 checks passed | Actual parser/collector/S3 publication and C5 Watch getter; legacy/extended container bounds, beacon/NAN, malformed suffixes, Pwn coexistence, typed identity, changed-ID clearing, generation/chunk/final-status failures. |
| `tools/check_remote_id_inventory.py` | 1,255 checks passed | Atomic per-radio banks, whole-payload replacement, immutable selection, absent/unavailable return, malformed-row/status rejection, wide tokens and paging. |
| `tools/check_remote_id_session.py` | 5,962 checks passed | Actual session/backend/scheduler; independent radios, no radio calls while quiescent, fresh START polls, rejected/ambiguous starts, foreign ownership, atomic capture retry, Stop/drain/retry and 257 sweeps across byte-generation wrap. |
| `tools/check_remote_id_ui.py` | 2,613 checks passed | Actual renderer/backend; source selection after publication/reorder, distinct receipt ages, Basic-ID-only replacement, typed-source absence, native bounds, differential repaint and failed-paint tap exclusion; nine native previews. |
| `tools/check_remote_id_live_ui.py` | 5,617 checks passed | Production live adapter and Scan route; allocation failure, passive independent starts, legacy C5 acknowledgement checks, service ownership gates, Stop retry/drain, painted selection and transfer failure retention. |
| `tools/check_remote_id_diagnostics.py` | 48 assertions and 100,000 coherent concurrent reads passed | Production value store and console extraction; bounded locked copies, clear/closed state, complete aggregate output and no radio calls. |

The receive suite explicitly records generic `c5_watch_link.h` limitations:
its existing row getter does not itself reject every malformed source address
or report/drop-counter inconsistency. The dedicated inventory validates these
before publishing. The local S3 Watch getter can leave its output unchanged on
failure; the new adapter clears staging and publishes only successful reads.

The five build targets are S3, onboard C5, bridge, expansion WROOM and expansion
XIAO. Only the S3 bootloader, partition table and application were written for
rev67; the bridge and expansion images were built, not installed. The receipt
binds the frozen manifest, build/matched-package records, host logs and flash
log by SHA-256. The installed S3 application SHA-256 is
`7c9c391e724eb74df3e653c4a74db5e8f0c439b1d9ec66142e4f4247ef49b6af`.
All three retained onboard C5 image role/hash/size/offset records match the
verified rev66 installation.

Exact sizes from the frozen target ELF are distinct from host layout and heap
measurements:

| Allocation or type | Target bytes | Placement |
| --- | ---: | --- |
| Complete Remote ID live store | 6,088 | One on-demand PSRAM allocation, including the components below and UI bookkeeping. |
| Inventory | 1,888 | Inside live store; two banks, reusable staging and selected copies. |
| Two-radio session controller | 264 | Inside live store; reuses existing radio workers. |
| View / render cache | 472 / 3,184 | Inside live store. |
| Aggregate diagnostics value | 32 | Internal static memory; no UI pointer. |

S3 internal static allocation increases 64 bytes over rev66; C5's delta is
zero. Existing BLE snapshot/focus/Monitor/Analyzer and normal scan view/cache/
display-store sizes are unchanged. Startup reports 116,752 internal bytes
free (minimum 116,072), 1,428,324 PSRAM bytes free, and C5 internal free 73,540.
The lazy 6,088-byte store is not allocated in that closed-page PSRAM baseline;
these figures do not measure peak memory or throughput during reception.

Startup confirms C5 firmware 51 online at 40 MHz SPI, mounted SD and no prior
reset fault. The baseline also records four initial READY timeouts and four
legacy fallbacks, zero SPI mutex timeouts and zero driver errors; it is not a prolonged link
test. Real Remote ID transmitter reception, selected-source absence/return,
repeated Stop/Back, concurrent reception and physical extended/coded-PHY
acceptance remain unverified. Neither successful installation nor host fixtures
complete the remaining upstream workflow or hardware acceptance.
