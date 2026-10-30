# Flock receive evidence parity

Source audit: 2026-10-30, against Marauder `develop` commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363).
This original Cerberus implementation observes advertising reports and typed
addresses. It adds no advertising, Wi-Fi transmission, GATT, Sound or Beep All
operation. Source checks and installed-device acceptance are separate.

## Upstream behavior and the focused-workflow gap

The [Flock Sniff menu](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/MenuFunctions.cpp#L5521)
and [CLI](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/CommandLine.cpp#L1383)
start `BT_SCAN_FLOCK`. Its [dedicated receive callback](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L1604)
accepts either `isFlockCamera` or `checkFlockOUI`. The legacy callback at line825
contains the corresponding logic. The [payload predicate](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L1862)
searches arbitrary bytes for `FF C8 09` and accepts these name cases: exactly
`FS Ext Battery`, `Penguin-` followed by ten decimal digits, ten decimal digits
alone, or an empty name. It also attempts to extract serial-looking text after
`TN` in manufacturer bytes. That is not a verified serial format.

The [OUI predicate](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L8960)
compares three address bytes to a [27-prefix table](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.h#L929).
It does not establish an address's public/random provenance or authenticate
the transmitter. Cerberus retains these exact prefix values as a compatibility
clue, with stricter typed-address admission and explicit vendor limitations.

Marauder's Flock mode also [transmits a Wi-Fi beacon](https://github.com/justcallmekoko/ESP32Marauder/blob/79cc6bcba1db6679baee1dab2b0c1ad5010e6363/esp32_marauder/WiFiScan.cpp#L12751).
That side effect is deliberately excluded. No upstream executable parser or
radio implementation is copied into this patch.

Before this patch, Cerberus focused Flock rows came from the single winning
`sentinel_watch_classify_ble` result (`s3_ble_snapshot.c:155–174` at rev62).
That Watch parser accepts at most31 bytes, and the bridge sees only original
reports. A valid31-byte report with a Flipper service/name plus the Flock
accessory UUID therefore loses its Flock bit to the higher-specificity Watch
result. Complete extended reports and matched-response-only evidence are also
missed. Independent evidence addresses those outcomes without changing Watch.

## Exact bounded evidence

`s3_main/main/ble_flock_hints.h` exports the four-byte
`cerberus_ble_flock_hints_t`, `cerberus_ble_flock_hints_parse(data,length,out)`
and `cerberus_ble_flock_hints_level(hints)`. There is no allocation, retained
packet pointer, inferred serial, cached name or cross-report state.

| Field | Exact meaning |
| --- | --- |
| `facts` bit0 `FS_BATTERY` | Complete AD09 name exactly `FS Ext Battery` |
| bit1 `PENGUIN` | Complete AD09 name exactly `Penguin-` plus ten decimal digits |
| bit2 `SERIAL10` | Complete AD09 name exactly ten decimal digits; not a verified serial |
| bit3 `NO_LOCAL_NAME` | No **nonempty** AD08 or AD09 value anywhere in this report |
| bit4 `MFG_09C8` | Leading company identifier `C8 09` in an ADFF manufacturer field |
| bit5 `MFG_PAYLOAD` | That same company field has at least one additional payload byte |
| `accessory_contexts` | Exact custom UUID `e8ccbb38-9532-46a8-9fe5-1814df172e6f`, LIST128 and/or DATA128 |
| `raven_3100_contexts` / `raven_3200_contexts` | Independent exact encoding contexts for each service |

Context bits0–5 are LIST16, LIST32, LIST128, DATA16, DATA32 and DATA128.
List contexts accept both incomplete/complete-list AD types02–07. Data contexts
refer only to the leading UUID in Service Data types16/20/21, never bytes later
in its payload. UUID32 requires zero upper16 bits; UUID128 normalization
requires the exact Bluetooth base and zero upper16 bits. Arbitrary custom UUID
substrings, reversed byte order, manufacturer payloads and name bytes do not
become service evidence. The distinct accessory UUID is matched exactly in BLE
wire byte order. Contexts stay attached to their own service rather than forming
a misleading service-by-width cross-product.

The exact accessory and battery rules preserve Cerberus's narrower prior Watch
signatures. Their original evidence and notices are retained in
[WATCH_SIGNATURES.md](WATCH_SIGNATURES.md#primary-sources-and-provenance) and
[WATCH_THIRD_PARTY_NOTICES.md](WATCH_THIRD_PARTY_NOTICES.md):
[flock-you's firmware-derived notes](https://github.com/colonelpanichacks/flock-you/blob/6704f011f3e6c9cfe94fe18c593f863032c9c954/datasets/firmware_derived_signatures.md)
and [flock-back's service catalog](https://github.com/NSM-Barii/flock-back/blob/011e024e0fc0c06567f44ababfa7c957ac8b6b4e/src/signatures.py).
Supporting equivalent32/base128 representations is a parser capability, not
a claim that physical Flock products were observed emitting every encoding.

| Level | Predicate within one report |
| --- | --- |
| `NONE` | No qualifying combination, including a lone Raven service or decimal name |
| `CLUE` | `MFG_09C8` plus `SERIAL10` or `NO_LOCAL_NAME` |
| `SIGNATURE` | Exact FS/Penguin name, exact accessory service, or both3100 and3200 |
| `CORROBORATED` | Exact FS/Penguin name plus both `MFG_09C8` and its own `MFG_PAYLOAD` |

Empty local-name fields supply no name and allow the weak unnamed-company
case. Any nonempty shortened or binary name prevents that absence clue but
cannot satisfy the exact complete-name patterns. Identical nonempty complete
names coalesce; conflicting nonempty complete names invalidate the whole AD.
Empty names do not conflict with nonempty complete names, matching the other
passive parsers. A valid empty report has `NO_LOCAL_NAME` set and level `NONE`;
callers must use the level, not test whether any fact byte is nonzero.

An unrelated manufacturer's payload cannot strengthen an empty09C8 field.
Bare decimal-name and unnamed09C8 reports remain weak clues even with payload.
Names, UUIDs, company identifiers and all combinations here can be copied.
The levels describe signature specificity, never verified camera presence,
camera attachment, model, operator, location, activity or intent.

## Separate address clues and SD resources

`ble_flock_oui.h` exports `cerberus_ble_flock_oui_hint(nimble_address,address_type)`.
It compares the immutable81-byte compatibility table only for public/public
identity address types0/2 and globally administered individual addresses.
It understands NimBLE's reversed address byte order; random types1/3, unknown
types, local addresses and group addresses never qualify. It does not derive
an OUI from advertising bytes or interpret a company identifier as an OUI.

The boolean result means only that the typed address shares a prefix in the
pinned upstream table. It is separate from the advertising specificity level
and does not strengthen any name/company/service combination. Even a registry
assignment to Flock Safety is vendor attribution, not proof of a specific device.
Many table entries are shared radio-vendor prefixes.

The SD-owned `/CERBERUS/RECON.DB` remains the independent source of registry
vendor descriptions and attributed Dragon/Squach reference clues. Its builder
(`tools/build_recon_pack.py:47–89`) preserves source URLs, hashes and notices;
archived BLE rules are reference material rather than runtime predicates.
Dragon's `B4:1E:52` row explicitly marks product/activity unverified, and other
Flock-family rows include shared-module clues. Existing public BLE vendor lookup
keeps storage locking, mount epochs and bounded caching; the new parser and
address helper perform no SD access. See [CYM_SD_ASSETS.md](CYM_SD_ASSETS.md).

## Source binding and integration requirements

Every AD length and the entire suffix must validate before output commits.
UUID-list strides, service/manufacturer minima, Flags length1, conflicting
complete names and zero-only terminator padding follow the existing strict
passive contract. Complete unknown fields are permitted. Invalid input clears
all four output bytes. The parser accepts at most255 bytes; callers still must
prove radio completeness because truncation at an AD boundary can look valid.

Separate `flock_advertisement` and `flock_scan_response` slots reuse the shared
discovery identity/type/SID/generation/sequence/time binding. Invalid/new originals
invalidate matched-response evidence. A response cannot renew original raw bytes
or timestamps, and ADV company evidence cannot corroborate a response name.
Ordinary replacements clear current packet hints. The address clue is a separate
same-sweep typed-row fact and has no fabricated AD source timestamp or SID.

The existing FLOCK filter, immutable selected row and receive-side Fox Hunt
route are reused. Source-specific details distinguish clues, exact signatures
and corroboration. S3 owns BLE receipt/analysis; C5 keeps its independent Wi-Fi
role. No additional worker, menu, active profile or action eligibility is added.

Analyzer presents only the latest valid event's Flock evidence and separate
address clue, clearing them on subsequent invalid/ordinary events. Integration
preserves Watch/iBeacon presentation priority. Specific Flock signatures may
outrank lower-specificity Meta/HC fallback labels; a weak company/OUI clue must
not mask a tied Meta/HC result. This is explicit presentation precedence, not
maximum-confidence arbitration across every family. Independent evidence remains
available regardless of the single headline label.

## Validation and limits

Run `py -3.14 tools/check_ble_flock_hints.py`. The host fixture compiles the actual
production headers with warnings as errors and covers:

- Every service/context, list completeness/position, per-byte negative mutations,
  mixed contexts, all pair orders, repeated fields and no cross-report pairing.
- Exact accessory and mixed Flipper/Flock31-byte collision, preserving both hints.
- All exact/weak name classes, shortened/binary/empty names, company field order,
  same-company payload ownership, conflicting names and misleading byte contexts.
- Transactional malformed suffixes, all known field minima/strides, Flags, zero
  padding, real AD-boundary truncation,255-byte limit, canaries and input immutability.
- Structural validity agreement with the production Meta, Find My and Flipper
  parsers; classification remains independent.
- All27 prefixes, all256 address-type values, varying address suffixes, group/local
  rejection, prefix-bit mutations, reversed-order negative and null handling.

Snapshot/scanner/Analyzer/UI integration checks must separately establish source
binding, replacement clearing, public/random address treatment, exact selected
row retention and Fox Hunt/Back cleanup. No physical Flock sample, reception rate,
distance accuracy or installed rev63 workflow is claimed by these parser checks.

## Installed rev63 checkpoint, 2026-10-30

`candidate-ble-flock-evidence` is installed on the identified S3, with all C5
images byte-identical to the retained firmware 50 installation. Sixteen affected
host suites pass against the final stable inputs, including 16,676 Flock parser
and typed-address checks. Independent parser, snapshot/Analyzer, console and UI
reviews found no remaining blocker. Production navigation/render fixtures cover
filtered/sorted immutable selection, separate ADV/RSP levels and source ages,
privacy masking, payload priority and Fox Hunt/Back. Native screen previews
were inspected. The target compiler caught a possible remainder-label buffer
truncation; the buffer now fits the full unsigned representation, and host
checks and all builds were rerun after that correction.

All five firmware targets, the final S3 READY interrupt check, matched package
and three S3 flash-write hashes pass. Target shared provenance is 64 bytes;
snapshot/Monitor sizes are 8,688/7,432, frozen row 232 and Analyzer core/view
276/272. Each 32-row PSRAM bank grows 256 bytes over rev62. Internal static
allocation grows 16 bytes; the S3 app is 1,665,216 bytes, an increase of 5,488.
Fresh boot has 116,880 internal bytes free (minimum 116,200), 1,428,324 PSRAM
bytes free and ten tasks. SD is mounted and C5 is online at 40 MHz with 77,140
bytes free. No new reset fault is recorded. At 31 startup SPI exchanges,
mutex/fall/high/driver errors are zero; initial timeout/fallback counts are
four each, matching the prior startup baseline rather than a new regression.

The [installation receipt](../../integration/device-test-2026-10-27/rev63-flash-receipt.json)
links frozen inputs, image hashes, layout measurements and boot diagnostics.
BLE generation is zero and all jobs are idle, so startup establishes no RF or
touch acceptance. Current-build physical signatures, repeated transitions and
concurrent Wi-Fi/BLE operation remain pending. Prior rev59 Find My hint and
Monitor-exit owner acceptance is retained separately. The Wi-Fi NO MEM,
Association Sleep and Bad Message reports remain unresolved. No full Marauder
parity or verified Flock hardware identity is claimed.
