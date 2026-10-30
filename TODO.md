# Remaining firmware and flashing work

## Owner phase order and integration requirements (2026-10-29)

1. Finish and optimize every hardware-supported Marauder feature in the main
   Cerberus application, including multiple AP/station selection and coordinated
   S3/C5 operation. Menu labels do not establish parity.
2. Then merge the complete Cerberus Prototype Wardriving Expansion feature and
   resource set into that same application. Source is already retained at
   `../Cerberus-Wardriving-main` and `../integration/baselines/wardriving`.
   This must not become a separate selectable firmware, boot image, or app.
   Per-chip S3/C5 binaries remain an implementation necessity of one product.
3. GPS is currently absent. Scanning, deduplication, non-geotagged session logs,
   controls, resource lookup and other location-independent features must work
   without it. GPS status must distinguish absent/no fix/stale/fresh. Do not
   invent coordinates, write fake geotagged rows or label unlocated logs as a
   valid location-bearing WiGLE capture. Add actual GPS data when fitted.

The wardriving audit must cover R0-R6 scheduling, band/channel/region/dwell and
runtime presets, dedupe and signal-improvement re-logging, session storage,
network viewer and device health, display power controls, setup and profiles,
remote controls/file management, WiGLE/WDGWARS export and upload histories,
phone/USB compatibility and all shipped resources. Optional expansion radios
must remain optional. Upload support is not authorization to transmit captures
to external services during implementation. Reuse unified ownership, transport,
settings and storage rather than running a second independent firmware stack.

The preserved source is nested at
`../Cerberus-Wardriving-main/Cerberus-Wardriving-main`. Its README and
`s3_main/main/wigle_logger.c` confirm that scanning is independent of GPS,
while WiGLE rows require a valid fix (the session freshness policy is five
seconds). Simply removing that gate would produce misleading geotagged files.
During integration, add an unlocated session record path alongside the WiGLE
exporter, with explicit missing location and timestamp provenance. Keep the
same session controls and cross-radio dedupe for both paths. A receiver that
was connected and then stops reporting must become stale/absent; its last
coordinates must not silently carry forward into new observations.
Maintain separate logged baselines for unlocated and geotagged outputs: an
unlocated record must not suppress the first eligible WiGLE row when a real
fix arrives, and missing GPS must not make every repeated sighting log again.
See `docs/WARDRIVING_SOURCE_INVENTORY.md` for the source behavior and acceptance
matrix.

GPS-optional acceptance must cover boot without the module, uninterrupted
S3/C5 scanning, same-SSID/BSSID handling, SD save/stop/reopen, absent-to-fix and
fix-to-stale transitions, export eligibility, and no uploads of unlocated
records as WiGLE captures. Preserve the original useful logging behavior:
bounded cross-radio dedupe, channel-change/3 dBm improvement/five-minute
relogging, periodic commits and flush/close on Stop. Settings, profiles,
resource lookup, network viewing and file management must not depend on a fix.


## Version 1.0.0

- [x] Owner reported successful testing of the existing feature build and approved the first production release on 2026-10-27.
- [x] Use a matched S3, C5, and temporary bridge package with checksums and recovery instructions.

## Flashing and manufacturing

- [ ] Record supported hardware revisions and batch programming results as production expands.
- [ ] Define future updates around the current single factory slot on each processor.

For each future firmware or hardware revision, repeat physical acceptance for
the changed behavior and retain a matched recovery package. The owner's v1.0.0
acceptance predates the rebuilt, versioned v1 package and does not establish
compatibility with other board revisions or
guarantee recognition of every device in a broadcast category.
