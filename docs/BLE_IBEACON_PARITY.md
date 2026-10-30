# iBeacon receive evidence parity

Source audit: 2026-10-30, against Marauder `develop` commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363).
This work concerns received iBeacon-format evidence, the existing focused
inventory/details, Analyzer and receive-side target selection. It adds no
advertising, GATT operations, Sound eligibility or new radio mode. Host checks
do not establish installation, physical reception or complete upstream parity.

## Upstream behavior and the existing gaps

Marauder exposes [iBeacon Sniff and iBeacon Info](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/MenuFunctions.cpp#L5457).
The information page displays UUID, major, minor, measured power and received
RSSI. Its [callback](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L1385)
classifies the received payload and refreshes retained matching records with
the new payload/RSSI/time. The [iBeacon target list](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/MenuFunctions.cpp#L2082)
uses those retained observations for Fox Hunt.

The upstream [parser](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/IBeacon.h#L23)
searches for a 25-byte sequence starting `4C 00 02 15` anywhere in the supplied
payload. It has no 31-byte container limit and accepts the first match without
validating AD field context or later data. Its [identity comparison](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/IBeacon.h#L15)
compares UUID/major/minor. Those are advertised values, so equal tuples do not
prove that two reports came from the same physical device.

Cerberus already decoded an exact manufacturer AD field, but rejected every
whole report longer than 31 bytes. Its snapshot also retained only the first
matching payload while replacing raw packet/time with later reports; the old
detail view did not distinguish those evidence ages. A valid iBeacon field
plus additional complete AD fields could therefore be missed, and a changed
advertised tuple could leave old detail values beside newer observation data.
The parser change addresses container coverage and transactional output;
snapshot/UI work must separately address current evidence and source binding.

## Bounded decoder contract

`shared/ibeacon.h` retains `cerberus_ibeacon_t`,
`cerberus_ibeacon_parse_ad(data, length, out)` and the three result symbols:

| Result | Meaning | Output |
| --- | --- | --- |
| `CERBERUS_IBEACON_FOUND` | Complete valid AD stream with an unambiguous exact iBeacon field | Decoded values |
| `CERBERUS_IBEACON_NONE` | Complete valid AD stream without an exact iBeacon field | All zero |
| `CERBERUS_IBEACON_MALFORMED` | Invalid arguments, invalid AD stream, or conflicting exact iBeacon fields | All zero when output is nonnull |

The old decoder left output unchanged for non-FOUND results. The new contract
clears a nonnull output before validating input and commits decoded fields
only after the entire report succeeds. The existing production consumers in
snapshot and Analyzer already use decoded output only on `FOUND`; retaining
old evidence must be an explicit, separately timestamped caller decision.

The maximum complete container is 255 bytes. Normal legacy advertising fits
the 31-byte advertising-data limit: the legacy fixture has a three-byte Flags
field, a 27-byte manufacturer AD structure and zero padding. Supporting an
otherwise valid 32–255-byte container is an explicit decoder capability. This
audit does not verify that any particular iBeacon hardware, upstream board or
SDK emits an extended advertising format, and it does not change the iBeacon
manufacturer payload or radio packet format.

An exact manufacturer value contains 25 bytes:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | Company bytes `4C 00` |
| 2 | 2 | Type/length bytes `02 15` |
| 4 | 16 | Advertised UUID bytes, retained exactly |
| 20 | 2 | Major, network byte order |
| 22 | 2 | Minor, network byte order |
| 24 | 1 | Signed measured-power byte, decoded over −128…127 |

The containing AD type must be manufacturer data (`FF`). A marker in a name,
service-data payload, UUID list or later portion of unrelated manufacturer
data does not qualify. Other well-formed manufacturer values, including
different value lengths or prefixes, remain ordinary fields.

Byte-identical repetitions of the exact 25-byte value coalesce into one
interpretation. Two differing exact fields, including a difference only in
measured power, return `MALFORMED` and zero output. That result represents an
iBeacon-specific ambiguity; other passive parsers may still find valid,
independent evidence in the structurally well-formed report. No first-wins or
last-wins duplicate choice is made.

Every AD field is validated, even after an exact match: field boundaries,
UUID-list strides of 2/4/16 bytes, manufacturer and service-data minimum
lengths, exactly one Flags value byte, conflicting nonempty complete names
and zero-only padding after a terminator. Empty complete names supply no name
and do not conflict with nonempty names, matching the existing strict passive
parsers. Unknown complete fields are permitted; arbitrary vendor contents are
not semantically or cryptographically validated. The implementation is bounded
and allocation-free and does not copy upstream substring-search code.

## Source and identity requirements

The caller must establish that the radio delivered a complete report. A
truncation exactly at an AD boundary can be a valid shorter byte stream; the
decoder cannot discover an omitted tail. Never concatenate ADV and response
bytes to manufacture a field or resolve a conflicting interpretation.

Current source evidence uses the shared address/type, generation, sequence,
SID where available, and observation time. A matched response must use the
existing exact source-binding rules, and must not refresh the original ADV's
time or raw bytes. Complete source evidence may outlive the size of the bounded
raw copy; a truncated raw copy is not the same as an incomplete source.
The compact latest qualifying iBeacon record retains its own same-sweep
sequence, time and ADV/response origin under the snapshot's typed address.
Its historical SID is deliberately not duplicated. After source replacement,
the current discovery SID must not be presented as the earlier match's SID.
New ordinary, malformed or ambiguous reports must not relabel old values as
current evidence. The retained match is labelled EARLIER with its own age.
CURRENT means it belongs to the current valid ADV or latest accepted matched
response; it does not mean the most recent callback necessarily matched, or
that the evidence is still fresh.

UUID/major/minor are an advertised tuple rather than authenticated identity.
Preserve the selected radio address and address type for target tracking;
do not merge distinct devices solely because these bytes match. Measured power
is a transmitted calibration field, separate from locally measured RSSI. It
does not establish distance, location, manufacturer, model or ownership.

The existing S3 BLE scanner, shared source record, focused filter, frozen
selection and Fox Hunt routes should be reused. The C5 can retain its independent
Wi-Fi role. No extra scan task, duplicate menu or active operation is required.

## Meaningful checks and remaining acceptance

Run `py -3.14 tools/check_ibeacon.py`. The host fixture compiles the production
header with warnings as errors and passes 2,908 fixture checks covering:

- The legacy 31-byte container, exact standalone field, UUID byte preservation,
  all 256 signed-power encodings and network-order major/minor boundaries.
- Every alternate byte in the four-byte marker, all manufacturer value lengths,
  misleading field contexts and arbitrary embedded markers.
- Identical duplicates, separated duplicates, nine repetitions in a 255-byte
  container and conflicting UUID/major/minor/power fields in both orders.
- Every known structural width/minimum/Flags-length case before and after a
  valid beacon, malformed tails, name conflicts beyond 32 bytes, zero padding,
  all prefix truncations of a mixed-field report and valid AD-boundary prefixes.
- Exact 255/256-byte bounds with the beacon before or after other fields,
  null arguments, immutable input, output canaries and clearing the same output
  after success followed by NONE or MALFORMED.

Structural outcomes are independently compared with the strict Meta/HC,
Find My and Flipper parsers; conflicting exact iBeacon duplicates are tested
as a deliberate semantic difference. Snapshot/Analyzer/selection tests must
separately validate current source evidence, matched responses, changed tuples,
source ages, same tuple on different radio identities, frozen target routing
and unchanged raw action gates. Installed reception, readability and repeated
concurrent Wi-Fi/BLE transitions remain separate acceptance work.

## Rev62 integrated and installed, 2026-10-30

The snapshot now updates its retained tuple after each newer qualifying
original or strictly matched response. It keeps that tuple's own time,
sequence and origin. Ordinary or invalid source replacement preserves useful
same-sweep history while labeling it EARLIER; no newer raw packet timestamp
is assigned to the retained tuple. Response-only evidence leaves raw original
bytes/time untouched. Nonincreasing sequence or backwards match time cannot
roll the tuple back. Sequence exhaustion fails closed until a new sweep.

Analyzer retains iBeacon values only for its latest accepted event, including
complete containers up to 255 bytes. A later ordinary, malformed, incomplete
or oversized event clears that tuple. Watch retains its existing 31-byte
container contract and stronger evidence display priority. The iBeacon
values take display priority over generic Meta company-ID suppression details;
the exact Meta matrix remains available in the view. No reports are combined.

The existing IBEACON filter, sort, frozen selection, details and Fox Hunt/Back
flow show full UUID, major/minor, signed CAL and the matching source's own age.
CAL is advertised calibration, not receiver RSSI or an authenticated distance.
The general list/detail RSSI still describes the typed address's sweep peak;
it is not asserted to be the received strength of the retained iBeacon packet.
Analyzer's latest-event RSSI is tied to that event. Current/earlier badge and
detail previews, full Analyzer renders, longest counters/ages and signed CAL
extremes fit without clipping. Physical-screen readability is unverified.

Fifteen affected host suites passed with stable inputs. They cover the parser,
current/historical source transitions, ambiguous-field isolation, timestamp
wrap, sequence exhaustion, real legacy/extended scanner callbacks, Analyzer
clearing, frozen selection and return routes, display publication, prior
Find My/Flipper/Meta behavior, and coherent completed-sweep diagnostics.
Canonical 27-byte and extended 40-byte iBeacon reports both fail the unchanged
Find My Sound eligibility gate. Independent parser/backend/UI/console reviews
found no blocker. These are local source and host checks, not RF acceptance.

Installed `candidate-ble-ibeacon-provenance` on verified S3 COM76 after all
five target builds, stable matched package and final READY interrupt check
passed. All three write hashes verified. Every C5 image is byte-identical to
the retained verified C5 50 installation. NVS, PHY and SD were preserved;
the rev61 package remains frozen for rollback. See the
[rev62 receipt](../../integration/device-test-2026-10-27/rev62-flash-receipt.json).

Target ELF sizes are 56 bytes shared provenance (unchanged), 108 focus,
216 scanner record, 8,432 snapshot, 7,176 Monitor and 224 frozen row. The
source addition is eight bytes per row / 256 per 32-row PSRAM bank. Analyzer
core/view are 268/264 bytes, 24 bytes more each. No second raw buffer or radio
task was added. The S3 app is 1,659,728 bytes (+928); net internal static use
grows 16 bytes. Fresh boot has ten tasks, 116,896 internal bytes free,
minimum 116,216, and 1,428,324 PSRAM bytes free. C5 has 77,140 bytes free,
SD is mounted and the SPI link runs at 40 MHz. No new reset fault is recorded.
At 183 startup exchanges, mutex/fall/high/driver error counters are zero;
initial READY timeout/fallback counts are four each. All radio jobs are idle,
BLE generation is zero and all discovery summaries report `not_complete`.

Marauder `develop` was rechecked through GitHub at 07:55:33 UTC and remains
the audited commit above. Device testing of normal scan/Analyzer/STOP/Back/
following-Wi-Fi scan is pending; the owner was told to use rev62 for that
pending check. Physical iBeacon formats, response-only reception, repeated
sessions and concurrent Wi-Fi/BLE operation remain unverified. Rev59 Find My
owner acceptance is retained as historical evidence. Wi-Fi NO MEM,
Association Sleep and Bad Message remain unresolved. GPS/six-C5 hardware is
absent; this checkpoint is not full Marauder parity or active-mode repair.
