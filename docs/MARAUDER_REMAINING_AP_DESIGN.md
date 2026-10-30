# Remaining Marauder selected-AP actions: source and implementation design

This note audits the pinned local ESP32Marauder checkout at
`../../integration/marauder-source`, commit
`511bb3694d0c4dc064888e762bde44fcc9b7f48a`. Source anchors below are
`esp32_marauder/` paths and one-based line numbers in that checkout. The
checkout is MIT licensed; see `LICENSES/ESP32Marauder.txt`. This is a design
record, not an implementation or live RF result.

Cerberus revision 42 is installed with bounded selected-AP Deauthentication,
Probe Requests, Bad Message, Association Sleep, Channel Switch and Quiet Time
engines, including selected-client lists where applicable. Its build, flash
and idle boot are verified; live RF and touchscreen acceptance remain open.
The three rows below remain disabled in `s3_main/main/field_menu.c:26-47`.
Their AP prerequisite is real: Marauder's `MenuFunctions.cpp:5309-5418` routes
each through an AP/portal picker rather than the generic beacon-spam catalog.

| Action | Pinned upstream target and behavior | Current Cerberus gap |
| --- | --- | --- |
| SAE Commit Flood | `MenuFunctions.cpp:5407-5410` uses `AttackTargetKind::ACCESS_POINTS`: one or more selected APs, no station selection. `WiFiScan.cpp:8731-8746` rotates selected APs, changes to each AP channel and creates a random source MAC. `WiFiScan.cpp:8836-8903` constructs a group-19 SAE authentication commit from a random P-256 scalar/point, optionally adding an observed anti-clogging token, then calls `esp_wifi_80211_tx` from STA mode. `RunSAEScan` also enables promiscuous receive (`WiFiScan.cpp:6995-7030`). | No SAE frame builder, C5 driver admission, token parser, typed result, or S3 receive classifier. Raw driver feasibility and over-air behavior remain unverified. |
| AP Clone Spam | `MenuFunctions.cpp:5369-5372` uses `AttackTargetKind::ACCESS_POINTS`; one or more APs, no clients. `WiFiScan.cpp:13180-13189` sends for each selected AP. `broadcastCustomBeacon` at `WiFiScan.cpp:9445-9570` uses the chosen AP's SSID, channel and two capability bytes, pads the SSID with spaces to a random length, generates a random local-source/BSSID for this mode, and submits two beacons per call. | The current generic Radio Lab beacon is not this per-selected-AP variant. No dedicated mode/status/report path or source-specific frame builder is present. The generated BSSID is deliberately different from the selected AP; receiver matching cannot simply use the target BSSID. |
| Evil Portal / AP Clone | `MenuFunctions.cpp:5317-5359` selects one scanned AP's **SSID** (or a saved SSID/config file), then requires review. `WiFiScan.cpp:4363-4383` starts the portal; `EvilPortal.cpp:339-379` configures a local SoftAP at `172.0.0.1`, wildcard DNS and HTTP routes with selected HTML. `EvilPortal.cpp:119-134` accepts submitted form fields. `WiFiScan.cpp:12999-13018` optionally sends deauth when the `EPDeauth` setting is enabled and calls portal processing. Cleanup stops DNS/SoftAP and frees HTML (`EvilPortal.cpp:49-64`; `WiFiScan.cpp:3284-3287`). | No selected-AP portal lifecycle integrated with Unified's S3 web/SD owners and C5 radio. The upstream operation clones an SSID; it does **not** clone the scanned BSSID. The UI label must not promise BSSID cloning. |

## Constraints observed in the source

- SAE Commit has no station prerequisite. Its P-256 group-19 frame is an
  authentication management frame, separate from the already implemented
  Association Sleep request. The source loop has no per-job duration or rate
  limit; Cerberus must use its timed, leased worker with one aggregate rate
  across selected APs. The source's receive parser at
  `WiFiScan.cpp:8906-8963` allocates token bytes from a receive callback and
  does not establish a complete bounded token format. Parse and cap any token
  independently; never copy arbitrary receive lengths into the C5 TX buffer.
- AP Clone Spam uses the AP's SSID/channel/capability values, but changes the
  transmitted BSSID and may append spaces to the SSID. It is a separate
  selected-AP feature from targetless Beacon Spam and from the portal. A hidden
  scanned SSID needs an explicit policy because a copied empty name is not a
  meaningful visible clone. Avoid variable-length stack arrays from the
  upstream builder; keep one bounded frame and enforce SSID/IE/channel limits.
- The portal source can load HTML from SD or serial; the configured maximum is
  11,400 bytes without PSRAM or 30,000 with PSRAM (`configs.h:3077-3085`,
  `EvilPortal.cpp:143-209`). It opens a network service and has its own
  lifecycle, unlike a short raw-frame job. Form submission and storage need a
  clearly bounded local data path. A portal selected from the AP scan must
  bind the same BSSID/channel/SSID snapshot at Start even though its SoftAP
  advertises only the SSID. Same-SSID APs must stay distinguishable in the UI.
- The source's optional portal deauth is controlled by the separate
  `EPDeauth` setting (`WiFiScan.h:997`, `WiFiScan.cpp:12999-13018`). A portal
  service must not silently start a second transmit action. Expose and report
  the option separately, using the existing bounded deauth job and radio owner
  only if scheduling both is demonstrably possible.

## Unified S3/C5 boundaries

| Service | S3 role | C5 role and hardware limit |
| --- | --- | --- |
| SAE Commit | Freeze the selected AP identities, schedule one validated target at a time, show TX/RX counts, and write the report. On 2.4 GHz, prepare its passive receiver and classify matching authentication frames before C5 starts. | Validate each AP against the current C5 scan generation; build a bounded group-19 frame in a C5 worker; use a job-specific driver admission path only if the installed C5 IDF 6.0.1 driver requires it. C5 handles 2.4/5 GHz RF. S3 has no 5 GHz receiver, so 5 GHz S3 participation is control/reporting. |
| AP Clone Spam | Keep the per-AP selection and report, including original AP BSSID, the generated beacon identity policy, and separate driver/receiver observations. On 2.4 GHz, monitor beacons with a matcher tied to generated identities, rather than matching the original AP BSSID. | Reuse the exclusive timed TX worker and scan snapshot, preserve the selected SSID/channel/capability constraints, construct a bounded fresh beacon, and enforce an aggregate frame rate for multiple APs. C5 is the only 5 GHz RF participant. |
| Evil Portal | Own the UI, HTML resource selection, SD reads and lifecycle orchestration. Reuse the S3 web owner and storage mutex. For 2.4 GHz, an S3 SoftAP/HTTP/DNS portal could run while C5 independently observes the target; the S3 passive receiver cannot run concurrently with the S3 web AP. | Validate target metadata and coordinate C5 radio ownership. A 5 GHz portal requires C5 SoftAP plus bounded HTTP/DNS service or a carefully bounded SPI-backed content path; S3 cannot supply a 5 GHz SoftAP. Actual simultaneous C5 AP/scan or AP/deauth behavior needs driver/board testing before any combined mode is enabled. |

For all three, network selection remains inert: it copies identities and opens
setup. Only an explicit Start may reserve radios. A stale scan, changed
SSID/BSSID/channel, busy radio, partner startup failure, Stop, screen exit or
lease loss must end the action without proceeding to a later AP. One job ID
and immutable target data must span S3 control, SPI and C5 work. Report C5
driver acceptance separately from S3 reception; neither proves a client's
behavior or the physical origin of a received frame.

## Implementation and acceptance order

1. Add bounded, host-tested shared frame builders and strict AP identity
   validation. Keep SAE, beacon-clone and portal types distinct; do not route
   AP Clone Spam to generic Radio Lab. Retain source provenance/licensing for
   any bytes actually derived from Marauder.
2. Add C5 worker modes with status/capability bits only when their backend
   exists. Probe management-frame driver behavior and the final linked C5
   call path per mode. Use the existing C5 radio ownership/lease and preserve
   cleanup-fault reservation. Test Stop, retries and stale scans before RF.
3. Extend S3's selected-AP UI and frozen multi-AP scheduler. Add the relevant
   2.4 GHz receive classifiers and bounded CSV audit; label 5 GHz as C5-only
   RF with S3 controls/storage. Keep missing receive data explicit.
4. For the portal, design the single S3/C5 web and radio ownership path before
   enabling its row. Test HTML size, missing SD, repeated Start/Stop, DNS/HTTP
   teardown, local submitted-data handling, and conflicts with the current
   S3 web AP and passive receiver. Verify 5 GHz capability independently.
5. Pass host tests, all five native builds, frozen image/hash and bridge
   identity checks, paired flash/boot diagnostics, and physical UI/RF tests on
   the owner's test network. Live client effects and portal service behavior
   remain unverified until measured on the device.

GPS is not a prerequisite for these actions. The later Wardriving merge must
remain in the same main application and must keep no-GPS sessions separate
from location-bearing WiGLE output.
