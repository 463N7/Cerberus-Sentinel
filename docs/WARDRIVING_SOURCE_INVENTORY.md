# Preserved Wardriving firmware: merge inventory

This is an implementation checklist for the owner's merge into the single
Cerberus application. The current installed S3
`candidate-wardrive-spi-ble-peripheral` checkpoint (rev50, C5 firmware 46
unchanged) has C5 Wi-Fi plus S3 BLE scan paths, optional R1–R6 reads,
cross-radio dedupe, SD unlocated CSV, and Stop/USB handoff. GPS defaults OFF;
no UART or GPS task is allocated unless selected. The optional UART2 RX GPIO1
NMEA reader reports absent/no-fix/stale/fresh status, but no receiver is
attached. Unlocated rows record GPS state and the latest validated RMC UTC at
append time; they contain no coordinates. Rev49 added a PSRAM-backed expansion
queue and focused BLE scan filters. Rev50 pauses idle expansion SPI probes
during Wardriving and adds bounded C5 status, survey, Start, result and Stop
recovery. Its boot, SD mount, C5 link and 106,116-byte S3 idle internal heap
are verified in the [flash receipt](../../integration/device-test-2026-10-27/rev50-s3-flash-receipt.json).
The owner's rev49 no-GPS, R1–R6 OFF Wardriving attempt failed on a C5 SPI
timeout before a completed sweep. Rev50 then failed its post-NimBLE memory
reserve before a session started: 47,744 bytes remained against an unconditional
49,152-byte gate despite GPS being off. A smaller no-GPS reserve is source-only
pending another build and live test, so sustained Wardriving is **not** accepted
on hardware yet. BLE RF operation, GPS UART input
and expansion radios also remain live acceptance gaps. A host-tested
seven-radio region/channel/dwell planner exists as source but is not connected
to the runtime scheduler.
Region/dwell controls, WiGLE stream, saved profiles, remote and upload
workflows remain pending. The table below records the **earlier source audit**;
use this paragraph and `FEATURE_PARITY.md` for the installed state. Marauder
parity remains an active, separate requirement. The source audited here
is `../../Cerberus-Wardriving-main/Cerberus-Wardriving-main`; the preserved
baseline also exists under `../../integration/baselines/wardriving`. Source
file SHA-256 values below identify this audit's input:

| Source file | SHA-256 |
| --- | --- |
| `README.md` | `52bdbc34178777ec19b2288fa8975bf4bcdd0ee69118773d9deff0f26b883fee` |
| `s3_main/main/main.c` | `2391ce5d8179acf426658db35f64040aa41d390339aa1ae5589f6e96201b2d34` |
| `s3_main/main/wardrive_state.c` | `ed0c745aa3667bd7037562f7e593093836b5d9647ea558243f016fb1da0ac8f3` |
| `s3_main/main/wigle_logger.c` | `fbe979f35379c8d61e60cfab264d399c026b7045f32169ab5a96b53c66b2b238` |
| `s3_main/main/gps.c` | `b571b5a4a08337270cbc8e5e808cf0c0fda6d85f33a3433315e13cc605e2dff2` |
| `c5_scanner/main/main.c` | `97fe3534c240b17e39ed3767d4dd4bd7eb498c1a8dbeb8f9fffc4368ea32b972` |

All source paths in the tables are relative to that preserved root. “Partial”
means Cerberus Unified already has a related capability, not that the original
Wardriving workflow has been ported.

## Functional and resource inventory

| Area | Preserved implementation and source evidence | Unified state at this audit |
| --- | --- | --- |
| Seven-radio topology | `shared/cerberus_pins.h` defines onboard R0 and optional expansion R1–R6 chip selects. `s3_main/main/c5_spi_link.c` polls individually; `c5_scanner/main/main.c` supplies onboard/WROOM/XIAO scan profiles. Radio identity comes from the S3 slot. | **Partial.** Unified has onboard C5 scan and a separate R1–R6 field survey in `s3_main/main/field_survey.c`. It does not run the source's one seven-radio scheduler. Unplugged expansion slots must stay optional. |
| Independent scan plans | `s3_main/main/scan_settings.c` constructs per-radio 2.4/5 GHz masks for US/ETSI/JP, balanced channel allocation, Fast/Balanced/Thorough dwell and Mixed/2.4-only/5-only presets. `s3_main/main/main.c` applies changed profiles while scanning. `c5_scanner/main/main.c` uses active scans except passive DFS dwell, country code, a 512-entry observation cache, and a 128-record SPI queue. | **Missing as a continuous Wardriving plan.** Unified supports bounded onboard scans and a simple expansion survey, but the source's per-radio profiles, live controls, queue drain, and sustained scheduling need one new coordinated service. |
| Cross-radio observation state | `s3_main/main/main.c::batch_record` coalesces same record type/BSSID within a poll. `wardrive_state.c::cerberus_wardrive_state_ingest` retains 160 identities, 24 recent history slots, and ten visible rows. A row qualifies after channel change, at least 3 dBm improvement, five-minute absence, or cache replacement; `note_logged` advances the logged baseline. | **Partial.** Unified's 32-row expansion snapshot dedupes by BSSID and saves a bounded non-geotagged report. It does not have the source's sustained cross-radio session state or relogging policy. |
| Session and SD logging | `s3_main/main/main.c::open_next_wigle_session` chooses `WIGLE001.CSV` through `WIGLE999.CSV`. `wigle_logger.c` writes metadata/rows, commits by flush plus sync, and closes on Stop; `main.c` commits after 25 accepted rows or two seconds. Scan logging requires a ready SD logger, and write errors stop the session. | **Missing.** Unified has SD report storage and manual field-survey snapshot save, but no append-only Wardriving session, periodic commit, clean Stop/reopen, or WiGLE file stream. |
| Location | `gps.c` parses NMEA RMC/GGA on S3 UART2, reads PPS, and exposes fix, UTC, satellites, HDOP and altitude. `main.c::handle_scan_record` accepts a WiGLE row only with a fix updated within five seconds. `wigle_logger.c::wigle_logger_write_record` also rejects missing/fixless status. | **Missing.** No GPS is fitted. Unified needs optional GPS detection and explicit absent/no-fix/stale/fresh status; a real fresh fix alone permits geotagged rows. |
| Unlocated sessions | The preserved scanner and recent-network viewer operate before GPS lock, but `main.c::handle_scan_record` writes nothing without a fresh fix. It still opens a WiGLE file with headers on Start. | **New behavior required by owner.** Add a distinct non-geotagged session log with missing-location and time-source fields. Do not weaken the WiGLE row gate or call the unlocated file a WiGLE capture. |
| Touchscreen instrument | `s3_main/main/ui.c` has Home, Quick, Settings, individual radio/2.4/5-channel/timing views, Nets paging, Diagnostics, Uploads, Web Setup, Stop confirmation, Power Save and Remote screens. Home shows seven radio states, battery, UTC, SD/GPS, scan timer and totals. `display.c`, `touch.c`, `battery.c` back it. | **Partial.** Unified already has display, touch, battery, Wi-Fi and field-survey views, but not this integrated Wardriving instrument, seven-radio status, source's controls or recent-net viewer. Keep the current UI system and add the workflows there. |
| Saved scan profiles | `settings_store.c` stores checksummed per-radio profiles in NVS and can load/save them. `scan_settings.c` applies region and presets. | **Missing.** Reconcile NVS keys/schema with Unified settings; preserve current user data and avoid source's incompatible-NVS erase path as a migration strategy. |
| Browser setup and Wi-Fi profiles | `web_setup.c` serves a local setup AP/form and save/rescan endpoints. `upload_settings.c` validates/persists two Wi-Fi profiles, service keys and remote credentials, including older schema migrations. | **Partial.** Unified has its own web/network/settings services. Merge the actual two-profile controls and persistence into those services instead of running a second setup server. |
| Remote dashboard | `remote_mode.c` tries saved Wi-Fi profiles then a protected fallback AP; serves authenticated status, recent nets, live scan/settings/actions and idle SD list/download/upload/delete routes. It dims the LCD. | **Missing as source workflow.** Unified has web UI and SD ownership; remote controls/file management need integration with the existing radio and storage owners. Local HTTP authentication and file permissions need review before enabling. |
| Direct uploads and history | `wigle_uploader.c` sends eligible nonempty `WIGLE###.CSV` to WiGLE or WDGWARS only on user request with scanning stopped, certificate-verified HTTPS, per-service SD ledgers `/CERBUPL.DAT` and `/CERBWDG.DAT`, and 96 recent sampled fingerprints each in NVS. `upload_settings.c` stores credentials. | **Missing.** Eligibility must exclude unlocated logs. Implement controls/history after session correctness; no upload is authorized during this engineering phase. |
| USB phone compatibility | `ghost_compat.c` implements a bounded `chipinfo`, `scanap`, `list -a`, `stopscan` subset with a separate five-second in-memory result window. It does not require SD/GPS and does not control an SD session. | **Missing.** Unified's production USB console is read-only diagnostics. A future compatibility mode needs explicit ownership/isolation; this is protocol behavior only, not a reason to import GhostESP firmware. |
| Shipped visual/build resources | `assets/cerberus_boot_logo.svg`; eight `assets/ui-icons/*.svg` plus their `LICENSE`; generated `s3_main/main/ui_icons.h`; `docs/images/instrument-home.png` synthetic preview; `tools/generate_ui_icons.cjs`, `tools/preview_instrument_ui.py` and `tools/tests/instrument_ui_test.c`. Hardware profiles are in `boards/`, `platformio.ini`, `c5_scanner/platformio.ini` and `sdkconfig*.defaults`. | **Not merged wholesale.** Reuse assets only if they fit the unified interface, retain the icon notice, and migrate relevant tests/resources. Board profiles describe optional hardware, not installed radios. |

The source defines a BLE record type in `shared/cerberus_protocol.h`, but
`c5_scanner/main/main.c::spi_config_payload_valid` accepts only Wi-Fi radio
mode. This inventory therefore does not count a seven-radio BLE wardriving
scanner as an implemented source feature.

## No-GPS behavior and merge rules

| Condition | Scan and local records | Location-bearing WiGLE output | UI/API status |
| --- | --- | --- | --- |
| Receiver absent at boot | Continue R0 and available expansion scans; append unlocated session observations to SD with monotonic capture time and explicit unknown UTC/location. | No row. A header-only file must not be presented as a successful WiGLE capture. | `absent`; no prior coordinates exposed. |
| Receiver present, no fix | Continue the same scans, dedupe and unlocated logging. | No row. | `no_fix`; sentence activity may be shown, coordinates withheld. |
| Fresh genuine fix (age at most five seconds) | Continue scans and log eligible records. A previously unlocated BSSID needs its first eligible geotagged row. | WiGLE rows may be written with real location and valid timestamp provenance. | `fresh`; show fix age/quality. |
| Fix or receiver becomes stale | Continue scans and unlocated logging; do not silently reuse prior coordinates or UTC. | No new row until a fresh fix returns. | `stale` after fix expiry; `absent` if sentence activity also expires. |
| No SD card or write error | Scanning may be useful for the on-screen/USB viewer; no successful SD session may be claimed. | No row. | Clear storage error, bounded stop/retry behavior. |

The preserved `gps.c::mark_sentence` sets `present` once and does not clear
it when messages stop. Its last coordinates remain in the status structure.
`main.c::handle_scan_record` protects WiGLE writes with a five-second fix-age
check, but `remote_mode.c` serializes the raw `has_fix` and coordinates.
During the merge, derive location state from both sentence and fix age at the
point of use, and omit coordinates whenever state is not fresh.
Also, `wigle_logger.c::format_wigle_time` leaves the timestamp empty when the
GPS date or time is missing even if a fix exists. Record timestamp provenance
explicitly and validate it before treating an export as complete.

The preserved `wardrive_state.c::note_logged` has **one** logged baseline.
The source calls it only after a WiGLE write. A no-GPS path must maintain
separate unlocated and geotagged baselines (or equivalent per-output flags):
otherwise every no-fix sighting remains eligible forever, or an unlocated
write can suppress the first real-location row after a fix appears. Both paths
should retain the channel-change, 3 dBm and five-minute relogging rules and
cross-radio identity logic. Same SSID with different BSSIDs remains distinct.

An unlocated record needs a deterministic schema: radio/record type, BSSID,
raw SSID representation, channel, RSSI, security, monotonic capture time,
optional verified UTC and explicit `location_status`. It must never contain
placeholder latitude/longitude. Session Stop must flush/sync/close either
stream, and reopening must produce a new session identity. WiGLE/WDGWARS
candidate enumeration should accept only validated geotagged files; local file
management may handle both file types.

## Integration boundaries and acceptance

1. **One radio owner.** Adapt the source's R0–R6 scheduler to Unified's C5
   transport and expansion bus; it must coexist with Marauder scan, capture,
   transmit and S3 receiver operations. The two onboard chips participate in
   orchestration/observation according to their RF capabilities. Extra C5
   modules are optional, not a boot requirement.
2. **One storage owner.** Reuse Unified's SD mutex/mount and USB mass-storage
   arbitration. Do not run the source's separate mount/session/file manager
   beside it. Preserve existing files and NVS through schema migration.
3. **One UI/web/settings system.** Port the controls and resources into the
   main navigation, web service and persistent settings. Do not install a
   separate Wardriving image or a second standalone application.
4. **Acceptance without GPS.** Boot with no module; run sustained onboard and
   available expansion scans; verify same-SSID/different-BSSID treatment,
   cross-radio dedupe, 3 dBm/channel/five-minute relogs, SD commit/Stop/reopen,
   viewer/settings/remote file management, clear absent/no-fix/stale/fresh
   transitions, and zero upload-eligible rows from unlocated sessions.
5. **Acceptance with GPS later.** Feed valid and stale/invalid NMEA fixtures,
   then use a real receiver to verify fresh-fix gate, first geotagged row after
   no-fix capture, fix loss/recovery, timestamp provenance, and separately
   selected WiGLE/WDGWARS upload history. Do not perform live service uploads
   merely to test the merge.

This inventory records source behavior and current gaps. It does not establish
field acceptance, seven-radio hardware availability, network transmission,
or completed parity.
