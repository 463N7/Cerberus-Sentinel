# Selected-AP attack navigation

The owner requested all Marauder operations that use selected APs, with a real
network selector before choosing a test. The previous menu opened the entire
Wi-Fi inventory for any selected network, starting with unrelated beacon spam.

The requested upstream `develop` reference is ESP32Marauder commit
[`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363),
retrieved on 2026-10-29. It has the same Git tree as the previously pinned
`master` snapshot. The separate checkout is retained in
`../../integration/marauder-develop-2026-10-29`; its MIT license applies to
upstream code.
The exact prerequisite routing is in `esp32_marauder/MenuFunctions.cpp`,
`MenuBuildKind::WIFI_ATTACK`, `beginAttackPlan`, and `buildAttackModeMenu`.

## Flow implemented

- Home -> Attack -> Wi-Fi Attacks -> Select APs From Scan opens the real Wi-Fi list.
- A tapped scan record is copied, including BSSID, SSID, security and channel.
  Tapping toggles that AP in a bounded collection of up to 32 networks. The
  picker stays open until **Attacks** is tapped; **Clear** empties the selection.
- Selecting a row from the ordinary Scan flow opens its network tools card,
  where **Attack This AP** opens the chosen AP's attack menu.
- Change opens the selector again; Network tools returns to the chosen AP's
  connection, network scans, station capture, details, trust and vendor actions.
- The attack screen shows the chosen name, privacy-formatted BSSID and channel.
  Two APs with the same SSID are distinct. A hidden SSID remains a valid AP target.
- Back from the attack selector returns to the menu and retains selections.
- Opening, selecting, paging and returning do not start a transmit job.
- The selector reuses a completed, matching C5 inventory on return. **Again**
  explicitly requests a new sweep. It preserves the current page, shows the full
  SSID (smaller text for long names), BSSID and channel, and outlines the selected
  APs. The attack picker shows the selected count, says **Tap networks to toggle
  / then Attacks**, and enables **Attacks** only when at least one AP is selected.

## AP feature mapping

The revision-44 source adds SAE Commit Flood to Deauthentication, Probe
Requests, Bad Message, Association Sleep, Channel Switch, Quiet Time and AP
Clone Spam. SAE authentication-frame TX still needs a device test; the IDF
driver may reject that subtype. Evil Portal/AP Clone has a separate backend.
Each implemented row has its own C5 capability gate; an older C5 keeps the
corresponding row disabled. AP Clone Spam is a selected-AP beacon transmit job;
Evil Portal/AP Clone is a separate captive portal service requiring a real
backend and HTML resource. Installed/build/RF status is in the worklog.

Multiple selected APs run in an endless round robin until Stop or screen exit,
with an internal five-second dwell per AP. Tap the AP name on the setup screen
to cycle through the collection.
Each AP retains its own observed clients and scope. Deauthentication offers
Broadcast, All Observed Clients and Picked Clients; Bad Message and Association
Sleep offer All Observed or Picked Clients. Find / Pick Clients opens passive
capture, where rows can be toggled independently or Pick All/Clear Picks used.
One C5 job cycles 2–32 chosen clients, yielding after 64 accepted frames and
backing off on bounded driver queue pressure;
a single client uses the existing one-station scope. These engines are not a
claim of complete Marauder parity or verified effects on a client.

Before starting a batch, S3 resolves every frozen AP identity against C5's
current inventory and asks read-only command 72 to check each target/client,
at most one SPI transaction per preflight UI poll. A changed scan or invalid
client rejects the batch before either processor begins RF. Stop or screen exit
can cancel before the first transmit. C5 revalidates every client at each job
start. Station evidence survives AP captures in a lazy 32-AP × 32-client C5
cache, expires after one hour and stays scoped to BSSID/channel/MAC. Cache
eviction or expiry requires another capture; observation does not prove
association.

The next AP starts only after the prior C5 job and required S3 receiver have
finished cleanup. Stop, leaving the screen or any error prevents advancement.
Reports retain attempted and unattempted targets, completed visits, state,
mode, scope and saturating per-AP driver/RX totals. Wi-Fi CSVs allocate a temporary
PSRAM-preferred buffer sized to the frozen queue, with a 256 KiB storage limit;
other resident report buffers remain 4 KiB.

**Bad Message** requires one or more observed clients on the selected AP and constructs
a fresh malformed EAPOL-Key frame for each send, with an increasing 64-bit
replay counter and random nonce. C5 selects the short variant from its locked
raw authentication snapshot for WPA3 (including transition), WPA3 Enterprise
and WAPI, avoiding the UI's lossy security label. The S3 receives management and
data frames and separates matching EAPOL observations from target beacons.
Unlike the upstream beacon parser, this includes WPA3 Enterprise modes even
when their advertisement does not contain an SAE AKM.

**Association Sleep** requires the same observed-client evidence and uses the pinned
upstream association-request layout with the Power Management flag. Its SSID
comes from the current C5 inventory. A narrowly scoped adapter constructs and
admits only this packet template; arbitrary calls retain vendor validation.
The upstream WPA2 RSN/rate/class template is preserved, including its limited
applicability across security modes and bands; acceptance is not proof that
an AP honors the request.

**SAE Commit Flood** targets the selected AP BSSID. C5 generates a fresh
P-256 group-19 scalar and element plus a local unicast source MAC per frame;
the guarded raw adapter admits only the bounded authentication-frame layout.
The S3 classifies matching frames on 2.4 GHz. These synthetic commits do not
complete password authentication. IDF may reject the authentication subtype
at runtime, in which case the UI reports the C5 transmit stage and driver
error rather than counting a successful frame.

**Channel Switch** and **Quiet Time** require the selected AP. C5 first listens
for up to two seconds for a matching beacon to obtain its capability bytes;
a timeout or identity/SSID mismatch prevents transmission. Channel Switch
announces a random supported non-DFS channel different from the AP's channel;
RF transmission stays on the AP's current channel. Quiet Time includes the
upstream Quiet IE. Both retain upstream information-element layouts, but use
fresh timestamps. The loop runs until Stop, with cooperative yields and bounded
queue-pressure retries. Upstream's paired beacon bursts are not reproduced. S3 independently
classifies matching announcement beacons on 2.4 GHz. These counts cannot prove
that a client switched channel or went quiet.

**AP Clone Spam** requires the selected AP. C5 first reads its beacon capability
bytes, then sends beacons until Stop under a generated local BSSID
with the selected SSID and variable space padding. S3 verifies matching
2.4 GHz beacons; batch preflight freezes a different clone identity per AP.
The revision-43 live channel-10 test completed with 600 C5 driver-accepted
frames and 600 S3 matches. Upstream randomizes the clone BSSID per beacon;
Cerberus currently holds one BSSID per job for exact receive attribution, so
that variant remains a parity gap. This action is distinct from Evil Portal.

All selected-AP actions default to manual Stop, with no visible duration
selector. They retain explicit Start/Stop, stale-target checks,
a three-second C5 lease and the existing S3 readiness/cleanup flow. Reports
identify the actual mode, scope and separate C5/S3 counts. No transmission is
started by selecting an AP, entering a mode, or opening the station list.

Choose **Deauth Flood** to configure broadcast to the selected AP, all observed
clients or a picked client set. **Find / Pick Clients** opens the AP's passive
capture, then Back returns to deauth setup. Only **Start** transmits. The job
has no timer and yields after 64 accepted frames across all selected clients;
client scope alternates AP-to-station and station-to-AP frames per client. Stop,
leaving the screen or a three-second control lease timeout ends the job. The
display stays awake while a job runs so Stop remains available.
The C5 requires an idle, disconnected radio and
revalidates the current AP snapshot and station table before starting.
For 2.4 GHz jobs the S3 now prepares a passive receiver before C5 transmission
starts. It counts target beacons and matching, unprotected reason-7 frames on
the selected channel. Receiver failure stops the C5 job; leaving stops both
workers. S3 Wi-Fi ownership excludes the local web AP and active BLE test.
The S3 has no 5 GHz receiver; its role there remains control and reporting.
C5 driver counts and S3 heard counts are separate on screen and in the CSV.
Heard frames cannot prove the physical transmitter or a client's disconnection.
The owner-stopped revision-43 channel-10 test accepted 29 C5 frames and recorded
13 S3 matches without an error; client effects and other scopes/bands remain
unverified.

**Probe Request Flood** (page 3, first row) opens the **Probe
Requests** implementation. C5 copies the selected scan snapshot's SSID under
the scanner lock and sends AP-directed probe requests to that BSSID, with a new
locally administered unicast source MAC per packet. Hidden APs use an empty
SSID element while retaining the chosen BSSID. Packets include band-appropriate
supported rates and use the vendor-supported raw probe-request path; they do
not use the deauth adapter. Receiver readiness, manual Stop, lease behavior
and reports reuse the AP job service.

This is a partial upstream port: Marauder's pinned `sendProbeAttack` uses
broadcast destination/BSSID, selected SSIDs and three transmissions per burst.
Cerberus currently uses the exact selected BSSID and a cooperative TX loop. Multi-AP
iteration is implemented; the broader upstream broadcast variant remains
pending. The revision-43 channel-10 test completed with 601 C5 accepted frames
and 601 S3 matches. Revision 39 first advertised the capability; older C5
firmware keeps the row disabled.

From the selected AP's tools, **Stations / Passive Capture -> Start** listens
on that AP's channel for 15, 30 or 60 seconds. The C5 must be disconnected from
Wi-Fi first. Stop or wait for completion to load the final list; tap a station
to select it, tap again to clear, and use **Save SD** to export the observation
table. `HEARD TX` and `AP ONLY` distinguish the available evidence. These are
observed addresses, not proof that a station is currently associated. Leaving
the view or putting the screen to sleep stops capture; selection never starts
an attack. See the worklog for installation and hardware verification status.

| Entry | Upstream target requirement |
| --- | --- |
| Deauth | AP scope: broadcast, all associated stations, or selected stations |
| Bad Message | Stations associated with selected APs, or selected stations |
| Association Sleep | Stations associated with selected APs, or selected stations |
| SAE Commit Flood | Selected AP; synthetic group-19 commit, with C5 driver acceptance still requiring a device test |
| Channel Switch | Selected AP |
| Quiet Time | Selected AP |
| Probe Request Flood | Selected AP list |
| AP Clone Spam | Selected AP identities; this is AP-dependent despite its name |
| Evil Portal / AP Clone | AP-derived name plus an HTML resource; this is the selected-AP variant of a workflow that also supports standalone SSID/config inputs |

Beacon list/random/funny/Rick Roll and Karma do not require a selected AP and
are excluded from this view. They remain part of the full parity requirement.
BLE attacks remain a separate category. Signal Monitor, Fox Hunt, captures and
network scanners are separate upstream feature groups, not Wi-Fi attack entries.

The wiki inventory used by the earlier implementation was incomplete. The
pinned source also contains Funny Beacon, Apple Juice and Google BLE Spam;
the previous “20 attacks” count must not be used as the full parity denominator.

## Verification

`tools/render_field_menu.py` exercises the native menu renderer and tap handler,
checks each AP entry, excludes unrelated labels, verifies no-target behaviour,
three-page bounds, inert unported rows and separate change/tools routes.
`tools/render_wifi_target.py` exercises the native selection helpers, including
same-SSID/different-BSSID transitions, direct return to attacks, ordinary scan
behaviour, and connection identity guards. `tools/render_network.py` checks the
existing connection-scoped port controls. Physical touch acceptance is separate.
`tools/render_network_picker.py` checks full names, separate same-SSID cards,
selection redraw, retained snapshots and explicit rescan. `check_s3_receiver.py`
exercises the real owner/receiver with fake drivers, including all startup
failures, leases, cancellation and retained ownership on uncertain cleanup.
`render_ap_job.py` verifies that C5 start waits for S3 readiness, cancels on
receiver failure and includes the receiver evidence in reports.
