# Cerberus Base-Board Feature Backlog

Scope: ESP32-S3, onboard ESP32-C5, ST7796 display, GT911 touch, microSD,
native USB, and the existing S3/C5 SPI and UART connections. No expansion
radios or GPS are included.

Legend:

- `[x]` implemented in the base firmware
- `[~]` implemented but awaiting validation on the physical board
- `[ ]` not implemented

## Platform

- [x] ESP-IDF/PlatformIO S3 and C5 projects
- [x] 16 MB S3 flash layout
- [x] Dedicated 2 MB C5 layout with a 1,984 KiB factory application slot
- [x] ST7796 hardware-SPI display driver
- [x] Cerberus logo boot screen
- [x] Header-only base screen
- [x] Adjustable backlight API
- [x] S3 hardware-SPI3 master for the onboard C5 at the C5-rated 40 MHz ceiling
- [x] C5 hardware-SPI slave heartbeat/status service
- [x] C5-driven SPI READY handshake on the normal-runtime C5-to-S3 UART wire
- [x] S3-controlled C5 reset and boot mode
- [x] C5 UART flashing bridge
- [x] Fixed-size central event bus with typed privacy-safe events and drop telemetry
- [~] Versioned persistent settings service using NVS
- [~] Privacy-safe structured CSV system logging with bounded rotation
- [~] Live boot/runtime capability discovery with unavailable-feature gating
- [~] Last-reset reason and live S3 heap diagnostics
- [~] Internal-flash crash record with guarded metadata viewer and explicit clear
- [~] Guarded NVS-only factory-reset workflow

## User Interface

- [~] GT911 I2C touch driver
- [~] Touch-driven main menu
- [~] Direct feature-hub navigation for Wi-Fi, Bluetooth LE, and 802.15.4 tools
- [~] Dashboard showing C5 and touch status
- [x] Reusable list, grid, dialog, keyboard, graph, and progress widgets
- [~] Live status bar with uptime, storage, USB, touch, C5, Wi-Fi, BLE, and 802.15.4 state
- [~] Bounded on-screen keyboard with letters, numbers, spaces, editing, and URL punctuation
- [~] Theme and color settings
- [~] Persistent full/masked Wi-Fi and BLE address-display privacy control
- [~] Brightness control screen with reboot persistence
- [~] Persistent screen timeout with first-touch wake guard
- [~] Clock, timer, and notification screens
- [~] Accessibility sizing and touch calibration screen

## S3/C5 Protocol

- [x] Version 1 fixed-size heartbeat protocol
- [x] Version 2 fixed-size framed messages with automatic V1 fallback
- [x] CRC-16/CCITT-FALSE and payload-length validation
- [x] C5 capability bitmap for dual-band Wi-Fi, BLE, and IEEE 802.15.4
- [x] CRC-protected full-record C5 event batching with single-event fallback
- [x] Generic bounded fragmented-object reads with generation and end-to-end CRC32 validation
- [x] Bounded asynchronous C5 event queue with aggregate-only state notifications
- [~] Negotiated event-queue watermarks, back-pressure, and adaptive S3 draining
- [~] C5 passive Wi-Fi scan start/stop commands
- [x] Wi-Fi scan status and result records with chunked full-length SSIDs
- [x] BLE result records with chunked names and service UUIDs
- [x] 802.15.4 energy-scan status and 16-channel result records
- [~] C5 free/minimum heap and live radio-state telemetry
- [~] Protocol CRC, frame-length, corruption, and V1-layout test vectors

## microSD and Files

- [~] Native SDMMC 1-bit microSD on GPIO35-37 with prototype DAT3 held high on GPIO38
- [~] FAT32 mount/unmount service
- [~] Card presence, capacity, and free-space reporting
- [~] Read-only directory browser
- [~] Create, rename, copy, move, and delete operations
- [~] Read-only text viewer with touch paging
- [~] Scaled 24-bit and 32-bit uncompressed BMP image viewer
- [~] File details and on-demand SHA-256 calculation
- [~] Versioned CRC-protected configuration export and guarded staged restore
- [~] Bounded 802.15.4 diagnostic CSV export to `/CERBERUS/LOGS`
- [~] Firmware package storage
- [~] Verified 512 KiB read/write benchmark with automatic temporary-file cleanup
- [~] Files-root safe unmount and explicit remount workflow
- [~] Exclusive SD ownership between firmware and USB mass storage

## QR Code

- [~] QR renderer
- [~] Preset text and project-URL QR generation
- [~] Device-information QR linking to the Cerberus development-board page
- [~] Free-form text and URL QR entry with a 180-character limit
- [~] Wi-Fi connection QR with temporary masked credential entry
- [~] Web UI address QR
- [~] Diagnostic-report identifier QR containing only filename, size, and SHA-256
- [~] Explicit generated-QR BMP export to `/CERBERUS/QR`

## Web UI

- [~] Authenticated local HTTP server
- [~] First-run password setup with no universal default password
- [~] Session expiry and CSRF protection
- [~] Device dashboard
- [~] S3/C5 health and firmware information
- [~] Wi-Fi, BLE, and 802.15.4 result pages
- [~] Channel and signal graphs
- [~] File manager
- [~] Log download
- [~] Settings editor
- [~] Firmware upload and progress display
- [~] Reboot and recovery controls
- [~] QR generator

## USB

- [x] Bounded read-only USB CDC serial diagnostic console
- [~] USB mass storage backed by an exclusively unmounted SD card
- [x] Read-only device-information interface over USB serial
- [~] Physically confirmed S3 ROM download-mode entry with safe SD release
- [ ] U2F/FIDO-style authenticator evaluation
- [ ] Physically confirmed keyboard/media-key self-test
- [x] Fixed USB command API: help, info, health, c5, and storage

## C5 Wi-Fi

- [x] Passive 2.4 GHz and 5 GHz access-point scanning
- [x] SSID, BSSID, channel, RSSI, and security records
- [x] Hidden-network count for stored results
- [~] Single-scan 2.4/5 GHz channel occupancy ranking and strongest-RSSI graph
- [~] Six-scan rolling in-memory channel observation history with manual reset
- [~] Persistent trusted Wi-Fi SSID/BSSID/security baseline management
- [~] Station connection and disconnect control
- [~] DHCP address, gateway, subnet, channel, and RSSI status
- [~] DHCP-provided primary, backup, and fallback DNS status
- [~] Bounded gateway ping, default-route, NTP, and certificate-verified HTTPS connectivity tests
- [ ] Multi-hop traceroute
- [~] Bounded, user-initiated mDNS service discovery with paged local results
- [ ] Rate-limited reachability checks on configured private subnets
- [~] Aggregate Wi-Fi radio statistics with scan health, touchscreen/web summaries, USB diagnostics, and privacy-safe CSV export
- [~] Aggregate deauthentication/disassociation activity detection in a bounded, user-initiated per-channel survey
- [~] Heuristic interference indicators based on aggregate noise-floor and frame-rate observations
- [~] Passive duplicate-SSID and security-inconsistency review screen
- [~] Passive exact/mismatch indicators against user-configured trusted baselines
- [~] Suspicious aggregate beacon-rate indicators per observed channel
- [ ] Defensive device-signature alerts
- [~] Aggregate management/control/data frame counters without payload or address retention
- [~] Optional stable address pseudonyms for touchscreen and web display

## C5 Bluetooth LE

- [x] Passive advertisement scanning
- [x] Name, address, RSSI, manufacturer ID, connection indicator, and service UUID records
- [x] Persistent favorites and local allowlist
- [~] RSSI history and nearby-device count
- [~] User-initiated GATT connections without a favorite prerequisite
- [~] GATT service browser
- [~] Read-only GATT characteristic, handle, and property browser
- [~] Bounded read-only characteristic value viewer for the active favorite
- [~] Standard BLE value decoding for device information, battery, and environmental data
- [ ] Characteristic writes restricted to selected or allowlisted devices
- [ ] Cerberus telemetry and companion-app BLE service
- [~] Dedicated read-only standard environmental-sensor viewer with sequential refresh
- [ ] Defensive AirTag, Flipper, drone, and skimmer signature alerts
- [~] Optional stable address pseudonyms for touchscreen and web display

## C5 IEEE 802.15.4

- [x] Channel energy scan
- [x] Channel occupancy graph
- [~] Passive classic Zigbee beacon discovery with no join, transmit, or raw-frame storage
- [ ] Thread network discovery
- [ ] Matter-capable network indicators
- [~] PAN ID, extended PAN ID, channel, RSSI, LQI, capacity, and join-permit display
- [~] Passive per-channel Beacon/Data/ACK/MAC Command/Other frame counters without payload storage
- [~] Defensive quiet/protocol-active/energy/interference channel assessment from separate passive snapshots
- [~] On-demand uptime-stamped energy, frame-counter, and assessment logging to microSD
- [~] Dedicated generated-report browser with text inspection, SHA-256, and confirmed deletion

## Restricted Local Scripting

- [~] Versioned `.cbs` script loader confined to `/CERBERUS/SCRIPTS`
- [~] Bounded text, dialog, wait, timer, and QR actions
- [ ] Sandboxed storage access
- [ ] Read-only system information and radio-result APIs
- [~] Fixed file/action/text/wait limits with pre-execution validation
- [x] Explicitly exclude raw frame transmission, arbitrary HID, shell execution,
      credential access, unrestricted GPIO, and firmware-control APIs

## Updates and Recovery

- [ ] Signed S3 firmware packages
- [ ] S3 recovery image strategy
- [ ] C5 package validation before bridge flashing
- [ ] C5 update progress and recovery screen
- [ ] Configuration backup before update
- [ ] Version rollback policy
- [ ] Reproducible release artifacts and checksums

## Validation

- [~] GT911 orientation and edge-coordinate test
- [ ] 24-hour display/touch soak test
- [ ] SD hot-insert and removal tests
- [ ] USB/SD ownership and corruption tests
- [ ] S3/C5 protocol fault-injection tests
- [ ] C5 2.4/5 GHz coexistence tests
- [ ] C5 Wi-Fi/BLE/802.15.4 concurrency tests
- [~] Live heap and main/UI task stack high-water monitoring with bounded warnings
- [ ] Brownout and interrupted-update recovery tests
- [ ] Privacy and address-anonymization tests

## Permanently Out of Scope for the Base Build

- Wardriving and location-linked radio collection
- Credential collection or recovery
- Handshake or PMKID collection
- Deauthentication, beacon, association, pairing, or notification spam
- Evil portals, Karma behavior, impersonation, or spoofing
- Packet-payload capture
- Jamming, replay, brute force, reverse shells, or network poisoning
- Unattended or arbitrary USB HID execution
- CC1101, NRF24, infrared, RFID/NFC, LoRa, FM, Ethernet, GPS, microphone,
  speaker, or other hardware not present on the base board
