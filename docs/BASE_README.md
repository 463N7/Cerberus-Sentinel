# Cerberus Base Firmware

This repository is the clean starting point for future Cerberus firmware.
It intentionally contains no wardriving, GPS logging, WiGLE, expansion-radio,
web setup, remote-mode, pairing, or radio-transmission functionality.

## Included

- ESP32-S3 application with the Cerberus logo boot screen and a header-only
  blank-slate screen.
- ST7796 display on the S3 hardware SPI peripheral.
- Onboard ESP32-C5 connection using hardware SPI on both processors and a
  C5-driven READY handshake on the C5-to-S3 UART wire.
- A backward-compatible V2 framed S3/C5 protocol with CRC-16 validation,
  payload bounds, V1 fallback, and a radio capability bitmap.
- User-initiated, passive dual-band C5 Wi-Fi scanning with bounded result
  storage and a touchscreen list showing SSID, BSSID, channel, RSSI, security,
  band, and hidden-network counts. It does not connect, inject frames, or
  capture packet payloads.
- Aggregate Wi-Fi radio statistics for band/security/channel counts, scan
  duration and health, touchscreen/web summaries, and privacy-safe CSV export.
- Bounded, user-initiated defensive Wi-Fi observation surveys across channels
  found by the last scan, with aggregate frame-type, beacon-rate,
  deauthentication/disassociation, noise-floor, and activity indicators. No
  packet payloads, SSIDs, BSSIDs, or device addresses are retained by the
  survey, and its heuristic indicators are explicitly not treated as proof of
  an attack or jammer.
- User-initiated, passive C5 Bluetooth LE advertisement scanning with bounded
  records for address, RSSI, name, manufacturer ID, connection indicator, and
  one advertised service UUID. The scan itself does not connect, pair,
  advertise, or write; connections are separate and explicitly user initiated.
- A user-initiated, read-only BLE Environmental Sensing viewer for standard
  temperature, humidity, and pressure characteristics, with bounded sequential
  refreshes and no pairing or characteristic writes.
- Onboard C5 reset/boot control and the USB-to-UART flashing bridge.
- GT911 touch input and a base-board feature menu.
- Direct Wi-Fi, Bluetooth LE, and 802.15.4 feature hubs so each radio tool is
  selected from its section landing page instead of reached through scan-page chains.
- A persistent privacy control that replaces Wi-Fi BSSIDs and BLE addresses in
  touchscreen and web output with stable local pseudonyms while leaving trusted
  baseline, favorite, and connection matching intact.
- Live dashboard, self-test status, and display brightness controls.
- Versioned NVS settings with validated, reboot-persistent display brightness.
- Lazy-mounted FAT32 microSD service, read-only touchscreen file browser, and
  paged text viewer.
- File details with an on-demand, memory-bounded SHA-256 checksum.
- Scanline-streamed viewing of uncompressed 24-bit and 32-bit BMP images.
- Offline QR rendering for the Cerberus development-board page, the project
  URL, and custom text or URLs using a bounded touch keyboard and Espressif's
  maintained QR encoder component.
- Restricted `.cbs` automation loaded only from `/CERBERUS/SCRIPTS`, with
  pre-execution validation and bounded text, dialog, wait, timer, and QR
  actions. The format exposes no shell, HID, radio transmission, GPIO,
  credential, or firmware-control operations.

The planned base-board feature set and implementation status are tracked in
[`TODO.md`](TODO.md).

The restricted script format and a ready-to-copy example are documented in
[`docs/SCRIPTING.md`](docs/SCRIPTING.md) and [`examples/HELLO.CBS`](examples/HELLO.CBS).

## Hardware

| Function | S3 pins | Device pins |
| --- | --- | --- |
| LCD hardware SPI | SCLK 17, MOSI 18, CS 16, DC 4, RST 5, BL 6 | ST7796 |
| microSD SDMMC 1-bit | CLK 36, CMD 37, DAT0 35 | microSD socket |
| Onboard C5 hardware SPI3 | SCLK 12, MOSI 11, MISO 13, CS 10 | SCLK 4, MOSI 5, MISO 6, CS 8 |
| C5 control | EN 21, BOOT 40, UART TX 43, UART RX/READY 44 | Onboard ESP32-C5 |

The display uses SPI2 at 40 MHz, the C5 link uses SPI3 at 40 MHz, and the
microSD card uses the separate SDMMC peripheral in 1-bit mode at up to 20 MHz.
Production boards leave GPIO38 free and provide 10 kOhm card-side pull-ups on
CMD and DAT0-DAT3. The current prototype still routes DAT3 to GPIO38, so this
test firmware holds GPIO38 high until the production board is available.
During normal operation, GPIO44 is the C5-to-S3 READY signal so the S3 never
clocks a frame before the C5 hardware slave has queued it. The flash bridge
temporarily restores that wire to UART RX before resetting the C5 into its ROM.

The onboard C5 uses a dedicated 2 MB partition map: 24 KiB NVS, 4 KiB PHY
calibration data, and a 1,984 KiB factory application slot. The through-S3
flasher validates this map and the application size before erasing anything.

## Build

```powershell
py -m platformio run -e s3-main
py -m platformio run -e s3-c5-bridge
py -m platformio run -d c5_firmware -e c5-onboard
```

Flash the S3 normally:

```powershell
py -m platformio run -e s3-main -t upload
```

Flash the onboard C5 through the S3:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_c5_through_s3.ps1
```

## Starting A Project

Add project services to `s3_main/main/`, register them in its `CMakeLists.txt`,
and extend `shared/cerberus_protocol.h` only when communication with the C5 is
required. Keep protocol changes versioned on both processors.

The base S3 application shows the Cerberus logo and boot text, then clears to a
blank-slate screen containing only the CERBERUS header. It releases the onboard
C5 and reports the C5 heartbeat every five seconds.

## Layout

- `s3_main/`: main ESP32-S3 firmware.
- `c5_firmware/`: minimal onboard ESP32-C5 firmware.
- `shared/`: board pins and the shared base protocol.
- `docs/`: onboard C5 flashing notes.
- `tools/`: onboard C5 flashing utility.
