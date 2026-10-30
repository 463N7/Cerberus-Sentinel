# Passive PineScan evidence

Installed checkpoint, 2026-10-30: S3 rev58 with the unchanged onboard C5 50
images. The route is **Scan -> Wi-Fi -> TOOLS -> PINESCAN -> START**. All five
stable builds, matched-package validation, final S3 ISR gate and three S3
write hashes pass. Boot confirms SD mounted, the 40 MHz C5 link and no reset
fault. Idle S3 internal free heap is 117,024 bytes, 304 below rev57; the new
workflow adds 304 internal static bytes. The shared evidence bank is 6,648
target bytes in lazy PSRAM, eight more than rev57; PineScan's separately lazy
display store is 1,584 bytes. No additional resident task was added. See the
[installation receipt](../../integration/device-test-2026-10-27/rev58-flash-receipt.json).
Live beacon reception, positive cases, Follow, both-band coverage, repeated
transitions and PCAP contents remain unverified.

This original Cerberus C implementation reproduces the valid-beacon pattern
rules of Marauder `develop`
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363).
The parent verified that remote head remained current on 2026-10-30. The source
inventory pin is unchanged. Rule names describe source patterns, not verified
hardware vendors or hostile devices. No upstream callback/list implementation
was copied, and this helper has no radio, allocation or transmission API.

The installed workflow is a generic PineScan entry, C5 dual-band
beacon reception, S3 analysis and immutable result pages, optional SD capture,
and receive-only signal follow for a chosen transmitter. MultiSSID and
PineScan share one session manager, mutex, union evidence bank and capture
worker. A successful new-mode Start replaces prior retained results; a failed
Start preserves them. Stale mode calls cannot stop or free the new mode.
Source/host/build verification and hardware acceptance are tracked separately;
parser fixtures alone do not establish that the installed workflow works.

## Upstream workflow and predicate audit

At the pinned snapshot, `WiFiScan.cpp:5292` starts PineScan by clearing its
previous list, opening `pinescan` PCAP, selecting promiscuous Wi-Fi reception
and setting the channel. `StartScan` dispatches it at 2854, and the general
channel scheduler includes the mode at 3207 and 12642. The sniff menu routes
to it in `MenuFunctions.cpp:5228–5231`; CLI `sniffpinescan` is dispatched in
`CommandLine.cpp:1038–1039`. Results can be listed at 1707–1708 and selected
for Fox Hunt at 828–831. Menu Fox Hunt selection uses
`MenuFunctions.cpp:2066–2068,2121–2122,2151–2152,2201`. The target getter at
`WiFiScan.cpp:12503` passes the observed transmitter address, label and channel
to signal follow.

The callback at `WiFiScan.cpp:8146` processes beacons and stores one result
after **one qualifying beacon**; no three-SSID or repeated-match threshold
applies. Its key and prefix source are `addr2` at offset 10, not `addr3`/BSSID.
It tests two independent predicates and combines them with OR:

1. An exact three-byte transmitter prefix, subject to its privacy-bit rule.
2. Capabilities exactly `0x0001`, with exactly SSID then DS Parameter Set IEs.

The prefix conditions are:

| Exact prefix | Upstream profile hint | Capability privacy-bit condition |
| --- | --- | --- |
| `00:C0:CA` | Alfa | Clear |
| `00:13:37` | Orient Power | Either |
| `1C:BF:CE` | Shenzhen Century | Clear |
| `0C:EF:AF` | IEEE Registration Authority | Clear |
| `02:C0:CA`, `02:13:37` | Hak5 local-prefix rule | Set |
| `00:0A:00`, `00:0C:43`, `00:0C:E7`, `00:17:A5` | MediaTek | Either |
| `9C:EF:D5`, `9C:E5:D5` | Panda Wireless | Either |
| `DE:AD:BE` | Address pattern | Either |

This is all 13 prefixes from `WiFiScan.cpp:8074–8103`, not a general vendor
database or a broader normalized-prefix match. The locally administered bit
is never cleared to manufacture an OUI match. `DE:AD:BE` and the local-prefix
rules are packet address patterns, not evidence of an IEEE assignment.

“Privacy” here means capability bit `0x0010` only. Upstream calls the two
states open/protected; neither state proves the actual encryption, RSN/AKM,
authentication or security policy. Cerberus exposes the bit and rule condition
explicitly. Adding an RSN or vendor IE does not replace that condition.

The minimal-layout rule requires SSID IE 0 first, DS IE 3 with length 1 next,
and the DS value as the final byte. Additional IEs, reversed order, or any
capability bits beyond `0x0001` prevent this layout match. An empty or all-zero
SSID is allowed and retained as its exact bytes. The DS channel remains an
untrusted advertised value; it does not override receiver metadata.

## Validation and deliberate differences

The classifier first calls the existing strict `cerberus_wifi_beacon_decode`.
Only complete 802.11 beacon bytes without FCS/radiotap are accepted. The shared
decoder validates frame type/version, header/address classes, broadcast
destination, nonfragmented/unprotected header, one bounded SSID, at most one
well-formed DS element, every IE boundary and the 2,304-byte limit. Unknown
complete IEs are allowed. This is structural validation, not semantic or
cryptographic validation of every arbitrary IE payload.

The caller must establish that the source frame was complete. A buffer ending
exactly at an IE boundary can also be a legitimate shorter beacon, so the
parser cannot detect a prior transport truncation from bytes alone. The
existing C5 complete-frame length, immutable chunk record and full-frame CRC
contract supplies that boundary before analysis.

Upstream assumes the SSID is first when reading its label/channel and its
minimal-layout helper tolerates a one-byte trailing remainder. Cerberus rejects
that malformed remainder. Prefix rules still work on a structurally valid
beacon whose SSID appears later; the minimal-layout rule requires its exact
order. Cerberus does not reproduce callback display-buffer gating, random
callback delay or unsafe short-frame reads.

The C5 callback's boot-monotonic microsecond timestamp, actual RX channel and
RSSI are retained independently from the advertised channel. They are not
UTC, not a hardware receive timestamp and not the time of later S3 delivery.
The UI can display S3 delivery ages separately. RSSI is a signal trend, not
distance or device identity.

## Bounded table and evidence meaning

`wifi_pinescan.h` exposes a stateless decoder and a caller-owned bounded table,
default 32 rows and maximum 128. The production session places the table
bank in S3 PSRAM and reuses the existing C5 beacon-only capture worker. Only
matching transmitter identities consume rows; unrelated beacons are counted
without allocating an entry. Upstream's extra pre-match tracking list is not
needed because admission requires a single match.

Rows preserve:

- `latest`: the newest valid beacon from that exact transmitter, including a
  later beacon which no longer matches.
- `last_match`: the latest qualifying beacon and its exact reason bits,
  profile, privacy condition, privacy bit and total IE count.
- `reasons_seen`: a historical OR of reasons, never a claim that the latest
  frame satisfies every reason at once.
- First stored match time, received/matching frame counts, peak RSSI and a
  flag recording a change in claimed BSSID.

SSID values are raw length-qualified bytes, including embedded NUL and binary
bytes. No name hashes, presentation strings or BSSID substitute for the key.
Value copies remain immutable when later observations update the table.
One transmitter address may be spoofed or reused; records do not prove one
physical device or simultaneous network operation.

After capacity is reached, existing rows continue updating and new matching
identities increment `capacity_drops`; nothing is silently evicted or cleared.
`valid_beacons` counts structurally valid inputs and `matching_beacons` counts
their matching classifications, including stale/full inputs. `stale_frames`
reports source times older than the session's latest accepted source time;
they never alter a row. Equal timestamps are permitted. All 32-bit counters
saturate. The caller resets the table per capture generation/source clock and
deduplicates retried transport records before observing them.

These are weak fingerprints. Ordinary adapters, AP reconfiguration and
forged beacons can match. The UI labels a prefix/layout **hint**, displays
the exact evidence, and avoids “confirmed Pineapple” or attack attribution.
Follow filters the selected `addr2`, because an `addr3` BSSID filter is a
different identity rule. Following a target does not reclassify later ordinary
beacons as fresh PineScan matches.

## Host checks

Run `py -3.14 tools/check_wifi_pinescan.py`. The fixture compiles production
PineScan and shared beacon decoding with warnings as errors and no SDK or
radio dependencies. It covers all 13 prefixes with both privacy states,
prefix exactness/local-bit differences, addr2 versus addr3, independent and
combined reason bits, exact layout/order/capability gates, malformed full-body
and IE tails, binary/32-byte/hidden-like SSIDs, no-DS five-GHz observations,
receiver/advertised channel and callback-time provenance, preserved matching
proof after a later nonmatch, changing BSSID, frozen copies, capacity overflow
with continued updates, global stale-source rejection, saturation and reset.

Production `check_wifi_multissid_session.py` now checks both modes, including
failed switches, stale-mode and generation isolation, immutable pages,
matching-proof retention, Follow and PSRAM release. The 40 cases in
`check_wifi_capture_session.py` cover capture/SD/ACK/cleanup faults.
`check_wifi_pinescan_navigation.py` and `check_wifi_multissid_navigation.py`
exercise full entry paths and their actual UI/rendering fixtures. Selected
and absent AP states pass, and native menu/MATCH/LATEST/Follow previews were
inspected. Independent classifier, lifecycle and UI evidence reviews found
no blockers.

Hardware acceptance still requires reception/positive cases, normal scan
recovery, repeated transitions, saved-file inspection and on-device Follow.
A nearby environment with no matching beacons can validly show zero rows
while its valid-beacon count increases.
