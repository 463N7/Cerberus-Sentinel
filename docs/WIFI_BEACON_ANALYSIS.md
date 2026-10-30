# Passive beacon analysis

Current installation: S3 rev58 retains rev57's normal Wi-Fi AP-list access to the
generic tools hub. The route is **Scan -> Wi-Fi -> TOOLS (bottom right) ->
MULTISSID -> START**, with or without a retained AP selection. Selecting an
AP row still opens its details. The C5 50 images and capture/analysis engines
retain the paired rev56 receive contract described below. Rev58 moves S3
lifecycle into one shared beacon-analysis session manager and lazy PSRAM bank
for MultiSSID and PineScan. The existing MultiSSID API remains mode-scoped;
stale PineScan calls cannot stop or release its session. Production
launch-to-Start navigation, existing UI and BLE-route checks pass, with an
independent review; all five stable builds, matched package, three S3 write
hashes, boot/SD/40 MHz link pass. Rev58 touch/RF acceptance remains pending.

Current source scope, 2026-10-30: the parser and accumulator are integrated
with a generic Wi-Fi menu entry, C5 v50 beacon-only hopping, an S3 PSRAM
analysis session, immutable evidence details, optional PCAP export and
receive-only selected-transmitter follow. Production host checks pass; the
paired rev56 firmware was installed, including onboard C5 firmware 50. All five
stable build targets, matched-package validation and nine flash-write hash
checks passed. The device owner reports that the MultiSSID beacon count
increases, Stop completes and a subsequent normal Wi-Fi scan works. These are
owner acceptance reports. A read-only console snapshot on the same boot showed
capture state 0/generation 0 with no retained capture, while completed normal
Wi-Fi scans increased from 0 to 2 with zero failures and the device was idle.
That snapshot corroborates normal scanning and idle state, not the reported
MultiSSID run. Capture-specific validation remains unresolved pending live
counters from a repeat left running. Controlled three-SSID detection, live
Follow and saved PCAP contents remain unverified. This is partial owner
acceptance, not a claim of full upstream equivalence.

## Upstream behavior audited

Reference: Marauder `develop` commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363),
verified against the remote by the parent on 2026-10-30. Paths below are in
`esp32_marauder` at that snapshot.

| Mode | Actual source behavior | Cerberus integration implication |
| --- | --- | --- |
| MultiSSID | `WiFiScan.cpp:5340` opens PCAP and enables receive-only promiscuous scanning; `multiSSIDSnifferCallback` at 8505 processes beacons, keys records by transmitter address at offset 10, and reports after three distinct 16-bit SSID hashes. Empty SSID is a sentinel. `WiFiScan.h:198` sets threshold 3. Confirmed rows retain latest nonempty label and strongest RSSI; list/selection feeds Fox Hunt. | Raw complete beacons are required; SDK AP inventory can collapse successive SSID variants. Cerberus now integrates a generic entry, beacon-only hopping, exact-byte evidence, optional capture/export, selected-transmitter follow and cleanup. Owner acceptance covers increasing broad beacon counts and Stop followed by normal Wi-Fi scanning; controlled three-variant detection, live Follow and PCAP contents still need verification. |
| PineScan | `WiFiScan.cpp:8072` onward uses a prefix/privacy-bit table or a minimal beacon with capabilities `0x0001` and only SSID then DS tags. The callback is beacon-only despite the broader mode name. Several prefixes include ordinary widely used adapters. | Rev58 installs raw-beacon classification, immutable qualifying/latest evidence, selected Follow and optional PCAP using the shared session engine. [PineScan evidence](WIFI_PINESCAN.md) separates exact reasons from device identity; live RF and saved-file acceptance remain pending. |
| Espressif | `WIFI_SCAN_ESPRESSIF` is defined in `WiFiScan.h`/`Display.h` and included in legacy menu state checks. A repository-wide search found no active dispatcher, handler, or start route for this symbol in the pinned snapshot. | Do not invent a missing upstream workflow based on this constant. An optional SD OUI vendor filter would be a separate useful feature, with vendor-assignment limits; that is not implemented here. |

MultiSSID list and Fox Hunt evidence: `MenuFunctions.cpp:2067–2071`,
`2122–2154`, `2202`; CLI sniff entry `CommandLine.cpp:1041–1043`, list at
`1711–1712`. The general scan scheduler includes `WIFI_SCAN_MULTISSID` in
`WiFiScan.cpp:3208` and `12643`. Cerberus now implements beacon-only channel
hopping through its existing capture service; live coverage of both bands and
individual channels remains unverified.

The helper is original C code with synthetic fixtures. It does not copy the
upstream linked-list implementation, 16-bit hash, callbacks, or radio setup.

## Evidence contract

Input is a complete successful-RX 802.11 beacon, without radiotap or FCS, plus
the actual receiver channel, RSSI and C5 receive-callback monotonic timestamp.
This is not the Wi-Fi hardware RX timestamp. The caller must
establish completeness from its capture metadata; a byte parser cannot infer
that a frame ending at an IE boundary was truncated earlier in transport.

The decoder rejects non-beacons, nonzero protocol versions, DS/fragmented/
protected/ordered frames, nonbroadcast destinations, invalid transmitter or
BSSID address classes, missing/duplicate SSID fields, SSIDs over 32 bytes,
malformed/duplicate DS tags, and any incomplete trailing IE. It validates the
entire frame before publishing output. Unknown complete IEs are skipped;
SSID need not occur first. The 2,304-byte bound is explicit.

Receiver channel and advertised DS channel are separate. An adjacent-channel
beacon does not silently move the receiver, and a packet's DS value does not
prove a valid or supported channel. Five-GHz frames without DS retain their
receiver channel and advertised-channel zero. Radio/regulatory channel
validation belongs to the caller.

MultiSSID state keys by the exact transmitter address (addr2), matching the
upstream observation key. A change in addr3/BSSID is latched separately. It
stores three exact SSID values, avoiding collisions such as upstream hashes
for `Aa` and `B@`. Empty and all-zero SSID encodings share one hidden-like
sentinel to avoid treating different blank lengths as different network
names. Binary nonblank SSIDs, including embedded NUL bytes, remain distinct.
Latest raw SSID evidence is preserved even when blank normalization applies.
This normalization is a deliberate false-positive reduction relative to the
upstream empty-only sentinel, not a byte-for-byte copy of its predicate.

The third distinct variant raises one confirmation event. The stored count
saturates at three and means **at least three observed variants**, including
at most one hidden-like sentinel. It is not the total number of networks or
physical devices. Later evidence updates current channel, RSSI, source time
and latest SSID, while retaining the peak RSSI and first observation time.
Older source timestamps cannot replace current evidence. There is no automatic
permanent identity or inference of simultaneous operation: legitimate AP
configuration changes and spoofed addresses can both produce this pattern.

The standalone accumulator resets when initialized for a new source session.
The integrated service starts a fresh table for each broad scan and retains it
unchanged during a separate Follow session. Storage is caller-owned and
bounded; the helper performs no allocations or radio calls. The default 32 rows use
5,888 bytes on the host ABI (184 bytes per row), plus a small table descriptor;
the S3 service allocates this storage in PSRAM. A full table preserves existing rows and
increments an explicit drop counter for new observations. Confirmed state is
not silently evicted. The caller serializes updates/copies and copies a record
before allowing later observations to change selection ordering.

## Integrated source workflow and remaining acceptance

1. Scan → Wi-Fi → TOOLS → MULTISSID is a generic receive-only entry. START is
   explicit; no AP selection is required. Analysis can run without SD. SAVE
   requests transactional PCAP output and is explicit in the view.
2. C5 accepts only beacons for this mode, pauses admission while retuning,
   and drains admitted callbacks before each channel change. The existing
   scanner worker hops with 250 ms dwell across its 2.4/5 GHz list; SDK/country
   rejections are skipped. One capture generation, ring sequence and CRC
   contract survive every hop. Status channel zero denotes retuning; pinned
   frames retain their actual receiver channel. Band/channel restoration
   failures retain ownership for retry. See [the radio contract](WIFI_CAPTURE_HOPPING.md).
3. The existing S3 capture worker reads complete CRC-verified frames and calls
   the analyzer once per sequence, independently of SD append and ACK retries.
   Frame/block workspaces are now session-only PSRAM, not internal static
   buffers. No second worker is created for analysis. The S3 owns bounded
   evidence, presentation and export; the C5 supplies observations.
4. The list shows confirmed address groups only. Details keep a copied record,
   escaped binary SSIDs, hidden-like normalization, latest raw SSID, lower-bound
   variant count, RSSI, separate receiver/advertised channels and source span.
   S3 delivery ages are labeled as delivery/update age; clocks from separate
   processors are never subtracted to invent RF age. Findings do not establish
   physical device identity, simultaneous networks or hostile intent.
5. FOLLOW first stops the broad scan, then opens a new fixed-channel receiver
   for the selected row's receiver channel. Only exact transmitter/addr2
   matches update its separate signal panel; the original broad evidence stays
   frozen. The existing BSSID/addr3 filter is deliberately not substituted for
   transmitter identity. Stop and Start ownership use exact, locally unique
   capture generations; stale views cannot stop a different capture.
6. Stop/Back/sleep cleanup retains the context until reception is stopped,
   the ring is drained and any SD file is closed. An SD-close mutex timeout
   remains STOPPING/busy and retries. USB handoff stays blocked by an open file.
   An unreadable or torn record is not reported as saved. PSRAM failures,
   pre-radio failures and generation overflow have explicit error paths.
7. Rev56 is built and installed. The owner accepted increasing broad beacon
   counts, Stop and a subsequent normal Wi-Fi scan. The console corroborates
   two normal scans and idle state only; its zero capture generation does not
   corroborate the MultiSSID run. Live capture counters remain pending.
   Remaining acceptance includes controlled three-variant
   beacons producing a confirmed group, live selected Follow, saved PCAP
   content, both-band reception, repeated Start/Stop and follow-to-scan
   transitions, SDK channel restrictions, long sessions and real scheduler/
   mutex behavior. Host checks alone do not prove live RF detection.

## Host verification

Run `py -3.14 tools/check_wifi_beacon_analysis.py` in this Windows environment
(or a Python interpreter with `ziglang` installed). It compiles the production
helper with warnings as errors and an isolated Zig cache.

Fixtures cover reordered SSID tags, binary and 32-byte SSIDs, hidden-like
normalization, trailing/duplicate/malformed IEs, header/address/fragment gates,
adjacent-channel provenance, source timestamp ordering, exact SSID hash
collisions, repeated frames, independent transmitters, changing BSSID, frozen
selection, table saturation with continued existing-row updates, saturated
counters and clean session reset. No radio transmissions or device actions
are used. Additional production checks are `check_wifi_capture_filter.py`,
`check_wifi_capture_service.py`, `check_wifi_capture_radio.py`,
`check_wifi_capture_link.py`, `check_wifi_capture_session.py`,
`check_wifi_multissid_session.py` and `check_wifi_multissid_ui.py`.
They cover radio lifecycle, CRC/sequence ownership, source clocks, failure
cleanup, no-SD operation, immutable page/follow identity and actual UI routes.
Native screen previews were inspected. Rev56 build, matched-package and flash
verification passed; the owner reported broad reception and Stop followed by a
normal Wi-Fi scan. The captured console verifies normal scans and idle state,
not MultiSSID reception or Stop. Controlled MultiSSID detection, live Follow,
PCAP contents and full RF coverage remain unverified.
