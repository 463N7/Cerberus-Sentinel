# Cerberus Sentinel

**Watch. Scout. Sense. Learn.**

## USB installer

The public [Cerberus USB Installer](https://463n7.github.io/Cerberus-Flasher/)
guides desktop Chrome/Edge through a temporary S3 bridge, onboard C5 programming,
final S3 programming, and a startup check using the board's USB connector.
The firmware source repository remains private. Only the web application,
release metadata, firmware binaries, and redistribution notices are published
in the separate [Cerberus-Flasher repository](https://github.com/463N7/Cerberus-Flasher).

**Development preview:** the browser workflow and revised automatic USB handoff
still require a complete physical-board test. BOOT/RESET recovery instructions
are included. See [web installer maintenance](docs/WEB_FLASHER.md) and the
[bridge control protocol](docs/C5_USB_BRIDGE.md).

Run the installer locally with `cd web`, `npm ci`, and `npm run dev`.
Run `npm test` and `npm run build` in `web/` before publishing.

An original defensive wireless companion for production Cerberus, built on
the working Cerberus Base firmware. A three-headed cyborg Cerberus guides
you through Wi-Fi, Bluetooth LE, and 802.15.4 on the 320 × 480 touchscreen.

**0.5.0-dev — development candidate.** The new experience needs acceptance
testing on the actual production board. The original Base folder is preserved.

## What is implemented

| From Home | What it does |
| --- | --- |
| **Wi-Fi Watch** | Passive 2.4/5 GHz network scans, security/channel summaries, bounded activity surveys, trusted baseline comparison, and explicit connection tools from Base. |
| **Bluetooth Scout** | Passive advertisements, signal/device details, and explicitly selected read-only services and environmental sensor tools. |
| **Radio Sense** | 802.15.4 channel energy, passive Zigbee beacon observation, frame activity, and snapshot comparison through the onboard C5. |
| **Field Guide** | Six activities with goals, experiments, explanations, quizzes, and locally saved completion. Tools opened from a lesson have a **Return to Lesson** button. |
| **Watch** | Continuous alternating passive BLE/Wi-Fi sweeps, documented device hints, original animated alert scenes, evidence and local history. Includes an offline scene gallery. |
| **Device Tools** | Settings, SD files, logs and reports, local web access, grouped device care, and optional offline extras. |

The guardian uses animated eyes/status accents. The UI composes completed
screens in PSRAM before sending changed regions through a small transfer buffer.
Home shows C5 readiness, privacy state, lesson progress, and last observed
scan counts. It does not invent threats or declare a network safe.

### Device Tools

Six entries keep everyday controls easy to reach:

- **Settings** — display, timeout, text size, privacy, backups, and calibration.
- **SD files** — browse and manage microSD files. **Tools → Start USB SD Drive**
  shares the card with a computer; eject it before returning control to Cerberus.
- **Logs + reports** — saved scan reports and diagnostic logs. Watch history
  remains in memory for the current power session.
- **Local web** — explicitly start or stop the local browser dashboard.
- **Device care** — System health, Hardware checks, and USB support. Touchscreen
  and SD read/write checks run only when selected. USB support provides console
  help and confirmed recovery-mode entry.
- **Extras** — QR maker and local message/timer/QR scripts. QR maker offers Wi-Fi,
  text, and URL codes; a Wi-Fi code includes its password, including in an
  exported image. Local web shows its own connection QR when running.

Back returns to the group that opened a tool; returning to Tools from Home
opens the main list. The Tools menu stays still between touches and battery
updates. Version labels and baseline exports use the built firmware version.

### Battery status

Every interactive page shows a battery icon and percentage at the top right,
including Watch, its gallery, the Field Guide, scan tools, and keyboard forms.
The onboard MAX17048 gauge is sampled every five seconds. The indicator turns
amber at 20% and red at 10%; it stays steady instead of blinking.
An unavailable or invalid reading appears as **--%** and is retried after
30 seconds. USB attachment is not treated as proof that the battery is charging.
Only the small indicator is redrawn when its value changes. The read-only USB
console's `battery` command reports the cached percentage, voltage, and status.

### Live radio pages

Opening **Wi-Fi Watch**, **Bluetooth Scout**, or **Radio Sense** starts passive
observation automatically for **one sweep**, then holds the results. Use
**Again** for another sweep or **Stop** to cancel; large result cards retain
the previous completed observations while a requested sweep runs, with their
age visible. Browse four results at a time or open **Tools** for detailed views.
Beacon and frame activity pages also take one sweep when opened. Wi-Fi channel,
summary, baseline, and defensive activity views update when their scan finishes.
Survey Summary and Channel View reuse an available completed Wi-Fi
capture when opened; **Scan Again** requests fresh data. Their first use still
requests a sweep when there are no saved observations in this session.
Routine progress and age changes update only the affected screen regions;
unchanged result cards and controls are left in place.
Page clears and redrawing happen in memory, so opening older menus does not
send an intermediate blank screen to the panel.

The radio coordinator stops Wi-Fi/BLE when leaving a tool and waits for
the C5 to finish before switching radio modes. Existing 802.15.4 operations
finish their bounded sweep before switching. Inspecting a Wi-Fi result lets
the current sweep finish, then holds that selection. Connections remain
explicit and are resolved by the selected network's identity, even if the
result ordering changes. Active connections keep automatic scans waiting.
Home, lessons, Demo, and a sleeping screen do not start new sweeps.

The display, menus, keyboard, and shared controls use the fixed Sentinel navy,
cyan, violet, and amber style. Older color selections, including restored backups,
use this style while preserving brightness, privacy, calibration, favorites,
and trusted networks. Brightness, screen timeout, and text size remain adjustable.

Upgrade **both S3 and C5** for this release. C5 firmware revision 31 adds bounded
Remote ID / Pwnagotchi observations and Bluetooth activity measurements alongside
raw BLE advertisements and inventory-limit reporting. Watch remains passive,
with one radio operation at a time.

### Cerberus Watch

Open **Home → Watch** for ongoing passive observation. The full-body cyborg
Cerberus looks around while the onboard C5 alternates an eight-second BLE
sweep with a dual-band Wi-Fi sweep. Only one radio operation runs at a time.
Watch keeps the display awake while running. **Pause**, **Back**, or **Demo**
stops the current sweep; the ordinary observation pages retain their single-scan
behavior. Active connections keep Watch waiting until the radio is available.

A supported signature queues an eight-second scene and a local notification.
Scenes include Axon red/blue police lighting, a Flock roadside lookout with
solar camera poles and a reflective vest, a tasteful wellness honeymoon suite
with hearts, an Apple-inspired AirTag / Find My locator, a Tile/Chipolo/Find Hub
courier, and a GoPro film director.
Other scenes include a Flipper electronics workshop with an orange hoodie and
oversized handheld, Meshtastic explorer, Samsung galaxy finder with blue space
suit and large keyring tag, beacon scientist, Pwnagotchi arcade companion,
drone aviator, and Bluetooth activity shield. The **Demo** gallery also previews
the smart-glasses scene; a scene alone does not imply that its device family
has a supported detection signature.

**History** retains the latest 32 matching broadcast identities in RAM. Tap a
hint for the rule, evidence strength, signal level, and masked address. Repeated
sightings update that entry; returning after five minutes absent can alert again.
Each category has a 20-second alert cooldown and the scene queue is bounded.
History lasts for the current power session and is not exported or uploaded.
Rotating addresses can represent the same physical device.

**History → Categories** lets each supported family use **Animated Alert**,
**History Only**, or **Off**. Educational beacons default to History Only.
These preferences last until power off. Open a hint and tap **Why This Match?**
for a short explanation of what was recognized and its limits.

The new recognition includes:

| Family | Observable evidence |
| --- | --- |
| Flipper Zero | Its documented Bluetooth service together with an advertised Flipper name. |
| Meshtastic | Its Bluetooth phone-interface service; no LoRa reception is implied. |
| Samsung finding tags | A structured finding-network advertisement, rather than a generic Samsung device name. |
| Educational beacons | Structured Eddystone and AltBeacon packets. |
| Pwnagotchi peers | Supported custom Wi-Fi peer advertisements; silent peers are not visible. |
| Drone Remote ID | Supported BLE, Wi-Fi beacon, and Wi-Fi NAN broadcasts; details show transmitted ID, drone position, and altitude when received. |
| Unusual Bluetooth activity | An experimental local traffic-rise indicator learned over several completed sweeps. Crowds can trigger it; it identifies no attacker or tool. |

Remote ID observations have a separate bounded capture, so they do not depend
on fitting into the ordinary device inventory. Coordinates are reported by the
transmitter and are unverified; Cerberus has no GPS and computes no receiver
distance or bearing. ID and location fields are merged only within one sweep.
Skimmer detection is excluded.

Matches are unauthenticated broadcast hints. Evidence strength describes the
signature, not certainty about a device, its owner, recording, or being followed.
Apple frames are labeled **AirTag / Find My hint** because AirTags and other
Find My accessories share that broadcast format; the model remains unknown.
Tile and Chipolo hints name their respective tracker ecosystem. Google Find Hub
keeps its own label and explanation. Camera hints currently cover GoPro only.
Smart glasses remains a demo scene with no live detection rule.
Unseen/silent devices, scan timing, inventory limits, and address rotation limit
coverage. See [the sourced detection catalog](docs/WATCH_SIGNATURES.md) for exact
rules, provenance, exclusions, and supported families.

The scene gallery stops radio work and never creates live alerts or history.
The original educational sample views remain available in **Field Guide**.

### Learn by doing

1. **Meet Your Network** — signal, channel, and security information.
2. **Crowded Air** — channel overlap and measurement limits.
3. **Bluetooth Footprints** — advertisements and rotating addresses.
4. **Follow the Signal** — why RSSI is not distance.
5. **Shared Airspace** — 802.15.4 energy and coexistence.
6. **What Changed?** — trusted baselines and interpreting differences.

Completion requires the quiz and an explicit completion action. It is a
learning checklist, not certification that a live experiment succeeded.
Progress saves in NVS; storage errors are shown and can be retried.

### Trusted baseline

Scan, select your own access point, and save it as trusted. **Wi-Fi Watch →
Tools → Trusted baseline** compares a completed scan's stored records with your saved
entries. Exact matches, same-name differences, and other networks are shown.
Mesh nodes and router changes can explain differences; an unseen network
does not prove it is gone. At most 32 stored records are compared. **Save**
exports aggregate CSV to microSD without network names or radio identifiers.

### Local web control

The S3 access point starts from **Device Tools → Local web → Start Local Web**.
Its generated per-device Wi-Fi password appears on the touchscreen. Set a
separate browser password at first use. Existing authenticated Base controls
are retained. **Stop Local Web** shuts down the server and access point; a
failed shutdown offers a retry. Factory reset clears configuration and lessons.

## Production hardware

| Part | Profile |
| --- | --- |
| Main processor | ESP32-S3, 16 MB flash, 2 MB quad PSRAM |
| Radio processor | One onboard ESP32-C5; 2 MB flash budget, no PSRAM |
| Screen / touch | ST7796 320 × 480 portrait / GT911 |
| Storage / link | microSD SDMMC 1-bit; native USB; existing SPI/READY link |

No GPS, extra C5 bank, or expansion board is required. Production builds do
not drive GPIO38 as a prototype SD test pin. Radio observations are bounded
and user initiated. Connections and the local web AP are separate visible
actions. Settings can mask addresses; this does not anonymize network names.

**SDR is deferred.** The later investigation is the internal ESP-SDR approach
at [ESP-SDR](https://espargos.net/espsdr/), using existing hardware. No SDR
capture, streaming, or additional-board dependency is included now.

## Build and work in VS Code

Open this folder in VS Code. PlatformIO, C/C++, and Serial Monitor extension
recommendations and build/test tasks are included. The default build task
builds S3, C5, and the temporary S3 flashing bridge; it never uploads.

```powershell
python tools/build_sentinel.py --target all --package
python tools/test_sentinel.py
```

The package in `dist/` includes matching versions, generated flash layouts,
source hashes, and image checksums. The host checks exercise actual new C
modules with simulated display/NVS/link services. Previews go to `artifacts/`.
Host rendering uses `ziglang==0.14.1`. See
[Build and recovery](docs/SENTINEL_BUILD.md) for setup and flashing.

## Project map

- `s3_main/main/sentinel_dashboard.*` — guardian and home.
- `s3_main/main/sentinel_guide.*` — learning, progress, demo.
- `s3_main/main/sentinel_hub.*` — radio tool menus.
- `s3_main/main/sentinel_scan_view.*` — live radio cards, energy graph, controls.
- `s3_main/main/sentinel_scan_session.*` — single-sweep scheduling and safe radio switching.
- `s3_main/main/sentinel_live_ui.h` — private UI integration and atomic snapshots.
- `s3_main/main/sentinel_baseline.*` — passive snapshot comparison.
- `s3_main/main/sentinel_watch*` — Watch rules, history, category controls, activity baseline, scenes, and UI.
- `shared/sentinel_watch_protocol.*` / `cerberus_watch_capture.*` — bounded broadcast parsing and radio snapshots.
- `s3_main/main/ui.c` — navigation into Base tools.
- `c5_firmware/` — onboard radio services.
- `shared/` — protocol and pin definitions.
- `tools/` — builds, packages, previews, tests, explicit flashing.

See [the roadmap](TODO.md), [board acceptance](docs/SENTINEL_ACCEPTANCE.md),
[USB bridge and recovery](docs/C5_USB_BRIDGE.md), and [restricted scripting](docs/SCRIPTING.md). Historical Base documentation
is retained in `docs/BASE_README.md` and `docs/BASE_BACKLOG.md`.
