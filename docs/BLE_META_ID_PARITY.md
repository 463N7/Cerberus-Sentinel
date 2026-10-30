# Meta identifier discovery parity

Source audit: 2026-10-30, against Marauder `develop` commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363).
This patch concerns received identifier evidence in the existing BLE focused
list, selected details and Analyzer. It adds no advertising, GATT operation,
Sound eligibility, Beep All behavior or radio mode. Parser and host-workflow
checks are source validation; installed and physical acceptance are separate.

## Upstream behavior and the concrete gap

Marauder's [Meta Detect menu](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/MenuFunctions.cpp#L5526)
starts `BT_SCAN_RAYBAN`; the [CLI](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/CommandLine.cpp#L1387)
also exposes Meta sniff. Its
[receive callback](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L1707)
checks manufacturer identifiers, service UUIDs and service-data UUIDs, then
displays matching address/name/RSSI observations. The
[general BLE classifier](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L2171)
uses manufacturer and service UUID checks for Meta Fox Hunt targets.

The inherited [identifier tables](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.h#L962)
contain six allowed values (`FD5F`, `FEB7`, `FEB8`, `01AB`, `058E`, `0D53`)
and five suppressing values (`FD5A`, `FD69`, `004C`, `0006`, `FEF3`). These are
discovery heuristics inherited from that source, not a newly verified model
catalog. The same numeric value in a manufacturer field and a service field
belongs to a different namespace; Cerberus retains that distinction in evidence.

Before this patch, `ble_ad_hints.h` recognized these values only in 16-bit
service lists, the leading UUID of 16-bit service data and the leading company
identifier of manufacturer data. It validated the lengths of wider UUID
fields but ignored their identifiers. For example, this complete AD field
encodes service `0000FD5F-0000-1000-8000-00805F9B34FB` and was missed:

```text
11 07 FB 34 9B 5F 80 00 00 80 00 10 00 00 5F FD 00 00
```

Marauder's [UUID helper](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/utils.h#L63)
extracts four characters from a 36-character UUID string. That also accepts
unrelated custom UUIDs sharing those characters. Cerberus does not reproduce
that ambiguity: a UUID32 must have zero upper 16 bits, and a UUID128 must be
an exact Bluetooth-base encoding with zero upper 16 bits. The original parser
implementation uses format facts and the inherited value table; upstream
callback code is not copied.

## Exact, bounded evidence

`s3_main/main/ble_meta_hints.h` defines `cerberus_ble_meta_evidence_t` as ten
bytes containing 77 possible identifier/context pairs. The fixed identifier
order is the six allowed values above, followed by the five suppressing values.
Each identifier has seven contexts:

| Context | AD type | Value interpretation |
| --- | --- | --- |
| `MFG16` | `FF` | Leading two-byte manufacturer identifier only |
| `LIST16` | `02`, `03` | Every complete two-byte service UUID |
| `LIST32` | `04`, `05` | Every complete zero-extended four-byte service UUID |
| `LIST128` | `06`, `07` | Every exact Bluetooth-base 128-bit service UUID |
| `DATA16` | `16` | Leading two-byte service UUID only |
| `DATA32` | `20` | Leading zero-extended four-byte service UUID only |
| `DATA128` | `21` | Leading exact Bluetooth-base 128-bit service UUID only |

Bit `id_index * 7 + context` records one exact pair. The final three bits
remain zero. Repeated occurrences do not increase the pair count; multiple
identifiers and contexts remain distinct. A report-wide ID mask plus a separate
width mask would lose which width belonged to which ID, so it is not used.
The helpers `cerberus_ble_meta_evidence_has`, `_count`, `_allowed`, `_blocked`
and `_at` inspect bounded evidence without allocation or received-byte pointers.
Enumeration returns stable ID/context order. Invalid reads leave caller outputs
unchanged; padding bits are ignored. The parser's existing `meta_id` and
`blocked_id` booleans are derived from the matrix after successful validation.

Only exact field values count. Identifiers embedded later in manufacturer or
service payloads, names, arbitrary fields, reversed UUIDs, nonzero UUID32 upper
bits and custom UUID128 substrings do not acquire a service/company meaning.
An exact manufacturer or service identifier remains unauthenticated and may
be copied by unrelated devices. It does not prove Meta glasses, a manufacturer,
a model, ownership, purpose or recording status.

## Validation and source ownership

`cerberus_ble_ad_hints_parse` is bounded to one report of at most 255 bytes.
It clears the caller output first, builds a local result, and commits only
after the entire AD stream passes. A positive prefix followed by malformed
data cannot leave names, identifier bits or compatibility flags behind.
Checks cover AD bounds, all UUID-list strides, manufacturer/service-data
minimum lengths, exactly one Flags value byte, conflicting nonempty complete
names and zero-only padding after a terminator. Complete unknown fields remain
allowed; arbitrary vendor payloads are not semantically validated.

HC name behavior remains exact: only complete, untruncated names `HC-03`,
`HC-05` and `HC-06` qualify. A shortened name, suffix, wrong case or binary
near-match does not qualify. Empty complete-name fields provide no name and
do not contradict a nonempty complete name, matching the existing Find My and
Flipper parser contract. Conflicts are checked against full name bytes even
when the display copy is limited to 32 bytes. These are name hints, not proof
of a skimmer.

The helper has no identity, timestamps, radio completeness or cached fields.
Integration must keep ADV and matched-response matrices separate within the
existing shared discovery source record. The snapshot binds responses to the
exact address/type, SID, generation, report sequence and original time window;
response data must not renew the original advertisement's age or raw bytes.
Invalid/new originals invalidate old response binding. A complete source longer
than the bounded raw copy may still supply valid evidence, but source-complete
and raw-copy-complete flags must remain distinct.

Blocked identifiers suppress a Meta hint independent of field order. The
existing snapshot policy additionally retains suppression for that address
through the sweep. Current per-source matrices and source ages must not be
relabelled as historical blocker evidence. No helper merges separate packets
or makes a decision about an active operation. The S3 uses the existing BLE
scanner and Analyzer; the C5 keeps its independent Wi-Fi role. No new worker,
scan mode or duplicate menu is required.

## Host checks and acceptance limits

Run `py -3.14 tools/check_ble_ad_hints.py`. It compiles the actual production
headers with warnings as errors and passes 6,241 fixture checks covering:

- All eleven IDs in every field context, both list variants, multiple list
  positions, repeated exact pairs and mixed identifiers without context loss.
- Every allowed/blocked pair across every context pairing, in both orders.
- Exact UUID normalization, mutations of every base/upper byte, misleading
  payloads, unknown neighboring values, arbitrary fields and reversed UUIDs.
- Every prefix truncation of a relevant field, valid/invalid known field
  lengths before and after a positive field, malformed tails, 255/256-byte
  limits, empty input, null arguments, canaries and immutable source bytes.
- Complete/shortened/empty/conflicting/long/binary HC names and failed-parse
  output clearing. All structural decisions are cross-checked against the
  existing passive Find My and Flipper parsers.
- Full 77-pair capacity, stable lossless enumeration, duplicate suppression,
  invalid indexes/contexts and padding-bit boundaries; the matrix is 10 bytes.

Integration checks must separately exercise matched-response provenance,
ordinary or invalid replacement, retained sweep suppression, source-specific
ages, the existing focused/sorted selection, frozen detail and Fox Hunt/Back,
Analyzer evidence and unchanged raw active-action eligibility. Hardware
reception of wider UUIDs, readability and repeated/concurrent radio sessions
remain unverified until recorded independently. These parser checks alone are
not a claim of complete upstream workflow or installed-feature acceptance.

## Integrated workflow checks

The snapshot now stores `meta_advertisement` and `meta_scan_response` in the
shared discovery record. `focus.meta_suppressed` is separate sweep history;
ordinary, invalid and incomplete replacements retire current positive pairs
without pretending that a historical blocker belongs to the new source.
Unmatched responses cannot add a hint or poison suppression. Existing raw
advertisement bytes, timestamps and Find My action eligibility are preserved.

Analyzer's latest event stores the same exact matrix, ADV/RSP origin and
validity. Meta/HC classification accepts complete reports up to 255 bytes;
Watch and iBeacon keep their existing whole-report 31-byte contract. Later
ordinary, malformed, incomplete and oversized events clear displayed earlier
identifier evidence. Counters measure callback events, not unique devices or
all packets transmitted over the air.

The normal Meta list filter, sorting and frozen selection reach the existing
Fox Hunt flow. Selected details display independent ADV/RSP pairs and ages,
with at most two pairs per line plus an explicit remaining count. Analyzer
keeps stronger Watch evidence visible. Native list and Analyzer previews,
changed-detail previews, maximum counters/ages and the complete 77-pair
capacity fit without clipping. Physical-screen readability remains a separate
acceptance check.

The read-only console aggregates exact pairs independently for ADV and RSP,
then checks completed-sweep generation/count/state again before printing.
It prints no partial aggregate if retrieval fails or changes mid-read. Pair
counts refer to unique ID/context combinations across the completed scan,
not device counts. Row counts and sweep-suppressed rows are labeled separately.

Fourteen affected host suites passed against stable source inputs, including
the production scanner in legacy and extended configurations, snapshot,
Analyzer, Find My/Flipper regressions, display publication, selection/return
routes, native UI rendering, console coherence and unchanged raw action-gate
negative cases. Independent parser, backend, diagnostic and UI reviews found
no blocker. The receipt is `rev61-host-checks.json` in the integration evidence
directory. Build, installation and physical acceptance are recorded below
only after their own checks succeed.

## Rev61 installed checkpoint, 2026-10-30

Installed `candidate-ble-meta-evidence-compact` on the verified Cerberus S3.
All five target builds and the matched package passed with stable inputs;
the final READY interrupt check and all three S3 flash-write hashes passed.
The onboard C5 images are byte-identical to the retained verified C5 50
installation. NVS, PHY and SD were preserved; the frozen rev60 package remains
available for rollback. See the
[installation receipt](../../integration/device-test-2026-10-27/rev61-flash-receipt.json).

Exact target ELF sizes are 56 bytes per shared discovery record, 100 per
focus, 208 per scanner record, 8,176 per snapshot, 6,920 per Monitor and 216
per frozen row. Analyzer core/view are 244/240 bytes. The two flag bitfields
remove four padding bytes per target row compared with the first candidate,
saving 128 bytes per 32-row bank. Relative to rev60, each row grows 20 bytes
and each bank grows 640 bytes; those banks remain in PSRAM. Analyzer core/view
grow 12 bytes each. No raw response buffer, radio task or duplicate mode was
added. These are target measurements, not host pointer-layout estimates.

S3 application size is 1,658,800 bytes (+2,944); internal static use grows
48 bytes. Fresh boot reports 116,912 internal bytes free, minimum 116,232,
1,428,324 PSRAM bytes free and ten tasks. C5 has 77,140 bytes free and its
40 MHz SPI link is online; SD is mounted and no new reset fault is recorded.
At 92 startup exchanges, mutex/fall/high/driver error counters are zero;
initial READY timeouts and legacy fallbacks are four each. All radio jobs are
idle, BLE generation is zero and all three discovery summaries correctly
report `not_complete` before a scan. Boot checks do not establish RF coverage,
throughput or memory behavior after long sessions.

Current-build physical Meta encodings, response-only reception, screen
readability, repeated exits and simultaneous Wi-Fi/BLE operation remain
pending. The normal scan/Analyzer/Stop/Back/following-Wi-Fi workflow has been
requested from the owner. Rev59 Find My labels and Monitor exits retain their
separate limited owner-and-console acceptance. Wi-Fi NO MEM, Association
Sleep and Bad Message remain unresolved; this checkpoint does not establish
full Marauder parity or repair those active modes. GPS and the six-C5
backboard are absent.
