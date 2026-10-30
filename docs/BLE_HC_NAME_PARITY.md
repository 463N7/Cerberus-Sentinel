# HC advertised-name evidence and receive RSSI

Source audit:2026-10-30 against Marauder develop
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363).
This patch concerns receive evidence, inventory and presentation. It adds no
active profile, GATT operation, Sound eligibility, advertising or radio mode.
Installed acceptance is separate from the source/host checks below.

## Upstream and prior Cerberus behavior

Marauder exposes `BT_SCAN_SKIMMERS` through its
[menu](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/MenuFunctions.cpp#L5511)
and [CLI](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/CommandLine.cpp#L1490).
Both [legacy](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L995)
and [NimBLE2](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L1659)
receive branches compare the advertised name exactly with `HC-03`, `HC-05`
and `HC-06`. Those unauthenticated names can be used or copied by ordinary
equipment. A match cannot establish a skimmer, hardware model, owner or purpose.
Cerberus therefore retains the existing **HC NAME HINT** wording.

Before this patch, `ble_ad_hints.h` already recognized those exact complete,
untruncated names. The focused snapshot retained only a sticky sweep bit and
static origin string; it did not retain which exact name matched or the matching
source's own age. Original-path HC classification also preceded the newer
event/SID/sequence admission gate. See the
[rev63 audit](../../integration/rev63-next-receive-audit-2026-10-30.md).

## Compact exact-name API

`ble_ad_hints.h` continues to own the single complete-AD parser. The new
`cerberus_ble_ad_hc_name_code(parsed)` returns `3`, `5`, `6` or `0`. It requires
a successfully parsed report, complete name, no truncation and exactly five
matching bytes. Lowercase, suffixes, embedded NULs, arbitrary fields and shortened
names cannot qualify. The existing `cerberus_ble_ad_hc_name_hint(parsed)` boolean
wrapper has exactly the same matching semantics as before.

`ble_hc_hints.h` defines `CERBERUS_BLE_HC_NONE`, `_03`, `_05`, `_06` and
`cerberus_ble_hc_name_label(code)`. Labels are `NONE`, the exact recognized name,
or `UNKNOWN` for other codes. This header adds no second parser or device catalog.

Full-report validation remains transactional and bounded to255 bytes. A matching
name followed by malformed data is rejected and output is cleared. Identical
nonempty complete names coalesce; conflicting nonempty complete names reject
the whole report. Empty name fields do not contradict a nonempty complete name,
preserving the shared parser's existing rule. The scanner must independently
establish radio completeness; a truncation at an AD boundary can look valid.

Integration uses independent current ADV/RSP codes within the shared discovery
identity/type/SID/generation/sequence/time binding. No cached inventory name or
other packet may supply a code. Ordinary/new invalid originals clear current
evidence; matched responses do not renew original raw bytes or timestamps.
The existing focused list, immutable selected record and receive Fox Hunt route
are reused. Codes and source ages describe received names, never authentication.

## Shared receive RSSI policy

`ble_rssi.h` supplies `cerberus_ble_rssi_valid(int)` and
`CERBERUS_BLE_RSSI_UNAVAILABLE=127`. The accepted range remains **-127 through20
inclusive**, matching the existing list comparator, live tracker and signal
validity policy. Accepting an `int` avoids accidentally validating a narrowed
out-of-range input. This is received-signal validity, not a distance estimate.

The list comparator now calls the helper while preserving measured-first order,
descending valid signal, name ties, stable original indexes and immutable inputs.
The tracker calls the same helper; invalid samples still cannot refresh its
signal, age, seen state or report count. It remains a latest-valid-sample tracker,
not a peak accumulator. No radio ownership or tracking lifecycle behavior changes.

Snapshot integration separately applies this policy to its scan peak:127 or
another unsupported value must not replace a measured peak, and a first valid
sample must recover from unavailable state. A valid measured original signal
does not depend on whether its AD name/classification succeeds. Response handling,
raw-profile eligibility and raw observation metadata must remain independent.

## Validation

Host commands:

- `py -3.14 tools/check_ble_ad_hints.py`:6,569 checks pass, including exact
  three-name codes, all label inputs, unchanged boolean outcomes, shortened/
  binary/conflicting names, identical duplicates, full255-byte containers,
  malformed suffixes and parser output clearing.
- `py -3.14 tools/check_s3_ble_list.py`: passes boundary/sentinel sorting,
  stable equal-key order, pages, bounded labels and input immutability.
- `py -3.14 tools/check_s3_ble_tracker.py`: passes all256 int8 measurement
  cases, endpoints, invalid-sample nonmutation, exact typed identity, freshness,
  wrap, reset and existing UI wiring. Shared helper checks also reject INT_MIN,
  INT_MAX and values that would appear valid after narrowing.

Snapshot, actual scanner callback, Analyzer and UI checks must separately verify
source admission/clearing, exact codes/ages, peak recovery, frozen selection and
unavailable rendering. These helper checks alone do not establish physical
reception, current-device acceptance or complete upstream parity.

## Installed rev64 checkpoint, 2026-10-30

`candidate-ble-hc-rssi` is installed on the identified S3. All C5 images are
byte-identical to the retained verified firmware 50 installation. Sixteen
affected host suites pass against the final stable inputs, including the
6,569 shared AD checks, actual legacy/extended callbacks, source lifecycle,
peak recovery, sorted selection and unchanged raw-action eligibility. Independent
parser/backend/UI/console reviews found no blocker. Production UI fixtures cover
exact per-source codes and ages, same-address/different-type selection, frozen
Fox Hunt/Back, stronger payload priorities and valid/unavailable signal display.
Native list, detail blocks and maximum-age Analyzer previews were inspected.
The legacy history early return and list N/A branch were reviewed directly;
they are not claimed as additional fixture coverage.

The two HC codes reuse provenance padding, keeping that structure at 64 bytes.
The redundant HC text pointer is removed. Frozen target measurements show four
bytes saved per row / 128 per 32-row PSRAM bank: snapshot 8,560, Monitor 7,304,
frozen row 228 and focus 112. Analyzer core/view remain 276/272. Internal static
allocation falls 16 bytes. The S3 app is 1,667,520 bytes, 2,304 more than rev63.
No radio task, per-report allocation or additional raw buffer was introduced.

All five firmware targets, the final S3 READY interrupt check, matched package
and three S3 flash-write hashes pass. Fresh boot has 116,896 internal bytes free
(minimum 116,216), 1,428,324 PSRAM bytes free and ten tasks. SD is mounted and
C5 is online at 40 MHz with 77,140 bytes free. No new reset fault is recorded.
At 29 startup SPI exchanges, mutex/fall/high/driver errors are zero and initial
timeout/fallback counts are four each, consistent with prior startup evidence.

The [rev64 receipt](../../integration/device-test-2026-10-27/rev64-flash-receipt.json)
retains frozen input hashes, target measurements and boot evidence. BLE generation
is zero and all jobs are idle; boot does not establish RF or touchscreen acceptance.
The pending scan/Analyzer/STOP/Back/following-Wi-Fi check should use rev64. Physical
HC broadcasts, prolonged operation and concurrent Wi-Fi/BLE behavior remain
unverified. Rev59 Find My hint/Monitor-exit owner acceptance is separate. Wi-Fi
NO MEM, Association Sleep and Bad Message remain unresolved; no full upstream
parity or physical device identification is claimed.
