# Marauder `develop` baseline for Cerberus

Observed on 2026-10-29 from `https://github.com/justcallmekoko/ESP32Marauder.git`:

| Ref | Commit | Commit date | Subject |
| --- | --- | --- | --- |
| `refs/heads/develop` | [`79cc6bcba1db6679baee1dab2b0c1ad5010e6363`](https://github.com/justcallmekoko/ESP32Marauder/tree/79cc6bcba1db6679baee1dab2b0c1ad5010e6363) | 2026-10-28 00:36:07 -04:00 | Bump firmware version to v1.20.0 (#1681) |
| `refs/heads/master` | [`511bb3694d0c4dc064888e762bde44fcc9b7f48a`](https://github.com/justcallmekoko/ESP32Marauder/tree/511bb3694d0c4dc064888e762bde44fcc9b7f48a) | 2026-10-28 01:34:49 -04:00 | Promote v1.20.0 to master (#1682) |

The two commits have the **same Git tree**, `8b5d8ede62ce3e4e78941ca5b10abfb97896ec60`. An explicit fetch of `refs/heads/master` into the separate `develop` checkout followed by `git diff --quiet` returned zero. The old checkout at `../integration/marauder-source` was not changed; the new checkout is `../integration/marauder-develop-2026-10-29`. These are snapshots. Recheck the remote before future parity claims because `develop` can advance.

## Source difference, `develop` versus pinned `master`

| Surface | `develop` | Added | Removed | Changed |
| --- | ---: | ---: | ---: | ---: |
| Mode symbols in `esp32_marauder/WiFiScan.h:91-186` | 95 | 0 | 0 | 0 |
| Literal menu entries in `esp32_marauder/MenuFunctions.cpp` | 265 | 0 | 0 | 0 |
| Top level CLI command branches in `esp32_marauder/CommandLine.cpp` | 58 | 0 | 0 | 0 |
| Any tracked file, including Wi-Fi and BLE implementation | Same tree | 0 | 0 | 0 |

All five key source hashes in [the existing source inventory](MARAUDER_SOURCE_INVENTORY.json) match the `develop` checkout byte for byte (`WiFiScan.h`, `MenuFunctions.cpp`, `CommandLine.cpp`, `CommandLine.h`, and `lang_var.h`). The existing [95-mode coverage ledger](MARAUDER_COVERAGE_LEDGER.md) therefore still describes this exact upstream source. Its stated *Cerberus implementation statuses* are time-sensitive and require a separate refresh after firmware changes and on-device testing. Counts overlap and do not represent 418 distinct features. A source symbol, menu route, or CLI branch does not establish Cerberus parity.

## AP selection and attack routing in this baseline

The upstream Wi-Fi attack menu calls `beginAttackPlan` for each action (`MenuFunctions.cpp:5293-5418`). That function routes AP-scoped actions to an AP picker and station-scoped actions to an AP/station picker (`MenuFunctions.cpp:4689-4716`). The AP picker offers scanned AP rows, selection state, select/clear all, and review (`MenuFunctions.cpp:4765-4803`); the station route offers broadcast, all stations, or picked stations where applicable (`MenuFunctions.cpp:4719-4752`, `4805-4877`). The review screen requires valid targets before exposing Start Attack (`MenuFunctions.cpp:4913-4984`). The corresponding scan and attack symbols are in `WiFiScan.h:94-184`; dispatch and transmit handlers are in `WiFiScan.cpp:2800-3105` and `WiFiScan.cpp:12950-13185`.

| Upstream action | Mode used by attack menu | Target route |
| --- | --- | --- |
| Deauth | `WIFI_ATTACK_DEAUTH` | AP, then broadcast/all/picked stations |
| Bad Msg | `WIFI_ATTACK_BAD_MSG_TARGETED` | AP, then all/picked stations |
| Assoc Sleep | `WIFI_ATTACK_SLEEP_TARGETED` | AP, then all/picked stations |
| SAE Commit Flood | `WIFI_ATTACK_SAE_COMMIT` | AP picker |
| Channel Switch | `WIFI_ATTACK_CSA` | AP picker |
| Quiet Time | `WIFI_ATTACK_QUIET` | AP picker |
| Auth | `WIFI_ATTACK_AUTH` | AP picker |
| AP Spam | `WIFI_ATTACK_AP_SPAM` | AP picker in upstream; keep its distinct name and behavior |
| Beacon List | `WIFI_ATTACK_BEACON_LIST` | Saved SSID picker |
| Beacon Spam, Funny Beacon, Rick Roll | Their respective modes | No AP target |

Upstream exposes AP discovery in its Wi-Fi scan menu and retains an AP/station inventory (`MenuFunctions.cpp:5238-5290`, `WiFiScan.h:94-126`). Its `Wardrive` Wi-Fi menu item is compiled/displayed only when `HAS_GPS` is set and a GPS module reports ready (`MenuFunctions.cpp:5282-5290`). Cerberus's requested unified wardriving tab should explicitly handle **no GPS** instead of inheriting this menu gate.

## Priority for future Cerberus parity work

1. **P0 — make the installed path work:** scan APs, keep a stable AP identity (BSSID, band, channel and fresh observation), expose an obvious Select AP control, show only actions whose target requirements are satisfied, then review/start/stop. Reproduce and diagnose the installed device's “stopped with an error” result at transmit time. A visible menu entry is not acceptance.
2. **P1 — complete each AP-targeted action:** verify Deauth, Bad Msg, Assoc Sleep, SAE Commit Flood, Channel Switch, Quiet Time, Auth and upstream AP Spam separately against their target scope and actual RF result. Preserve AP/station selection and report C5/S3 roles per operation. Keep general beacon/SSID actions in their own workflow.
3. **P2 — finish the remaining Marauder inventory:** use every mode row in the source inventory and coverage ledger, plus the 265 menu and 58 CLI rows, to audit Wi-Fi capture/scans, BLE scans and active actions, connected-network tools, GPS/POI, Remote ID, CSI, device services, and stop/cancel. Record board prerequisites and behavior, not just a matching label. Rebuild and physically verify S3/C5 coexistence and each implemented action.
4. **P3 — integrate wardriving into the main firmware:** add one unified session/UI with Wi-Fi AP/station and BLE observations, SD export, time/channel provenance and optional GPS fields. Then compare the separate wardriving reference firmware's complete inventory without duplicating commands or requiring a connected GPS module to open the tab.

This baseline changes source attribution from the old `master` pin to the requested `develop` ref. It makes **no claim** that the current Cerberus firmware implements all upstream behavior or that current attack transmission succeeds.

## Reproduce the branch comparison

From `Cerberus-Unified`, with Git and network access:

```powershell
git ls-remote https://github.com/justcallmekoko/ESP32Marauder.git refs/heads/develop refs/heads/master
git -C ../integration/marauder-develop-2026-10-29 rev-parse 'HEAD^{tree}'
git -C ../integration/marauder-develop-2026-10-29 rev-parse 'refs/remotes/origin/upstream-master-2026-10-29^{tree}'
git -C ../integration/marauder-develop-2026-10-29 diff --name-status refs/remotes/origin/upstream-master-2026-10-29 HEAD
```

The two tree IDs must match and the last command must print no paths for this snapshot. The source inventory's SHA-256 values give a second byte-level check of the five parser inputs.
