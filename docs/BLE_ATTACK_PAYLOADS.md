# Marauder Bluetooth attack payload slice

Source reference: ESP32 Marauder `develop` at
`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`, especially
`esp32_marauder/MenuFunctions.cpp` (Bluetooth Attack menu) and
`esp32_marauder/WiFiScan.cpp` (`GetUniversalAdvertisementData`,
`executeBLESpam`, and the attack loop). The upstream repository is MIT licensed.
Cerberus implements its own bounded C builder in `shared/ble_attack_payload.c`.
The fixed bytes represent advertising format and vendor/protocol identifiers;
no upstream heap management, radio lifecycle, UI code, or timing loop was copied.

The upstream menu has **nine** attack routes. Cerberus's current field menu
shows seven greyed rows; it omits Apple Juice and Google BLE Spam. This source
slice does not enable the rows. An S3 advertiser with one shared NimBLE owner,
Stop handling, selected-capture routing, controller metrics and live receiver
checks is still required.

| Upstream route | Builder output | Reference burst | Remaining runtime work |
| --- | --- | ---: | --- |
| Sour Apple | Apple action (90%) or device (10%) manufacturer AD; payload/identity cached for up to one second | 60 ms | S3 advertise and verify reception |
| Apple Juice | Same Apple AD variants, interval 0x20, varying connectable flag | 500 ms | Add missing menu row; S3 advertise |
| Swiftpair Spam | Microsoft manufacturer AD with bounded 1–10 character generated name | 10 ms | S3 advertise |
| Samsung BLE Spam | Samsung manufacturer AD with one of 17 referenced watch model bytes | 10 ms | S3 advertise |
| Google BLE Spam | Fast Pair UUID/service data and TX power AD | 10 ms | Add missing menu row; S3 advertise |
| Flipper BLE Spam | Flags, five-character name, 16-bit service, TX power and manufacturer AD | 10 ms | S3 advertise |
| BLE Spam All | Google → Samsung → Swiftpair → Sour Apple → Apple Juice → Flipper | each above | One S3 host schedule with explicit Stop |
| Spoof AirTag | Byte-for-byte replay plan from a **selected, observed** BLE advertisement and its address | 10 ms | AirTag/FMNA/DULT scan selection, identity support and S3 advertise |
| FindMy Sound | No advertisement; builder returns `NEEDS_GATT` | n/a | Selected device GATT connection, service/characteristic check and authorized write |

The Flipper reference declares its manufacturer AD field as length five but
appends eight value bytes. Cerberus emits a valid length-nine field containing
all eight bytes. All generated AD streams are validated and capped at 31 bytes.
The random address in a plan is a NimBLE-order static-random address request;
the builder does not change the device address or transmit. A captured tracker
plan retains its captured address type and bytes; whether the S3 controller can
use that identity remains a hardware acceptance gate.

The 10 ms reference bursts can be shorter than a controller advertising
interval, so a successful API call alone cannot prove on-air reception. Runtime
work must report controller state and use independent observation before
claiming these modes work. No timed whole-job limit is in the payload plan; a
future engine repeats until the user taps Stop or leaves the page.

`tools/check_ble_attack_payloads.py` compiles the pure builder and tests all
nine routes, combined rotation, AD bounds/format, Apple one-second caching
across clock wrap, malformed selected captures and GATT routing. It exercises
format and scheduling intent only; it does not claim S3 RF acceptance.
