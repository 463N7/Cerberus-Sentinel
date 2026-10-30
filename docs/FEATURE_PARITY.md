# Field kit feature ledger

Current installed checkpoint, 2026-10-30 UTC: S3
`candidate-ready-edge-portable-build` (rev54), paired with C5 firmware 49.
All five target builds passed with stable inputs, including final S3 ELF
inspection of the READY interrupt handler. Matched package checks, nine
bridge/C5/S3 write hashes, startup, SD and the 40 MHz link pass. See the
[rev54 receipt](../../integration/device-test-2026-10-27/rev54-flash-receipt.json).

The S3 records READY falling edges so a short pulse between level polls is
retained. Each request and response has a separate baseline; frame validation,
retry behavior and the legacy fallback remain unchanged. Read-only console
counters now separate mutex waits, READY waits and SPI errors. Startup records
23 exchanges, zero mutex/fall/high timeouts or driver errors, four initial
READY timeouts and four fallback uses. These are the startup baseline for
the following Wardriving check.
An idle follow-up at 127 seconds of C5 uptime records 523 additional
exchanges with no additional READY timeouts, fallback uses, mutex timeouts
or driver errors. Wardriving had not started at that observation.

The subsequent owner run starts and stops normally. Console confirms STOPPED
after 22.592 seconds, four C5 Wi-Fi sweeps, two S3 BLE sweeps, 72 unlocated SD
rows and one saved file. All service errors and scan-status timeouts are zero;
the BLE host is released. At 1,397 SPI exchanges the initial READY/fallback
counts remain four and all other timeout/error counts remain zero. Maximum
mutex wait is 25 microseconds; cumulative hold/exchange maxima still include
startup. Post-Stop S3 internal free heap is 45,116 bytes, minimum 37,656.
This accepts a short onboard no-GPS run; longer/repeated rev54 sessions, actual
CSV contents and real GPS/backboard remain unverified. The rev52 253.9-second
run has a different duration, so no sustained improvement factor is claimed.

Idle S3 internal free heap is 104,056 bytes (minimum 103,376), a 1,036-byte
cost for this change. C5 idle free heap remains 77,172 bytes. Shared C5 build
configuration now uses portable pinned packages; ignored local overrides
retain the verified Windows compiler. Receipts include actual compiler and
package identities. Linux compilation has not been executed locally.

Rev53's own-AirTag BEEP and Packet Monitor-to-scan checks remain prior-version
evidence. The owner reports that START fails on Quiet Time and other Wi-Fi
attack screens; those reports remain unresolved and no repair is claimed.
Full Marauder parity, physical GPS/backboard and saved-file content acceptance
remain open.

Previous installed checkpoint, 2026-10-29: S3
`candidate-live-findmy-shared-worker` (rev53), paired with C5 firmware 49.
All five target builds passed with stable inputs. Frozen image hashes, nine
bridge/C5/S3 write hashes, startup, SD and 40 MHz SPI checks pass. See the
[rev53 receipt](../../integration/device-test-2026-10-27/rev53-flash-receipt.json).
Idle S3 internal free heap is 105,092 bytes; C5 idle free heap is 77,172 bytes,
up 4,440 bytes from rev52 after removing the dedicated packet-monitor task.
The exclusive scan worker now runs Packet Monitor. A focused device test
confirms channel 8 activity, generation 1 and sample 92 at 18.5 seconds, with
no error. After Back the monitor is idle at sample 211 after 42.4 seconds.
Subsequent scans raise the successful-scan count from one to four without a
reset or failed scan. This accepts the shared-worker monitor-to-scan path;
packet-rate accuracy, other channels and prolonged load remain unverified.

The owner subsequently reports errors from Quiet Time and other Wi-Fi
actions. Exact on-screen text is pending. Console records no started transmit
job, an idle monitor, healthy link and successful scans; this does not yet
diagnose the UI error. That report remains unresolved.

The normal BLE live list now exposes focused filters through its count/header
and initializes selected Find My evidence from a frozen completed sweep.
Original advertisement age is preserved when selecting a row. A continuous
Find My Monitor uses bounded PSRAM history, shows age and per-sweep peak RSSI,
and releases scanning on Stop or Back. Production-route, scanner and monitor
host checks pass. The owner confirms selecting their AirTag from the normal
Find My list exposes BEEP and audibly sounds the tag. Console records one
accepted AirTag-profile write, no error and BLE host owner zero afterward.
Continuous Monitor update/Back acceptance remains pending; the owner's
earlier audible AirTag reports are also retained.
Matched scan-response hints, mixed profiles, normalized UUIDs and discovery
across supported GATT profiles remain gaps documented in
[Find My profile parity](FINDMY_PROFILE_PARITY.md).

Rev52's two successful no-GPS Wardriving sessions remain prior-version
acceptance. Physical GPS, the six-C5 backboard and saved CSV content are still
unverified. The full Marauder port remains partial.

Previous installed checkpoint, 2026-10-29: S3 `candidate-scan-hints-gps-log`
(rev52), paired with C5 firmware 48. All five target builds, frozen package
hashes, nine bridge/C5/S3 write hashes, startup, SD and 40 MHz SPI checks pass.
Idle S3 internal free heap is 105,428 bytes. See the
[rev52 receipt](../../integration/device-test-2026-10-27/rev52-flash-receipt.json).

Installed additions are Meta identifier and exact HC-03/05/06 name hint
filters, selected-AP receive-only SAE Commit capture, and GPS-aware WiGLE
logging with an unlocated fallback stream. SAE capture is fixed-channel;
upstream hopping and source/destination rows remain pending. Hints do not
prove device identity. GPS routing, file rotation, SD ownership and injected
partial-write retention pass host checks; a physical GPS receiver remains
absent. The log metadata explicitly describes GPS at result receipt, since
radio records lack synchronized RF timestamps.

The no-GPS startup reserve is now 43,008 bytes, and optional GPS uses a
1 KiB UART RX ring and 45,568-byte reserve. The owner confirms two no-GPS
runs finished STOPPED without resetting. The final session's console records
253.9 seconds, 52 C5 sweeps, 31 S3 sweeps, 111 unlocated rows and one saved
file, with all service errors zero. It recovered 79 status-lock timeouts.
After Stop, BLE host owner is zero and internal free heap is 46,620 bytes
(minimum 39,160). Counters describe the final session only. This closes the
observed restart regression; saved CSV contents and physical GPS still need
acceptance. Continuous Find My Monitor and C5 worker
consolidation are subsequent source work, not part of this installed package.

Route review found that the normal BLE list dispatches through
`sentinel_live_ui.h`, bypassing legacy focused-filter controls and selected
Find My raw-evidence setup. Correcting the live route is in progress. The
owner's audible AirTag report is retained, but acceptance of the exact
selected-tag route and focused filter controls requires the corrected build.

Previous installed checkpoint, 2026-10-29: S3 `candidate-findmy-monitor-wardrive`
(rev51), paired with onboard C5 firmware 47. All five target builds passed
with stable inputs; the matched package and nine bridge/C5/S3 write hashes
were verified. Boot confirms C5 online at 40 MHz SPI, SD mounted, no new reset
fault, and 104,788 bytes of free internal S3 heap before BLE starts. The
[rev51 receipt](../../integration/device-test-2026-10-27/rev51-flash-receipt.json)
records the frozen images and device evidence.

The owner completed a no-GPS, R1–R6 OFF Wardriving session: RUNNING then
STOPPED after 57.4 seconds, 11 C5 Wi-Fi sweeps, 7 S3 BLE sweeps, 83 SD rows,
one saved file, and all service error fields zero. The session recovered 23
C5 status timeouts; these remain an optimization item. Measured running S3
internal heap was 39,256 bytes (minimum 38,520), and the BLE host was released
after Stop. This verifies the onboard no-GPS workflow, not six-node or GPS
operation.

Rev51 also installs selected Find My BEEP and continuous Beep All, active BLE
scan-response enrichment, exact-address Fox Hunt, BLE Analyzer, and C5 Wi-Fi
Packet Monitor. Selected BEEP uses the original advertisement timestamp;
tapping an old result cannot renew its eligibility. Beep All cycles fresh,
observed compatible addresses with bounded storage and Stop cleanup. GATT
write acceptance, tag responses, and audible sound are distinct evidence.
The owner confirmed Packet Monitor activity and normal exit, Fox Hunt signal
updates and Back, and BLE Analyzer graph updates and Back. The owner's AirTag
audibly beeped through selected BEEP and Beep All; Stop completed and console
confirmed host owner zero. Other FMNA/DULT peers, multiple tags and prolonged
cycling remain unverified. Expansion channel allocation is
activated for confirmed R1–R6 modules, with an explicitly unverified legacy
module path. Onboard R0 still scans its full plan, so complete seven-radio
load balancing remains pending. GPS and the backboard are not connected.

Known rev51 restart issue: after the successful no-GPS session, internal free
heap was 45,980 bytes, below the current 46,080-byte startup reserve. The first
session passed; repeated Start needs the source reserve correction and a new
device test. This must not be treated as sustained Wardriving acceptance.

Previous installed checkpoint, 2026-10-29: S3
`candidate-wardrive-spi-ble-peripheral` (rev50) with unchanged onboard C5
firmware 46. All five target builds, host checks, matched receipt and S3 write
hashes passed. Boot shows an online C5, mounted SD, buffered display and no
reset fault. Measured idle S3 internal free heap is 106,116 bytes, above the
98,304-byte BLE first-use preflight threshold. Wardriving now pauses idle
R1–R6 SPI presence probes while scanning with the onboard C5 and has bounded
status, survey, Start, result and Stop recovery. The owner's no-GPS retest
reported `ESP_ERR_NO_MEM` before a session started. Diagnostics show the S3 BLE
host initialized and 47,744 bytes free, just below an unconditional 49,152-byte
post-init reserve intended to cover a GPS UART/task that was off. A conditional
no-GPS reserve is now source-only and needs another build and live run. The
[rev50 receipt](../../integration/device-test-2026-10-27/rev50-s3-flash-receipt.json)
records the installed images, boot and failure. S3 NimBLE peripheral role
is enabled for Apple Juice and BLE Spam All. Apple Juice reached COMPLETE on
the device with 180 accepted and 0 rejected controller bursts and a 40 KiB
minimum heap; external RF reception and connectable peer behavior remain
unverified. BLE Spam All continued until Stop, then completed with 239 accepted
and 0 rejected controller bursts and released the S3 BLE host. External RF
reception remains unverified for both modes.

Previous installed checkpoint: S3 `candidate-wardrive-nogps-ble-focus`
(rev49). Its no-GPS, R1–R6 OFF session entered ERROR after 944 ms with
`ESP_ERR_TIMEOUT` (263) in the C5 Wi-Fi path, before a completed sweep. S3 BLE,
SD and GPS reported no error. The [rev49 receipt](../../integration/device-test-2026-10-27/rev49-s3-flash-receipt.json)
records that failure. GPS is OFF by default and allocates no UART or task in
that mode.

Rev49 added a 256-record PSRAM expansion queue and focused BLE scan
filters for All, iBeacon, Find My, Flipper and Flock. These routes, BLE
advertising, GATT, concurrent load and sustained Wardriving still need live
device acceptance. No GPS receiver is connected. The seven-radio
region/channel/dwell planner is host-tested source only and is not wired into
the live scheduler.

Previous installed checkpoint: S3 `candidate-wardrive-gps-memory` (rev48)
with unchanged onboard C5 firmware 46. The [rev48 receipt](../../integration/device-test-2026-10-27/rev48-s3-flash-receipt.json)
records its boot, SD, C5 link and 106,900-byte idle internal heap evidence.

The same main-firmware Wardriving service now includes an optional UART2 NMEA
reader (RX GPIO1) and absent/no-fix/stale/fresh GPS status. It continues to
write **unlocated** SD CSV, even with a fresh fix; it records location state
and validated RMC UTC where available, but no coordinates or WiGLE rows. No
GPS receiver is connected, and UART input has not been accepted on hardware.
Expansion-radio and simultaneous Wi-Fi/BLE operation also remain unverified.

Previous installed checkpoint, 2026-10-29: `candidate-wardrive-ble-central` on
the S3 with the unchanged C5 revision 46. Wardriving is now a continuous mode
inside the main Scan menu: the onboard C5 surveys Wi-Fi while the S3 scans BLE,
optional R1–R6 expansion radios contribute when present, and an SD-backed
unlocated CSV session works without GPS. Stop drains completed observations and
closes the file before USB SD handoff. The S3 also owns BLE central/GATT
discovery and six newly actionable BLE advertising test routes: Sour Apple,
Swift Pair, Samsung, Google Fast Pair, Flipper and selected-device Spoof AirTag.
Apple Juice, Spam All and Find My Sound remain disabled with explicit backend
requirements. The GPS NMEA parser exists in source, but no UART GPS service or
geotagged WiGLE stream is in this installed image.

All five target builds and host checks passed. The S3 bootloader, partition and
app were hash-verified on the identified device; the C5 images match the
previously installed revision 46, so the C5 was not reflashed. Boot diagnostics
show an online C5, mounted SD and no new reset fault. S3 idle internal free heap
is about 94 KB, down from about 112 KB in the prior build. The
[rev47 receipt](../../integration/device-test-2026-10-27/rev47-s3-flash-receipt.json)
records image hashes and boot evidence. **BLE RF behavior, GATT interactions,
continuous Wardriving, expansion radios and concurrent-load memory have not
yet passed live hardware acceptance.** This is a partial merge; the preserved
Wardriving controls, GPS location stream, profiles, uploader, remote functions
and substantial Marauder Wi-Fi/BLE coverage remain on the inventory.

Previous installed checkpoint, 2026-10-29: `candidate-recon-parallel-channel-assets`,
C5 revision 46. The C5 owns Wi-Fi AP discovery and passive 100 ms channel
packet surveys while S3 owns BLE passive observation; the Recon schedulers are
independent. The BLE device list, web inventory and Watch observations use the
S3 scanner. Public BLE address and Watch details can read SD OUI vendor labels.
All five build targets, frozen image hashes, paired flash hashes, boot, 40 MHz
SPI link, idle heap and SD mount passed. The [rev46 install receipt](../../integration/device-test-2026-10-27/rev46-flash-receipt.json)
records those checks. **Simultaneous live scan, BLE post-init heap, channel
survey RF counts and AP takeover remain unverified on hardware.** BLE GATT
client operations still use C5; all Bluetooth attack menu routes remain
disabled until their S3 engines and RF tests are complete.

Previous installed checkpoint, 2026-10-29: `candidate-infinity-capture-portal`,
C5 revision 45. Selected-AP transmit jobs run until manual Stop and show
measured C5 accepted-frame and S3 observed-frame rates where reception is
available. SAE Commit Flood, selected-AP portal service and fixed-channel
PCAPNG capture have source and host checks. All five build targets, frozen
image hashes, paired install hashes, boot, SPI link and SD mount passed. The
replacement SD card's `RECON.DB` SHA-256 and a vendor lookup passed on-device.
Physical RF acceptance of the new engines, portal HTTP flow and capture file
quality are **pending**. See the [rev45 install receipt](../../integration/device-test-2026-10-27/rev45-flash-receipt.json).

The supplied CYM OUI asset pack has the same 40,202 MA-L assignments already
present in Cerberus's indexed SD database; see [asset audit](CYM_SD_ASSETS.md).

Previous installed checkpoint, 2026-10-29: `candidate-attack-rate-fix`, C5
revision 43. The C5 AP worker no longer calls ESP-IDF's init-only TX-rate API
after Wi-Fi starts. Five build targets, paired flash hashes and boot, SPI, SD,
idle state and host checks pass. On the owner's selected channel-10 test AP,
Probe Request completed with 601 C5 driver-accepted frames and 601 S3 receiver
matches; Deauth accepted 29 frames with 13 S3 matches before the owner stopped
it; Channel Switch completed with 600 accepted frames and 600 S3 matches.
AP Clone Spam and Quiet Time each completed with 600 C5 accepted frames and
600 S3 matches.
These counts verify the tested transmit and receive paths, not client effects
or every attack variant. AP selection and scan-card labels are clearer. See the
[paired install and live-job receipt](../../integration/device-test-2026-10-27/attack-fix-verification.json)
and the requested [Marauder `develop` baseline](MARAUDER_DEVELOP_BASELINE.md).
Many Wi-Fi/BLE modes remain partial or pending; the full GPS-optional
Wardriving merge is still to be built in this main application.

Previous installed checkpoint, 2026-10-29: `candidate-multi-station`, C5
revision 42. Each selected AP now has independent Broadcast, All Observed or
Picked Client scope where the mode supports it. Deauthentication accepts all
three; Bad Message and Association Sleep require observed clients. A C5 job
accepts 2–32 frozen clients at 20 frames/second total, after C5 validates each
against per-AP evidence. S3 handles selection, all-target preflight, 2.4 GHz
reception, cancellation and bounded SD reports; C5 handles 2.4/5 GHz RF. Up to
32 APs × 32 observed clients can be retained lazily for one hour. All five
builds, flash hashes, paired boot, SPI, SD, idle state and host checks pass.
Physical touch and live RF acceptance remain open, as do the three remaining
selected-AP engines and broad Marauder parity. The complete Wardriving merge
follows Marauder work in this same application and must work with no GPS; see
[its source inventory](WARDRIVING_SOURCE_INVENTORY.md) and
[the revision 42 evidence](MARAUDER_PORT_WORKLOG.md).
The [95-mode Marauder ledger](MARAUDER_COVERAGE_LEDGER.md) is a source-pinned
inventory updated through the current installed checkpoint; statuses do not
claim complete upstream parity.

Previous installed checkpoint, 2026-10-29: `candidate-multi-ap`, C5 revision 41.
The AP picker supports up to 32 selected networks with Clear and explicit
Attacks navigation. All six implemented modes support sequential multi-AP
jobs, per-AP station choices, all-target preflight, coordinated S3/C5 cleanup
and per-target CSV. C5 retains a bounded, expiring cross-AP station cache.
Native checks, all five builds, flash hashes, paired boot, SPI and SD pass.
Live RF and physical touch acceptance, multiple/all stations per AP, the three
remaining AP engines and substantial other Marauder features remain pending.
The full Wardriving merge follows Marauder work, stays in this main application
and must operate without a GPS module; see `../TODO.md` for its acceptance scope.
See [revision 41 evidence](MARAUDER_PORT_WORKLOG.md).

Previous installed checkpoint, 2026-10-29: `candidate-ap-four-final`, C5 revision
40. **Bad Message, Association Sleep, Channel Switch and Quiet Time** now join
Deauthentication and AP-directed Probe Requests as implemented bounded engines.
All six have C5 work plus S3 control/reporting and independent 2.4 GHz reception.
Bad Message/Association Sleep require one observed station; Channel Switch/Quiet
read the selected AP's beacon first. Native checks, all five builds, paired
flash hashes, boot, SPI and SD pass. S3 idle internal free remains 127,428 bytes;
C5 idle free is 83,612 bytes. Live RF and physical touch acceptance remain open.
SAE Commit Flood, AP Clone Spam, Evil Portal/AP Clone, multi-AP/multi-station
selection and substantial other Marauder parity work remain outstanding. The
Banshee research used architecture documentation only; no GhostESP source was
imported. See [the worklog](MARAUDER_PORT_WORKLOG.md) and
[processor roles/reference decisions](PROCESSOR_ALLOCATION.md).

The checkpoints below describe earlier source/installed states.

Current source scope: the owner expanded the goal to every Marauder feature on
2026-10-28. The old 13 Wi-Fi / 7 BLE catalog below is a historical checkpoint,
not the complete upstream inventory. See [the source-pinned inventory](MARAUDER_SOURCE_INVENTORY.json),
[AP menu correction](MARAUDER_AP_MENU.md), [processor allocation](PROCESSOR_ALLOCATION.md)
and [current port work](MARAUDER_PORT_WORKLOG.md). Unreviewed source entries remain
pending; no menu-only entry counts as implemented.

Latest installed checkpoint, 2026-10-29: `candidate-ap-probe-final` pairs S3
picker/receiver changes with C5 revision 39. **Probe Request Flood** now opens
bounded AP-directed probe controls using the selected BSSID/SSID, random local
source MACs, timed C5 worker and 2.4 GHz S3 receiver. This is a partial port;
upstream broadcast/multi-AP behavior remains pending. Seven other AP attack
engines remain unimplemented. Native tests, all five builds, identity-checked
paired flashing, boot, SPI and SD pass; live RF and physical touch checks remain
pending. The checkpoints below describe earlier installed versions.

2026-10-29 picker/receiver checkpoint: S3 `candidate-picker-s3-rx` is installed
with C5 revision 38 unchanged. Network selection retains its completed snapshot,
shows full SSIDs and distinct BSSIDs, and marks the selected AP. AP job startup
checks the selected index first. The S3 now receives independently for 2.4 GHz
deauth jobs, gates C5 start on receiver readiness and contributes separate
beacon/frame counters and reports. Host checks, all five builds, hash-verified
S3 flash, boot, SPI and SD pass. Physical RX/touch acceptance, multi-AP scheduling
and the other eight AP attack engines remain pending.

Revision-38 source update, 2026-10-29: selected-AP deauthentication now has a
bounded C5 backend and S3 configuration/reporting flow, with broadcast or one
observed station, explicit start/stop and control lease. Native checks and the
final linked adapter/vendor fallback pass. The revision-38 pair is installed;
boot, SPI, SD and idle state are verified. RF acceptance remains pending in
the worklog; all-station, multi-AP and other attack engines remain
pending. Historical statements below about all attacks being unavailable
describe their earlier checkpoints.

Updated 2026-10-28. This is the implementation checklist for the requested
Marauder scan/active-test feature set and SquachWatch-style Recon workflow.
An item is complete only when its service, UI, storage behavior, error paths,
and applicable hardware checks work. Menu labels and archived source files do
not establish feature parity. Initial paired S3/onboard-C5 flashing and boot have
passed; the owner confirmed display and touch. Full hardware acceptance remains
open. The corrected USB SD path passed a physical copy/eject/return cycle
without restart, including native USB restoration and firmware database
hash/lookup verification, followed by a second entry/return without reboot.
Sustained-cycle stress and full RF acceptance remain open.

## Recon interface

The dashboard's **Recon** button opens the existing passive Sentinel scheduler
with a new pixel-art three-headed Cerberus. The native 320×480 view has a dark
background, restrained cyan/violet accents, slow animation, stable text regions,
explicit radio/paused/offline states, and no strobe. The live artwork is original
code-native pixel art; the separate scene gallery retains the imported artwork.

**Pause → Log → Evidence → Actions → Signal tracking / Ignore / Snooze** and
**Options → Alert filter / Mute / Type filter** are working application routes.
Confidence, mute, ignore, and snooze govern alerts while retaining observations.
Turning a category off excludes its matches from history. Medium+ is the default
alert threshold. Signal tracking shows the last received RSSI and sample age;
it updates after completed sweeps, becomes stale after 30 seconds or radio loss,
and makes no distance claim. Tracking uses address + address type + radio.

There are 32 history entries and at most 16 ignore/snooze policies. A full policy
list reports an error instead of replacing a still-ignored device. Clearing the
session log and resetting policies each require a second on-device tap. The
log, filters and policies currently last only for the power session.

Comparison source: [SquachWatch README](https://github.com/skizzophrenic/SquachWatch-CYD/blob/3e9be6c02265b14c798e58da01a0913866143b0f/README.md).
Its source was inspected as a workflow reference; no SquachWatch UI code or
mascot artwork was copied into the firmware.

| SquachWatch control or capability | Cerberus implementation |
| --- | --- |
| Scan screen, findings, device evidence | Implemented using Sentinel's bounded passive rules and new pixel Recon view |
| Minimum confidence and category filters | Implemented; category modes also support history-only |
| Ignore / un-ignore | Implemented for exact broadcast identity; session-only |
| Snooze | Implemented: 10 min, 30 min, 1 hour, 4 hours; immediate selection |
| Hunt signal gauge | First version implemented as Signal tracking; no distance estimate or continuous directional tracking |
| Watch list and locked-on behavior | Pending |
| Adaptive auto-snooze and fake-tag flood grouping | Pending; existing per-category cooldown and bounded queues retained |
| Persistent detection log and automatic SD CSV | Pending; expansion Field survey can save a separate bounded snapshot |
| Ignore-list management | Reset-all exists; individual list view and persistence pending |
| Device type catalog parity | Pending; the SD database stores source hints but does not execute them as rules |
| Desk, appearance, squad/mesh, diary and extras | Separate backlog; not implemented by this Recon change |

See [WATCH_SIGNATURES.md](WATCH_SIGNATURES.md) for what actually matches today.
In particular, a vendor OUI is not a device model or maliciousness verdict.
Meta glasses, skimmer heuristics, broad camera/Ring OUIs, and other SquachWatch
families cannot be reported as live detection parity simply because their
reference data is on SD. The current classifier is intentionally more specific
than some of the reference project's broad matching rules.

## Marauder scan capabilities

Sources reviewed on 2026-10-27: [Wi-Fi sniffers](https://github.com/justcallmekoko/ESP32Marauder/wiki/wifi-sniffers),
[network scanners](https://github.com/justcallmekoko/ESP32Marauder/wiki/wifi-scanners),
and [Bluetooth sniffers](https://github.com/justcallmekoko/ESP32Marauder/wiki/Bluetooth-Sniffers).
These are menu-level requirements, not an API or binary compatibility claim.

| Requested capability | Current status / implementation work |
| --- | --- |
| AP scanning | Sentinel onboard dual-band discovery; first R1–R6 survey added |
| Station scanning; combined AP/station view | Targeted passive discovery with AP snapshot binding, direction/RSSI provenance, 32 rows, selection and CSV; native checks and revision-37 installation verified; combined multi-AP inventory and live reception acceptance pending |
| Probe, beacon and raw capture | Existing selected broadcast parsing is narrower; generic streaming PCAP pending |
| Deauth observation | Existing defensive tools; continuous capture parity pending |
| EAPOL/PMKID and SAE commit capture | Pending receive-only record formats, capture pipeline and fixtures |
| Channel analyzer; packet count/monitor | Existing bounded survey statistics; raw per-channel telemetry parity pending |
| Signal monitor / Fox Hunt | First exact-identity Recon RSSI view; focused high-rate acquisition and trends pending |
| Pwnagotchi detection | Existing validated broadcast parser; hardware verification pending |
| Remote ID | Existing BLE/Wi-Fi decoder; not all focused tracking workflows or message fields |
| CSI motion monitor | Pending hardware/driver feasibility and calibrated observation service |
| Ping and ARP discovery | Implemented connected /24–/31 sweeps: correlated ICMP echo replies, ARP MAC resolution with explicit cached-entry labels, UI/CSV and lifecycle tests; live device scan acceptance pending |
| TCP port scan | Implemented: 16 common ports or a custom 1–65535 range on a selected host in the connected subnet; common-port hardware run passed (16 checked, 3 open); custom-range hardware acceptance pending |
| SSH and Telnet discovery | Implemented: port 22/23 sweeps over connected /24–/31 networks, excluding this device; SSH identification prefix checked on ports 22/2222; TCP-open alone is not protocol verification; device acceptance pending |
| SMTP, DNS, HTTP, HTTPS and RDP discovery | Implemented TCP service-port sweeps (25/53/80/443/3389), scope-aware UI/CSV and capability gating; live device scan acceptance pending; no application authentication or protocol verification implied |
| Select a discovered network and open its tools | Existing AP selection/join now leads to the new port/SSH/Telnet screen; selected-network lifecycle, cancellation and SD CSV connected; additional target types/test tools still pending |
| BLE analyzer/sniffer | Existing passive inventory, details and selected GATT reads; capture parity pending |
| BLE wardriving | Pending unified SD sessions and later GPS |
| Flipper, AirTag/Find My, Flock, Pwnagotchi | Existing bounded subsets; do not assume identical catalog coverage |
| iBeacon, card-skimmer, Meta rules | Not installed by this change; typed evidence rules and fixtures pending |
| Durable multi-radio Recon sessions | Pending merged R0–R6 records, channel scheduling and continuous SD writer |

## Marauder active-test inventory

Source menus: [Wi-Fi attacks](https://github.com/justcallmekoko/ESP32Marauder/wiki/wifi-attacks)
and [Bluetooth attacks](https://github.com/justcallmekoko/ESP32Marauder/wiki/Bluetooth-Attacks).
This inventory records the owner's parity request. **None of the following
active-transmission features was implemented or exercised in this checkpoint.**
Each requires separate firmware/driver feasibility, supported target handling,
bounded job lifecycle and controlled equipment testing. They are not represented
by working buttons in Recon. Passive Recon has no transmit action.

The owner's requirement is the complete Wi-Fi and Bluetooth feature inventory
within the existing Cerberus firmware, not a second bootable application. Track
each source feature separately through implementation and hardware acceptance;
do not close this requirement after adding only a subset of scans or menu items.

| Source feature group | Items to assess individually |
| --- | --- |
| Wi-Fi advertisements and probes | Beacon list/random, Rick Roll beacon, probe request flood, AP clone |
| Wi-Fi management-frame tests | Deauthentication, association sleep, bad-message, SAE commit, channel switch, quiet time |
| AP/portal workflows | Evil Portal, Karma |
| BLE advertisements | Sour Apple, Swift Pair, Samsung, Flipper, combined spam modes |
| Finding-network tools | AirTag spoofing, Find My Sound |

Hardware-specific extras such as sub-GHz, NFC or nRF24 require their own radios.
S3/C5 combinations do not add unsupported RF hardware through firmware alone.

## SD and release acceptance

The current SD pack contains 40,210 /24 index entries in one 6,333,004-byte file.
Its vendor lookup is live in expansion Field survey and AP details; source hints and archived
BLE reference material still require a typed runtime rule format. Keeping
large references, assets and sessions on SD remains an architectural requirement.
The small pixel fallback stays in firmware so removing SD does not remove Recon.

Next acceptance gates: matched recovery images and confirmed module profiles;
shared-SPI timing and stop behavior; sustained scans with heap/drop measurements;
continuous SD logging including removal/full/USB ownership; persistent filters;
then incremental detector and capability parity against saved fixtures.

## Connected-network scan implementation

C5 firmware revision 32 advertises an optional network-scan capability through
protocol V2 commands 58–61. Existing revision-31 services and wire messages remain
compatible; an older C5 leaves the new UI action disabled. The C5 owns one
nonblocking TCP socket, a 5,120-byte worker stack, and at most 64 retained open
results. Status retains totals beyond that result cap, and UI/CSV disclose
truncation. Each probe uses a 500 ms UI-default timeout with <=100 ms cancellation
checks, and a 20 ms inter-probe delay. It stops on Wi-Fi session changes, leaving
the page, sleep, or five seconds without an S3 status lease. Starting a job is
explicit; no network probes run at boot.

The S3 manages address/range entry, progress, result paging and SD report export.
IPv4 target scope is validated on both processors. Unsupported larger LAN sweeps
are rejected rather than silently truncated. Custom host ranges can cover all
65,535 TCP ports. Only the first 10 banner bytes are retained, and only a received
`SSH-` prefix earns the SSH label. No SSH/Telnet login or session implementation
is included. Exports record requested scope, job state, error, tested/total,
open/closed/no-reply counts, retention count, duration and hexadecimal banner
prefixes. Additional modes and outcomes still need hardware acceptance.

One owner-initiated common-port run now passed on the physical device:
job 1 completed 16/16 probes, with 3 open, 0 refused and 13 no-reply/unreachable
outcomes. C5 status reported ESP_OK and zero job error. The owner confirmed
SAVE TO SD succeeded, but a second USB SD inspection found no network CSV on
the card. Export acceptance remains open; the exact on-device save message
has been requested. No write failure or successful export is assumed yet.
This does not validate SSH banners, Telnet sweeps, cancellation under load, or
all custom ranges. C5 minimum free heap reached 10 KiB over discovery/join/scan,
recovering to 70 KiB after the job. Profile those phases separately before
adding concurrent services; SD is data storage, not a substitute for C5 RAM.
Evidence: `../../integration/device-test-2026-10-27/network-live-acceptance.txt`.

## Scan and Attack navigation

The home screen now has Scan and Attack launchers while preserving direct
Wi-Fi, BLE, radio, Recon and Tools access. The field menu has Scan, Attack and
Recon tabs. Scan routes to Wi-Fi, Bluetooth, 802.15.4, expansion survey,
connected-network tools and the original field guide. No scan starts merely
by opening this menu. Back from a radio tool returns to its launcher.

Attack lists all 13 Wi-Fi and 7 Bluetooth entries from the upstream menu pages
reviewed above, with Wi-Fi/Bluetooth selection and pagination. Every entry is
marked **NOT AVAILABLE IN THIS BUILD** and is inert. This is navigation and
inventory work, not attack-engine implementation or feature completion.
`tools/render_field_menu.py` renders actual home/menu C code and checks bounds,
touch gaps, scan actions, page limits, inert entries and animation preservation.

C5 broadcaster and peripheral roles are currently disabled in sdkconfig. The
pinned IDF header documents raw TX support for beacon/probe/action/non-QoS data
frames; it does not establish support for every requested management-frame
test. Driver feasibility and memory profiles are required per engine. Relevant
reference: [Espressif C5 raw transmit API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c5/api-reference/network/esp_wifi.html#_CPPv417esp_wifi_80211_tx16wifi_interface_tPKvib).

The frozen `candidate-tabs` S3 image is 1,284,112 bytes with 177,344 bytes static
RAM: +2,432 image bytes and no reported static-RAM increase over the installed
network candidate. All five targets pass the stable-input build matrix. It
was not flashed independently. The second USB SD return now passed without
reset and restored native USB; navigation is included in the paired Radio Lab
candidate described below.

Initial memory review found three resident 4,096-byte 802.15.4 worker stacks,
two resident 4,096-byte Wi-Fi worker stacks, a 4,096-byte event worker, and the
4,096-byte SPI worker, in addition to NimBLE/IDF tasks. The network scan creates
its own 5,120-byte worker only for a job. These are configured allocations,
not measured stack usage. A shared exclusive radio worker could reduce idle
stack allocation, but needs lifecycle/stop tests before changing those services.
The next profile must capture free heap, largest block and per-task stack
low-water marks separately at boot, AP discovery, join, scan and disconnect.

`tools/check_network.py` checks full port-range endpoints, malformed inputs,
network/broadcast/self/out-of-subnet exclusions and /24–/31 enumeration.
`tools/render_network.py` exercises the actual private UI code with simulated
SPI/SD, including missing STOP delivery without renewing the lease, button gaps,
CSV contents, all modes and screen bounds. Its PNG is a fixture, not a device
screenshot or actual scan result.

## Radio Lab implementation checkpoint

The Attack tab now links to Radio Lab. It implements two ordinary discovery
advertisement tests: a C5 Wi-Fi beacon using the C5's own MAC on channels
1/6/11/36/40/44/48, and an S3 nonconnectable BLE advertiser. The UI owns one
explicitly started test at a time. This establishes actual radio workers and
processor allocation, not Marauder attack parity: all 20 upstream catalog
entries remain unavailable. No deauthentication, copied-identity flood,
credential collection, malformed advertisement or crash payload is implemented
by these two engines.

C5 revision 33 adds capability bit 20 and V2 commands 62–66. A staged profile is
committed explicitly; retransmitted START commands do not restart the same job.
The C5 restores its previous Wi-Fi band/channel after a job. Existing C5 scanning
and connection services reject starts while its test worker owns the radio.
The S3 lazily initializes its own NimBLE broadcaster on first START, retains the
initialized host for reuse, and stops advertisements after the test. Each
processor has an on-demand 4,096-byte job stack. SD cannot substitute for those
live stacks or radio buffers.

The UI offers 10/20/30 seconds, a 100 ms advertisement interval, STOP and SD CSV
reports. Leaving the screen or sleeping requests stop; a three-second control
lease also bounds a lost controller. Diagnostic status reads do not renew the
lease. Failed BLE stop calls retain ownership and retry until inactive. Lost
C5 start acknowledgements are resolved from status, and a staged but unstarted
job is reported as an error rather than successful completion. C5 counts are
driver-accepted packets, not receiver-confirmed delivery; BLE packet counts
are not claimed.

The SD pack adds `/CERBERUS/TESTS/WIFI.TXT`, `BLE.TXT` and `HELP/RADIO.TXT`.
Profile files contain one printable name, limited to 32 bytes for Wi-Fi or 26
for BLE. Missing SD uses small fallback names; malformed profiles fail clearly.
`RECON.DB` remains the same combined 6,333,004-byte database. The nine-file pack
is prepared locally; physical deployment is tracked separately in the device
receipt. Reports use `DIAGnnnn.CSV`; network reports use `WIFInnnn.CSV`.

Host checks exercise the actual workers with mocked IDF services: heap guards,
staging, busy rejection, idempotency, lease expiry, explicit cancellation,
transmit/init errors, BLE stop retry, channel restoration and packet bounds.
The actual UI renderer additionally checks touch gaps, profile validation,
lost responses, hidden-screen stop, report contents and persistent errors.
These are not RF acceptance. The matched pair is now flashed and verified;
C5 revision 33 is online and both workers boot idle. S3 free heap at startup
is 129,152 bytes; C5 free heap is 73 KiB. First transmitter runs and receiver
checks remain pending. Exact images and observations are in the device receipt.

Network-save errors now persist across status refresh, and the CSV buffer is
bounded to the storage API's 4 KiB limit. Read-only USB `reports` inspects saved
network CSVs; `radio` inspects both workers without starting or extending jobs.
The original missing network CSV still needs a fresh physical reproduction.

## Selecting a Wi-Fi target

Selecting a scan row now opens a compact target screen: Attack Options,
Connect/Connection, Port/SSH/Telnet Scan, Details/Trust/Vendor, and Change Network.
The selected SSID, BSSID, channel, security and RSSI remain visible. Attack's
Wi-Fi page has a target selector above its catalog, and retains that chosen
network when returning to the action menu. Bluetooth has a direct device-list
shortcut. Catalog pagination is now three entries per page (five Wi-Fi pages,
three BLE pages); the same 20 unimplemented source entries remain unavailable.

The target is an immutable copy of the painted scan row. Joining still resolves
that BSSID/SSID/security against a fresh completed C5 inventory, and the C5 pins
the join to that BSSID. A second AP with the same SSID cannot inherit the first
AP's connected state on the target screen. Port scanning is enabled only for
the matching established connection; opening it refreshes connection state and
uses that connection's gateway. The connection screen names the actual selected
connection, independently of whichever scan row is being inspected. Entering
the action menu drains an in-progress Wi-Fi sweep so its inventory remains
available for joining, and redraws the correct page when that sweep finishes.

`tools/render_wifi_target.py` exercises the native view and private handlers:
hidden/long names, touch gaps, same-SSID/different-BSSID cases, lost/changed
connections, password/open-network routes and no automatic transmit action.
The static-RAM increase in the initial build is 112 bytes. No target database,
per-network worker, or additional C5 allocation is added for this navigation.

The requested Deauth All scope is awaiting clarification: all clients on one
selected AP versus all explicitly selected APs. No deauthentication transmission
or working Deauth All control is introduced by this navigation change.
The pinned [IDF 6.0.1 transmit API](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32c5/api-reference/network/esp_wifi.html#_CPPv417esp_wifi_80211_tx16wifi_interface_tPKvib)
documents beacon/probe/action/non-QoS support, not deauthentication. The current
[Marauder source](https://github.com/justcallmekoko/ESP32Marauder/blob/8244eab69b0ddc316ccaf6ded470d9a999cf5b5a/esp32_marauder/WiFiScan.cpp)
provides its own raw-frame validation symbol. Our pinned C5 library contains a
strong definition of that symbol in the same object as the transmit function;
copying the menu or function is not evidence of a compatible working backend.
Any backend integration still needs matched-version linking, worker lifecycle,
error/stop behavior and controlled receiver validation. The installed driver
and its checks have not been modified in this checkpoint.
