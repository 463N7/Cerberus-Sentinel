# BLE receive-list parity audit

## Installed rev79: focused live ordinary discovery

The ordinary Bluetooth list uses a dedicated S3 discovery attachment. It
publishes atomic active banks approximately every 200 ms instead of waiting
for the eight-second scan to finish. The caller rearms finite sweeps while
enabled, retaining up to 32 typed observations. First entry starts discovery;
Stop, Back, leaving the page or sleeping disables it. Returning to retained
results requires an explicit Start. The display refresh interval is a software
target, not a measured physical latency guarantee.

Family selection applies before the result capacity. An eight-record binding
cache lets a matched response promote its original report without letting
unrelated originals occupy all 32 focused rows. Binding overflow, producer
replacement and retained-history replacement are distinct bounded counters;
none measures all over-the-air loss. Explicitly dequalified records retire
their earlier hints. Absent records may remain as EARLIER history, so omission
does not establish that a device disappeared. The history is volatile PSRAM,
not an SD log or a claim of complete nearby-device coverage.

The UI freezes the four actually painted rows. A tap copies that exact typed
observation and waits for the discovery attachment to release before opening
details. Full-width source-time anchors preserve age across timer wrap;
redrawing or copying never refreshes evidence. Sorting caches include their
source kind and bank revision. Changing families drains the old attachment
before starting another. A failed read leaves the last committed bank intact;
an ambiguous Stop or release retains ownership for cleanup.

C5 remains dedicated to its existing Wi-Fi work. Ordinary BLE discovery skips
unneeded SPI queries and defers the five-second optional C5 telemetry refresh.
Any outstanding C5 live session still owns its cleanup polling. Watch,
Wardriving, Analyzer and MAC Monitor retain their existing receive workflows.
No new task or per-publication allocation is introduced. The UI history remains
allocated for browsing; the native binding cache releases with its attachment.

The [rev79 receipt](../../integration/device-test-2026-10-27/rev79-flash-receipt.json)
binds 75 passing host suites, five stable-input builds, matched images and
three verified S3 writes. C5 firmware 59 exactly matches the paired rev78
images and is retained. The first native attempt rejected three cross-enum
compile-time comparisons; explicit integer casts fixed them. All 85 first-run
evidence files remain separate from the final successful validation.

The live UI store is 17,032 target bytes in PSRAM, containing its 8,240-byte
history, 7,352-byte staging bank, painted/selected copies and 96-byte controller.
Focused scans separately allocate a 1,764-byte binding cache; ALL needs no
binding cache. The shared scanner snapshot grows 16 bytes to 8,912. S3 static
internal RAM grows 80 bytes; C5 static RAM is unchanged. The S3 app is 1,759,728
bytes. Settled startup reports 116,192 free internal bytes, 1,428,324 free PSRAM
bytes, ten tasks, mounted SD, online C5 and idle discovery diagnostics. C5 free
internal RAM remains 65,924 bytes. Contained types are not extra allocations;
startup/layout evidence does not establish peak scan memory or response time.

Physical dense-scan admission, response-only matches, latency, selection and
repeated Stop/Back/restart/sleep acceptance remain pending. Existing Wi-Fi
NO MEM, Association Sleep and Bad Message reports remain unresolved; Remote
ID Follow remains owner-deferred. No active transmission engine is changed.

## Installed rev76: recent retention and explicit connection recovery

The generic 32-row scan bank now replaces the least recently observed original
advertisement when a new, independently valid identity arrives. Existing
original callback sequence numbers order replacement; stable slot order breaks
ties. Invalid, partial or backward-time reports and scan responses cannot evict
rows. Each replacement clears the entire victim, including raw bytes, name,
peak RSSI and discovery hints. Typed addresses and response provenance remain
distinct, completed banks remain immutable, and selected device copies survive
later sweeps. The third sort label is now SCAN ORDER because reused slots do
not describe first arrival time.

Replacement and rejection counts are separate saturating values. The console
reads both with completed-sweep status under one lock, or prints unavailable;
neither counter measures all RF loss or unique devices. The retained bank stays
bounded. At rev76, family filters operated after retention. Rev79 introduces
focused admission for the ordinary list; generic completed-bank consumers
retain the rev76 behavior.

The selected-device connection button now offers Cancel while connecting and
Retry Close after a failed close, while confirming the actual typed peer and
current state on each tap. A pending close remains owned until the host reports
release. A failed termination following a late connection is recorded rather
than only logged. No automatic retry or new connection/write operation is
introduced. Aggregate diagnostics report connection state, ownership and GATT
activity without addresses or values and without initializing the controller.

Physical scan-overflow and connection-cancellation/retry acceptance is pending.
This increment does not resolve the reported Wi-Fi NO MEM, Association Sleep
or Bad Message failures. Remote ID Follow remains deferred by the owner.

The [rev76 installation receipt](../../integration/device-test-2026-10-27/rev76-flash-receipt.json)
binds 56 passing host suites, five stable-input builds, matched images and
three verified S3 writes. The first aggregate run's sole failure was a console
fixture extracting the new diagnostic printers without their declarations;
the corrected extraction passed, and initial results remain archived. Target
snapshot size is 8,896 bytes (+8), records remain 220 bytes, and internal static
RAM is unchanged. Connection diagnostics occupy a temporary 16-byte value.
The S3 app is 1,705,792 bytes; the exact paired rev75 C5 firmware 57 is retained.
Settled startup shows 116,336 free internal bytes, 1,428,324 free PSRAM bytes,
ten tasks, SD mounted and idle connection diagnostics. An earlier startup
sample is preserved separately and is not a memory-saving claim.

## Installed rev74: selected-vendor cache reuse

BLE's selected-vendor cache now also serves the Wi-Fi detail page. One entry
retains exact selected bytes and a distinct radio/address-type key, so Wi-Fi,
BLE public and BLE public-identity selections cannot share a cached result.
BLE address normalization happens only for lookup. Existing BLE vendor labels,
private/local exclusion, zero-wait storage guards, 30-second found/unknown
reuse and transient retry behavior are preserved; cold lookups still block.

The focused 31,342-check host suite exercises both actual callers, storage
contention and cross-radio/type transitions. Scan/filter/sort, immutable
selected scan evidence, tracking, radio scheduling and active paths do not
change. This is an SD presentation optimization, not another ported mode.
The [rev74 receipt](../../integration/device-test-2026-10-27/rev74-flash-receipt.json)
records 52 host suites, five stable-input builds, matched-package/READY checks,
three S3 write hashes and successful startup. The target cache remains 72
bytes; static memory and other measured types are unchanged. Physical
transition/recovery/latency acceptance remains pending. All 95 mode statuses
and older acceptance scopes remain unchanged.

Baseline: Marauder `develop` commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363),
rechecked against the remote on 2026-10-30. The local reference is
`../integration/marauder-develop-2026-10-29`. This audit concerns receive-side
inventory and selection; no transmitter or sound/GATT write paths changed.

## Installed rev73: Wardriving evidence and Analyzer statistics

The existing Wardriving session now retains Flock packet facts and original
source times through an atomic completed-bank copy. Advertisement, matched
response and typed-address OUI context remain separate. Its counters count
processed rows across sweeps; no-GPS CSV v2 exports the individual facts and
times. Changed evidence can trigger a new unlocated row, while timestamps
alone cannot. WiGLE's schema/policy stays unchanged, so located evidence
export remains open. Stop resumes an immutable bank's unprocessed suffix.

BLE Analyzer now shows peak and mean callback counts per completed 200 ms
bucket over its existing 64-bucket window. The partial bucket stays separate;
completed silent buckets contribute zero events, and not-yet-started history does
not enter the mean. Saturated counters make the displayed statistics lower
bounds. An exact 64-bucket advance retains the oldest still-visible completed
bucket. These are received callbacks, not unique devices or all RF packets.
Existing classification priorities and packet/source details remain intact.

The selected-device SD vendor label also follows the storage epoch through
zero-wait guards on the existing mutex. Busy/unavailable storage clears the
label and retries on the next render, including contention with the resources
console's database hash. Found/unknown results retain the existing 30-second
cache within an unchanged mounted session. Random/local addresses remain
excluded. Cold recon lookups and the blocking epoch API remain unchanged;
host checks cover guard/cache contention, not measured device latency.

The [rev73 receipt](../../integration/device-test-2026-10-27/rev73-flash-receipt.json)
records 52 host suites, five stable-input builds, matched-package/READY checks
and three verified S3 writes. C5 firmware 56 images are unchanged from rev72;
the bridge is build-only. S3 static memory grows 16 bytes, and the on-demand
Wardriving PSRAM workspace grows 3,460 bytes to 16,900. The 8,888-byte snapshot,
276-byte Analyzer, 272-byte Analyzer view and 72-byte vendor cache stay the same
size. The eight-byte window-statistics value is temporary, not another history.
Startup shows the C5 link online at 40 MHz, SD mounted and ten S3 tasks. These
are install/idle checks; physical CSV/counters, Analyzer accuracy and cache
transition/latency acceptance remain pending. Full BLE/Marauder parity remains
open, as does the owner's deferred Follow/motion test.

## Installed rev72: passive Remote ID motion

The shared decoder now preserves advertised horizontal speed, direction and
height/reference from admitted complete BLE Location messages. S3's detailed
getter copies the row, source age, motion and its own Location callback age
under one scan lock. Basic ID-only callbacks cannot refresh Location timing;
new Location replaces unavailable fields too. Scan responses and malformed
reports still cannot refresh this evidence. The new Motion detail panel adds
no scan scheduling or active eligibility change. Ordinary BLE focus/list,
Monitor, Analyzer, Sound and GATT paths remain outside this increment.

The focused shared suite passes 1,064,928 checks. The [rev72 receipt](../../integration/device-test-2026-10-27/rev72-flash-receipt.json)
verifies all 38 host suites, five stable-input builds, the matched package and
nine paired-installation write hashes. Measured collector size is 896 bytes
(+200); the PSRAM snapshot grows to 8,888 (+200), separately from the Remote ID
page's PSRAM store at 8,888 (+1,544). BLE focus/list, Monitor and Analyzer type
sizes remain unchanged. S3 internal static use grows 336 bytes; startup free
internal heap is 116,384, PSRAM free 1,428,324 and task count 10. Physical
motion values, independent Location age and repeated two-radio operation
remain unverified. Remote ID and overall Marauder parity remain partial.

## Installed rev71: Wi-Fi Follow beside independent BLE sweeps

The installed [Remote ID increment](REMOTE_ID_PARITY.md) adds explicit Wi-Fi source
Follow/Unfollow on C5 and keeps the existing independent S3 BLE sweep path.
BLE parsing, ADV/response admission, raw evidence, focused-list ages and
active eligibility are unchanged. Shared collector state gains a bounded
Wi-Fi source-protection key; BLE never enables that key, and no selected row
is fabricated. Target layout measures the collector at 696 bytes (+8) and
the enclosing S3 snapshot at 8,688 (+8); focus, row, Monitor and Analyzer
sizes remain unchanged. These are contained sizes, not extra allocations.

Desired Wi-Fi Follow, the exact in-flight source and C5 requested/bound status
are separate. Peer errors, downgrade, Stop and cleanup cannot be presented as
new BLE evidence or active selected-source reception. The [rev71 receipt](../../integration/device-test-2026-10-27/rev71-flash-receipt.json)
verifies 37 stable-input host suites, five target builds, the matched package
and nine bridge/C5/S3 writes. S3 internal static use and idle free heap remain
unchanged at 116,720 bytes free (minimum 116,040), with 1,428,324 PSRAM bytes
free; C5 firmware 55 is online and SD mounted. Physical concurrent radio and
Follow acceptance remain pending. This adds no BLE transmit or sound feature
and does not establish full Marauder parity.

## Rev70 installation record: C5 priority windows beside S3 BLE (historical)

The installed [Remote ID increment](REMOTE_ID_PARITY.md) changes the C5 Wi-Fi
receive schedule while keeping independent passive S3 BLE sweeps on the
existing host/worker. BLE parsing, original-versus-response admission, raw
evidence, focused-list ages and active eligibility remain unchanged. Exact
C5 job recovery and cleanup are coordinated by the existing two-radio
controller; one radio's failed Start or publication does not publish or replace
the other radio's evidence.

Focused production controller/live-route fixtures cover concurrent progress,
lost replies, terminal-first recovery, peer changes and Stop/Back cleanup.
The [rev70 receipt](../../integration/device-test-2026-10-27/rev70-flash-receipt.json)
verifies 37 stable-input host suites, five target builds, the matched package
and nine bridge/C5/S3 writes. BLE snapshot/focus/Monitor/Analyzer layouts remain
unchanged. Startup reports C5 firmware 54 online, SD mounted, S3 internal free
heap 116,720 bytes (minimum 116,040) and PSRAM free 1,428,324. Physical priority
reception, concurrent BLE progress and cleanup acceptance on rev70 remain
pending. No new BLE feature or full Marauder parity is claimed by this Wi-Fi
scheduling increment.

## Rev69 installation record: separate Remote ID callback timing (historical)

The dedicated [Remote ID workflow](REMOTE_ID_PARITY.md) now carries optional
callback-age bounds independently of ordinary BLE/Find My/iBeacon list ages.
S3 records a full-width time at GAP callback entry before locking/parsing;
the existing 32-bit observation fields keep their prior behavior. Only complete
qualifying original Remote ID reports update their collector row's timing.
Scan responses remain outside Watch collection and cannot refresh that age.

The new local getter copies the completed row and its elapsed callback age
under the same scan mutex. Its nonzero 64-bit capture epoch persists across
snapshot/host resets and refuses exhaustion. Existing untimed snapshot/getter
APIs remain available. Selected evidence retains its own timing independently
of later matches, absence and sweep receipts. The displayed bound concerns
source activity, not individual location-field age or hardware RF time.

Production snapshot, both legacy/extended callback paths, renderer and live
adapter host fixtures, all 34 aggregate suites, five target builds and matched
package pass. Target S3 snapshot size is 8,680 bytes (+120), including the enlarged
Watch collector; ordinary row/focus/Monitor/Analyzer sizes remain unchanged.
The [rev69 receipt](../../integration/device-test-2026-10-27/rev69-flash-receipt.json)
verifies paired installation and startup: nine bridge/C5/S3 writes, C5 firmware
53 online at 40 MHz SPI, SD mounted, and S3 idle internal free heap 116,752 bytes
(minimum 116,072), with PSRAM free 1,428,324. Rev69 physical reception and
callback-age acceptance remain pending. Ordinary focused-list predicates,
raw Sound eligibility, GATT/advertising and radio scheduling are unchanged.

## The two simple-scan symbols

`WiFiScan.h:167-168` declares `BT_SCAN_SIMPLE` and `BT_SCAN_SIMPLE_TWO`.
The legacy pre-NimBLE-2 callback (`WiFiScan.cpp:382-1138`) handles them in
adjacent branches at 989-993, and both only increment `bt_frames`. Their
shared scan initialization disables duplicate filtering, requests active
scanning and supplies different display titles (`WiFiScan.cpp:7211-7371`).
The NimBLE-2 callback starting at 1142 has no matching simple-mode branches.
No menu or top-level CLI start route for either symbol occurs in this pinned
source; the normal Bluetooth sniffer menu is at `MenuFunctions.cpp:5435-5541`.

The symbols therefore do not establish two separate exposed features to
duplicate in Cerberus. They remain recorded in the source inventory as
internal/dormant simple-counter modes. Cerberus's Analyzer already counts
delivered advertising events; that does not by itself prove identical active
scan behavior or legacy callback semantics. No full parity claim is made for
the dormant symbols.

## Exposed sorting behavior

The upstream Fox target lists expose Signal Strongest and Name A-Z for BLE,
Find My, Flipper, iBeacon, Meta and Flock target lists
(`MenuFunctions.cpp:1942-2104`). Channel order is limited to Wi-Fi lists.
`TargetListSort.cpp:7-60` defines case-insensitive label comparison, signal
descending with name as a secondary key, and stable ties. Unnamed generic
BLE targets use their address label (`MenuFunctions.cpp:2075-2078`).

Cerberus previously displayed focused BLE rows in discovery order only.
The new [s3_ble_list.h](../s3_main/main/s3_ble_list.h) supplies a bounded
presentation helper:

- `cerberus_s3_ble_focus_page_sorted(results, category_bits, count, filter,
  sort, offset, indexes, capacity)` returns the total focused matches and
  one page of **original scan indexes**.
- Sort modes are `SIGNAL_DESC`, `NAME_ASC` and `DISCOVERY`; the last preserves
  the previous ordering. Equal keys preserve their original discovery order.
- Signal sorting ranks measured RSSI before unavailable/out-of-range values,
  then strongest signal and case-insensitive display label. Names sort by
  case-insensitive display label. Address type never gets merged or discarded.
- `cerberus_s3_ble_list_label()` bounds name reads to 32 bytes and maps
  nonprintable/non-ASCII bytes to `?` for the ASCII display. Unnamed results
  use their exact displayed MAC. Original name bytes and all evidence remain
  in the frozen record unchanged.
- The helper uses a 32-byte index workspace and no heap allocation. It never
  sorts raw records in place or changes generation, index, RF timestamp, UUID,
  category, address or address type.

The existing `cerberus_s3_ble_focus_page()` remains available to other callers.
The production live list now uses its painted sorted index array when handling
a tap, then copies the existing immutable evidence for that source index.
The right summary badge cycles SIGNAL, NAME A-Z and FIRST SEEN; the left
summary retains the focus-filter control. Changing either resets pagination.
The original ordering cache was invalidated by a new published sweep or
filter/sort change. Recent visibility additionally requires mask-driven
invalidation on UI ticks; see the source-age contract below.
Unnamed row labels still obey the existing address privacy setting; the helper
does not bypass it. Rev55 was built and installed, and the owner confirmed
sorting and selected-device details at that revision; see the historical
[installation receipt](../../integration/device-test-2026-10-27/rev55-flash-receipt.json).

## Recent source-age visibility (installed rev65)

Pinned upstream exposes recent filtering for AP, AP-with-stations, Find My and
iBeacon Fox target lists (`MenuFunctions.cpp:1951-1966,2006-2009`).
[`TargetListSort.cpp:35-42`](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/TargetListSort.cpp#L35)
requires a nonzero retained timestamp and uses unsigned elapsed time **at most
30,000 ms**, even though its label says `Seen <30s`. Find My/iBeacon timestamps
are supplied by `MenuFunctions.cpp:2075-2086`. Cerberus uses the inclusive label
`WITHIN 30S` for this predicate; no AP age is inferred from a sweep receipt.

The new [s3_ble_recent.h](../s3_main/main/s3_ble_recent.h) is a bounded,
allocation-free presentation helper for **admitted frozen source evidence**:

- `cerberus_s3_ble_recent_supported(filter)` accepts only Find My and iBeacon.
- `cerberus_s3_ble_focus_recent(focus, filter, now_ms)` uses each qualifying
  source's own sequence, validity and 32-bit monotonic observation time.
- Find My requires a complete valid current original, nonzero original
  sequence and valid SID state. Only recognized qualifying hint bits count.
  A response additionally needs the admitted response flag and a later
  sequence; its own qualifying hints/time may remain recent after the original
  expires. A newer ordinary raw packet, cached name, nonmatching response or
  list-publication time cannot refresh a hint. The snapshot owns exact typed
  identity/generation/event/SID and response-window admission; this helper
  neither reconstructs nor relaxes that binding.
- iBeacon uses its retained valid match's own sequence/time, including an
  `EARLIER` same-sweep match after an ordinary or invalid replacement. It does
  not require that match to remain current or borrow the replacement's time.
- A genuine timestamp of zero is supported when validity plus a nonzero
  sequence proves the source exists. This differs intentionally from upstream's
  timestamp-zero sentinel. Missing evidence remains ineligible at time zero.
- Unsigned elapsed time includes 30,000 ms, excludes 30,001 ms, handles a nearby
  timer wrap and rejects future/rewound times. A 32-bit time alone cannot detect
  an entire timer revolution. The caller **must also** reject a snapshot when
  its 64-bit publication age is negative or exceeds 30,000 ms. This is a maximum
  snapshot lifetime, never a substitute for source freshness; it prevents old
  snapshots from reappearing after approximately 49.7 days.

`cerberus_s3_ble_focus_page_sorted_masked(results, category_bits, count, filter,
sort, visible_mask, offset, indexes, capacity)` intersects a 32-bit mask of
original scan indexes with the existing focus filter before stable sorting.
Its original `cerberus_s3_ble_focus_page_sorted()` API wraps an all-visible mask,
preserving existing callers. Both reject inventories above 32, return total
matches, leave output untouched for an empty/out-of-range page and never mutate
source rows or evidence. Bit 31 is supported without a signed shift. No new
per-row metadata, heap bank, radio work, SD resource or task is required.

The installed [live UI](../s3_main/main/sentinel_live_ui.h) starts with **All
ages**. Normal Find My and iBeacon lists split the right badge into the existing
sort control above and `ALL AGES >` / `WITHIN 30S >` below. Switching to an
unsupported family resets the age option; Watch keeps its existing filtering
and ordering without this age exclusion.

Successful BLE snapshot publication records a separate 64-bit uptime, including
empty captures. Failed captures preserve the previous evidence and publication
time. The existing bounded UI tick recomputes the source mask from frozen
evidence and applies the publication-age ceiling above. A changed mask
invalidates cached ordering even on completed or paused lists, and pagination
clamps after rows expire. Taps use the last painted original-index mapping;
the selected immutable typed record survives expiration and later sweeps through
Fox Hunt and Back. Empty recent results show `NO HINTS WITHIN 30S` and do not
start a scan. No new radio service, task or evidence refresh is introduced.
Existing Find My Monitor retention, raw action eligibility, tracking lifecycle
and scan scheduling are unchanged.

Remaining acceptance includes varied focused peers, repeated transitions,
continuous Monitor update/Back behavior,
and the profile-classification gaps in [FINDMY_PROFILE_PARITY.md](FINDMY_PROFILE_PARITY.md).
This is a list-selection improvement, not complete BLE scan parity.

Rev65 `candidate-ble-recent-list` was installed on 2026-10-30 after 18 host suites,
five stable-input target builds, the matched package and three verified S3 write
hashes passed. Only S3 was flashed; all C5 images are byte-identical to the
retained verified firmware 50 installation. Startup confirmed mounted SD and
the 40 MHz C5 link. See the
[rev65 installation receipt](../../integration/device-test-2026-10-27/rev65-flash-receipt.json).

Target ELF sizes are 152 bytes for the scan view (+4 versus rev64) and 792 bytes
for the renderer cache (+16). Snapshot, focus, record, Monitor and Analyzer
structures and their row banks are unchanged. Total internal static storage
increased by 48 bytes. The recorded idle S3 free memory is 116,848 internal
bytes and 1,428,324 PSRAM bytes; these are startup readings, not sustained-session
performance measurements.

Current-build touchscreen placement, physical Find My/iBeacon filtering and
expiration, repeated transitions and concurrent Wi-Fi/BLE acceptance remain
pending. Historic rev55 acceptance and memory figures below apply to that
earlier sorting release. This installation does not establish full Marauder
parity or resolve the reported Wi-Fi NO MEM, Association Sleep or Bad Message
failures. AP recent filtering still lacks per-AP RF observation timestamps.

## Validation

`py -3.14 tools/check_s3_ble_list.py` compiles the production header on the host
and passes fixture checks for all three orderings, case-insensitive stable
ties, same MAC with different address types, combined focus masks, offset and
capacity boundaries, maximum inventory, invalid RSSI, binary/nonterminated
names, output canaries, invalid enums and byte-for-byte input immutability.
The rev65 extension independently intersects static expected sort orders with
all 256 eight-row visibility masks, every focus family, all three orderings and
paged offsets. It also checks bit 31, excess mask bits, zero masks, 33-row
rejection, count-only calls, disappearing final pages and stable original indexes.

`py -3.14 tools/check_s3_ble_recent.py` compiles the production age header and
unchanged snapshot implementation; 1,452 checks pass. Fixtures cover inclusive boundaries, nearby
timer wrap, future/rewound times, zero timestamps, missing sequences, every
source-flag/known-hint combination, invalid SID, response-only hints, one expired
and one fresh source, and own-time retained iBeacon evidence. Actual snapshot
observations prove ordinary/incomplete/malformed original replacement, response
invalidation, unchanged retained raw time, frozen selected evidence and new-sweep
reset. The helper harness does not simulate the required UI 64-bit publication
ceiling, painted mapping or real radio timing. The production UI checks below
cover the first two separately; real radio timing still needs hardware acceptance.

`tools/check_s3_ble_live_routes.py` also compiles the production ordering block
and tap handler, checking the original index/time through all three orderings,
focus intersection, page reset and immutable source records.
`tools/check_s3_ble_discovery_ui.py` additionally compiles the complete production
live tick with the real scan-session state machine and renderer. It checks
completed/paused expiration without RF starts, inclusive boundaries, time zero,
future/rewound times, nearby wrap and full-wrap rejection even when recent mode
was never enabled before. It exercises all sorts, page collapse, empty views,
unsupported-family reset, new publication and retained earlier iBeacon evidence.
A tap after expiration but before repaint freezes the painted typed target,
including equal address bytes with different address types, through Fox Hunt and
asynchronous Back. Find My/Flipper/Meta/iBeacon/Flock/HC detail priorities,
privacy and unchanged raw Sound gating retain their regression checks.

`tools/render_network_picker.py` checks split badge hit bounds, in-place label
cache redraw, unchanged body pixels, empty/disabled/unsupported views and the
legacy sort control when no age label is supplied. Recent Find My, iBeacon and
empty-list native previews were inspected. The standalone and integrated empty
previews also contain the exact `WITHIN 30S >` production-font pixels; their PNG
pixels match the native framebuffer PPM output.

`tools/check_scan_display_store.py` compiles production capture/allocation
paths. The 5,664-byte display/staging pool remains allocated once in PSRAM,
retained across pages and published only after a complete validated sweep.
Empty scans need no pool; failed allocations or transfers retain the previous
snapshot. Tests cover count limits, generation/count/state changes, evidence
cleanup, retry/reuse and sort-cache invalidation. Rev65 checks also establish
full-width publication uptime on successful and empty BLE captures, with the
previous publication time retained across failed transfers and generation,
count or state mismatches.

## Historical rev55 evidence

The rev55 ELF comparison showed 5,584 fewer internal static bytes after its
new metadata was included. Rev55
idle internal free heap is 109,512 bytes (5,456 above the rev54 idle reading).
After BLE selection and a short no-GPS Wardriving run it is 50,716 bytes,
minimum 43,256. Session lengths differ, so no sustained performance factor
is inferred. All five rev55 target builds passed; the retained C5 image hashes
matched the prior verified install. At rev55, masked unnamed-row formatting was
code-reviewed but was not exercised by that revision's new route/render fixtures.
