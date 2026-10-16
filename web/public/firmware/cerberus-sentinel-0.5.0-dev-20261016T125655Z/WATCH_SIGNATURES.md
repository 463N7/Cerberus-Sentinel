# Cerberus Sentinel Watch: broadcast signature reference

Catalog reviewed 2026-10-13; expanded protocol review 2026-10-14; plain-language label and explanation update 2026-10-15.

Implementation: `s3_main/main/sentinel_watch_detect.c` / `.h` and `shared/sentinel_watch_protocol.c` / `.h`.
Verification: `python tools/test_sentinel_watch_detect.py`.
Redistribution notices: [WATCH_THIRD_PARTY_NOTICES.md](WATCH_THIRD_PARTY_NOTICES.md).

## What a match means

Watch recognizes documented patterns in received Wi-Fi names and BLE advertisements. A match is an **unauthenticated broadcast hint**. Names, addresses, service identifiers and complete advertisement payloads can be copied or spoofed. A match does not establish who owns a device, whether it is recording, or whether someone is being tracked. No match does not establish that a camera or tracker is absent.

The classifier has no radio APIs, heap allocation, connection logic, location lookup, online database lookup, or transmit operations. It takes observations already obtained by the radio adapter. Its results contain fixed printable labels and evidence descriptions, never untrusted broadcast text.

Confidence describes the evidence in a packet, not a measured probability:

| Level | Meaning |
| --- | --- |
| High | A documented complete frame layout, or two specific observations that agree. Still unauthenticated. |
| Medium | A documented vendor/service or constrained name pattern. Model and device role may remain unknown. |
| Low | A distinctive name prefix with no protocol corroboration. |

Axon results say **equipment hint**, not confirmed body camera. Flock results include accessory and battery hints; they do not establish that a camera is attached. Apple results are labeled **AirTag / Find My hint**: the format is shared by AirTags and other Find My accessories and does not confirm a model. Tile and Chipolo results name their tracker ecosystem, which can also include compatible accessories. Google Find Hub retains its separate label and explanation within the tracker category. Camera hints currently cover GoPro; recording status stays unknown.

## Input and matching contract

- Wi-Fi: at most 32 SSID bytes and one BSSID. SSIDs need not be NUL terminated. No OUI-only Wi-Fi rules are installed.
- BLE: one legacy advertisement of at most 31 bytes, plus an optional bounded cached local name. No concatenation of advertisements and scan responses.
- A nonzero transport truncation/malformed flag, invalid length, malformed TLV, invalid service UUID field width, duplicate name field, or nonzero bytes after an AD terminator rejects the complete observation.
- Exact name-plus-protocol corroboration uses a complete local name carried **inside the same packet**. A separate cached name can only produce the specified name-only hint. Shortened AD names do not satisfy exact-name rules.
- All fields are checked before any result is published. A valid-looking prefix followed by a malformed field does not produce a match.
- 16-bit UUIDs are recognized in 16-bit lists, compatible 32-bit lists, the exact Bluetooth base 128-bit encoding, and their corresponding service-data fields. Proprietary 128-bit UUIDs are matched in the BLE wire byte order.
- The strongest result wins; ties retain the first documented rule evaluated. The engine returns one result per observation, rather than claiming to enumerate every possible role.
- Address type and address are accepted for the caller's observation bookkeeping. The BLE classifier currently does not infer products from an address prefix.

## Installed rules

The original rules below contain 45 exact proprietary 128-bit UUID fingerprints, nine assigned member service IDs that directly produce a vendor/ecosystem hint, additional corroborating fields, and strict frame/name rules. The expanded families follow in a separate table. The source keys link to the original material and its license.

| Rule ID | Required evidence | Result / confidence | Source |
| --- | --- | --- | --- |
| `apple.offline-finding` | Manufacturer `004C`, subtype `12`, internal length `19`, exactly 29 manufacturer bytes including company ID | AirTag / Find My hint / High; no model claim | HAYSTACK |
| `google.findhub-160` | Service data `FEAA`, type `40` or `41`, exactly 23 or 24 service-data bytes including UUID | Find Hub broadcast / High; no model claim | GOOGLE |
| `axon.fc81` | Advertised service `FC81` | Axon equipment / Medium | SIG |
| `axon.fe6b` | Advertised service `FE6B` | Axon / TASER equipment / Medium | SIG |
| `axon.fe6c` | Advertised service `FE6C` | Axon / TASER equipment / Medium | SIG |
| `flock.ssid-format` | Case-insensitive `Flock-` followed by exactly six hexadecimal digits, no extra characters | Flock Wi-Fi hint / Medium | FLOCK |
| `flock.ssid-mac-suffix` | Same strict SSID, with its six digits matching the BSSID's final three bytes | Flock Wi-Fi hint / High | FLOCK |
| `flock.battery-name` | Exact `FS Ext Battery`, or `Penguin-` followed by exactly ten decimal digits | Flock battery hint / Medium | FLOCK |
| `flock.battery-corroborated` | Same battery name plus manufacturer `09C8` with payload in the same packet | Flock battery hint / High | FLOCK |
| `flock.accessory-service` | Advertised `e8ccbb38-9532-46a8-9fe5-1814df172e6f` | Flock accessory hint / Medium | FLOCK |
| `flock.raven-service-pair` | Both custom services `3100` and `3200` in one packet | Raven service hint / Medium | RAVEN |
| `tile.feed`, `tile.feec`, `tile.fd84` | Corresponding advertised Tile-assigned service | Tile ecosystem / Medium | SIG |
| `chipolo.fe65`, `chipolo.fe33` | Corresponding advertised Chipolo-assigned service | Chipolo ecosystem / Medium | SIG |
| `gopro.fea6` | Advertised `FEA6` | GoPro camera hint / Medium; recording unknown | GOPRO, SIG |
| `gopro.schema` | `FEA6` plus registered company `02F2`, 14 manufacturer bytes including company ID, schema byte 2 or 3 | GoPro camera hint / High | GOPRO, SIG |
| `wellness.lovense-service` | One of 37 exact proprietary advertised service UUIDs in the pinned Lovense configuration | Lovense protocol hint / Medium | BUTTPLUG |
| `wellness.lovense-name-service` | `LVS-` name with a nonempty alphanumeric, hyphen or underscore suffix plus `FFF0`, in one packet | Lovense protocol hint / Medium | BUTTPLUG |
| `wellness.lovense-name` | Same constrained `LVS-` name without protocol corroboration | Lovense name hint / Low | BUTTPLUG |
| `wellness.wevibe-name-service` | An exact configured WeVibe model name plus `f000bb03-0451-4000-b000-000000000000`, in one packet | WeVibe protocol hint / High | BUTTPLUG |
| `wellness.mysteryvibe-service` | `f0006900-110c-478b-b74b-6f403b364a9c`; an exact configured model name in the same packet raises confidence | MysteryVibe service hint / Medium or High | BUTTPLUG |
| `wellness.kiiroo2-name-service` | Exact `Launch` or `Onyx2` plus either documented Kiiroo V2 service, in one packet | Kiiroo protocol hint / High | BUTTPLUG |
| `wellness.kiiroo21-name-service` | An exact configured Kiiroo V2.1 / OhMiBod model name plus `1900` or the documented proprietary service, in one packet | Kiiroo / OhMiBod hint / Medium | BUTTPLUG |
| `wellness.keon-name-service` | Exact `KEON WIFI`, `Keon Wifi`, or `KEON2` plus `1900`, in one packet | Kiiroo Keon hint / Medium | BUTTPLUG |
| `wellness.satisfyer-pair` | Complete name beginning `SF ` plus the documented proprietary service or company `005D` with payload prefix `00 00 27` / `00 00 28`, in one packet | Satisfyer protocol hint / Medium | BUTTPLUG |

The proprietary UUID allowlist is explicit in `uuid_rules` in the C source. The Lovense entries use only the pinned configuration's `advertised_services` values; the generic Nordic UART UUID is excluded. Other wellness service values are documented GATT protocol identifiers: they can be recognized **if a device broadcasts them**, but their presence in a GATT profile does not guarantee that it advertises them. Watch never connects to discover them.

WeVibe names such as `Nova`, `Wand`, and `Sync` are not classified on their own. Exact name lists are tied to the protocol files below. No wellness result identifies a person, an activity, or device usage.

## Deliberate exclusions and coverage limits

- Apple company ID `004C` alone, iBeacon subtype `02`, unrelated Apple continuity formats, and short Offline Finding packets are not called AirTags or Find My trackers.
- Google Fast Pair `FE2C` and Eddystone types `00`, `10`, `20`, `30` are not Find Hub detections. Extended BLE advertisements and the 256-bit Find Hub format are outside this 31-byte input contract.
- Samsung company ID `0075` and service `FD5A` alone are not called SmartTags by this catalog.
- XUNTONG `09C8`, general Qualcomm/Espressif/Texas Instruments OUIs, and Nordic UART do not establish Flock or wellness products.
- `Penguin`, `Flock`, `Raven`, `Pigvision`, a bare ten-digit number, `toy`, `vibe`, and the prefix `LOVE-` alone are excluded. Generic BLE services `180A`, `1809`, `1819`, `FFF0`, and `1900` do not classify a device alone.
- The Axon services identify assignments to Axon/TASER, not a product model, police presence, or recording state. No transmission or triggering of Axon equipment is implemented.
- `SMART_GLASSES` is a reserved category and demo scene with **no installed live classifier rule**. Meta/Luxottica company IDs or `FD5F` alone do not distinguish glasses from other products sufficiently for this release.
- Firmware changes, renamed devices, silent devices, short scan windows, packet loss, unsupported radio formats, and bounded result storage all limit coverage. Classification has been checked against documented formats and synthetic host fixtures; this is not a field-measured sensitivity/specificity study.

## Expanded Watch families (0.5)

| Rule / protocol | Required evidence | Result / confidence | Primary source |
| --- | --- | --- | --- |
| `flipper.serial-name` | Service `3080`, `3081`, `3082`, or `3083` plus a complete `Flipper` or `Flipper ` local name in the **same** packet | Flipper broadcast hint / High; normal serial profile only | FLIPPER |
| `meshtastic.service` | Exact `6ba1b218-15a8-461f-9fa8-5dcae273eafd` service | Meshtastic BLE hint / Medium; no LoRa reception | MESHTASTIC |
| `samsung.smarttag-v1` | `FD5A` service data with exactly 20 payload bytes; version nibble 1, state 1–6, reserved bytes 13–15 zero and a nonzero privacy-ID field | SmartTag protocol hint / High; observed registered v1 format, no model-generation claim | SAMSUNG |
| `beacon.eddystone` | `FEAA` service data with supported UID, URL, TLM or EID type and exact format checks | Eddystone beacon / High; no URL opening or identity resolution | EDDYSTONE |
| `beacon.altbeacon` | Exactly 26 manufacturer bytes, `BE AC` marker and reference RSSI in the documented signed range | AltBeacon broadcast / High; company assignment alone cannot match | ALTBEACON |
| PwnGrid advertisement | Management beacon, source `DE:AD:BE:EF:DE:AD`, complete IE 222 JSON carrying a display name, 64-hex identity, and `grid_version` | Pwnagotchi-compatible broadcast / High; presence does not establish attack activity | PWNGRID |
| Remote ID over BLE | `FFFA` service data, application code `0D`, counter, complete supported 25-byte message or message pack | Remote ID broadcast / High; transmitter claims, not verified aircraft identity | ODID |
| Remote ID Wi-Fi beacon | Vendor IE `FA:0B:BC`, type `0D`, counter and complete supported message pack | Remote ID broadcast / High | ODID |
| Remote ID Wi-Fi NAN | Public vendor action header `04 09 50 6F 9A 13`, service `88 69 19 9D 92 09`, supported follow-up descriptor and complete message pack | Remote ID broadcast / High | ODID |
| `ble.activity-rise` | Three baseline sweeps followed by two consecutive elevated sweeps: valid report rate at least 80/s and four times baseline, at least 16 observed address/type pairs, and a completed one-second peak of at least 100 reports | Unusual BLE activity / Low; experimental area measurement, no device attribution | Original heuristic, not a vendor signature |

The general BLE classifier still consumes one complete legacy packet (31 bytes maximum). The separate Remote ID decoder also accepts one complete extended report up to 255 bytes. Fragmented, truncated or partial reports must be rejected by the capture adapter. The decoder does not concatenate reports or perform periodic advertising synchronization.

### Remote ID fields and bounds

The original shared parser accepts documented protocol versions 0, 1 and 2, a 25-byte message or a pack of 1–9 such messages, and validates enclosing AD/IE/NAN lengths before releasing a result. A malformed trailing element rejects a preceding candidate. Unsupported versions/types and inconsistent pack widths/counts are rejected. Recognized messages are Basic ID, Location, Authentication, Self ID, System and Operator ID; authentication data is **not verified**. System/operator locations and owner identity are not exported.

Only printable serial/registration Basic IDs are exposed as text (20 bytes maximum). Binary UUID/session IDs are recognized without manufacturing a text identifier. Location exports reported latitude/longitude in units of 1e-7 degrees. The both-zero unknown-position sentinel stays absent. Altitude is reported in decimeters with an explicit reference: geodetic WGS84 height above ellipsoid, or pressure altitude if geodetic altitude is unavailable. Encoded unknown altitude stays absent. These are never labeled as receiver coordinates, terrain clearance, distance, bearing or altitude above mean sea level. Separate messages may be merged only for the same source address/type/kind; counters can wrap 255 to 0.

Raw Wi-Fi input is a management frame of at most 2304 bytes with radiotap and FCS already removed. Protected, fragmented, HT-control and unsupported frame layouts are rejected. Channel dwell and scan gaps limit capture; a valid decoder does not imply continuous RF coverage.

### PwnGrid fields and bounds

The parser supports the original uncompressed, non-streamed PwnGrid advertisement. IE 222 chunks within one beacon are consumed with a bounded streaming JSON parser (1536 total JSON bytes, nesting limit 6, collection/member limits 128). Quoted-string escapes, number grammar, complete containers, semantic field types, duplicate required keys and trailing data are checked. Compressed IE 223 and stream-fragment IE 226 formats are explicitly unsupported; bytes from separate beacons are never joined.

The shared transmitter MAC is a protocol marker, **not a peer identity**. The capture adapter must use the per-peer session BSSID (802.11 address 3) for history and deduplication. The printable display name is limited to 20 characters. Cryptographic signatures are not verified, and names/identities remain spoofable. A changing session BSSID can count the same physical device again.

### Limits specific to these additions

- Flipper Bluetooth disabled, connected profiles that stop advertising, HID operation and altered/custom advertisements can be invisible. A copied Flipper name alone does not match. Color values include transparent hardware (`3083`).
- Meshtastic identifies a BLE interface; there is no sub-GHz/LoRa radio or mesh-message decoder in the production board.
- Samsung support is deliberately the documented registered-tag v1 payload. Other versions, unregistered tags and different SmartTag generations require validated evidence before adding rules. The embedded signature is not authenticated and rotating privacy IDs are not linked.
- Eddystone checks UID length/reserved bytes, URL scheme/encoded bytes, plain/encrypted TLM version and width, and EID width. Encrypted TLM/EID payloads are not decrypted. Ordinary beacons are educational discoveries.
- Skimmer rules are excluded. No iBeacon rule or new broad manufacturer/OUI match was added.
- Behavioral BLE activity is a separate aggregate heuristic; it never attributes activity to Flipper or another product from its traffic volume alone.
- Activity counts are controller-delivered advertisement reports, not all RF packets. Address/type tracking is bounded at 128 and is a lower bound, not a count of physical devices. A gap over two minutes restarts baseline learning. Saturated counters and incomplete/error sweeps do not train or trigger the heuristic.

## Primary sources and provenance

### SIG — Bluetooth SIG assigned numbers

Read from the SIG-maintained public registry on 2026-10-13:

- [Member service UUID registry](https://bitbucket.org/bluetooth-SIG/public/raw/main/assigned_numbers/uuids/member_uuids.yaml): `FC81` Axon Enterprise, `FE6B` / `FE6C` TASER International, `FEED` / `FEEC` / `FD84` Tile, `FE65` / `FE33` Chipolo, `FEA6` GoPro.
- [Company identifier registry](https://bitbucket.org/bluetooth-SIG/public/raw/main/assigned_numbers/company_identifiers/company_identifiers.yaml): confirms the company assignments used for corroboration and exclusions, including GoPro `02F2`.

These are registry assignment facts, not a product model database. No registry document or vendor logo is bundled. The registry's own notices remain applicable to its documents; Cerberus does not relabel the SIG registry as MIT-licensed material.

### FLOCK — original Flock research

[colonelpanichacks/flock-you, firmware-derived signature notes](https://github.com/colonelpanichacks/flock-you/blob/6704f011f3e6c9cfe94fe18c593f863032c9c954/datasets/firmware_derived_signatures.md), commit `6704f011f3e6c9cfe94fe18c593f863032c9c954`.

The source documents the SoftAP naming construction, battery naming formats, manufacturer identifier, and accessory service UUID. Cerberus uses those bounded passive identifiers, with narrower matching and evidence labels. It does not import the source's network operations, broad OUI tables, credentials, location retrieval, or geographic deployment data. License: [MIT](https://github.com/colonelpanichacks/flock-you/blob/6704f011f3e6c9cfe94fe18c593f863032c9c954/LICENSE), copyright 2026 colonelpanichacks. Notice retained in the companion notice file.

### RAVEN — original service catalog

[NSM-Barii/flock-back, src/signatures.py](https://github.com/NSM-Barii/flock-back/blob/011e024e0fc0c06567f44ababfa7c957ac8b6b4e/src/signatures.py), commit `011e024e0fc0c06567f44ababfa7c957ac8b6b4e`.

Cerberus requires both exact custom services `3100` and `3200`; it does not adopt matches on generic Device Information, Health Thermometer, or Location and Navigation services. License: [MIT](https://github.com/NSM-Barii/flock-back/blob/011e024e0fc0c06567f44ababfa7c957ac8b6b4e/LICENSE), copyright 2025 Jabari Lucien. Notice retained.

### HAYSTACK — Offline Finding frame

[OpenHaystack Zephyr, modules/openhaystack/src/openhaystack.c](https://github.com/koenvervloesem/openhaystack-zephyr/blob/86c4ea5219602eac8a7a800eccf4164ba865aa6e/modules/openhaystack/src/openhaystack.c), commit `86c4ea5219602eac8a7a800eccf4164ba865aa6e`.

This original implementation documents the full manufacturer frame layout. Cerberus implements a receive-only format check and does not import advertiser or key-generation code. License: [MIT](https://github.com/koenvervloesem/openhaystack-zephyr/blob/86c4ea5219602eac8a7a800eccf4164ba865aa6e/LICENSE), copyright 2021 Koen Vervloesem. Notice retained. The separate `seemoo-lab/openhaystack` repository was reviewed for context but its AGPL implementation was not imported.

### GOOGLE — Find Hub protocol specification

[Google Find Hub Network Accessory Specification, advertising payload tables](https://developers.google.com/nearby/fast-pair/specifications/extensions/fmdn), retrieved 2026-10-13. Cerberus transcribes the legacy frame's UUID, type, and length conditions and does not implement its pairing or message stream.

Google Developers documentation is covered by [its site content license](https://developers.google.com/terms/site-policies): generally CC BY 4.0 for documentation and Apache 2.0 for code samples, except where otherwise noted. Attribution is provided here; the summary and receive-only matching conditions are adapted by the Cerberus Sentinel contributors. No sample code is included.

### GOPRO — vendor BLE protocol documentation

[Open GoPro BLE setup and advertising schemas](https://gopro.github.io/OpenGoPro/docs/ble/protocol/ble_setup/), retrieved 2026-10-13. The vendor explicitly documents `FEA6` for discovery and schema versions 2 and 3. The source table prints company bytes as `F202`; the classifier parses the registered GoPro identifier `02F2` in BLE little-endian wire order (`F2 02`).

Source revision reviewed: [GoPro/OpenGoPro](https://github.com/gopro/OpenGoPro/tree/0f963572611c4410a15678531e9681a6ff874edb), commit `0f963572611c4410a15678531e9681a6ff874edb`, including the vendor's advertisement parser and tests. The [Open GoPro project page](https://gopro.github.io/OpenGoPro/) describes its API as available under MIT; the repository also carries notices for third-party components. Only documented wire facts were used here; no SDK implementation or third-party library was imported.

### BUTTPLUG — maintained original device protocol configurations

[buttplugio/buttplug](https://github.com/buttplugio/buttplug/tree/179b0d9306f519912c10bdddd111686622b7f63a), commit `179b0d9306f519912c10bdddd111686622b7f63a`. License for the selected configuration material: [BSD 3-Clause](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/LICENSE), copyright 2016–2023 Nonpolynomial Labs, LLC. The full applicable notice is retained in the companion notice file. Names and UUIDs were selected from these original protocol definitions; control commands and protocol handlers were not imported.

Pinned files under `crates/buttplug_server_device_config/device-config/protocols/`:

- [lovense.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/lovense.yml)
- [wevibe.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/wevibe.yml), [wevibe-8bit.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/wevibe-8bit.yml), [wevibe-chorus.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/wevibe-chorus.yml)
- [mysteryvibe.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/mysteryvibe.yml), [mysteryvibe-v2.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/mysteryvibe-v2.yml)
- [kiiroo-v2.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/kiiroo-v2.yml), [kiiroo-v21.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/kiiroo-v21.yml), [kiiroo-v3.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/kiiroo-v3.yml)
- [satisfyer.yml](https://github.com/buttplugio/buttplug/blob/179b0d9306f519912c10bdddd111686622b7f63a/crates/buttplug_server_device_config/device-config/protocols/satisfyer.yml)

### Discovery sources that were not imported

[Argus](https://github.com/kevwillow/argus-db) was used to find original research and registry references. Its current licenses were checked: code AGPL-3.0-or-later, compiled database ODbL-1.0, documentation CC BY-SA 4.0. It is **not an MIT database**. No Argus database/export subset, confidence score, source excerpt collection, code, or documentation text is distributed here. The installed rules were independently checked against the primary sources listed above.

No SquachWatch / SquatchWatch code, signature table, artwork, character, or asset is imported. Cerberus's parser, decision logic, evidence wording, tests, and user interface integration are its own implementation.

## Tests and future changes

The host suite compiles the production C implementation and checks positive format fixtures; wrong lengths, companies, subtypes, UUID endianness and Bluetooth base encodings; strict Flock names and SSID suffixes; generic-name and shared-vendor negatives; cached-name separation; full-packet validation; null and overlength input; and 32,000 deterministic packet-boundary cases. On Windows the input ends at a protected memory page so overreads fault immediately. Test identifiers and payloads are synthetic and do not contain captured device keys or deployment locations.

Before adding a rule, record the original source and revision, preserve its license notices, specify exactly which broadcast evidence is required, and add a realistic positive fixture plus nearby false-positive cases. Distinguish a GATT service known only after connecting from one actually present in advertisements. Keep claims at the level supported by the source. A renamed or spoofed broadcast must never be described as authenticated proof.


## Primary sources for the expanded families

### FLIPPER — official serial profile and hardware color values

Revision `3fb1d51727733d2007abc5ec636a89328857803e`:

- [Official serial profile](https://github.com/flipperdevices/flipperzero-firmware/blob/3fb1d51727733d2007abc5ec636a89328857803e/targets/f7/ble_glue/profiles/serial_profile.c).
- [Hardware color enum](https://github.com/flipperdevices/flipperzero-firmware/blob/3fb1d51727733d2007abc5ec636a89328857803e/targets/furi_hal_include/furi_hal_version.h).
- [Local Bluetooth name construction](https://github.com/flipperdevices/flipperzero-firmware/blob/3fb1d51727733d2007abc5ec636a89328857803e/targets/f7/furi_hal/furi_hal_version.c).

The official serial advertisement ORs the base service with the color value; the current enum has unknown, black, white and transparent values 0–3. Only these protocol constants and naming facts were recorded. No GPL-3.0 implementation, assets or copied tests were imported.

### MESHTASTIC — official BLE client discovery

[Meshtastic BLE interface](https://github.com/meshtastic/python/blob/0a1835760c2437314a2d3544bbdd8b6fead559bc/meshtastic/ble_interface.py), revision `0a1835760c2437314a2d3544bbdd8b6fead559bc`. The official scanner filters by its declared service UUID. Source license GPL-3.0; only the interoperable identifier is used, with original matching code and synthetic tests.

### SAMSUNG — original peer-reviewed protocol measurements

Tingfeng Yu, James Henderson, Alwen Tiu and Thomas Haines, [Security and Privacy Analysis of Samsung’s Crowd-Sourced Bluetooth Location Tracking System](https://www.usenix.org/system/files/usenixsecurity24-yu-tingfeng.pdf), USENIX Security 2024, especially Figure 3 and Table 1. The paper measured SmartTag firmware 1.01.26/1.02.06. Its diagram numbers bits from the most significant side: the version is the high nibble of byte 0 and state is the low three bits. This implementation checks those format facts and the reserved-byte layout, without importing a paper figure, software, real tag identifiers or its active research procedures.

### EDDYSTONE — original Google protocol specifications

Revision `bb8738d7ddac0ddd3dfa70e594d011a0475e763d`: [protocol overview](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/protocol-specification.md), [UID](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-uid/README.md), [URL](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-url/README.md), [TLM](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-tlm/README.md), [EID](https://github.com/google/eddystone/blob/bb8738d7ddac0ddd3dfa70e594d011a0475e763d/eddystone-eid/README.md). The source repository is Apache-2.0. Wire-format facts only; no Google implementation or specification text is bundled.

### ALTBEACON — original protocol definition

[AltBeacon specification](https://github.com/AltBeacon/spec/blob/d5c7f6da1192313d28222f9da2e0251074153252/README.md), revision `d5c7f6da1192313d28222f9da2e0251074153252`. The repository does not carry a separate standard LICENSE file at the reviewed revision. Only the public packet discriminator, field width and numeric range are implemented independently; no specification artwork, text or code is redistributed.

### PWNGRID — original Pwnagotchi advertisement transport

Revision `52d5685f2cea1ada86e9c28b16dc912013c68b71`: [Wi-Fi constants](https://github.com/evilsocket/pwngrid/blob/52d5685f2cea1ada86e9c28b16dc912013c68b71/wifi/defines.go), [packet framing](https://github.com/evilsocket/pwngrid/blob/52d5685f2cea1ada86e9c28b16dc912013c68b71/wifi/pack.go), [peer advertisement fields](https://github.com/evilsocket/pwngrid/blob/52d5685f2cea1ada86e9c28b16dc912013c68b71/mesh/peer.go). GPL-3.0 source was used only to establish protocol facts. The streaming JSON parser, frame validation and fixtures are original; no Pwnagotchi/PwnGrid code, icons, faces or radio operations were copied.

### ODID — official OpenDroneID wire definitions

[Core wire definitions](https://github.com/opendroneid/opendroneid-core-c/blob/849c03adbe0cdd68997a462fafab397edcfe53f3/libopendroneid/opendroneid.h), [Wi-Fi framing](https://github.com/opendroneid/opendroneid-core-c/blob/849c03adbe0cdd68997a462fafab397edcfe53f3/libopendroneid/wifi.c), [Wi-Fi structures](https://github.com/opendroneid/opendroneid-core-c/blob/849c03adbe0cdd68997a462fafab397edcfe53f3/libopendroneid/odid_wifi.h), revision `849c03adbe0cdd68997a462fafab397edcfe53f3`; [official BLE framing](https://github.com/opendroneid/transmitter-linux/blob/6852afd0d4bed0d139b9dac3cd3a45eb595e30f0/bluetooth.c), revision `6852afd0d4bed0d139b9dac3cd3a45eb595e30f0`. Both repositories use Apache-2.0. This is an independent receive-only subset using explicit byte reads, not a bundled OpenDroneID library or transmitter. No third-party source code was imported. It is not a certification or a claim to implement every revision of ASTM/ASD-STAN Remote ID.

### Expanded host verification

`python tools/test_sentinel_watch_detect.py` exercises all previous rules, new-family positives/near negatives, four Flipper color variants, Samsung structural negatives, all supported Eddystone frames and AltBeacon. The protocol fixture checks legacy and extended BLE, a nine-message pack, Wi-Fi beacon and NAN framing, ID/location/altitude sentinels and bounds, malformed/trailing fields, supported counter rollover, 255 PwnGrid IE chunk sizes, strict JSON negatives, and 12,000 guarded protocol-buffer cases. The original classifier also retains its 32,000 guarded packet-boundary cases. Fixtures are synthetic and no claim of live device validation is made by these tests.
