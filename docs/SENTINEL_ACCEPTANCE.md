# Production-board acceptance

Use a development package from `tools/build_sentinel.py --target all --package`.
Host checks do not replace these physical checks. First-board flash/boot
evidence is recorded separately in FIRST_BOARD_FLASH.md. This list remains
the release acceptance record, including the new live-page behavior.

## Test record

| Item | Result |
| --- | --- |
| Date / tester | |
| Board revision / C5 marking | |
| Package / manifest SHA-256 | |
| S3 / C5 versions | |
| Detected flash / PSRAM | |
| Power source / microSD / USB host | |
| S3 / C5 minimum free heap | |

## Startup and navigation

- [ ] Cold boot and warm reset reach Home without a reset loop.
- [ ] Logs detect expected quad PSRAM and C5 capabilities.
- [ ] Guardian, labels, colors, and touch targets fit the physical panel.
- [ ] Home starts with no observations; sample data never appears as live.
- [ ] Each radio hub, Device Tools, Demo, and Field Guide opens and returns.
- [ ] Screen timeout and first-touch wake avoid accidental activation.
- [ ] C5 unavailable state and reset/recovery work.

## Device Tools

- [ ] Six main entries fit the panel: Settings, SD files, Logs + reports,
  Local web, Device care, and Extras; the battery remains visible.
- [ ] Device care opens System health, Hardware checks, and USB support.
  Extras opens QR maker and Local scripts. Each child returns to its group;
  group Back returns to Device Tools, then Home. Reopening Tools starts at main.
- [ ] Opening menus does not start a radio, web access point, SD benchmark,
  script, or recovery action. Gaps and off-button touches do nothing.
- [ ] Hardware checks distinguish readiness from a completed check; touchscreen
  targets work and the SD check runs only from Check microSD.
- [ ] QR maker offers Wi-Fi, text, and URL only; creation and bitmap saving work.
  The Wi-Fi password notice matches the encoded and exported content.
- [ ] System health and new baseline CSV files show the installed version.
- [ ] SD file sharing/eject, logs, backups, and confirmed recovery remain usable.

## Battery indicator

- [ ] Top-right icon and percentage remain readable on Home, Watch/live/demo/history,
  Field Guide/lessons, scan results, Tools, Settings, and keyboard forms.
- [ ] Page changes and ongoing Watch animation preserve the indicator without
  full-screen flashes or overlap with the C5/Watch badges.
- [ ] Compare USB `battery` diagnostics with the screen and a reference voltage
  measurement across battery discharge and charging conditions.
- [ ] Check steady amber at 20% and red at 10%, full at 100%, and distinct `--%`
  for an unavailable gauge; USB alone must not produce a charging claim.
- [ ] Missing/failed gauge reads recover and leave touch/radio operation usable.

## Learning and observation

- [ ] Complete all six lessons with wrong and correct quiz answers.
- [ ] Open each linked tool and use Return to Lesson.
- [ ] Progress survives reboot and clears through guarded factory reset.
- [ ] Demo remains labeled and works with C5 offline.
- [ ] Wi-Fi 2.4/5 GHz and BLE scans finish, stop, and can be requested again.
- [ ] Home radio cards open directly into live results and start automatically.
- [ ] Color selection is absent from device and web Settings; older saved themes and restored backups use Sentinel without losing other settings.
- [ ] Last completed records stay visible with an honest age during each sweep.
- [ ] Open Channels and Summary before the first completed sweep; waiting text
  and the page's own controls appear immediately.
- [ ] Failed/cancelled sweeps retain the previous valid records and their age;
  a successful empty sweep replaces them with an empty result.
- [ ] One completed sweep stays complete without another automatic scan; Again
  requests exactly one further sweep, and Stop stays stopped.
- [ ] Tools and Home stop/drain the scan; reopening a radio tool requests one sweep.
- [ ] Progress/age updates leave result cards and unchanged controls still;
  changing result pages does not blank the header or footer.
- [ ] Console reports `screen_buffered=yes`; opening Home, Tools, Settings,
  Survey Summary, and Channel View does not expose an intermediate blank screen.
- [ ] After a Wi-Fi capture, opening Summary/Channels reuses that
  capture without incrementing its generation; Scan Again requests one new scan.
- [ ] Energy, beacons, and frame activity switch after a bounded operation finishes.
- [ ] Select Wi-Fi during a sweep: selection stays fixed, Connect waits for completion,
  and a reordered or missing result cannot target a different network.
- [ ] BLE/network connections block automatic scans until explicitly disconnected.
- [ ] Channel, summary, baseline, and defensive views refresh after a sweep;
  footer Stop/Scan Again works.
- [ ] Screen sleep drains/stops active scanning; completed or stopped results
  remain held after waking. An interrupted active page may request one new sweep.
- [ ] Radio views match controlled test conditions and respect measurement limits.
- [ ] Repeated Wi-Fi/BLE/802.15.4 transitions do not leave a stuck radio state.
- [ ] Explicit connections/read-only services work with test devices.

## Watch mode

- [ ] Home Watch opens the full-body guardian and alternates passive BLE and
  dual-band Wi-Fi sweeps; only one radio operation runs at a time.
- [ ] Active Watch stays awake; Pause restores the normal screen-timeout behavior.
- [ ] Pause, Back, and Demo stop/drain the owned radio operation; Resume begins
  a fresh sweep. Existing connections block scans without being disconnected.
- [ ] All 15 gallery scenes animate without intermediate blank frames and
  remain clearly marked Demo; the gallery creates no real history/notifications.
- [ ] Known test devices match the expected sourced rule and evidence strength.
  Record model, advertised fields, firmware, rule ID, and any false positives.
- [ ] Ordinary Apple devices, generic Bluetooth UUIDs, arbitrary camera/toy
  names, and unsupported Meta equipment do not cause specific-device claims.
- [ ] Repeated sightings update history without repeated alerts; a returning
  identity after five minutes absent may alert again, subject to category cooldown.
- [ ] Multiple new categories show queued eight-second scenes and can be found
  in History; addresses follow the privacy masking setting.
- [ ] Inventory limit, offline radio, interrupted reads, and scan faults are
  visible; no partial/mixed-generation evidence appears as a completed match.
- [ ] Ordinary Wi-Fi/BLE/Radio Sense pages still perform one sweep and hold it.
- [ ] Categories cycle through Animated Alert, Off, and History Only; disabled
  categories add no new history/alerts, and History Only adds no scene or notification.
  Previously saved history remains available. Preferences reset after power off.
- [ ] Evidence and Why This Match remain fixed while new sweeps complete in the
  background; returning to Watch resumes the animated scenes.
- [ ] Check Flipper service/name, Meshtastic BLE, Samsung finding tags, and
  educational beacon packets against known devices and nearby negative cases.
- [ ] Check supported Pwnagotchi peer broadcasts with two peers; session BSSIDs
  remain distinct even though their transmitter address is shared.
- [ ] Check Remote ID over BLE legacy/extended 1M/coded, Wi-Fi beacon, and Wi-Fi
  NAN with controlled transmitters. Record missing/unsupported transports.
- [ ] Remote ID shows only fields received in the current sweep and labels
  position/altitude as transmitter claims, without receiver distance or bearing.
- [ ] A crowded baseline alone produces no Bluetooth activity alert. A sustained
  rise across two subsequent sweeps can produce an experimental area alert.
- [ ] Capture unavailable/overflow and bounded address counts are visible and
  do not become claims of complete coverage or physical-device counts.
- [ ] At least two hours of Watch operation remains responsive with stable heap
  and no resets. Radio classifications need real-device validation before release.

## Baselines, files, privacy, web, USB

- [ ] Save a controlled access point and confirm an exact baseline match.
- [ ] A controlled same-name AP with changed BSSID/security produces a review
  clue, not an attack verdict.
- [ ] Failed scans never publish a partial comparison.
- [ ] Baseline CSV values are correct and contain no SSIDs or radio identifiers.
- [ ] Missing/removed SD produces a clear failure.
- [ ] Address masking works on touchscreen and web.
- [ ] The S3 web AP starts only from Start Local Web.
- [ ] Unique AP password persists; browser setup/login/logout/expiry work.
- [ ] Stop Local Web removes AP/server; repeated start/stop works.
- [ ] SD/USB ownership handoff, safe unmount, and return preserve data.

## Endurance and recovery

- [ ] Run repeated scans/navigation for at least two hours. Record heap minima
  and check for progressive loss, watchdog resets, and display failures.
- [ ] Repeat with local web and SD logging/export.
- [ ] Power loss during progress/settings saves recovers valid state or shows
  a storage error; preserve reset/crash logs.
- [ ] On a recoverable test unit, verify interrupted S3/C5 update recovery
  through ROM/USB bridge using known-good Base images.

Record reproduction steps and package hashes for failures. Resolve them before
shipping. Both binaries compiling is only the computer verification stage.
