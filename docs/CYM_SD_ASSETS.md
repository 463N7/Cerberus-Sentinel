# Supplied CYM SD assets in Cerberus

## Installed rev74: shared Wi-Fi/BLE selected-vendor cache

The Wi-Fi and BLE detail pages now share one bounded cache entry. Wi-Fi no
longer invokes the recon lookup on every healthy repeated detail redraw. The
cache key uses the exact selected address bytes plus a distinct source/type:
Wi-Fi, BLE public and BLE public-identity never reuse each other's entry. Only
the cold lookup argument is converted from NimBLE order to registry order.
Private/local addresses still return without a storage guard or SD lookup.

Both pages now use rev73's zero-wait epoch guards, found/unknown-only caching,
30-second lifetime and retry after transient errors. A different address/type,
remount, unavailable guard, backward clock or expiry invalidates reuse. The
existing labels and SD database remain unchanged. Cold recon lookups still
use the blocking contract; this does not make every detail draw nonblocking.

The focused host suite reports 31,342 checks using the actual shared helper,
both caller/label blocks and the actual storage guard. It covers cross-radio
identity, TTL, held-lock ownership, remount/USB recovery, failure text and
output bounds. The [rev74 receipt](../../integration/device-test-2026-10-27/rev74-flash-receipt.json)
records 52 host suites, five stable-input builds, matched-package/READY checks
and three verified S3 writes on 2026-10-30. The shared cache remains 72 bytes,
with no static-memory growth on either chip. C5 firmware 56 is retained; the
bridge is build-only. Startup mounted SD, but physical transitions, recovery
and lookup latency remain pending. No SD database-content change is claimed.

## Installed rev73: selected-device vendor cache coherence

The BLE detail page's outer vendor-label cache now checks the mounted storage
epoch with a zero-wait attempt on the existing mutex before reusing a found or
unknown result. It creates no mutex and allocates no memory. USB ownership,
an unmounted card or a busy/failed guard clears the displayed cached name.
Lookup results are cached only if a second zero-wait guard confirms the same
epoch afterward; transient busy/I/O/invalid-resource results retry on the next
render instead of remaining unavailable for 30 seconds. A changed selected
typed address, backward clock or expiry also requires a new lookup. Random/local
addresses still bypass SD lookup and receive no registry attribution.

The existing `resources` console command holds this storage mutex while hashing
`RECON.DB`. Cached renders now skip that wait rather than entering the blocking
one-second epoch getter. The host fixture exercises the actual storage guard
and UI cache code with a held mutex, including lock ownership and recovery;
it does not simulate a complete resources-console hash or measure device
latency. The blocking epoch API and cold recon lookups remain unchanged, so
this is not a claim that every vendor lookup is nonblocking.

This extends the existing recon-store safeguards to the UI cache; it does
not replace the SD database, import duplicate OUI tables or establish a new
whole-file integrity check. The existing successful found/unknown cache lasts
up to 30 seconds within one unchanged mounted session. The
[rev73 receipt](../../integration/device-test-2026-10-27/rev73-flash-receipt.json)
records 52 host suites, five stable-input builds and three verified S3 writes;
the focused storage/UI host suite includes 5,609 checks. The target vendor
cache remains 72 bytes. C5 firmware 56 images are unchanged, and the bridge
was built only. Startup mounted the card; physical USB/card transitions,
contention recovery and lookup latency remain unverified. This checkpoint
does not claim a new on-card database hash or replace the prior verified asset
hashes and historical rev55 lookup evidence below.

The owner's `CYM-SD-Assets-main` pack was inspected on 2026-10-29. Its `oui.csv`
has SHA-256 `99ba26f2ebb59b0b5c6511a7a77d5f8abbc85140549fab80dc00d89f19e8fba2`.
All 40,205 CSV rows, representing 40,202 unique MA-L assignments, are
semantically identical to the retained CYM IEEE CSV used to build Cerberus
`/CERBERUS/RECON.DB`. The CSV bytes differ, but the assignments, organization
names and addresses do not. The supplied `ouilist.bin` indexes the same 40,202
prefixes in CYM's `OUI1` format. Cerberus keeps one indexed `CERBREC1` database
on SD; loading `OUI1` beside it would duplicate the table.

The installed 6,333,004-byte `RECON.DB` was verified on the device through the
read-only `resources` console: SHA-256
`bb6a366efd96e1f562a3ce235ce84b115d1c4c295be09e92844dacd6c8cc2357`,
lookup `00:00:0c` returned `Cisco Systems, Inc`. Its 40,210 records combine
registry assignments and source-specific references. Wi-Fi AP details and Field
survey already read vendor names from this SD file. The BLE selected-device page
now uses the same lookup for **public** BLE addresses, reversing NimBLE's byte
order first. Random BLE addresses and locally administered addresses are not
attributed. The lookup uses a 4 KiB scratch area and an eight-entry vendor cache;
the UI retains successful selected-device results for up to 30 seconds within
the mounted session, with the installed rev73 invalidation rules described above.

The Cerberus portal service already accepts an optional SD page at
`/CERBERUS/PORTAL/INDEX.HTM`. The resource package now includes an original
Cerberus page with the service's `/submit` and `name` form fields. It stays
under the firmware's 2 KiB template limit. The built-in page remains a small
fallback for an absent card.

The other CYM asset folders are not copied wholesale. RFID key dictionaries
need an RFID/NFC reader and a matching feature. BlueDuck scripts need a matching
BLE HID service. CYM's sample portal posts credential fields to `/login`, which
does not match Cerberus's local portal test handler. Copying those into the
Cerberus SD root now would create dead entries and a misleading interface.

`tools/build_sd_resources.py` packages currently consumed files under one
`CERBERUS` directory and records their hashes in `RESOURCES.JSON`. The portal
page and manifest were copied through the device's USB SD mode on 2026-10-29;
both host-side file hashes matched afterward. After safe eject and a device
reset, the firmware mounted the card and its read-only vendor lookup passed.
The portal HTTP flow still needs a live device test. Later help-text updates in
the workspace resource bundle have not yet been copied to the card.

## Bounded lookup cache and storage ownership

The source now caches both validated vendor hits and definitive unknown
prefixes in the same eight-entry cache. It still checks the database header
on every call, and it never stores a missing card, a busy lock, a short read,
an invalid database or an allocation failure as an unknown vendor. Public BLE
addresses use the registry byte order; random/local addresses return before
any SD access. The existing 4 KiB scratch area still prefers PSRAM, with its
original internal-memory fallback; no database-wide RAM copy is added.

Each successful mount/unmount changes a storage epoch. The getter takes the
storage mutex and rejects USB ownership, an unmounted card or lock contention.
Recon compares the epoch before and after lookup, including a cache hit, so
a complete USB handover and remount cannot silently reuse a cached result even
if the database header is identical. The core lookup now checks the final
header and file size on an unknown result as well as a found result. The SD
reader binds the core lookup to the outer header, preventing a result from a
replacement database being cached under the prior header.

This is bounded metadata validation, not a filesystem snapshot or a whole-file
integrity scan on every lookup. It detects normal replacement through changed
headers and mounted-session changes. Undeclared in-place corruption that keeps
the header unchanged is outside the cache guarantee; the resource verification
command and builder's whole-file checks remain necessary for that purpose.
No open file handle or filesystem lock is retained between chunk reads, so a
handover is permitted and an interrupted lookup returns unavailable.

`tools/check_recon_store.py` runs production cache and storage lifecycle code
with media/lock/read failures and swaps. On the actual combined SD database,
a repeated unknown prefix takes one filesystem chunk read instead of 19;
the one remaining read checks the header. This is a host operation count,
not a measured on-device latency or throughput claim. Same-header remounts,
USB transitions during reads, positive cache behavior, bounded eviction,
PSRAM fallback and transient failures are covered. Existing field checks
validate all 40,187 globally administered records. All five firmware targets
build successfully and the change is installed in rev55. The device verifies
the complete 6,333,004-byte database hash against the local file and resolves
the public `00:00:0C` prefix to Cisco Systems, Inc. On-device lookup latency
and negative-cache read counts remain unmeasured; see the
[build/flash receipt](../../integration/device-test-2026-10-27/rev55-flash-receipt.json).

BLE company identifiers remain numeric advertising metadata. The CYM OUI
asset is an IEEE address registry, not a Bluetooth SIG company-ID table, so
the firmware must not use OUI names to interpret an unrelated company ID.
The other database source hints remain attributed reference material; a
vendor match alone is not a product or surveillance-device identification.
