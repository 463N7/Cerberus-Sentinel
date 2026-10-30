# Find My profile parity audit

Date: 2026-10-29. Compared against Marauder `develop` commit
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363` in
`../integration/marauder-develop-2026-10-29`.

**Historical baseline: rev53/source reviewed on 2026-10-29 was partial.** The
reported AirTag sound and Beep All hardware tests establish that path works for
the user's tag, not complete AirTag/FMNA/DULT coverage. The original gap table
and broader future-work proposal below describe that baseline. The receive-only
hint changes dated 2026-10-30 are documented at the end; they do not expand
Sound eligibility, central/GATT discovery, or Beep All scheduling.

## Historical coverage gaps at the audit baseline

| Valid input or capability | Pinned upstream | Current Cerberus |
| --- | --- | --- |
| FMNA/DULT UUID present only in a complete matched legacy scan response | Active scan supplies combined ADV/response data; service predicate accepts it | Scan response may enrich displayed UUID but cannot establish Find My/Sound evidence |
| Both FMNA and DULT UUIDs in one valid ADV; or Apple and service hints in combined data | Accepted; display-label priority does not restrict GATT discovery | More than one profile hint returns `NONE` |
| FD44 represented as a 32-bit UUID or its full Bluetooth-base 128-bit UUID | NimBLE normalizes these against the FMNA service UUID | Only the 16-bit UUID-list representation is recognized |
| An accepted advertisement hints at one family while GATT exposes another supported family | Discovers supported service/characteristic pairs independently of the hint | Commits to one advertised profile before connecting; a missing service/characteristic ends the operation |

These are source-proven supported input cases. This audit does not claim that a
particular commercial tag emits every combination. A service UUID occurring
only in a Service Data AD field is **not** established as upstream parity:
`isAdvertisingService` enumerates UUID-list fields. Upstream's broad Apple byte
substring checks also accept ambiguous input; reproducing those false positives
is unnecessary.

## Evidence

Cerberus references are relative to this repository:

- `s3_main/main/s3_findmy_core.h:64-119`: rejects every scan response; recognizes
  only 16-bit FMNA UUID lists; rejects multiple profile hints.
- `s3_main/main/s3_ble_snapshot.c:135-157`: a response is already matched to the
  current sweep, address **and address type**, advertising SID, and an original
  ADV no more than two seconds old. The response currently cannot strengthen
  Find My evidence.
- `s3_main/main/s3_findmy_ui_route.h:18-33`: selected Sound requires the latest
  complete original connectable ADV, excluding merged UUID fields.
- `s3_main/main/s3_findmy_monitor.h:70-105`: mixed hints can establish a monitor
  observation, but the same record cannot establish Sound eligibility.
- `s3_main/main/s3_ble_central.c:532-664,876-905`: discovery verifies an exact
  service and characteristic with suitable properties, but only for the one
  profile chosen before the connection.

Pinned Marauder references are relative to its checkout:

- `esp32_marauder/WiFiScan.cpp:1244-1271`: Apple signature **or** FMNA service
  **or** DULT service admits a result. Lines 1299-1304 prioritize its display
  label as FMNA, DULT, then AirTag without rejecting mixed hints.
- `esp32_marauder/WiFiScan.cpp:5437-5549`: Sound discovers the exact supported
  service/characteristic pairs; selection priority is AirTag, FMNA, then DULT.
  Lines 5770-5856 send one selected profile, with no fallback after its send
  fails. Advertised profile does not constrain discovery.
- `esp32_marauder/WiFiScan.cpp:7356` and `:5957`: normal and live Find My paths
  request active scanning.
- `esp32_marauder/WiFiScan.h:294-296`: FMNA is the full base UUID
  `0000fd44-0000-1000-8000-00805f9b34fb`.

Supporting official NimBLE-Arduino 2.3.8 implementation:

- [NimBLEScan.cpp:79-136](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.8/src/NimBLEScan.cpp#L79)
  matches advertiser identity/SID and delivers a legacy active-scan result after
  the response.
- [NimBLEAdvertisedDevice.cpp:72-74](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.8/src/NimBLEAdvertisedDevice.cpp#L72)
  appends legacy response data; [lines 380-445](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.8/src/NimBLEAdvertisedDevice.cpp#L380)
  enumerate incomplete/complete 16-, 32-, and 128-bit UUID lists.
- [NimBLEUUID.cpp:267-279](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.8/src/NimBLEUUID.cpp#L267)
  expands short UUIDs against the Bluetooth base for comparison.
- [NimBLEAddress.cpp:186-191](https://github.com/h2zero/NimBLE-Arduino/blob/2.3.8/src/NimBLEAddress.cpp#L186)
  compares both address type and all six address bytes.

## Prior audit plan — broader active work is deferred

This is the original proposed follow-up, not an implementation claim. The
current patch covers passive classification, filtering and Monitor evidence
only. The Sound/GATT/Beep All integration and discovery changes in steps 3–5
are outside this patch; the new hint set must not widen their eligibility.

1. Separate the **advertising hint set** from the **GATT-confirmed Sound
   profile**. Parse each complete, well-formed report independently into a
   bitmask. Accept multiple supported hints and normalize FD44 in all three
   UUID-list widths. Keep raw manufacturer parsing structural, not substring
   based.
2. Preserve bounded response evidence separately from original ADV evidence.
   Bind it to address, address type, sweep/generation, SID where applicable,
   source report sequence, completeness, and source timestamps. Reuse the
   existing same-sweep/two-second response match. Do not merge unrelated cached
   bytes, match by name, or refresh original ADV time on a response or row tap.
   A response cannot itself establish connectability. Invalidate its association
   when the original report changes; record absent/truncated/malformed evidence
   explicitly. A compact parsed hint record is sufficient if it preserves this
   provenance; a second full payload per device is not inherently required.
3. Feed that common evidence into Find My filtering, Monitor, selected Sound,
   and Beep All. Preserve a fresh exact connectable peer requirement for Sound
   and the existing selected-observation freshness bound. Nonconnectable
   observations remain useful to Monitor. Names and sticky inventory flags
   remain descriptive, not Sound authorization evidence.
4. On a Sound connection, probe all three exact supported service/characteristic
   pairs independently of the hint label. Choose one confirmed usable profile
   in upstream priority order: AirTag, FMNA, DULT. Check write properties and,
   where required, notification/indication and the exact CCCD. A missing or
   unusable pair permits discovery of the next pair. Never send multiple profile
   commands merely because multiple hints appeared. Do not retry another command
   profile after a write has been submitted or a response becomes ambiguous.
5. Preserve immutable peer/generation binding through asynchronous callbacks,
   cancellation, disconnect, and queue advancement. Apply the same helper to
   selected Sound and Beep All. Keep per-identity attempt/cooldown handling and
   distinguish write acceptance, protocol response, and user-confirmed sound.

This changes evidence and discovery on the S3 BLE path. The C5 can continue its
independent Wi-Fi work; it need not duplicate the GATT transaction.

## Prior audit validation for the deferred broader work

The Sound/GATT expectations below remain deferred. Passive parser and report
provenance checks are tracked separately in the 2026-10-30 section; passing
those does not establish active eligibility or command behavior.

- A connectable original ADV containing only flags plus a matched response
  `03 03 44 FD` becomes FMNA eligible. Repeat with the DULT UUID in the response.
- Reject responses with wrong address type/address/SID/generation, expired
  pairing, incomplete data, malformed AD lengths, or no matching original ADV.
  A response and UI selection must not renew original-ADV freshness.
- FMNA UUID-list fields `05 05 44 FD 00 00` and
  `11 07 FB 34 9B 5F 80 00 00 80 00 10 00 00 44 FD 00 00` match the existing
  16-bit representation. Test both complete and incomplete list AD types.
- A valid FMNA+DULT packet retains both hints; confirm one selected GATT write,
  not rejection or two writes. Test hints split across ADV and response too.
- With advertised FMNA but only an AirTag or DULT service/characteristic pair,
  mock discovery chooses the actual supported profile. Test missing service,
  missing characteristic, wrong properties, missing CCCD, and no usable pair.
- Confirm stale callbacks/changed generations cannot advance a new job, and
  failed/ambiguous writes cannot trigger a second profile command.
- Selected Sound, Monitor, filtered list, and Beep All agree on the same fresh
  evidence. Retest the user's working AirTag and STOP behavior. Mark FMNA/DULT
  physical behavior unverified until representative owned test tags are tested.

## Passive hint parser update — 2026-10-30

`s3_main/main/ble_findmy_hints.h` adds an original, allocation-free parser for
one complete AD report. Its two-byte result contains independent `bits` for
`APPLE`, `FMNA` and `DULT`, plus `fmna_uuid_widths` recording which 16-, 32- or
128-bit UUID-list representations actually appeared. Multiple hints remain
visible; there is no priority-based loss or conversion into a Sound profile.

| Hint | Required evidence in this report |
| --- | --- |
| Apple offline-finding frame | Manufacturer AD type `FF`, exactly 29 value bytes: company `4C 00`, type `12`, declared length `19`, all 25 following frame bytes present |
| FMNA service | `FD44` in AD UUID-list types `02`/`03`, `0000FD44` in `04`/`05`, or exact Bluetooth-base UUID `0000fd44-0000-1000-8000-00805f9b34fb` in `06`/`07` |
| DULT service | Exact `15190001-12f4-c226-88ed-2ac5579f2a85` in a 128-bit UUID list, AD type `06` or `07` |

UUID values use Bluetooth advertising byte order, least-significant byte first.
Both incomplete and complete list *types* are accepted; an incomplete list type
does not mean that its containing radio report was truncated. Every listed UUID
must still be a full value of its declared width. The 32-bit FD44 value requires
zero upper bytes, and the full 128-bit form requires every Bluetooth-base byte;
nearby UUIDs, byte-reversed strings and substring matches do not normalize.
Repeated hints are idempotent. An empty list supplies no hint.

The parser validates the entire bounded AD stream (maximum 255 bytes), not just
the matching field. AD lengths, UUID-list alignment, minimum manufacturer and
service-data lengths, one-byte Flags payloads, conflicting complete names and
nonzero bytes after a zero terminator all participate in rejection. Any invalid
suffix clears the entire result. Unknown complete fields are allowed; this is
structural validation, not proof that every vendor-specific payload is valid.
An ordinary or empty well-formed report returns success with zero hint bits.

Service Data fields `16`, `20` and `21` are length-checked but do not establish
FMNA/DULT hints: upstream's `isAdvertisingService` path enumerates advertised
UUID lists. A name such as “AirTag” and Apple-looking bytes embedded in another
AD field establish no hint. Unlike upstream's broad Apple byte search at
`WiFiScan.cpp:1244–1257`, the Apple predicate requires the exact complete field
already used by Cerberus's passive Watch classifier. Upstream's UUID admission
at `:1264–1271`, and the service UUID constants in `WiFiScan.h:294–304`, provide
the supported service rules. Labels are hints about broadcast bytes, not
authenticated tag identity, an owner, a physical device count or intent.

### Report provenance and integration boundary

The parser deliberately has no address, scan-response flag, connectability,
timestamp or generation input. Its caller must provide one complete report and
retain those facts separately. A buffer truncated exactly at an AD boundary can
look like a valid shorter report, so the radio completeness flag is essential.
No concatenation of cached ADV/response bytes is permitted.

Snapshot/monitor integration must preserve the existing match on address,
address type, sweep/generation, advertising SID and the two-second original-ADV
window. Store parsed ADV and response hints separately, preserve their own
report times/sequences, and invalidate associated response evidence when a new
original ADV replaces its source. A response cannot refresh original-ADV age or
establish connectability. Mixed passive hints may be displayed together without
becoming Sound eligibility.

At the source audit baseline, `s3_ble_snapshot.c` obtained its Find My focus bit
from Watch's single winning category; `sentinel_watch_detect.c` recognized the
Apple frame but did not classify FD44/DULT service lists as Find My. The new
parser supplies independent passive Find My evidence for snapshot and monitor
integration. Changing that focus bit does not by itself change Watch's separate
event classifier. Existing active parsing in `s3_findmy_core.h`, selected Sound,
central/GATT and Beep All is outside this patch and retains its prior rules.

### Parser checks and acceptance limits

Run `py -3.14 tools/check_ble_findmy_hints.py`. It compiles the production header
without ESP/NimBLE/active Sound code and tests both list AD types and every UUID
position/width, exact base/DULT bytes, mixed/duplicate hints, full Apple length,
false signatures inside other fields, service-data exclusions, malformed tails
after a valid hint, short known fields, conflicting names, zero padding, the
255-byte boundary, null input handling and input immutability. Per-byte UUID
mutations and all prefix truncations exercise matching boundaries directly.

The parser fixture alone does not prove installed scan-response matching,
monitor freshness, UI selection or hardware coverage. Those require production
snapshot/monitor integration checks and device validation. No active operation
is made eligible by this helper, and FMNA/DULT physical-device behavior remains
unverified until representative owned test devices are observed.

### Rev59 integrated discovery evidence, 2026-10-30

The scanner now publishes independent original-ADV and matched-response hints
through its existing locked `get_focus` API. Each row adds a 32-byte provenance
record rather than another payload buffer. It contains exact address/type,
sweep generation, source sequences/times, SID, hint sets and separate
full-source/raw-copy completeness flags. A complete source report can exceed
the retained 31-byte raw copy; that copy stays marked truncated and cannot
become eligible for Sound through these new hints.

Every new original clears its prior response association before any malformed,
incomplete, oversized or null-data return. A valid response must match the
current complete source, normalized event kind, identity, SID and sweep, arrive
within two seconds, and advance the source sequence/time. Sequence exhaustion
fails closed until the next sweep. The original raw packet, original time,
original RSSI, Watch counters and advertisement counters do not change when a
response arrives. An ordinary new original clears the current Find My filter
bit; a historical monitor record retains its own immutable evidence until it
expires, without being relabeled with the newer ordinary packet's time.

The list separates ADV and RSP hints. Detail and Monitor show each source's
profiles, FMNA UUID widths and original age. Monitor validates copied
provenance, rejects stale same-millisecond source-sequence replacement and
expires rows after 120 seconds without new qualifying evidence. These are
address observations, not a count of physical tags. Normal list/Monitor sweeps
already use standard active discovery to request scan responses; Watch,
Wardriving, Fox Hunt and Analyzer retain their passive scanning configuration.

Production parser, snapshot, legacy/extended scanner, monitor/cache, UI
lifecycle/renderer, frozen scan-store and live-navigation checks pass. Native
Monitor previews and the changed selected-detail block were inspected. Added
negative regressions prove the unchanged raw action gate rejects newly
recognized UUID32/base128-only, mixed-profile and response-only observations.
`s3_findmy_core.h`, `s3_findmy_ui_route.h` and `s3_ble_central.c` are unchanged.
This does not implement the broader active gaps in the historical audit above.

The read-only `radio` console summarizes ADV, response, mixed-hint and
raw-copy-truncated rows plus FMNA width bits only from a completed sweep. It
discards the summary if the generation changes during retrieval. These
diagnostics neither start a scan nor renew a radio lease.

### Rev60 shared evidence and retained acceptance

The observational source metadata now lives in `focus.discovery`, with generic
`CERBERUS_S3_BLE_EVIDENCE_*` flags and explicit `findmy_advertisement` /
`findmy_scan_response` fields alongside Flipper hints. The common record is
36 bytes on the S3, four above rev59. All Find My validation, immutable
selection, original-time, same-millisecond ordering and raw-gate tests pass
after migration. The Find My parser, raw action gate and central/GATT source
hashes remain unchanged. This is source sharing, not expanded Sound eligibility.

The owner confirmed the rev59 ADV/RSP hint display and Monitor STOP/Back on
their tag. The post-exit console records BLE generation 3, idle scanner,
released host and no service error/new reset fault. Stopped row evidence is
cleared, so the console cannot independently prove the specific advertising
bytes, response-only formats or completed-sweep count. Broader physical
format coverage and rev60 device acceptance remain pending. See the
[rev59 receipt](../../integration/device-test-2026-10-27/rev59-flash-receipt.json)
and [rev60 receipt](../../integration/device-test-2026-10-27/rev60-flash-receipt.json).
