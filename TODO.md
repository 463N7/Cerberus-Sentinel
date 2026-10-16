# Sentinel roadmap

Here, [x] means implemented in source, not accepted on production hardware.
Historical Base work is preserved in [BASE_BACKLOG.md](docs/BASE_BACKLOG.md).

## First development candidate

- [x] Separate project based on the working Base.
- [x] S3 N16R2 profile and one C5 with a 2 MB flash budget.
- [x] Original cyborg Cerberus and animated home dashboard.
- [x] Wi-Fi Watch, Bluetooth Scout, and Radio Sense hubs.
- [x] Six Field Guide activities, quizzes, and local progress.
- [x] Return from live tools to the lesson that opened them.
- [x] Three clearly labeled offline demo views.
- [x] Atomic comparison with user-saved Wi-Fi baselines.
- [x] Aggregate baseline CSV export without network identifiers.
- [x] Explicit web start/stop and unique AP password.
- [x] Device Tools retaining Base utilities.
- [x] VS Code build and host-check tasks.
- [x] Partition, memory, size, version, source, and checksum package checks.
- [x] Host checks for rendering, learning, baselines, web lifecycle, and packaging.

## Production acceptance

- [ ] Complete [SENTINEL_ACCEPTANCE.md](docs/SENTINEL_ACCEPTANCE.md).
- [ ] Confirm flash/quad PSRAM detection and actual C5 module memory.
- [ ] Exercise radio transitions, cancellation, and C5 recovery.
- [ ] Measure live heap minima, touch response, animation, and boot time.
- [ ] Validate SD/USB ownership, exports, web lifecycle, and privacy display.
- [ ] Validate lesson persistence and reset.
- [ ] Run extended operation and interrupted-update recovery tests.
- [ ] Review wording and graphics on the actual LCD.

## Sentinel 0.3 live pages

- [x] Unified Sentinel colors, shared controls, keyboard, and one-time saved-theme migration.
- [x] Direct-to-live Wi-Fi/BLE/energy pages, four-card results, and calibrated energy bars.
- [x] One sweep per observation tool, explicit Scan Again/Stop, and visible snapshot age.
- [x] Preserve completed results and channel summaries during new sweeps.
- [x] One active UI scan session, stop acknowledgement, bounded 802.15.4 drain, and retry limits.
- [x] Freeze inspected records and resolve connections against current identities.
- [x] C5 radio exclusion and access-point BSSID pinning; failed staging cleanup.
- [x] Host tests of lifecycle, snapshots, connection races, renderer bounds, and animation parity.
- [x] PSRAM composition and comparison of completed screens before LCD transfer.
- [x] Summary/channel views reuse captured results until Scan Again.
- [ ] Verify reduced flashing and single-sweep operation on the physical panel.
- [ ] Complete live-page physical acceptance and endurance checks above.

## Sentinel 0.4 Watch

- [x] Dedicated continuous alternating passive Wi-Fi/BLE mode; other tools remain single-scan.
- [x] Raw bounded BLE advertising data with version/capability and generation checks.
- [x] Local bounded hint history, repeat suppression, category cooldown, and paced alert scenes.
- [x] Full-body cyborg Cerberus with eight distinct original animated scenes.
- [x] Explicit offline gallery isolated from live observations and alert history.
- [x] Complete source catalog and classifier verification, with distribution notices.
- [x] Validate Watch integration with host checks and build matched images.
- [x] Install matched S3/C5 images and verify production-board startup diagnostics.
- [ ] Exercise Watch transitions, animation, touch behavior, and extended operation on the board.
- [ ] Confirm known-device matches and false-positive behavior with actual radios.

## Sentinel 0.5 Watch expansion

- [x] Flipper, Meshtastic, Samsung finding tags, Eddystone, and AltBeacon signatures.
- [x] Bounded passive Drone Remote ID and Pwnagotchi Wi-Fi peer observations.
- [x] Experimental Bluetooth traffic baseline and sustained-rise indication.
- [x] Seven additional original animated Cerberus scenes; 15 gallery scenes total.
- [x] Per-category Off / History Only / Animated Alert and educational evidence explanations.
- [x] Finish expanded protocol, capture, transport, and UI verification (20 host groups).
- [x] Build and package a matched S3/C5 pair.
- [x] Install the expansion and verify board startup.
- [ ] Validate supported real devices, radio sensitivity, false positives, and extended scans on hardware.

## Watch scene refresh

- [x] Distinct Axon, AirTag / Find My, other tracker, camera, glasses, beacon, and Pwnagotchi artwork.
- [x] Rework Flipper, Samsung, and Flock with separate settings, outfits, oversized props, and floors.
- [x] Clearer tracker labels and explanations; smart glasses remain demo-only.
- [x] Verify renderer, classifier, and UI checks; build and checksum the matched package.
- [x] Install the refreshed S3 image and verify flash hashes and startup diagnostics.
- [ ] Check the revised scenes and Axon lighting on the physical LCD.

## Release hardening after board results

- [x] Add a consistent top-right battery indicator using the onboard fuel gauge, with bounded polling and unknown/low states.
- [x] Verify battery reading and startup after installing the battery-indicator update (MAX17048: 93%, 4,138 mV).
- [x] Remove color selection from device/web Settings, migrate older preferences and backups to fixed Sentinel colors, and install the verified update.
- [x] Audit Device Tools: group maintenance and optional extras, remove obsolete QR shortcuts, clarify hardware checks, and use actual firmware versions.
- [x] Verify Tools navigation/rendering, build the matched package, install the S3 update and check startup, radio, SD and battery status.
- [ ] Inspect battery visibility across pages and check discharge/charge behavior on hardware.

- [ ] Resolve acceptance failures before labeling a shipping release.
- [ ] Document manufacturing provisioning and recovery.
- [ ] Decide update strategy: current maps have one factory slot, no OTA
  rollback, and no automatic dual-processor updater.
- [ ] Record supported tools and validated hardware revisions.

## Last: SDR

- [ ] Verify internal ESP-SDR IQ access with the actual C5 silicon and pinned
  ESP-IDF, without an expansion board.
- [ ] Measure RAM, burst limits, coexistence, and recovery.
- [ ] Prototype bounded educational spectrum/IQ views based on those results.
- [ ] Keep SDR experimental until it passes production acceptance.
