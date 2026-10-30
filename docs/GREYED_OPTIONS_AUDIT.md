# Greyed and unavailable field options

Current installed checkpoint: S3 `candidate-ready-edge-portable-build`
(rev54), onboard C5 firmware 49, 2026-10-30 UTC. The [rev54 receipt](../../integration/device-test-2026-10-27/rev54-flash-receipt.json)
records five passed builds with stable inputs, final S3 ELF READY ISR
inspection, matched images, nine write hashes, boot, mounted SD and 40 MHz SPI.
Idle S3 internal free heap is 104,056 bytes (minimum 103,376), a 1,036-byte
cost versus rev53. C5 idle free heap remains 77,172 bytes.

READY falling-edge epochs, read-only transport timings and portable compiler
and package provenance are installed. Startup records 23 exchanges, zero
mutex/fall/high timeouts or driver errors, four initial READY timeouts and
four fallbacks. These are a baseline, not sustained performance acceptance.
An idle follow-up recorded 546 exchanges, 523 more than startup, with all
timeout/fallback counts unchanged. One latched pulse does not establish that
the former level poll would have missed it. No load improvement is claimed.
Rev54 now has short no-GPS Wardriving Start/Stop acceptance: 22.592 seconds,
4 Wi-Fi/2 BLE sweeps, 72 unlocated rows, one saved file and no service/status
errors. Longer/repeated rev54 load remains unverified. Linux compilation has
not been executed locally.

Own-AirTag selected BEEP and Packet Monitor activity/Back/subsequent scans are
prior rev53 evidence. Repeated no-GPS Start/Stop acceptance is from rev52.
The source inventory is
Marauder [`develop` at `79cc6bc`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363);
see [the branch baseline](MARAUDER_DEVELOP_BASELINE.md). Source routes,
controller acceptance, received frames, audible output and full upstream
behavior are separate evidence.

This replaces the early audit in which two Wi-Fi rows and every BLE row were
placeholders. Those historical counts do not describe rev54.

## Current route counts

| Surface | Current state | Evidence limit |
| --- | --- | --- |
| Selected-AP Wi-Fi attacks | Nine rows, all linked to services and context gates | Availability does not prove RF effects or every upstream variant. |
| Bluetooth attacks | Nine rows, all linked to S3 advertiser or central services | Some payloads and tag types still lack live acceptance. |
| Scan menu | Seven destinations, including main-firmware Wardriving | Destination visibility does not establish complete upstream scan coverage. |
| Selected Find My BEEP | Normal-list selection now freezes the peer and required original observation evidence | Owner confirms the corrected rev53 selected-tag route audibly beeped their AirTag; other profile families remain unverified. |
| Find My Monitor | FIND MY filter exposes MONITOR; bounded history and Stop/Back are installed | Production-route host checks pass; separate live Monitor update/Back acceptance remains pending. |

The routing and subtitles are in [field_menu.c](../s3_main/main/field_menu.c).
The Wi-Fi subtitle `NOT PORTED / UNAVAILABLE` still combines unavailable
capability with implementation status; it should be split into useful reasons
in a later UI pass. Missing target selection is already shown separately as
`SELECT AP FROM SCAN FIRST`.

## Wi-Fi prerequisites and remaining acceptance

Unresolved report carried into rev54: the owner confirms START does nothing
on Quiet Time and other Wi-Fi attack screens and says nearly none work.
Exact error text and cause remain pending. Prior rev53 console records no
started transmit job, an idle monitor, healthy link and successful scans;
these facts do not diagnose or clear the failure. Rev54 claims no repair or
successful action test. Earlier successes remain historical evidence.

All nine rows require selected scanned APs. Selection retains a copied
BSSID/channel identity; opening a row does not start transmission. The live
capability gates are in [wifi_target_ui.h](../s3_main/main/wifi_target_ui.h),
with per-job revalidation in [wifi_ap_ui.h](../s3_main/main/wifi_ap_ui.h).

| Row | Why it can be unavailable or unable to start | Current evidence |
| --- | --- | --- |
| Deauth Flood | C5 offline/missing capability; fresh AP and broadcast/observed client scope required | Earlier owner AP-broadcast test accepted 29 frames and saw 13 S3 matches before intentional Stop; other scopes remain open. |
| Bad Message / Association Sleep | C5 capability and observed clients required | Installed engines and host checks; mode-specific live RF acceptance pending. |
| SAE Commit Flood | Requires C5 revision 44 or newer and its SAE capability | Installed selected-AP engine; mode-specific live RF and AP response acceptance pending. |
| Channel Switch / Quiet Time | Matching target beacon/capabilities required before transmission | Earlier owner tests each completed with 600 C5 accepted frames and 600 S3 matches. The attack-screen START/error report remains unresolved in rev54; no repair or successful retest is claimed. |
| Probe Request Flood | Fresh selected AP identity and C5 capability required | Earlier owner test completed with 601 accepted frames and 601 S3 matches; upstream broadcast/list variants remain open. |
| AP Clone Spam | Selected source beacon and C5 capability required | Earlier owner test completed with 600 accepted frames and 600 S3 matches; full clone variant parity remains open. |
| Evil Portal / AP Clone | Visible selected SSID, online C5 and available S3 Wi-Fi ownership required | Installed local S3 SoftAP/DNS/HTTP service with C5 target preflight and host checks; DHCP/DNS/HTTP/report/Stop device acceptance pending. |

`AP CLONE SPAM` and the portal are distinct services. Targetless Beacon Spam,
Beacon List, Rick Roll, Funny Beacon and Karma/Mimic remain separate missing
workflows in the [coverage ledger](MARAUDER_COVERAGE_LEDGER.md); they should not
be duplicated in an AP-only menu.

Selected-AP PCAP capture is also separate from active SAE Commit Flood. Rev52
installs `SAE COMMIT FRAMES` in the existing capture-mode selector and requires
C5 revision 48. Host parser/service tests and firmware builds pass; live
capture acceptance remains pending. Full upstream 250 ms channel hopping,
generic scan entry and live source/destination rows remain pending.

## Bluetooth prerequisites and acceptance

The S3 owns the BLE scanner, advertiser and GATT central. The C5 can survey
Wi-Fi independently. The shared S3 BLE host allows one owning operation at a
time; an in-use host is a runtime prerequisite, not a missing payload.

| Row or control | Availability rule | Current evidence |
| --- | --- | --- |
| Sour Apple, Swift Pair, Samsung, Google, Flipper | S3 advertiser available | Installed payload engines and host checks; each mode still needs external RF/peer acceptance. |
| Apple Juice | S3 advertiser plus connectable peripheral role | Rev50 completed with 180 accepted bursts and zero controller rejection; peer behavior and external RF remain unverified. |
| BLE Spam All | S3 advertiser plus connectable role for included variants | Rev50 ran until Stop, completed 239 accepted bursts with zero rejection and released host; external RF and every variant remain unverified. |
| Spoof AirTag | Opens observed-device selection; replay requires an eligible complete raw AD and random-static identity | Installed source/host gates; replay RF behavior remains unverified. |
| Find My Beep All | S3 central available; sweeps only currently observed compatible connectable addresses | Prior-version rev51 owner AirTag audibly beeped; Stop completed and console showed host owner zero. Rev53 retest, other tag types, multiple tags and prolonged cycling remain unverified. |
| Selected BEEP | Compatible, connectable original raw AD no more than 30 seconds old and no conflicting connection; normal-list selection retains original observation age | Owner confirms the rev53 normal-list selected-tag route audibly beeped their AirTag. Host route/freshness checks pass; other profile families remain incomplete or unverified. |

Find My results distinguish ATT write acknowledgement, tag response and DULT
completion from acoustic evidence. Beep All is a Cerberus extension; upstream's
live menu route is commented out. It cannot guarantee sound from every AirTag
or treat rotating addresses as a persistent physical identity.

The rev53 selected-tag console records profile 1, generation 1, one accepted
write, no service error and BLE host owner zero. Tag response and completion
fields are zero; audible output is the owner's separate confirmation.

Rev53 exposes All, iBeacon, Find My, Flipper, Flock, `META ID HINT` and
`HC NAME HINT` through the normal live-list count/header control in
[sentinel_live_ui.h](../s3_main/main/sentinel_live_ui.h). Completed sweeps
freeze peer identity, raw observation evidence and original timestamps in
PSRAM. Selecting a row cannot renew its freshness. Parser, scanner,
production-route and pagination checks plus all five firmware builds pass.
The owner confirms the corrected Find My selection/BEEP path on their AirTag;
other focused filters still need representative device acceptance.

Find My Monitor is installed with bounded PSRAM history, original observation
age, per-sweep peak RSSI and Stop/Back cleanup. Separate live update/Back
acceptance remains pending. Matched scan-response-only hints, mixed profiles,
FMNA UUID normalization and discovery across supported GATT profiles remain
gaps in [Find My profile parity](FINDMY_PROFILE_PARITY.md).

Identifier/name matches do not establish a device model or prove a skimmer.
In prior rev51, the owner confirmed Fox Hunt signal
updates and normal Back exit, BLE Analyzer graph activity and normal exit,
and Wi-Fi Packet Monitor activity and normal exit. Prior-version rev53 evidence
superseded its first inconclusive monitor retest: generation 1 on channel 8
reached sample 92 at 18,543 ms, then Back left the monitor idle at sample 211
after 42,394 ms. Subsequent successful scans increased from one to four on
the same boot, with failed scans zero. Monitor activity, exit and the
shared-worker transition were corroborated on rev53. Rev54 retest, exact
sample-rate accuracy, other channels and prolonged load remain unverified.
Fox Hunt and Analyzer current-build retests remain open. Focused Flock
Wardriving also remains pending.

## Wardriving and hardware limits

Wardriving is part of the main firmware and works without GPS or a backboard.
The owner confirms rev54 starts and stops. Console records STOPPED after
22,592 ms, 4 C5 Wi-Fi sweeps, 2 S3 BLE sweeps, 72 unlocated SD rows and one
saved file. All service errors and C5 status timeouts are zero; Stop released
the BLE host and no reset fault occurred. Post-Stop S3 internal free heap is
45,116 bytes, minimum 37,656.

At 1,397 SPI exchanges, mutex/fall/high timeouts and driver errors remain
zero. Initial READY timeouts and fallbacks remain four each, unchanged from
startup. Maximum mutex wait is 25 microseconds; maximum hold/exchange values
still reflect startup. This accepts a short onboard run. Longer/repeated
rev54 runs, saved CSV content and physical GPS/backboard remain unverified.
The different duration from rev52 prevents a matched performance comparison.

The owner confirms two rev52 no-GPS runs finished STOPPED without reset.
Counters describe the final session only: 253.917 seconds, 52 C5 Wi-Fi sweeps,
31 S3 BLE sweeps, 111 unlocated rows and one saved file, with all service errors
zero. It recovered 79 C5 status timeouts, which remain an optimization item.
Stop released the BLE host; post-Stop internal free heap was 46,620 bytes,
minimum 39,160. Rev52 lowers the no-GPS startup reserve to 43,008 bytes;
the prior rev51 restart-reserve issue now has repeated-Start device acceptance.
This is prior-version evidence; saved-file contents and prolonged sessions
remain unverified.

The optional R1-R6 allocation and legacy configuration path are installed and
host-tested. No backboard is attached. R0 still scans its full channel set,
so complete seven-radio load balancing is pending. GPS is absent; UART input,
PPS, geotagged logging and receiver behavior have not passed hardware tests.
GPS-aware WiGLE and unlocated fallback logging are installed in rev52, with
host checks for routing, rotation, SD ownership and partial-write retention.
GPS mode uses a 1 KiB UART RX ring and a 45,568-byte startup reserve. Metadata
describes GPS at result receipt because radio records lack synchronized RF
timestamps. Live receiver input and geotagged export remain unverified.

C5 revision 49 worker consolidation is installed: Packet Monitor shares the
exclusive scan worker, removing its dedicated 4 KiB task stack. Host worker
and cleanup checks and all five builds pass. Measured idle C5 free heap rose
from 72,732 to 77,172 bytes, retained in rev54. Focused rev53 device evidence
accepted the monitor-to-scan transition on the same boot. Rev54 retest, worker
stack watermark, prolonged load and exact sample-rate accuracy remain
unverified.

The S3 cannot receive 5 GHz Wi-Fi. C5 owns that radio path, and independent S3
receive counts are available only on supported 2.4 GHz operations. A connected
network is required for LAN diagnostics, fresh observed clients for client
scopes, and valid storage ownership for SD writes. Those prerequisites should
be visible as contextual reasons, not blanket claims of missing Marauder code.
