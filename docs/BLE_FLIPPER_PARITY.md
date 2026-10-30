# Flipper discovery hint parity

Source audit: 2026-10-30, against the installed rev59 source checkpoint and
Marauder `develop` commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363).
This change concerns received advertising evidence, the existing focused list,
selected details and receive-side Fox Hunt. It adds no advertising, attack,
GATT, Sound or Beep All behavior. Parser checks are source validation; installed
workflow and physical-device acceptance must be recorded separately.

## The actual upstream gap

Marauder exposes Flipper Sniff at `MenuFunctions.cpp:5447–5451` and CLI
`CommandLine.cpp:1381`, with Flipper Fox Hunt targets at
`MenuFunctions.cpp:2078,2133,2169,2212`. The receive callbacks at
`WiFiScan.cpp:584–649` and `:1325–1378` look for byte pairs `81 30`, `82 30`
or `83 30` without requiring a particular advertised name. Their output maps
these values to Black, White and Transparent. General BLE classification at
`:2142–2169` applies the same broad byte-pattern idea.

Rev59's passive Watch rule at `sentinel_watch_detect.c:270–275` requires a
recognized service plus a complete `Flipper` name in the same report. The
focused list obtains its Flipper bit from Watch's single winning category.
Consequently a structurally valid report such as
`02 01 06 03 03 81 30 02 0A C5` matches Marauder's callback but has no Flipper
focus bit in that baseline. A response-only service or a different higher
priority Watch category can likewise hide otherwise supported service evidence.

The new parser does not reproduce arbitrary substring matching or the
upstream loop's end-of-buffer omission. It recognizes complete, correctly
typed UUID values and still requires the entire report to be structurally valid.

## Official Flipper format verification

The GitHub connector verified the official `flipperdevices/flipperzero-firmware`
`dev` head as `66613d2b46e4aa533aeaf8181d2a1e6566616a4e` on 2026-10-30.
The files were fetched at that immutable commit:

- [serial_profile.c:50–80](https://github.com/flipperdevices/flipperzero-firmware/blob/66613d2b46e4aa533aeaf8181d2a1e6566616a4e/targets/f7/ble_glue/profiles/serial_profile.c#L50)
  selects a 16-bit advertising service, starts at `3080`, and ORs the hardware
  color code into it. Blob SHA: `9bba0d897e92d0dfecedd0a1f575e9856f82b80d`.
- [furi_hal_version.h:33–39](https://github.com/flipperdevices/flipperzero-firmware/blob/66613d2b46e4aa533aeaf8181d2a1e6566616a4e/targets/furi_hal_include/furi_hal_version.h#L33)
  assigns unknown=0, black=1, white=2 and transparent=3. Blob SHA:
  `f702aed13dc9ae960eafa444ebf42e7ee045f201`.
- [gap.c:309–318](https://github.com/flipperdevices/flipperzero-firmware/blob/66613d2b46e4aa533aeaf8181d2a1e6566616a4e/targets/f7/ble_glue/gap.c#L309)
  constructs a service-list field, and
  [lines 575–584](https://github.com/flipperdevices/flipperzero-firmware/blob/66613d2b46e4aa533aeaf8181d2a1e6566616a4e/targets/f7/ble_glue/gap.c#L575)
  encode the configured 16-bit service least-significant byte first. Blob SHA:
  `c3a6310c64bcd9512e223d6bb60de73711e16c35`.
- [serial_service_uuid.inc:2–3](https://github.com/flipperdevices/flipperzero-firmware/blob/66613d2b46e4aa533aeaf8181d2a1e6566616a4e/targets/f7/ble_glue/services/serial_service_uuid.inc#L2)
  defines a distinct custom 128-bit serial GATT service. It is not the advertised
  `308x` variant value and is not admitted by this parser. Blob SHA:
  `577e8f2eda449a590d10bf17017b8595be1fbb91`.

The original Cerberus implementation uses these format facts; no upstream
callback or advertising implementation was copied. Standard 32-bit and
Bluetooth-base 128-bit representations are also understood as exact equivalent
service values. This is an explicit parser capability, not a claim that the
official serial profile emits those representations. An unrelated custom UUID
containing `81 30` is not equivalent.

## Compact evidence contract

`s3_main/main/ble_flipper_hints.h` exports
`cerberus_ble_flipper_hints_parse(data, length, out)`. It returns false and
clears output for malformed input. A valid ordinary report returns true with
`bits == 0`. The two-byte `cerberus_ble_flipper_hints_t` contains:

| Field | Meaning |
| --- | --- |
| `bits` bit 0, `HINT_BASE` | Exact `3080` with same-report complete-name corroboration |
| `bits` bits 1/2/3, `HINT_BLACK`/`HINT_WHITE`/`HINT_TRANSPARENT` | Qualified advertised codes `3081`/`3082`/`3083` |
| `flags` bits 0/1/2, `UUID16`/`UUID32`/`UUID128` | Widths of qualifying UUID values in this report |
| `flags` bit 3, `NAME` | Complete printable name satisfies the existing corroboration rule |
| `flags` bit 4, `SERVICE_DATA` | At least one exact service-data UUID prefix qualified with that same-report name |

Exact service-list values in AD types `02`–`07` establish `3081`–`3083` hints
without a name. A list containing only `3080` requires the existing complete
name rule: exactly `Flipper`, or `Flipper ` followed by a nonempty suffix,
printable ASCII only, maximum 32 bytes. A shortened-name field cannot satisfy
it. Names alone can set `NAME` but never set a service bit.

The prior Watch rule also consumed leading UUIDs in Service Data types `16`,
`20` and `21`. To preserve that existing behavior, exact `3080`–`3083` prefixes
in those fields qualify only when the same report has the complete matching
name. Unnamed service data cannot qualify. Bytes later in the service-data
payload, manufacturer fields, unknown AD fields or names cannot impersonate
a leading UUID. `SERVICE_DATA` distinguishes this compatibility path from the
new name-independent service-list admission.

All qualified variant bits are retained. Width flags and the service-data flag
are unions over qualifying values, not a per-variant assignment or a count of
physical devices. An unqualified `3080`/service-data representation contributes
no width bits. No state from an earlier report participates in qualification;
for example, `3080` in ADV plus a `Flipper` name in a response stays unqualified.

These broadcasts are unauthenticated and can be reproduced by other devices.
The UI should describe code/name hints and source evidence, not confirm a
Flipper model, physical color, owner or intent. RSSI remains a received signal
measurement, not a location or distance proof.

## Full-report validation and provenance

The allocation-free parser is bounded to 255 bytes. It validates every AD
length, every UUID-list stride, minimum manufacturer/service-data sizes,
one-byte Flags payloads, conflicting complete names and zero-terminator tails.
A matching field followed by malformed data is rejected transactionally.
Complete unknown fields are allowed; arbitrary vendor payloads are not
semantically or cryptographically verified.

The scanner remains responsible for a radio report's completeness. Truncation
at an AD boundary can look like a valid shorter report, so bytes alone cannot
establish that the radio delivered the full original. Shared snapshot provenance
must keep original ADV and response hint results distinct and bind responses
to the exact identity, address type, SID, sweep/generation, report sequence and
two-second original-report window. Invalid/new originals clear response binding;
responses never renew the original raw bytes or observation time. Source
completeness is separate from the bounded 31-byte raw copy.

The existing scan, filter, sorted selection and receive-side Fox Hunt routes
are reused. S3 owns BLE observation/parsing and bounded evidence; C5 can keep
its independent Wi-Fi role. No additional radio worker or duplicate menu is
needed. The parser cannot authorize or alter any active BLE operation.

## Meaningful validation

Run `py -3.14 tools/check_ble_flipper_hints.py`. The fixture compiles the actual
production header with warnings as errors and checks:

- All four codes, all three exact UUID widths, both list types, multiple list
  positions, mixed codes, repeated values and per-byte negative mutations.
- Complete/shortened/custom/binary/empty/32-byte/overlong names, order independence,
  no cross-report strengthening, conflicting names and width qualification.
- All four named service-data prefixes in all three widths, with and without
  corroboration, misleading payload bytes, and mixed list/data reports.
- Rejection of the official custom GATT UUID in both byte orders and arbitrary
  unrelated UUIDs/fields containing the byte pattern.
- Every prefix truncation of relevant fields and complete multi-field fixtures,
  real AD-boundary behavior, malformed suffixes after positive hints, zero
  padding, maximum size, null handling and input immutability.

The same fixture checks structural validity against the existing passive
Find My parser contract; their classification decisions remain independent.
Source snapshot/UI/route tests must additionally cover distinct source labels,
ordinary replacement clearing both hint families, sorted selection of the exact
address/type and Fox Hunt/Back cleanup. Physical reception and repeated mode
transitions remain unverified until separately tested on the device.

## Installed rev60 checkpoint

The frozen `candidate-ble-flipper-discovery` S3 package is installed, with
byte-identical C5 50 images retained. Five stable-input targets, the matched
package, final S3 READY interrupt gate and all three S3 write hashes pass.
The device boots without a new reset fault, mounts SD and connects to C5 at
40 MHz. See the [receipt](../../integration/device-test-2026-10-27/rev60-flash-receipt.json).

The 820 parser fixture checks pass, as do production shared snapshot,
legacy/extended scanner, retained Find My cache/lifecycle and unchanged raw
action-gate regressions. The new `check_s3_ble_discovery_ui.py` exercises the
actual filtered/sorted view, native hit testing, frozen selection after source
overwrite, exact typed Fox Hunt target and asynchronous Back cleanup. Native
list and changed detail-block previews were inspected. Independent snapshot
and UI reviews found no remaining blocker.

Find My and Flipper share one 36-byte source record. Compared with rev59,
each target row grows four bytes and each 32-row PSRAM bank grows 128 bytes;
static internal allocation and idle free memory are unchanged. S3 app size
is 1,655,856 bytes (+3,472). Idle internal free is 116,960 bytes; C5 free is
77,140. No new radio task or raw response buffer is introduced. This is a
measured allocation result, not a reception-rate or concurrency benchmark.

Physical service variants, real matched-response cases, touch readability,
repeated Fox Hunt/scan recovery and concurrent Wi-Fi/BLE acceptance remain
pending. The existing passive Watch rule remains separately name-corroborated.
All active engines, Sound eligibility and C5 radio behavior are unchanged.
