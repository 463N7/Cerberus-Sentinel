# SAE capture parity plan

Read-only design, 2026-10-30 UTC. This is proposed work, not implemented or
hardware-verified behavior. The fixed-channel SAE filter, C5 management RX
mask, selected-AP capture UI, and firmware-48 gate are already in source.
Coordinate later edits with the owner of `wifi_capture_session.c` and SD
session resources; the current restart/GPS candidate is being frozen first.

## Upstream behavior and current gap

The local `ESP32Marauder` develop snapshot is commit
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`.
`WiFiScan.cpp:6995` opens an SAE PCAP and starts passive promiscuous reception.
`getSAEACT()` at line 8909 recognizes authentication transaction 1 with
algorithm 3; its AP filter compares source addresses and its status-76 path
retains the anti-clogging token. The callback at line 9356 displays RSSI and
source/destination addresses and writes received bytes. The scheduler at line
12659 calls `channelHop(true)` every 250 ms. That helper at line 10990 rotates
through selected AP channels, or the normal channel set when none is selected.
Its dual-band array in `WiFiScan.h:927` contains 51 candidate channels.

Cerberus currently has a fixed-channel, optional single-BSSID capture request.
Its strict SAE classification checks complete fixed fields and the group field;
it does not claim cryptographic validity of scalar/element/token bytes. The
selected BSSID matches address 3, so both client commits and AP replies can be
captured. Full scan parity still needs targetless entry, channel hopping,
multiple selected APs, and live source/destination observations.

## Proposed wire additions

Keep protocol version 2 and the existing 17-byte START request, 22-byte STATUS
and PEEK responses, chunked READ, and exact generation/sequence ACK. Use a new
minimum-firmware constant for planned capture; choose the next unshipped C5
revision when implementation begins. Do not make old firmware infer support
from the general capture or SAE-filter capability.

Reserve command IDs 89, 90, and 91 only after checking they remain free:

| Command | Packed request | Response |
| --- | --- | --- |
| `WIFI_CAPTURE_PLAN` | `uint32_t generation; uint64_t channel_mask; uint16_t dwell_ms; uint8_t target_count; uint8_t reserved;` (16 bytes) | plan status below |
| `WIFI_CAPTURE_TARGET` | `uint32_t generation; uint8_t index; uint8_t bssid[6];` (11 bytes) | plan status below |
| `WIFI_CAPTURE_HOP_STATUS` | `uint32_t generation;` (4 bytes) | hop status below |

Plan status is exactly 22 bytes:
`uint32_t generation; uint64_t channel_mask; uint32_t target_present_mask;
uint8_t target_count; uint8_t state; int32_t error_code;`.
States are empty, staging, and ready. The accepted dwell is fixed at 250 ms for
this feature, so a successful response does not need a separate dwell echo.
`reserved` must be zero. Reject empty masks and bits above bit 50.

Hop status is exactly 22 bytes:
`uint32_t generation; uint64_t attempted_mask; uint64_t applied_mask;
uint16_t completed_sweeps;`.
The two masks refer to the current sweep, reset at its start, and remain as the
last sweep when stopped. The counter saturates. This exposes driver-rejected
channels without confusing a requested channel with one actually tuned.

Define one canonical shared channel table and bit mapping:

- Bits 0-13: channels 1-14.
- Bits 14-30: channels 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80,
  84, 88, 92, 96.
- Bits 31-42: channels 100, 104, 108, 112, 116, 120, 124, 128, 132, 136,
  140, 144.
- Bits 43-50: channels 149, 153, 157, 161, 165, 169, 173, 177.

These are candidate channels, not a promise that every country/driver accepts
every one. C5 must use its configured country and the checked driver's tuning
result. A selected-AP plan is the union of its selected channel bits. A
targetless plan uses all candidates in the selected band(s).

Add START flags `HOP_PLAN = 2` and `TARGET_LIST = 4`; keep `BSSID = 1`.
`HOP_PLAN` requires a ready staged plan with the same generation, `channel=0`,
and SAE mode for this first implementation. `TARGET_LIST` requires
`target_count > 0`; `BSSID` and `TARGET_LIST` are mutually exclusive. A
targetless plan has `target_count=0` and no BSSID flag. The original fixed
channel/single-BSSID request continues to use its original fields.

Bound the target list to 32 exact unicast, nonzero BSSIDs, matching the current
AP picker limit. Reject duplicates at different indices. Repeating the same
generation/index/value is idempotent; conflicting writes fail. A plan is ready
only when all indices are present (`UINT32_MAX` for count 32, avoiding an
undefined shift). Repeating an identical PLAN preserves already staged targets.
A different plan with the same generation fails. A higher unused generation
replaces abandoned staging only while capture is stopped and its ring is empty.
The S3 chooses `max(last_capture_generation, staged_generation)+1` and rejects
overflow. Staging never starts a radio or reserves a running capture.

The C5 staging storage is bounded: 192 bytes for 32 BSSIDs plus the plan,
presence mask, and small state. START freezes it until Stop and drain complete.

## C5 ownership and hopping

Use the existing capture-owner loop in `wifi_scanner.c:495`, which wakes every
50 ms to check the lease. Add a monotonic due time and perform one next-channel
attempt when 250 ms has elapsed; do not spin through missed deadlines. A
successful channel gets a full 250 ms dwell starting when tuning finishes.
Failed channel attempts are recorded and skipped; if an entire sweep has no
successful channel, stop with the first concrete tuning error.

Retuning and Stop must take the existing scanner mutex. Recheck capture
generation, ownership, and the lease after taking it. Before changing band or
channel, close a separate atomic RX channel gate. After successful tuning,
publish the new actual channel and reopen the gate. In the RX callback, accept
only packets whose driver-provided channel equals the current gate and whose
channel belongs to the frozen plan. Keep `current_request` immutable for
idempotent START. STATUS.channel reports the last successfully tuned channel;
it must no longer echo the initial request for planned sessions.

Do not restart the capture service or clear its ring at each hop. Pinned frames
from the previous channel remain valid because PEEK already carries channel,
RSSI, boot-monotonic time, length, and CRC. Target-list filtering compares each
authentication frame's address 3 with the frozen list; targetless sessions skip
that filter. No transmit operation is part of this scan.

The five-second S3-renewed lease remains active across hops. Stop closes the RX
gate, disables promiscuous reception, and retains ownership if disable fails.
A failed disable must not permit another radio owner. Existing exact ACK and
ring-drain behavior remains mandatory. A new plan/start cannot replace a
generation with pending frames. Retune, lease expiry, lost START ACK, repeated
Stop, and radio reservation cleanup must all converge on that same path.

## S3 observations and PCAP

Extend the capture start API with a scope/options entry point, retaining the
existing fixed-channel wrapper. The worker opens the SD file, stages the
channel plan and BSSIDs, verifies readiness, then starts C5. Failed staging
closes/discards the empty file. The scope is frozen before the worker starts.

`wifi_capture_session.c:read_record()` already has the full CRC-verified frame
before clearing it. Parse the SAE fixed fields there and publish a bounded
16-row recent-observation ring containing generation, frame sequence, source,
destination, BSSID, channel, RSSI, frame length, group, status, and C5 monotonic
capture time. Allocate the ring in PSRAM only for the session; do not increase
always-resident internal heap use. A snapshot getter copies rows under the
capture lock. Clear them when a new generation begins. Publish each sequence
once so a lost ACK cannot duplicate UI rows. Keep observation and SD-written
counts distinct when an append fails.

No extra per-frame SPI command is needed. Existing PCAPNG writing already
preserves the whole received frame plus C5 radio/channel/RSSI provenance.
Current no-UTC files use Simple Packet Blocks and omit timestamps; the UI must
continue to say so. The live list can show relative age from C5 monotonic time
without labeling it UTC. A future timestamp-format change is separate work.
Status-76 token bytes remain available in PCAP; the UI can show status, group,
and token length instead of dumping arbitrary token bytes.

## UI entry and behavior

Add `CAPTURE` to the Wi-Fi hub's bottom action bar using three 94-pixel buttons
(CAPTURE / LEARN / FIELD), retaining existing six main rows. Give CAPTURE a new
explicit action ID rather than reusing the current Learn/Field Survey numbers.
This opens the existing capture page in a targetless scope without selecting
or scanning an AP first. It exposes SAE Commit alongside the other available
receive filters through the existing mode control; planned hopping initially
enables only the SAE mode. No radio starts merely by opening the page.

For SAE, default to all supported candidate channels in both bands and all
BSSIDs. A scope button offers all channels, one channel, or the existing AP
picker. Reuse the picker for up to 32 APs, freezing their exact BSSIDs and union
of channels at Start. The selected-AP detail shortcut opens the same page with
its existing fixed-channel/BSSID scope. The title and scope text must say
whether it is all-channel scanning, selected channels, or one AP.

During a run show the current actual channel, completed sweeps, received and
written counts, drops, and recent RSSI/source/destination rows. Offer a detail
row for group/status/frame length. Keep Stop reachable without paging; Back
requests Stop and waits for C5 release and SD close. Apply the new minimum
firmware gate in both UI and SPI entry points, including programmatic starts.

## Required checks

1. Pure plan checks: all 51 bit mappings, invalid/empty mask, band subsets,
   selected-channel deduplication, wrap, 250 ms dwell, and timer wraparound.
2. Staging checks: 0/1/32 targets, invalid and duplicate BSSIDs, missing slots,
   identical retries, conflicting same-generation writes, abandoned staging,
   active-session rejection, and generation overflow.
3. Actual C5 owner-loop test with fake driver calls: successful cross-band
   hops, one rejected channel, all rejected channels, Stop during retune,
   expired lease, failed disable and later retry, and stale notifications.
4. RX/transport test: previous-channel frame remains readable after a hop;
   transition gate rejects stale metadata; selected BSSID lists accept both
   directions; CRC, sequence, ACK retry, and pending-ring gates still hold.
5. S3 session test: stage-before-Start ordering, failed staging closes file,
   corrupted read yields no observation row, lost ACK does not duplicate rows,
   SD failure preserves honest counters, and new generation clears old rows.
6. PCAP parse test: several channels in one file retain exact frames, channel,
   RSSI, and C5 interface identity; no invented UTC timestamps.
7. UI render/route checks: targetless entry is reachable, selected-AP shortcut
   preserves identity, multi-AP picker freezes scope, firmware gate explains
   unavailable hopping, and Stop/Back remain visible and wait for cleanup.
8. Paired builds and recovered flash, followed by on-device targetless
   hopping/Stop and selected-AP capture against a controlled WPA3 connection.
   Verify received SAE bytes in the saved PCAP independently. Absence of a
   WPA3 exchange does not establish that the receive path is broken or working.

Until those gates pass, record fixed-channel SAE capture, planned hopping,
multi-AP filtering, live observations, PCAP, and RF acceptance separately in
the parity ledger. The existing fixed-channel implementation alone does not
prove full upstream SAE scan parity.
