# Wi-Fi receive-list age filtering

## Installed rev74: selected-AP vendor cache

The selected AP detail page now reuses the same single vendor cache entry as
BLE. Eligible repeated draws perform one zero-wait storage-epoch check and no
recon lookup, avoiding repeated database-header reads on that warm path. The
key includes the exact BSSID bytes and a Wi-Fi source key distinct from both
admitted BLE public types. A changed selection, remount, failed guard, backward
clock or 30-second expiry requires a fresh lookup; private/local addresses
remain unattributed without accessing SD.

Existing vendor labels and all AP inventory, age filters, frozen selections,
target routes and radio scheduling remain unchanged. Cold recon lookups retain
their blocking behavior. The focused host suite reports 31,342 checks using
actual Wi-Fi/BLE caller and label blocks, including failures and cross-radio
identity. The [rev74 receipt](../../integration/device-test-2026-10-27/rev74-flash-receipt.json)
records 52 host suites, five stable-input builds, matched-package/READY checks,
three verified S3 writes and successful startup. The target cache remains 72
bytes, and neither chip gains static memory. Physical detail transitions, SD
recovery and latency remain pending. This adds no Marauder mode or repair
claim for the unresolved Wi-Fi action reports.

Feature installation checkpoint: rev66 `candidate-wifi-source-age`, paired with onboard
C5 firmware 51 on 2026-10-30. All 23 host suites, five stable-input target
builds, matched package, S3 READY ISR gate and nine bridge/C5/S3 write hashes
passed. Startup was verified; physical AP filtering and touchscreen acceptance
remain pending. This bounded receive-list option does not establish full
Marauder Wi-Fi parity. See the
[rev66 receipt](../../integration/device-test-2026-10-27/rev66-flash-receipt.json).

## Installed rev72: Remote ID motion

The separate [Remote ID workflow](REMOTE_ID_PARITY.md) now carries advertised
speed, direction and height from C5 Location messages through optional
firmware-56 detail/timing queries. The legacy Watch row remains 46 bytes.
Location replaces its whole group; newer ID-only packets cannot refresh the
Location callback age. Older peers expose unavailable extra fields, and
supported-peer transfer errors do not fall back to stale values. This does not
change ordinary AP rows, their **WITHIN 30S** filter, Follow scheduling or
transmit engines. The [rev72 receipt](../../integration/device-test-2026-10-27/rev72-flash-receipt.json)
verifies 38 host suites, five stable-input builds, the matched package and nine
bridge/C5/S3 write hashes. Startup shows C5 firmware 56 online at 40 MHz,
mounted SD, no previous reset fault and 25 closed/zero Remote ID diagnostics.
C5 internal static use grows 400 bytes, covering its two collectors' 200-byte
growth; its ordinary AP observation table stays 3,608 bytes. Physical motion,
Location-age and repeated-run acceptance remain pending. Both Remote ID modes
remain partial, and the reported Wi-Fi transmit errors remain unresolved.

## Installed rev71: separate passive Remote ID Follow

The installed [Remote ID increment](REMOTE_ID_PARITY.md) adds explicit source Follow
using firmware-55 C5 proof of a previously received beacon/NAN transmitter.
Approved target slots alternate with background coverage; missing or expired
proof becomes visible SEARCH. Both source freshness and the requested window
bound callback admission. Generic firmware-54 requests remain compatible,
while desired Follow never silently downgrades to a generic request.

The ordinary AP inventory, source-age metadata, **WITHIN 30S** filter and
normal scan commands remain separate. Follow does not publish an AP-list
refresh or establish that an AP was received. The [rev71 receipt](../../integration/device-test-2026-10-27/rev71-flash-receipt.json)
verifies 37 stable-input host suites, five target builds, the matched package,
nine bridge/C5/S3 writes and startup with C5 firmware 55 online at 40 MHz SPI
and SD mounted. The ordinary AP table, display pool and timing record sizes
are unchanged. C5 internal static use grows 32 bytes for this increment;
physical Follow/AP-list acceptance remains pending. Wi-Fi transmit errors
remain unresolved.

## Rev70 installation record: separate Remote ID priority window (historical)

The installed [Remote ID increment](REMOTE_ID_PARITY.md) gives firmware-54 C5 peers a
bounded passive priority job on their existing worker. Its supplied, approved
channel plan interleaves 6/149/6 with an advancing background channel and
retains background progress across windows. Expected-peer and 64-bit job
identity checks guard Start/Stop/renewal, and DATA_READY guards the retained
Watch bank. Older Watch peers continue ordinary sweeps; supported failures
are reported rather than silently switching paths.

The ordinary AP inventory, AP age evidence, **WITHIN 30S** filter and normal
scan commands are unchanged. Priority reception does not publish an ordinary
AP-list refresh or prove that a particular AP was received. The
[rev70 receipt](../../integration/device-test-2026-10-27/rev70-flash-receipt.json)
verifies 37 stable-input host suites, five target builds, the matched package,
nine bridge/C5/S3 writes and startup with C5 firmware 54 online at 40 MHz SPI
and SD mounted. The AP display pool, 16-byte AP timing record and C5 AP
observation table remain unchanged. Physical AP filtering and priority
reception acceptance remain pending. This does not establish AP-with-stations parity,
Wi-Fi sorting or a repair of the reported Wi-Fi transmit errors.

## Rev69 installation record: separate Remote ID callback timing (historical)

The dedicated [Remote ID workflow](REMOTE_ID_PARITY.md) now includes installed
per-source callback-age bounds. It reuses the existing C5
receive worker but adds a separate firmware-53 Watch timing query, leaving the
ordinary AP result, BSSID/SSID/channel join, 16-byte AP timing record and
WITHIN 30S predicate below unchanged. A Remote ID callback bound is not the
age of an AP driver's RSSI/security result or of every field in a merged Remote
ID claim. The ordinary Watch getter also keeps its original transactions.

The new observed Watch path brackets row chunks with coherent timing replies,
retains an S3 request-start bound and rejects supported malformed timing.
Older Watch firmware returns unavailable timing without a new query. Host
transport/inventory/session checks, the 34-suite aggregate, five target builds
and matched package pass. The target AP display pool, 16-byte AP timing record
and C5 AP observation table remain unchanged. The
[rev69 receipt](../../integration/device-test-2026-10-27/rev69-flash-receipt.json)
verifies nine bridge/C5/S3 writes and startup with C5 firmware 53 online at
40 MHz SPI and SD mounted. Physical AP filtering and Remote ID callback-age
acceptance remain pending. This does not add Wi-Fi sorting, AP-with-stations
parity or an active-radio change.

## Upstream behavior

The local Marauder `develop` reference is
`../integration/marauder-develop-2026-10-29`, commit
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`. The installation evidence records
a remote `develop` recheck at 2026-10-30 09:53:32 UTC with the same commit.

- `TargetListSort.cpp:35-42` accepts `RECENT_30S` when a retained timestamp
  exists and unsigned elapsed time is at most 30,000 ms. Despite the displayed
  `<30s` abbreviation, the exact boundary is inclusive.
- `MenuFunctions.cpp:1951-1966,2006-2009` exposes the recent option for AP,
  AP-with-stations, Find My and iBeacon target lists.
- `MenuFunctions.cpp:2053-2058` supplies the AP's `last_seen_ms` to that filter.
  The same menus also expose sorting and band filters. This patch does not
  claim to port those other Wi-Fi options or the AP-with-stations list.

## Device behavior and evidence contract

The ordinary Wi-Fi AP list defaults to **ALL AGES**. With a firmware51-or-newer
C5, the existing right summary control switches to **WITHIN 30S**. This keeps
the original driver-result order and filters before pagination. No additional
menu, radio scan or timer is created. The normal UI tick expires rows, clamps
an out-of-range page and displays `NO NETWORKS WITHIN 30S` when none qualify.
Completed and explicitly stopped lists can expire without restarting a scan.

Recent means that a matching frame was observed, not that the entire driver
record is 30 seconds old. In particular, the callback does not establish the
measurement time of the driver's aggregate RSSI or security value. The
observation contract matches the result's BSSID, exact SSID bytes and receive
channel. A result without matching callback evidence remains visible under
ALL AGES and cannot qualify under WITHIN 30S.

The C5 normal passive scan uses the bounded 64-entry tuple map in
`shared/wifi_scan_observation.h`. It accepts beacon/probe-response management
frames only after validating the complete header and IE stream, excluding
FCS. The result join uses the SDK result's primary channel. Capacity loss,
hidden-SSID byte differences or absent matching frames leave timing
unavailable. A full-width per-scan epoch prevents an old in-flight callback
parse from committing after completion or a new start. Start resets the map;
Stop and failure close/discard it. No scan-time fallback invents an observation.

When full, the table retains the 64 strongest callback signal ranks, with
stable ties, so later strong 5-GHz observations can displace weak early ones.
This rank is not exposed as the dated SDK RSSI. A weaker newer callback still
updates presence time for a retained tuple; an older callback cannot improve
its rank or timestamp. Invalid RSSI ranks last without invalidating otherwise
valid frame presence. Supported receive channels include 177. Nonzero SDK
SSID bytes after its first terminator make its length ambiguous and suppress
timing, preventing a binary SSID from borrowing the timestamp of its prefix.

The same Wi-Fi result response carries optional timing evidence. Firmware51
uses previously reserved bytes in the existing 22-byte metadata payload for
source age, diagnostic source clock, validity/frame-kind flags and channel.
The SPI command, result index and subsequent SSID chunks remain the existing
ones. Older firmware supplies inventory with unavailable timing; its UI does
not offer the age option. Invalid timing extensions fail closed.

The C5 reports its callback-to-response age rounded upward to milliseconds.
The S3 records a full-width monotonic clock immediately before the metadata
request. Eligibility adds elapsed S3 time since that request to the reported
C5 age. This conservatively includes transfer delay without comparing C5 and
S3 uptime origins. The C5 source clock is diagnostic only. A future/rewound
S3 clock, missing evidence, saturated old age or elapsed age beyond 30,000 ms
cannot qualify; a valid exact-boundary observation can. Full-width S3 elapsed
time prevents old observations becoming recent after a 32-bit clock wrap.

The optional SPI getter publishes timing only after all SSID chunks validate.
The live display stages every row and observation, then validates the final
COMPLETE state, generation and stored count before committing both banks.
An allocation, transfer or validation failure leaves the previous frozen rows,
their timing and their publication metadata intact. The shared PSRAM pool is
reused across pages; it does not add a separate allocation per refresh.

## Selection and other views

The visible index array always contains original result indexes. A tap uses
the last painted array before recomputing expiration and copies the chosen AP
record into the existing private selection. A row expiring between paint and
tap cannot silently select its neighbor. A later scan or cleared display bank
cannot mutate that private selection. Existing connection resolution still
matches the frozen identity against a fresh completed generation.

The age option applies only to the normal Wi-Fi AP list. Explicit target
selection, derived views and Watch keep their prior unfiltered inventories.
Watch consumes the frozen result bank and original stored count directly; it
does not inherit the normal-list visibility option. Offline controls reject
age taps, and a legacy C5 resets the normal-list option to ALL AGES. Address
privacy and the existing generic TOOLS navigation remain in place.

The read-only `wifi_seen` diagnostic aggregates retained-row timing
availability, recent/not-recent and invalid-clock counts. It publishes only
after rechecking the completed snapshot and peer identity, prints no SSIDs or
addresses, and does not start RF work. The installed startup console returned
`wifi_seen=not_complete`, state 0 and radio error 0, as expected before any
scan. This verifies the idle command path, not live timing coverage.

## Repeated scan publication

The live display also separates a pending publication from its last frozen
generation. A successful START acknowledgement, successfully observed RUNNING
state or observed generation different from the published one permits the
next coherent COMPLETE capture even if the eight-bit generation repeats.
Failed START/status queries cannot establish a new cycle. Capture failures
retain the pending retry and old data; a successful commit clears it. Repeated
unchanged COMPLETE polls do not recopy or reallocate the same snapshot.

This fixes the reachable sequence of 255 intervening BLE starts followed by
a witnessed 256th start whose generation aliases the previously displayed
one. The scanner's private epoch still rejects stale callbacks. Exactly 256
wholly unobserved completed cycles ending with the same byte/state cannot be
distinguished by this interface; this is not a wider-generation protocol or
a claim that every invisible alias is solved.

## Verification and remaining acceptance

The affected host checks use production functions and renderer paths:

- `tools/check_s3_wifi_observation.py` exercises the S3 timing decoder,
  conservative age/index helper and extracted SPI result getter.
- `tools/check_wifi_scan_observation.py` checks the complete C5 frame parser,
  bounded ranking/tuple/age/epoch helper and extracted production callback,
  Start, Stop, worker and getter paths, including injected cleanup failures.
- `tools/check_wifi_seen_console.py` exercises the actual read-only diagnostic
  aggregation, end/peer validation and unavailable/clock-invalid cases.
- `tools/check_scan_display_store.py` extracts status/capture code and checks
  PSRAM-only allocation, empty/max captures, timing-bank retention on result
  and end-status faults, successful retry, same-byte publication and reuse.
- `tools/check_s3_ble_events.py` executes 256 real scanner starts and callbacks
  in both legacy and extended-event builds, verifying fresh same-byte results
  and stale private-epoch rejection.
- `tools/check_s3_ble_discovery_ui.py` extracts the view/tap/tick code and uses
  the real scan-session state machine and renderer. Wi-Fi checks cover
  pagination, completed/paused expiration without RF starts, unavailable
  timing, legacy/offline controls, immutable last-painted selection and both
  Wi-Fi/BLE START accepted/failed/busy acknowledgements. Existing BLE focused
  filter/sort/age/detail/Fox Hunt/Back checks remain present.

The frozen 23-suite host receipt passes, including 30,450 C5 parser/table
checks and 16,061 production lifecycle checks. The S3, onboard C5, bridge,
expansion WROOM and expansion XIAO builds all pass with stable inputs. Three
write hashes each verify the bridge, onboard C5 and restored final S3 images.
The rev65 paired rollback package, NVS, PHY and SD were preserved.

Measured target layout and startup values are distinct evidence:

| Measurement | Rev66 value | Change from rev65 |
| --- | ---: | ---: |
| S3 internal static use | ELF section comparison | +32 bytes |
| C5 internal static use | ELF section comparison | +3,600 bytes |
| S3 PSRAM display pool | 6,688 bytes | +1,024 bytes |
| S3 per-AP timing record | 16 bytes | New committed and staging arrays |
| C5 bounded table / entry | 3,608 / 56 bytes | New table; net static delta includes layout effects |
| S3 scan view / render cache | 152 / 792 bytes | Unchanged |
| S3 startup internal free | 116,816 bytes | -32 bytes |
| S3 startup PSRAM free | 1,428,324 bytes | Unchanged |
| C5 startup internal free | 73,540 bytes | -3,600 bytes |

BLE snapshot/focus/record, Monitor and Analyzer structures remain unchanged.
Startup shows ten S3 tasks, 171,724 internal bytes total, minimum internal
free 116,136, mounted SD, C5 firmware 51 online at 40 MHz and no prior reset
fault. At 21 SPI exchanges, mutex/fall/high/driver errors are zero; initial
timeout and legacy fallback counts are four each. All jobs and scans are
idle. The display pool is allocated when needed, so unchanged startup PSRAM
free does not measure its additional active allocation. These measurements
do not establish RF coverage, throughput or repair of another mode.

Pending optional acceptance is a normal Wi-Fi scan, switching WITHIN 30S on,
observing expiration, returning to ALL AGES and selecting Yasin Family Home
without a different row being chosen. Physical callback coverage, repeated
transitions and simultaneous Wi-Fi/BLE use remain unverified. The prior Wi-Fi
NO MEM, Association Sleep and Bad Message reports remain unresolved; GPS and
the six-C5 backboard are absent.

The S3 handles transport, frozen presentation and age eligibility while the
C5 supplies Wi-Fi reception and its own callback timing. BLE scheduling,
Find My raw eligibility, GATT/Sound actions and active radio engines are outside
this change.
